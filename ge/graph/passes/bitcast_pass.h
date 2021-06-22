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

#ifndef GE_GRAPH_PASSES_BITCAST_PASS_H_
#define GE_GRAPH_PASSES_BITCAST_PASS_H_

#include "framework/common/ge_inner_error_codes.h"
#include "framework/common/debug/ge_log.h"
#include "framework/common/types.h"
#include "external/graph/graph.h"
#include "graph/op_desc.h"
#include "graph/passes/base_pass.h"
#include "graph/passes/pass_utils.h"

namespace ge {
class BitcastPass : public BaseNodePass {
 public:
  Status Run(ge::NodePtr &node) override;
  typedef std::vector<int64_t> kVecInt64;

 private:
  Status CheckDstDataType(const OpDescPtr op_desc, ge::DataType &dst_data_type);
  Status CheckOutputShape(const OpDescPtr op_desc, const ge::DataType dst_data_type);
  Status CalcAndUpdateShape(BitcastPass::kVecInt64 &dim_vec, ge::DataType ori_data_type,
                            ge::DataType dst_data_type);
};
}  // namespace ge

#endif  // GE_GRAPH_PASSES_BITCAST_PASS_H_
