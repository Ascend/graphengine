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

#ifndef GE_GRAPH_PASSES_FOLDING_KERNEL_PACK_KERNEL_H_
#define GE_GRAPH_PASSES_FOLDING_KERNEL_PACK_KERNEL_H_

#include <vector>

#include "inc/kernel.h"

namespace ge {
/**
 * @ingroup ge
 * @brief Pack operator processing
 * @author
 */
class PackKernel : public Kernel {
 public:
  Status Compute(const ge::OpDescPtr op_desc_ptr, const std::vector<ge::ConstGeTensorPtr> &input,
                 std::vector<ge::GeTensorPtr> &v_output) override;
 private:
  Status ValidateKernelParams(const ge::OpDescPtr &op_desc_ptr, const std::vector<ge::ConstGeTensorPtr> &input);
  Status ValidateInputs(const ge::OpDescPtr &op_desc_ptr, const std::vector<ge::ConstGeTensorPtr> &input);
  void ExpandDims(const int64_t axis, const std::vector<ge::ConstGeTensorPtr> &input, GeShape &final_shape);
  Status CopyOutputData(const GeShape &final_shape, const std::vector<ge::ConstGeTensorPtr> &input,
                        ge::GeTensorPtr &output_ptr);
  int64_t n_ = 0;       // count of inputs
  int64_t axis_ = 0;    // axis to stack along.
  DataType data_type_;  // data type of inputs
};
}  // namespace ge

#endif  // GE_GRAPH_PASSES_FOLDING_KERNEL_PACK_KERNEL_H_
