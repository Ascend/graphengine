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

#ifndef GE_GRAPH_PASSES_TRANSPOSE_TRANSDATA_PASS_H_
#define GE_GRAPH_PASSES_TRANSPOSE_TRANSDATA_PASS_H_

#include "graph/passes/base_pass.h"

namespace ge {
class TransposeTransDataPass : public BaseNodePass {
 public:
  Status Run(NodePtr &node) override;
 private:
  Status CheckOneInAndOneOutDataAnchor(NodePtr &node) const;
  Status RemoveTranspose(NodePtr &node);
  bool FusionIfNeed(OpDescPtr &op_desc, OpDescPtr &transdata_op_desc);
  void CopyInputEdges(NodePtr &origin_node, NodePtr &new_node);
  bool TransDataCheckAccuracySupported(const OpDescPtr &op_desc);
};
}  // namespace ge
#endif  // GE_GRAPH_PASSES_TRANSPOSE_TRANSDATA_PASS_H_

