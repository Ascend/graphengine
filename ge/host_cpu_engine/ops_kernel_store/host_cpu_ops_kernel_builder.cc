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

#include "host_cpu_ops_kernel_builder.h"
#include <memory>
#include "common/ge_inner_error_codes.h"
#include "ge/ge_api_types.h"
#include "graph/utils/node_utils.h"
#include "graph/utils/tensor_utils.h"
#include "graph/utils/type_utils.h"
#include "framework/common/debug/ge_log.h"
#include "host_cpu_engine/common/constant/constant.h"
#include "register/ops_kernel_builder_registry.h"

namespace ge {
namespace host_cpu {
REGISTER_OPS_KERNEL_BUILDER(kHostCpuOpKernelLibName, HostCpuOpsKernelBuilder);

Status HostCpuOpsKernelBuilder::Finalize() {
  return SUCCESS;
}
Status HostCpuOpsKernelBuilder::Initialize(const map<std::string, std::string> &options) {
  return SUCCESS;
}

Status HostCpuOpsKernelBuilder::CalcOpRunningParam(Node &ge_node) {
  OpDescPtr op_desc = ge_node.GetOpDesc();
  if (op_desc == nullptr) {
    GELOGE(FAILED, "CalcOpRunningParam failed, as op desc is null");
    return FAILED;
  }

  bool is_shape_unknown = false;
  if (NodeUtils::GetNodeUnknownShapeStatus(ge_node, is_shape_unknown) == GRAPH_SUCCESS) {
    if (is_shape_unknown) {
      GELOGI("op:%s is unknown shape, does not need to calc output size.", ge_node.GetName().c_str());
      return SUCCESS;
    }
  }

  const string name = ge_node.GetName();
  const string type = ge_node.GetType();
  GELOGD("Calc op[%s:%s] running param, output size=%zu.", name.c_str(), type.c_str(), op_desc->GetOutputsSize());

  for (size_t i = 0; i < op_desc->GetOutputsSize(); ++i) {
    GeTensorDesc output_tensor = op_desc->GetOutputDesc(static_cast<uint32_t>(i));
    Format format = output_tensor.GetFormat();
    DataType data_type = output_tensor.GetDataType();

    int64_t mem_size = 0;
    // If mem size has been set, no need reset.
    if ((TensorUtils::GetSize(output_tensor, mem_size) == GRAPH_SUCCESS) && (mem_size > 0)) {
      GELOGD("Op[%s:%s] out[%zu] mem size has been set, no need calc again, format=%s, data_type=%s, mem_size=%ld.",
             name.c_str(), type.c_str(), i, TypeUtils::FormatToSerialString(format).c_str(),
             TypeUtils::DataTypeToSerialString(data_type).c_str(), mem_size);
      continue;
    }

    int64_t output_mem_size = 0;
    GeShape output_shape = output_tensor.GetShape();
    if ((TensorUtils::CalcTensorMemSize(output_shape, format, data_type, output_mem_size) != GRAPH_SUCCESS) ||
        (output_mem_size < 0)) {
      GELOGE(FAILED, "Calc op[%s:%s] out[%zu] mem size failed, mem_size=%ld, format=%s, data_type=%s.",
             name.c_str(), type.c_str(), i, output_mem_size, TypeUtils::FormatToSerialString(format).c_str(),
             TypeUtils::DataTypeToSerialString(data_type).c_str());
      return FAILED;
    }
    GELOGI("Calc op[%s:%s] out[%zu] mem size is %ld, format=%s, data_type=%s.",
           name.c_str(), type.c_str(), i, output_mem_size, TypeUtils::FormatToSerialString(format).c_str(),
           TypeUtils::DataTypeToSerialString(data_type).c_str());

    TensorUtils::SetSize(output_tensor, output_mem_size);
    if (op_desc->UpdateOutputDesc(static_cast<uint32_t>(i), output_tensor) != GRAPH_SUCCESS) {
      GELOGE(FAILED, "Update op[%s:%s] out[%zu] desc failed, format=%s, data_type=%s.", name.c_str(), type.c_str(), i,
             TypeUtils::FormatToSerialString(format).c_str(), TypeUtils::DataTypeToSerialString(data_type).c_str());
      return FAILED;
    }
  }

  GELOGD("Calc op[%s:%s] running param success.", name.c_str(), type.c_str());
  return SUCCESS;
}

Status HostCpuOpsKernelBuilder::GenerateTask(const Node &node, RunContext &context, vector<domi::TaskDef> &tasks) {
  // no need to generate device task
  return SUCCESS;
}
}  // namespace host_cpu
}  // namespace ge