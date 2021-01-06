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

#ifndef GE_GRAPH_PASSES_ASSIGN_REMOVE_PASS_H_
#define GE_GRAPH_PASSES_ASSIGN_REMOVE_PASS_H_

#include "graph/passes/base_pass.h"

namespace ge {
class AssignRemovePass : public BaseNodePass {
 public:
  Status Run(NodePtr &node) override;

 private:
  ///
  /// @brief Optimize for assign_node
  /// @param [in] assign_node
  /// @return Status
  ///
  Status OptimizedAssignNode(NodePtr &assign_node);

  ///
  /// @brief Transform assign_var_name attr
  /// @param [in] node
  /// @return Status
  ///
  Status TransformAttr(NodePtr &node);

  ///
  /// @brief Check if need optimize for assign_node
  /// @param [in] assign_node
  /// @param [in] peer_data_anchor for ref_input of assign_node
  /// @param [in] peer_data_anchor for value_input of assign_node
  /// @return Status
  ///
  static bool IsCondMatch(const NodePtr &node, const OutDataAnchorPtr &ref_peer_anchor,
                          const OutDataAnchorPtr &value_peer_anchor);
};
}  // namespace ge
#endif  // GE_GRAPH_PASSES_ASSIGN_REMOVE_PASS_H_
