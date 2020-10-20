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

#ifndef GE_GRAPH_PASSES_FOLDING_KERNEL_DYNAMIC_STITCH_KERNEL_H_
#define GE_GRAPH_PASSES_FOLDING_KERNEL_DYNAMIC_STITCH_KERNEL_H_

#include <map>
#include <vector>

#include "inc/kernel.h"

namespace ge {
class DynamicStitchKernel : public Kernel {
 public:
  Status Compute(const OpDescPtr attr, const std::vector<ConstGeTensorPtr> &input,
                 std::vector<GeTensorPtr> &v_output) override;

 private:
  Status ValidateParams(const OpDescPtr &attr, const std::vector<ConstGeTensorPtr> &input);
  void ComputeMergedShape(const vector<ConstGeTensorPtr> &input, GeShape &merged_shape);
  Status GenData(const vector<ConstGeTensorPtr> &input, GeTensorPtr &output_ptr);
  Status StitchDataFollowIndices(int64_t data_unit, const vector<ConstGeTensorPtr> &input, int64_t allowance,
                                 std::unique_ptr<uint8_t[]> &buf);
  int n_;  // data input number
};
}  // namespace ge

#endif  // GE_GRAPH_PASSES_FOLDING_KERNEL_DYNAMIC_STITCH_KERNEL_H_
