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

#ifndef INC_FRAMEWORK_MEMORY_MEMORY_API_H_
#define INC_FRAMEWORK_MEMORY_MEMORY_API_H_

#include <string>
#include <vector>

#include "ge/ge_api_error_codes.h"
#include "runtime/mem.h"

namespace ge {
enum MemStorageType {
  HBM = 0,
  RDMA_HBM,
  HOST_DDR,
};

struct HostVarInfo {
  uint64_t base_addr;
  uint64_t var_size;
};

///
/// \param size [in] rdma pool memory size to be allocated.
/// \param mem_type [in] memory type for rdma pool.
/// \return Status result of function
Status InitRdmaPool(size_t size, rtMemType_t mem_type = RT_MEMORY_HBM);

///
/// \param var_info [in] host variable addr infos.
/// \param mem_type [in] memory type for rdma pool.
/// \return Status result of function
Status RdmaRemoteRegister(const std::vector<HostVarInfo> &var_info, rtMemType_t mem_type = RT_MEMORY_HBM);

///
/// \param var_name [in] var_name name of host variable.
/// \param base_addr [out] base_addr vase addr of host variable.
/// \param var_size [out] var_size memory_size of host variable.
/// \return Status result of function
Status GetVarBaseAddrAndSize(const std::string &var_name, uint64_t &base_addr, uint64_t &var_size);
}  // namespace ge
#endif  // INC_FRAMEWORK_MEMORY_MEMORY_API_H_
