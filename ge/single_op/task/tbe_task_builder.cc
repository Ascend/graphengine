/**
 * Copyright 2019-2020 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "single_op/task/tbe_task_builder.h"

#include <mutex>
#include <vector>

#include "graph/debug/ge_attr_define.h"
#include "graph/load/model_manager/model_utils.h"
#include "graph/manager/graph_var_manager.h"
#include "runtime/rt.h"
#include "single_op/task/build_task_utils.h"

namespace ge {
namespace {
constexpr char const *kAttrSupportDynamicShape = "support_dynamicshape";
constexpr char const *kAttrOpParamSize = "op_para_size";
std::mutex g_reg_mutex;

inline void GetKernelName(const OpDescPtr &op_desc, std::string &kernel_name) {
  (void)AttrUtils::GetStr(op_desc, op_desc->GetName() + "_kernelname", kernel_name);
}

inline TBEKernelPtr GetTbeKernel(const OpDescPtr &op_desc) {
  return op_desc->TryGetExtAttr(ge::OP_EXTATTR_NAME_TBE_KERNEL, TBEKernelPtr());
}
}  // namespace

KernelHolder::KernelHolder(const char *stub_func, std::shared_ptr<ge::OpKernelBin> kernel_bin)
    : stub_func_(stub_func), bin_handle_(nullptr), kernel_bin_(std::move(kernel_bin)) {}

KernelHolder::~KernelHolder() {
  if (bin_handle_ != nullptr) {
    GE_CHK_RT(rtDevBinaryUnRegister(bin_handle_));
  }
}

const char *KernelBinRegistry::GetUnique(const string &stub_func) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = unique_stubs_.find(stub_func);
  if (it != unique_stubs_.end()) {
    return it->c_str();
  } else {
    it = unique_stubs_.insert(unique_stubs_.end(), stub_func);
    return it->c_str();
  }
}

const char *KernelBinRegistry::GetStubFunc(const std::string &stub_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto iter = registered_bins_.find(stub_name);
  if (iter != registered_bins_.end()) {
    return iter->second->stub_func_;
  }

  return nullptr;
}

bool KernelBinRegistry::AddKernel(const std::string &stub_name, std::unique_ptr<KernelHolder> &&holder) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto ret = registered_bins_.emplace(stub_name, std::move(holder));
  return ret.second;
}

TbeTaskBuilder::TbeTaskBuilder(const std::string &model_name, const NodePtr &node, const domi::KernelDef &kernel_def)
    : node_(node),
      op_desc_(node->GetOpDesc()),
      kernel_def_(kernel_def),
      stub_name_(model_name + "/" + node->GetName() + "_tvmbin") {}

Status TbeTaskBuilder::DoRegisterBinary(const OpKernelBin &kernel_bin, void **bin_handle,
                                        const SingleOpModelParam &param) const {
  rtDevBinary_t binary;
  binary.version = 0;
  binary.data = kernel_bin.GetBinData();
  binary.length = kernel_bin.GetBinDataSize();
  binary.magic = param.core_type == 0 ? RT_DEV_BINARY_MAGIC_ELF : RT_DEV_BINARY_MAGIC_ELF_AIVEC;
  auto ret = rtDevBinaryRegister(&binary, bin_handle);
  if (ret != RT_ERROR_NONE) {
    GELOGE(ret, "rtDevBinaryRegister failed, bin key = %s, core_type = %ld, rt ret = %d", stub_name_.c_str(),
           param.core_type, static_cast<int>(ret));
    return ret;
  }

  return SUCCESS;
}

Status TbeTaskBuilder::DoRegisterMeta(void *bin_handle) {
  std::string meta_data;
  (void)AttrUtils::GetStr(op_desc_, TVM_ATTR_NAME_METADATA, meta_data);
  GELOGI("TBE: meta data: %s", meta_data.empty() ? "null" : meta_data.c_str());
  if (!meta_data.empty()) {
    auto rt_ret = rtMetadataRegister(bin_handle, meta_data.c_str());
    if (rt_ret != RT_ERROR_NONE) {
      GELOGE(rt_ret, "rtMetadataRegister failed. bin key = %s, meta_data = %s, rt ret = %d", stub_name_.c_str(),
             meta_data.c_str(), static_cast<int>(rt_ret));
      return rt_ret;
    }
  }

  return SUCCESS;
}

Status TbeTaskBuilder::DoRegisterFunction(void *bin_handle, const char *stub_name, const char *kernel_name) {
  auto rt_ret = rtFunctionRegister(bin_handle, stub_name, stub_name, kernel_name, FUNC_MODE_NORMAL);
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(rt_ret, "rtFunctionRegister failed. bin key = %s, kernel name = %s, rt ret = %d", stub_name, kernel_name,
           static_cast<int>(rt_ret));
    return rt_ret;
  }

  return SUCCESS;
}

Status TbeTaskBuilder::DoRegisterKernel(const ge::OpKernelBin &tbe_kernel, const char *bin_file_key, void **bin_handle,
                                        const SingleOpModelParam &param) {
  std::string kernel_name;
  GetKernelName(op_desc_, kernel_name);

  void *handle = nullptr;
  auto ret = DoRegisterBinary(tbe_kernel, &handle, param);
  if (ret != SUCCESS) {
    return ret;
  }

  ret = DoRegisterMeta(handle);
  if (ret != SUCCESS) {
    GE_CHK_RT(rtDevBinaryUnRegister(handle));
    return ret;
  }

  ret = DoRegisterFunction(handle, bin_file_key, kernel_name.c_str());
  if (ret != SUCCESS) {
    GE_CHK_RT(rtDevBinaryUnRegister(handle));
    return ret;
  }

  GELOGI("Register function succeeded: kernel_name = %s", kernel_name.c_str());
  *bin_handle = handle;
  return SUCCESS;
}

Status TbeTaskBuilder::RegisterKernel(TbeOpTask &task, const SingleOpModelParam &param) {
  KernelBinRegistry &registry = KernelBinRegistry::GetInstance();
  // check if already registered
  const char *stub_func = registry.GetStubFunc(stub_name_);
  if (stub_func != nullptr) {
    task.SetStubFunc(stub_name_, stub_func);
    return SUCCESS;
  }

  // to avoid repeat register
  std::lock_guard<std::mutex> lock(g_reg_mutex);
  // check again
  stub_func = registry.GetStubFunc(stub_name_);
  if (stub_func == nullptr) {
    stub_func = registry.GetUnique(stub_name_);
    GELOGI("RegisterKernel begin, stub_func = %s", stub_func);

    auto tbe_kernel = GetTbeKernel(op_desc_);
    if (tbe_kernel == nullptr) {
      GELOGE(ACL_ERROR_GE_INTERNAL_ERROR, "OP EXT ATTR NAME TBE_KERNEL not found. op = %s",
             op_desc_->GetName().c_str());
      return ACL_ERROR_GE_INTERNAL_ERROR;
    }

    auto holder = std::unique_ptr<KernelHolder>(new (std::nothrow) KernelHolder(stub_func, tbe_kernel));
    if (holder == nullptr) {
      GELOGE(ACL_ERROR_GE_MEMORY_ALLOCATION, "create KernelHodler failed.");
      return ACL_ERROR_GE_MEMORY_ALLOCATION;
    }

    void *bin_handle = nullptr;
    auto ret = DoRegisterKernel(*tbe_kernel, stub_func, &bin_handle, param);
    if (ret == SUCCESS) {
      holder->SetBinHandle(bin_handle);
      if (!registry.AddKernel(stub_name_, std::move(holder))) {
        // should not happen. only one thread can reach here
        GELOGE(ACL_ERROR_GE_INTERNAL_ERROR, "Add kernel failed. stub name = %s", stub_name_.c_str());
        return ACL_ERROR_GE_INTERNAL_ERROR;
      }
    }
  }

  task.SetStubFunc(stub_name_, stub_func);
  return SUCCESS;
}

Status TbeTaskBuilder::GetSmDesc(void **sm_desc, const SingleOpModelParam &param) const {
  const std::string &sm_desc_str = kernel_def_.sm_desc();
  if (sm_desc_str.empty()) {
    *sm_desc = nullptr;
  } else {
    GELOGD("To process sm desc, size = %zu", sm_desc_str.size());
    char *sm_control = const_cast<char *>(sm_desc_str.data());
    auto *l2_ctrl_info = reinterpret_cast<rtL2Ctrl_t *>(sm_control);
    uint64_t gen_base_addr = param.base_addr;
    // There is no weight for te op now. Update L2_mirror_addr by data memory base.
    uint64_t data_base_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(param.mem_base)) - gen_base_addr;
    for (auto &data_index : l2_ctrl_info->data) {
      if (data_index.L2_mirror_addr != 0) {
        data_index.L2_mirror_addr += data_base_addr;
      }
    }

    auto rtRet = rtMemAllocManaged(sm_desc, sm_desc_str.size(), RT_MEMORY_SPM);
    if (rtRet != RT_ERROR_NONE) {
      GELOGE(rtRet, "rtMemAllocManaged failed, ret: %d", static_cast<int>(rtRet));
      return rtRet;
    }

    rtRet = rtMemcpy(*sm_desc, sm_desc_str.size(), sm_desc_str.data(), sm_desc_str.size(), RT_MEMCPY_HOST_TO_DEVICE);
    if (rtRet != RT_ERROR_NONE) {
      (void)rtMemFreeManaged(*sm_desc);
      GELOGE(rtRet, "rtMemcpy, ret: %d", static_cast<int>(rtRet));
      return rtRet;
    }
  }

  return SUCCESS;
}

Status TbeTaskBuilder::SetKernelArgs(TbeOpTask &task, const SingleOpModelParam &param, const OpDescPtr &op_desc) {
  size_t arg_size = kernel_def_.args_size();
  auto args = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[arg_size]);
  GE_CHECK_NOTNULL(args);

  auto rtRet = rtMemcpy(args.get(), arg_size, kernel_def_.args().data(), arg_size, RT_MEMCPY_HOST_TO_HOST);
  if (rtRet != RT_ERROR_NONE) {
    GELOGE(rtRet, "rtMemcpy args failed, size = %zu, ret = %d", arg_size, static_cast<int>(rtRet));
    return RT_ERROR_TO_GE_STATUS(rtRet);
  }

  const domi::KernelContext &context = kernel_def_.context();
  const auto *args_offset_tmp = reinterpret_cast<const uint16_t *>(context.args_offset().data());
  uint16_t offset = *args_offset_tmp;

  bool is_dynamic = false;
  (void)AttrUtils::GetBool(op_desc_, kAttrSupportDynamicShape, is_dynamic);
  if (is_dynamic) {
    GE_CHK_STATUS_RET_NOLOG(InitTilingInfo(task));
  } else {
    // copy args
    std::vector<void *> tensor_device_addr_vec = BuildTaskUtils::GetKernelArgs(op_desc_, param);
    void *src_addr = reinterpret_cast<void *>(tensor_device_addr_vec.data());
    uint64_t src_len = sizeof(void *) * tensor_device_addr_vec.size();
    rtRet = rtMemcpy(args.get() + offset, arg_size - offset, src_addr, src_len, RT_MEMCPY_HOST_TO_HOST);
    if (rtRet != RT_ERROR_NONE) {
      GELOGE(rtRet, "rtMemcpy addresses failed, ret = %d", static_cast<int>(rtRet));
      return RT_ERROR_TO_GE_STATUS(rtRet);
    }
  }

  task.SetKernelArgs(std::move(args), arg_size, kernel_def_.block_dim(), op_desc);
  return SUCCESS;
}

Status TbeTaskBuilder::BuildTask(TbeOpTask &task, const SingleOpModelParam &param) {
  GELOGD("Build tbe task begin");
  auto ret = SetKernelArgs(task, param, op_desc_);
  if (ret != SUCCESS) {
    return ret;
  }

  ret = RegisterKernel(task, param);
  if (ret != SUCCESS) {
    return ret;
  }
  auto task_info = BuildTaskUtils::GetTaskInfo(op_desc_);
  GELOGI("[TASK_INFO] %s %s", stub_name_.c_str(), task_info.c_str());

  void *stub_func = nullptr;
  auto rtRet = rtGetFunctionByName(stub_name_.c_str(), &stub_func);
  if (rtRet != SUCCESS) {
    GELOGE(rtRet, "rtGetFunctionByName failed.");
    return RT_ERROR_TO_GE_STATUS(rtRet);
  }

  task.SetStubFunc(stub_name_, stub_func);
  return SUCCESS;
}

Status TbeTaskBuilder::InitTilingInfo(TbeOpTask &task) {
  GELOGD("Start alloc tiling data of node %s.", op_desc_->GetName().c_str());
  int64_t max_size = -1;
  (void)AttrUtils::GetInt(op_desc_, kAttrOpParamSize, max_size);
  GELOGD("Got op param size by key: %s, ret = %ld", kAttrOpParamSize, max_size);
  if (max_size <= 0) {
    GELOGE(ACL_ERROR_GE_PARAM_INVALID, "[%s] Invalid op_param_size: %ld.", op_desc_->GetName().c_str(), max_size);
    return ACL_ERROR_GE_PARAM_INVALID;
  }

  void *tiling_buffer = nullptr;
  GE_CHK_RT_RET(rtMalloc(&tiling_buffer, static_cast<uint64_t>(max_size), RT_MEMORY_HBM));
  GE_CHECK_NOTNULL(tiling_buffer);
  GELOGD("[%s] Done allocating tiling buffer, size=%ld.", op_desc_->GetName().c_str(), max_size);

  task.EnableDynamicSupport(node_, tiling_buffer, static_cast<size_t>(max_size));
  return SUCCESS;
}
}  // namespace ge
