/**
 * Copyright 2020 Huawei Technologies Co., Ltd
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

#ifndef GE_HOST_CPU_ENGINE_OPS_KERNEL_STORE_HOST_CPU_OPS_KERNEL_INFO_H_
#define GE_HOST_CPU_ENGINE_OPS_KERNEL_STORE_HOST_CPU_OPS_KERNEL_INFO_H_

#if defined(_MSC_VER)
#ifdef FUNC_VISIBILITY
#define GE_FUNC_VISIBILITY _declspec(dllexport)
#else
#define GE_FUNC_VISIBILITY
#endif
#else
#ifdef FUNC_VISIBILITY
#define GE_FUNC_VISIBILITY __attribute__((visibility("default")))
#else
#define GE_FUNC_VISIBILITY
#endif
#endif

#include <map>
#include <string>
#include <vector>

#include "common/opskernel/ops_kernel_info_store.h"

namespace ge {
namespace host_cpu {
class GE_FUNC_VISIBILITY HostCpuOpsKernelInfoStore : public OpsKernelInfoStore {
 public:
  HostCpuOpsKernelInfoStore() {}
  ~HostCpuOpsKernelInfoStore() override = default;

  /**
   * Initialize related resources of the host cpu kernelinfo store
   * @return status whether this operation success
   */
  Status Initialize(const std::map<std::string, std::string> &options) override;

  /**
   * Release related resources of the host cpu kernel info store
   * @return status whether this operation success
   */
  Status Finalize() override;

  /**
   * Check to see if an operator is fully supported or partially supported.
   * @param op_desc OpDesc information
   * @param reason unsupported reason
   * @return bool value indicate whether the operator is fully supported
   */
  bool CheckSupported(const OpDescPtr &op_desc, std::string &reason) const override;

  /**
   * Returns the full operator information.
   * @param infos reference of a map,
   *        contain operator's name and detailed information
   */
  void GetAllOpsKernelInfo(std::map<std::string, ge::OpInfo> &infos) const override;

  HostCpuOpsKernelInfoStore(const HostCpuOpsKernelInfoStore &ops_kernel_store) = delete;
  HostCpuOpsKernelInfoStore(const HostCpuOpsKernelInfoStore &&ops_kernel_store) = delete;
  HostCpuOpsKernelInfoStore &operator=(const HostCpuOpsKernelInfoStore &ops_kernel_store) = delete;
  HostCpuOpsKernelInfoStore &operator=(HostCpuOpsKernelInfoStore &&ops_kernel_store) = delete;

 private:
  // store op name and OpInfo key-value pair
  std::map<std::string, ge::OpInfo> op_info_map_;
};
}  // namespace host_cpu
}  // namespace ge

#endif  // GE_HOST_CPU_ENGINE_OPS_KERNEL_STORE_HOST_CPU_OPS_KERNEL_INFO_H_
