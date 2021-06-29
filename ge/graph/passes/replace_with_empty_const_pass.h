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

#ifndef GE_GRAPH_PASSES_REPLACE_WITH_EMPTY_CONST_PASS_H_
#define GE_GRAPH_PASSES_REPLACE_WITH_EMPTY_CONST_PASS_H_

#include "graph/passes/folding_pass.h"

namespace ge {
class ReplaceWithEmptyConstPass : public FoldingPass {
 public:
  Status Run(NodePtr &node) override;

 private:
  Status GetOutputsOfCurrNode(const NodePtr &node_to_replace, vector<GeTensorPtr> &outputs);
  bool IsKnownEmptyTenor(const GeShape &shape) const;
};
}  // namespace ge
#endif  // GE_GRAPH_PASSES_REPLACE_WITH_EMPTY_CONST_PASS_H_
