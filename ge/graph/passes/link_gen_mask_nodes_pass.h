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

#ifndef GE_GRAPH_PASSES_LINK_GEN_MASK_NODES_PASS_H_
#define GE_GRAPH_PASSES_LINK_GEN_MASK_NODES_PASS_H_

#include <map>
#include <string>
#include <vector>

#include "external/graph/graph.h"
#include "inc/graph_pass.h"

namespace ge {
// Link all GenMask nodes using control edges.
class LinkGenMaskNodesPass : public GraphPass {
 public:
  LinkGenMaskNodesPass(const std::map<std::string, int> &stream_max_parallel_num);
  ~LinkGenMaskNodesPass() override = default;
  LinkGenMaskNodesPass(const LinkGenMaskNodesPass &) = delete;
  LinkGenMaskNodesPass &operator=(const LinkGenMaskNodesPass &) = delete;

  Status Run(ComputeGraphPtr graph) override;

 private:
  bool AreAllInputsConst(const NodePtr &node) const;
  void GetAllGenMaskNodes(ComputeGraphPtr graph, std::vector<NodePtr> &gen_mask_nodes) const;
  Status GetGenMaskGroupSize(std::vector<NodePtr> &gen_mask_nodes, size_t &gen_mask_group_size) const;

  const std::map<std::string, int> stream_max_parallel_num_;
};
}  // namespace ge

#endif  // GE_GRAPH_PASSES_LINK_GEN_MASK_NODES_PASS_H_
