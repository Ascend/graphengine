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

#include "graph/passes/merge_input_memcpy_pass.h"
#include "common/ge/ge_util.h"
#include "ge/ge_api_types.h"
#include "graph/common/omg_util.h"

namespace ge {
Status MergeInputMemcpyPass::Run(ComputeGraphPtr graph) {
  GELOGD("MergeInputMemcpyPass Enter");
  for (const auto &node : graph->GetDirectNode()) {
    if ((node->GetType() != MERGE) && (node->GetType() != REFMERGE)) {
      continue;
    }
    GE_CHECK_NOTNULL(node->GetOpDesc());
    GE_CHK_STATUS_RET(AddMemcpyAsyncNodes(graph, node, node->GetOpDesc()->HasAttr(ATTR_INSERT_BY_MBATCH)),
                      "Merge add memcpy node failed.");
  }
  GELOGD("MergeInputMemcpyPass Leave");
  return SUCCESS;
}

///
/// @brief Add MemcpyAsync Op as Merge in_node
/// @param [in] graph
/// @param [in] node
/// @param [in] multi_batch_flag
/// @return Status
///
Status MergeInputMemcpyPass::AddMemcpyAsyncNodes(const ComputeGraphPtr &graph, const NodePtr &node,
                                                 bool multi_batch_flag) {
  for (const InDataAnchorPtr &in_data_anchor : node->GetAllInDataAnchors()) {
    OutDataAnchorPtr peer_out_anchor = in_data_anchor->GetPeerOutAnchor();
    GE_IF_BOOL_EXEC(peer_out_anchor == nullptr, continue);
    NodePtr in_node = peer_out_anchor->GetOwnerNode();
    const std::string &type = in_node->GetType();
    // For WhileLoop no need memcpy for merge.
    GE_IF_BOOL_EXEC((type == ENTER) || (type == REFENTER) || (type == NEXTITERATION) || (type == REFNEXTITERATION),
                    continue);

    const std::string &memcpy_name = node->GetName() + "_input_" + std::to_string(in_data_anchor->GetIdx());
    NodePtr memcpy_node = CreateMemcpyAsyncNode(graph, memcpy_name, peer_out_anchor, multi_batch_flag);
    GE_CHK_BOOL_EXEC(memcpy_node != nullptr, return FAILED, "Create MemcpyAsync node failed.");
    GE_CHK_STATUS(GraphUtils::RemoveEdge(peer_out_anchor, in_data_anchor), "MemcpyAsync node remove edge failed.");
    GE_CHK_STATUS(GraphUtils::AddEdge(peer_out_anchor, memcpy_node->GetInDataAnchor(0)),
                  "MemcpyAsync node add edge failed.");
    GE_CHK_STATUS(GraphUtils::AddEdge(memcpy_node->GetOutDataAnchor(0), in_data_anchor),
                  "MemcpyAsync node add edge failed.");
  }

  return SUCCESS;
}

///
/// @brief Add MemcpyAsync Node
/// @param [in] graph
/// @param [in] name
/// @param [in] out_data_anchor
/// @param [in] multi_batch_flag
/// @return ge::NodePtr
///
NodePtr MergeInputMemcpyPass::CreateMemcpyAsyncNode(const ComputeGraphPtr &graph, const std::string &name,
                                                    const OutDataAnchorPtr &out_data_anchor, bool multi_batch_flag) {
  OpDescPtr pre_op_desc = out_data_anchor->GetOwnerNode()->GetOpDesc();
  GE_CHK_BOOL_EXEC(pre_op_desc != nullptr, return nullptr, "OpDesc of pre node is invalid.");

  const std::string &memcpy_type = multi_batch_flag ? MEMCPYADDRASYNC : MEMCPYASYNC;
  const std::string &node_name = name + "_" + memcpy_type;
  GELOGI("Create MemcpyAsync op:%s.", node_name.c_str());
  OpDescPtr op_desc = MakeShared<OpDesc>(node_name, memcpy_type);
  if (op_desc == nullptr) {
    GELOGE(FAILED, "Create op_desc failed, MemcpyAsync:%s.", node_name.c_str());
    return nullptr;
  }

  GE_CHK_BOOL_EXEC(op_desc->AddInputDesc(pre_op_desc->GetOutputDesc(out_data_anchor->GetIdx())) == GRAPH_SUCCESS,
                   return nullptr, "Create MemcpyAsync op: add input desc failed.");
  GE_CHK_BOOL_EXEC(op_desc->AddOutputDesc(pre_op_desc->GetOutputDesc(out_data_anchor->GetIdx())) == GRAPH_SUCCESS,
                   return nullptr, "Create MemcpyAsync op: add output desc failed.");

  return graph->AddNode(op_desc);
}
}  // namespace ge

