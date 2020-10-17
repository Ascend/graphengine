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

#include "common_subexpression_elimination_pass.h"

#include <sstream>
#include <string>
#include <set>

#include "common/base64.h"
#include "graph/utils/node_utils.h"
#include "ge_local_engine/engine/host_cpu_engine.h"
#include "graph/passes/folding_pass.h"

namespace ge {
namespace {
std::string GetCseKey(const NodePtr &node) {
  std::stringstream ss;
  ss << node->GetType() << "-data-inputs-";
  for (auto &in_anchor : node->GetAllInDataAnchors()) {
    auto src_anchor = in_anchor->GetPeerOutAnchor();
    if (src_anchor == nullptr) {
      ss << in_anchor->GetIdx() << "-null-";
    } else {
      ss << in_anchor->GetIdx() << "-"
         << src_anchor->GetOwnerNode()->GetName() << "-"
         << src_anchor->GetIdx() << "-";
    }
  }

  ss << "control-inputs-";
  std::set<std::string> control_in_node_names;
  for (auto &src_node : node->GetInControlNodes()) {
    control_in_node_names.insert(src_node->GetName());
  }
  for (auto &name : control_in_node_names) {
    ss << name << "-";
  }

  ss << "attrs-" << AttrUtils::GetAllAttrsStr(node->GetOpDesc());

  return ss.str();
}

/// As the operator category has not been defined, we do not know what types of node can be processed by CSE.
/// To avoid delete wrong nodes(e.g. stateful nodes),
/// only nodes have folding kernel will be considered for the CSE process
bool IsNodeSupportCse(const NodePtr &node) {
  if (HostCpuEngine::CheckSupported(NodeUtils::GetNodeType(*node))) {
    return true;
  }
  return folding_pass::GetKernelByType(node) != nullptr;
}
}  // namespace
Status CommonSubexpressionEliminationPass::Run(ComputeGraphPtr graph) {
  GELOGD("Begin to run the CSE process on the graph");
  GE_CHECK_NOTNULL(graph);
  std::map<std::string, NodePtr> keys_to_node;
  for (const auto &node : graph->GetDirectNode()) {
    if (!IsNodeSupportCse(node)) {
      continue;
    }
    bool is_unknown = false;
    auto ret = NodeUtils::GetNodeUnknownShapeStatus(*node, is_unknown);
    if (ret != GRAPH_SUCCESS) {
      GELOGW("Get node unknown status failed, node name:%s, type:%s.",
             node->GetName().c_str(), node->GetType().c_str());
      continue;
    }
    if (is_unknown) {
      GELOGI("Current node %s, type %s is unknown shape which should be skip.",
             node->GetName().c_str(), node->GetType().c_str());
      continue;
    }
    auto key = GetCseKey(node);
    GELOGD("The node %s cse key %s", node->GetName().c_str(), ge::base64::EncodeToBase64(key).c_str());
    auto iter = keys_to_node.find(key);
    if (iter == keys_to_node.end()) {
      keys_to_node[key] = node;
      continue;
    }

    if (node->GetAllOutDataAnchorsSize() != iter->second->GetAllOutDataAnchorsSize()) {
      GELOGW("The node %s and %s have the same CSE key, but different output anchor count, skip to fusion them",
          iter->second->GetName().c_str(), node->GetName().c_str());
      continue;
    }

    std::vector<int> output_map(node->GetAllOutDataAnchorsSize());
    for (size_t i = 0; i < node->GetAllOutDataAnchorsSize(); ++i) {
      output_map[i] = i;
    }

    ret = GraphUtils::ReplaceNodeAnchors(iter->second, node, {}, output_map);
    if (ret != GRAPH_SUCCESS) {
      GELOGE(INTERNAL_ERROR, "Failed to replace node %s by node %s error node %u",
          node->GetName().c_str(), iter->second->GetName().c_str(), ret);
      return INTERNAL_ERROR;
    }

    NodeUtils::UnlinkAll(*node);

    ret = GraphUtils::RemoveNodeWithoutRelink(graph, node);
    if (ret != GRAPH_SUCCESS) {
      GELOGE(INTERNAL_ERROR, "Failed to remove node %s from graph", node->GetName().c_str());
      return INTERNAL_ERROR;
    }

    GELOGI("Remove node %s by the CSE process, replace it with node %s",
        node->GetName().c_str(), iter->second->GetName().c_str());
  }
  return SUCCESS;
}
}  // namespace ge

