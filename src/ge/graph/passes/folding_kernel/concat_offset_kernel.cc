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

#include "graph/passes/folding_kernel/concat_offset_kernel.h"

#include <memory>

#include "common/ge_inner_error_codes.h"
#include "common/op/ge_op_utils.h"
#include "common/types.h"
#include "framework/common/debug/ge_log.h"
#include "graph/utils/type_utils.h"
#include "inc/kernel_factory.h"

namespace ge {
namespace {
const size_t kConcatOffsetInputIndexZero = 0;
const size_t kConcatOffsetInputIndexOne = 1;
const int kNumOne = 1;
}  // namespace
Status ConcatOffsetKernel::Compute(const OpDescPtr op_desc_ptr, const vector<ConstGeTensorPtr> &input,
                                   vector<GeTensorPtr> &v_output) {
  GELOGI("ConcatOffsetKernel in.");
  if (op_desc_ptr == nullptr) {
    GELOGE(PARAM_INVALID, "input opdesc is nullptr.");
    return PARAM_INVALID;
  }
  // validate attrs
  int N = 0;
  if (!(AttrUtils::GetInt(op_desc_ptr, "N", N))) {
    GELOGE(PARAM_INVALID, "Attr %s is not exist.", "N");
    return NOT_CHANGED;
  }
  // follow IR def, the first input is concat_dim
  ConstGeTensorPtr input_0 = input[kConcatOffsetInputIndexZero];
  GE_CHECK_NOTNULL(input_0);
  int32_t concat_dim = *(const_cast<int32_t *>(reinterpret_cast<const int32_t *>(input_0->GetData().data())));
  // validate inputs
  if (static_cast<int>(input.size()) != (N + kNumOne) || input.size() <= kConcatOffsetInputIndexOne) {
    GELOGE(PARAM_INVALID, "The number of input for concat offset must be equal with %d, and must be more than one.",
           (N + kNumOne));
    return NOT_CHANGED;
  }

  // calculate ouput dim
  GeShape output_shape = input[kConcatOffsetInputIndexOne]->GetTensorDesc().GetShape();
  int64_t output_size = output_shape.GetShapeSize();
  if (concat_dim >= output_size) {
    GELOGE(PARAM_INVALID, "Concat dim is biger than the size of output_shape.");
    return NOT_CHANGED;
  }
  GELOGI("Output shape size is %ld", output_size);
  int32_t offset = 0;
  unique_ptr<int32_t[]> buf(new (std::nothrow) int32_t[output_size]());
  if (buf == nullptr) {
    GELOGE(MEMALLOC_FAILED, "new buf failed");
    return INTERNAL_ERROR;
  }
  for (size_t i = 0; i < static_cast<size_t>(N); i++) {
    buf[concat_dim] = offset;
    // generate output
    GeTensorPtr output_ptr = MakeShared<GeTensor>();
    if (output_ptr == nullptr) {
      GELOGE(MEMALLOC_FAILED, "Failed to fold node %s, out of memeory", op_desc_ptr->GetName().c_str());
      return NOT_CHANGED;
    }

    output_ptr->MutableTensorDesc().SetDataType(DT_INT32);
    output_ptr->MutableTensorDesc().SetShape(output_shape);
    GE_IF_BOOL_EXEC(output_ptr->SetData(reinterpret_cast<uint8_t *>(buf.get()),
                                        static_cast<size_t>(sizeof(DT_INT32) * output_size)) != GRAPH_SUCCESS,
                    GELOGE(INTERNAL_ERROR, "set data failed");
                    return NOT_CHANGED);
    v_output.push_back(output_ptr);
    // caculate offset
    int64_t input_dim = input[i + kConcatOffsetInputIndexOne]->GetTensorDesc().GetShape().GetDim(concat_dim);
    if (input_dim > (INT32_MAX - offset)) {
      GELOGE(PARAM_INVALID, " %d and %ld addition can result in overflow!.", offset, input_dim);
      return INTERNAL_ERROR;
    }
    offset += input_dim;
  }
  GELOGI("ConcatOffsetKernel success.");
  return SUCCESS;
}
REGISTER_KERNEL(CONCATOFFSET, ConcatOffsetKernel);
}  // namespace ge
