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

#include "external/graph/graph.h"
#include "inc/graph_pass.h"

#ifndef GE_GRAPH_PASSES_MARK_SAME_ADDR_PASS_H_
#define GE_GRAPH_PASSES_MARK_SAME_ADDR_PASS_H_

namespace ge {
class MarkSameAddrPass : public GraphPass {
 public:
  Status Run(ComputeGraphPtr graph);

 private:
  bool IsNextNodeExpected(const ge::NodePtr &cur_node, const vector<string> &next_nodes, int &out_anchor_idx);
};
}  // namespace ge
#endif  // GE_GRAPH_PASSES_MARK_SAME_ADDR_PASS_H_
