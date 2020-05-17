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

#ifndef GE_SINGLE_OP_TASK_TBE_TASK_BUILDER_H_
#define GE_SINGLE_OP_TASK_TBE_TASK_BUILDER_H_

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "graph/op_desc.h"
#include "single_op/single_op.h"
#include "single_op/single_op_model.h"

namespace ge {
class KernelHolder {
 public:
  KernelHolder(const char *stub_func, std::shared_ptr<ge::OpKernelBin> kernel_bin);
  ~KernelHolder();

  void SetBinHandle(void *bin_handle) { bin_handle_ = bin_handle; }

 private:
  friend class KernelBinRegistry;
  const char *stub_func_;
  void *bin_handle_;
  std::shared_ptr<ge::OpKernelBin> kernel_bin_;
};

class KernelBinRegistry {
 public:
  ~KernelBinRegistry();

  static KernelBinRegistry &GetInstance() {
    static KernelBinRegistry instance;
    return instance;
  }

  const char *GetUnique(const string &stub_func);

  const char *GetStubFunc(const std::string &stub_name);

  bool AddKernel(const std::string &stub_name, const KernelHolder *holder);

 private:
  std::map<std::string, const KernelHolder *> registered_bins_;
  std::set<std::string> unique_stubs_;
  std::mutex mutex_;
};

class TbeTaskBuilder {
 public:
  TbeTaskBuilder(const std::string &model_name, const OpDescPtr &op_desc, const domi::KernelDef &kernel_def);
  ~TbeTaskBuilder() = default;

  Status BuildTask(TbeOpTask &task, const SingleOpModelParam &param);

 private:
  Status SetKernelArgs(TbeOpTask &task, const SingleOpModelParam &param);
  Status GetSmDesc(void **sm_desc, const SingleOpModelParam &param) const;

  Status RegisterKernel(TbeOpTask &task);
  Status DoRegisterKernel(const OpKernelBin &kernel_bin, const char *bin_file_key, void **bin_handle);
  Status DoRegisterBinary(const OpKernelBin &kernel_bin, void **bin_handle) const;
  Status DoRegisterMeta(void *bin_handle);

  static Status DoRegisterFunction(void *bin_handle, const char *stub_name, const char *kernel_name);

  const OpDescPtr &op_desc_;
  const domi::KernelDef &kernel_def_;
  const std::string stub_name_;
};
}  // namespace ge

#endif  // GE_SINGLE_OP_TASK_TBE_TASK_BUILDER_H_
