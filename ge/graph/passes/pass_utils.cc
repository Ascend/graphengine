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

#include "graph/passes/pass_utils.h"

#include <climits>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "framework/common/debug/ge_log.h"
#include "common/ge_inner_error_codes.h"
#include "common/ge/ge_util.h"
#include "common/op/ge_op_utils.h"
#include "common/types.h"
#include "graph/common/omg_util.h"
#include "graph/debug/ge_attr_define.h"
#include "graph/ge_tensor.h"
#include "graph/manager/graph_var_manager.h"
#include "graph/utils/graph_utils.h"
#include "graph/utils/op_desc_utils.h"
#include "graph/utils/tensor_utils.h"
#include "graph/utils/type_utils.h"

namespace ge {

Status PassUtils::ConstructTensorDescWithData(const GeTensorDesc &out_desc, std::vector<int64_t> &data,
                                              std::vector<GeTensorPtr> &v_output, const bool scalar_output) {
  Status ret = SUCCESS;
  const uint32_t dim_size = static_cast<uint32_t>(data.size());
  DataType data_type = out_desc.GetDataType();
  if (data_type == DT_INT32) {
    unique_ptr<int32_t[]> buf(new (std::nothrow) int32_t[dim_size]());
    if (buf == nullptr) {
      GELOGE(MEMALLOC_FAILED, "new failed");
      return MEMALLOC_FAILED;
    }
    for (uint32_t i = 0; i < dim_size; i++) {
      if (data[i] >= INT_MAX) {
        GELOGE(PARAM_INVALID, "int32 overflow, data[%u]:%ld", i, data[i]);
        return PARAM_INVALID;
      }
      buf[i] = static_cast<int32_t>(data[i]);
    }
    ret = ConstructTensorDescWithData(out_desc, buf.get(), dim_size, v_output, scalar_output);
  } else if (data_type == DT_INT64) {
    unique_ptr<int64_t[]> buf(new (std::nothrow) int64_t[dim_size]());
    if (buf == nullptr) {
      GELOGE(MEMALLOC_FAILED, "new failed");
      return MEMALLOC_FAILED;
    }
    for (uint32_t i = 0; i < dim_size; i++) {
      buf[i] = data[i];
    }
    ret = ConstructTensorDescWithData(out_desc, buf.get(), dim_size, v_output, scalar_output);
  } else {
    GELOGE(PARAM_INVALID, "Only support DT_INT32 and DT_INT64. data_type:%s",
           TypeUtils::DataTypeToSerialString(data_type).c_str());
    return PARAM_INVALID;
  }

  if (ret != SUCCESS) {
    GELOGE(ret, "GetShapeTensor failed.");
    return ret;
  }

  return SUCCESS;
}

template <typename T>
Status PassUtils::ConstructTensorDescWithData(const GeTensorDesc &out_desc, T *buf, uint32_t len,
                                              std::vector<GeTensorPtr> &v_output, const bool scalar_output) {
  // construct TensorDesc
  GeShape out_shape = (scalar_output ? GeShape() : GeShape({len}));
  GeTensorDesc output_tensor_desc(out_desc);
  output_tensor_desc.SetShape(out_shape);

  GeTensorPtr output_tensor_ptr = MakeShared<GeTensor>(
      output_tensor_desc, reinterpret_cast<uint8_t *>(buf), sizeof(T) * len);
  if (output_tensor_ptr == nullptr) {
    GELOGE(MEMALLOC_FAILED, "Make shared failed");
    return MEMALLOC_FAILED;
  }

  v_output.push_back(output_tensor_ptr);
  return SUCCESS;
}

bool PassUtils::IsConstant(const ConstNodePtr &node) {
  if (node == nullptr) {
    GELOGE(PARAM_INVALID, "node is null");
    return false;
  }

  auto src_node_type = node->GetType();
  bool is_constant = (src_node_type == CONSTANT) || (src_node_type == CONSTANTOP);
  return is_constant;
}

Status PassUtils::SetOutNodeWeight(const OutDataAnchorPtr &out_data_anchor, const NodePtr &src_node) {
  GE_IF_BOOL_EXEC(src_node == nullptr, GELOGE(PARAM_INVALID, "src_node is null"); return PARAM_INVALID);
  if (!IsConstant(src_node)) {
    return SUCCESS;
  }

  auto weights = OpDescUtils::MutableWeights(src_node);
  if (weights.empty()) {
    return PARAM_INVALID;
  }

  auto weight = weights.at(0);
  auto src_in_ctrl = src_node->GetInControlAnchor();
  if ((src_in_ctrl == nullptr) || (out_data_anchor == nullptr)) {
    GELOGE(FAILED, "parameter is null.");
    return FAILED;
  }
  auto src_out_control_anchors = src_in_ctrl->GetPeerAnchors();
  for (const auto &dst_in_data : out_data_anchor->GetPeerInDataAnchors()) {
    auto dst_node = dst_in_data->GetOwnerNode();
    auto dst_op_desc = dst_node->GetOpDesc();
    if (dst_op_desc == nullptr) {
      continue;
    }

    std::vector<bool> is_input_const = dst_op_desc->GetIsInputConst();
    auto input_index = static_cast<size_t>(dst_in_data->GetIdx());
    if (input_index < is_input_const.size()) {
      is_input_const[input_index] = true;
      dst_op_desc->SetIsInputConst(is_input_const);
    }

    GE_CHK_STATUS_RET(GraphUtils::RemoveEdge(out_data_anchor, dst_in_data), "remove edge failed");
    graphStatus ret = OpDescUtils::AddConstOpToAnchor(dst_in_data, weight);
    if (ret != SUCCESS) {
      return ret;
    }
    GE_CHECK_NOTNULL(dst_in_data->GetPeerOutAnchor());
    auto dynamic_const_node = dst_in_data->GetPeerOutAnchor()->GetOwnerNode();
    GE_CHECK_NOTNULL(dynamic_const_node->GetOpDesc());
    dynamic_const_node->GetOpDesc()->SetType(src_node->GetType());

    // restore control inputs to dynamically added constant ops, if any
    for (const auto &src_out_control_anchor : src_out_control_anchors) {
      GE_CHK_STATUS_RET(GraphUtils::AddEdge(src_out_control_anchor, dynamic_const_node->GetInControlAnchor()),
                        "add edge failed");
    }
  }

  /// Before:
  /// Op1 - - - > Constant ------> Switch - - - > Op2
  /// After:
  /// Op1 - - - > Op2
  for (const auto &dst_in_ctrl : out_data_anchor->GetPeerInControlAnchors()) {
    for (const auto &src_out_control_anchor : src_out_control_anchors) {
      GE_CHK_STATUS_RET(GraphUtils::AddEdge(src_out_control_anchor, dst_in_ctrl), "add edge failed");
    }
  }

  return SUCCESS;
}

Status PassUtils::RemoveBranch(const NodePtr &node, std::vector<NodePtr> &delete_nodes,
                               std::vector<NodePtr> &end_nodes) {
  if (node == nullptr) {
    GELOGE(FAILED, "parameter is null.");
    return FAILED;
  }
  GELOGI("Remove branch starting from node %s", node->GetName().c_str());
  std::queue<NodePtr> search_queue;
  search_queue.push(node);

  while (!search_queue.empty()) {
    const NodePtr src_node = search_queue.front();
    if (src_node == nullptr) {
      continue;
    }
    delete_nodes.push_back(src_node);
    search_queue.pop();

    for (const auto &src_out_anchor : src_node->GetAllOutAnchors()) {
      for (const auto &dst_in_anchor : src_out_anchor->GetPeerAnchors()) {
        if (dst_in_anchor == nullptr) {
          continue;
        }
        auto dst_node = dst_in_anchor->GetOwnerNode();
        std::string node_type;
        GE_CHK_STATUS_RET(GetOriginalType(dst_node, node_type), "get original type failed");
        if (node_type == NETOUTPUT) {
          if (dst_in_anchor->IsTypeOf<InDataAnchor>()) {
            GELOGE(INTERNAL_ERROR,
                   "[%s] Inactive branch connected to "
                   "NetOutput with data anchor.",
                   node->GetName().c_str());
            return INTERNAL_ERROR;
          } else {
            // safe to unlink control edges
            GE_CHK_STATUS_RET(GraphUtils::RemoveEdge(src_out_anchor, dst_in_anchor), "remove edge failed");
            end_nodes.push_back(dst_node);
          }
        } else if (node_type == MERGE) {
          /// Unlink connection between the inactive branch and Merge/NetOutput.
          /// The removal of inactive nodes will be handled in PrunePass
          GE_CHK_STATUS_RET(GraphUtils::RemoveEdge(src_out_anchor, dst_in_anchor), "remove edge failed");
          end_nodes.push_back(dst_node);
          GELOGD("Reach the end merge node %s, the branch removing stop", dst_node->GetName().c_str());
        } else {
          search_queue.push(dst_node);
        }
      }
    }
  }

  return SUCCESS;
}

NodePtr PassUtils::GetInDataNode(const ConstNodePtr &node, int index) {
  if (node == nullptr) {
    return nullptr;
  }

  auto in_data_anchor = node->GetInDataAnchor(index);
  if (in_data_anchor == nullptr) {
    return nullptr;
  }

  auto peer_out_data_anchor = in_data_anchor->GetPeerOutAnchor();
  if (peer_out_data_anchor == nullptr) {
    return nullptr;
  }

  auto src_node = peer_out_data_anchor->GetOwnerNode();
  return src_node;
}

bool PassUtils::IsNeedTrainIteFlowCtrl(const ComputeGraphPtr &compute_graph) {
  if (compute_graph == nullptr) {
    return false;
  }
  if (compute_graph->GetParentGraph() != nullptr) {
    GELOGI("Subgraph %s no need flow ctrl.", compute_graph->GetName().c_str());
    return false;
  }
  if (GraphUtils::IsUnknownShapeGraph(compute_graph)) {
    GELOGI("Unknown shape graph %s no need flow ctrl.", compute_graph->GetName().c_str());
    return false;
  }
  if (!ge::VarManager::Instance(compute_graph->GetSessionID())->IsVarExist(NODE_NAME_FLOWCTRL_LOOP_PER_ITER)) {
    return false;
  }
  return compute_graph->GetNeedIteration();
}

int PassUtils::GetUniqueInDataAnchorIndex(const NodePtr &node_ptr) {
  const int invalid_index = -1;
  if (node_ptr == nullptr) {
    GELOGE(INTERNAL_ERROR, "GetUniqueInDataAnchorIndex: node is null");
    return invalid_index;
  }
  for (const auto &in_anchor : node_ptr->GetAllInDataAnchors()) {
    if ((in_anchor != nullptr) && (in_anchor->GetPeerOutAnchor() != nullptr) &&
        (in_anchor->GetPeerOutAnchor()->GetOwnerNode() != nullptr)) {
      return (in_anchor->GetIdx());
    }
  }
  GELOGE(INTERNAL_ERROR,
         "GetUniqueInDataAnchorIndex: [%s] failed to find "
         "in data anchor with a valid peer out node",
         node_ptr->GetName().c_str());
  return invalid_index;
}

Status PassUtils::UnlinkNodeWithControlCopy(NodePtr &node, int index) {
  if (node == nullptr) {
    GELOGE(PARAM_INVALID, "node is null.");
    return PARAM_INVALID;
  }
  auto in_data_anchor = node->GetInDataAnchor(index);
  if (in_data_anchor == nullptr) {
    GELOGW("[%s] in_data_anchor is null with index [%d].", node->GetName().c_str(), index);
    return SUCCESS;
  }
  auto out_data_anchor = in_data_anchor->GetPeerOutAnchor();
  if (out_data_anchor == nullptr) {
    GELOGE(FAILED, "[%s] peer out_data_anchor is null with index [%d].", node->GetName().c_str(), index);
    return FAILED;
  }
  // Remove link between father_node and node
  in_data_anchor->UnlinkAll();

  auto father_node = out_data_anchor->GetOwnerNode();
  // link father_node's in control nodes to node
  if (GraphUtils::CopyInCtrlEdges(father_node, node) != GRAPH_SUCCESS) {
    return FAILED;
  }
  return SUCCESS;
}

Status PassUtils::RemoveInactiveBranchToMerge(const OutDataAnchorPtr &inactive_output_anchor,
                                              std::vector<NodePtr> &delete_nodes, std::vector<NodePtr> &end_nodes) {
  if (inactive_output_anchor == nullptr) {
    GELOGE(FAILED, "parameter is null.");
    return FAILED;
  }
  for (const auto &dst_anchor : inactive_output_anchor->GetPeerAnchors()) {
    if (dst_anchor == nullptr) {
      continue;
    }
    auto dst_node = dst_anchor->GetOwnerNode();
    if (dst_node != nullptr) {
      std::string dst_node_type;
      GE_CHK_STATUS_RET(GetOriginalType(dst_node, dst_node_type), "get original type failed");
      if (dst_node_type == MERGE) {
        GELOGD("[%s] Switch connected directly to Merge", inactive_output_anchor->GetOwnerNode()->GetName().c_str());
        GE_CHK_STATUS_RET(GraphUtils::RemoveEdge(inactive_output_anchor, dst_anchor), "remove edge failed");
        continue;
      }

      Status ret = PassUtils::RemoveBranch(dst_node, delete_nodes, end_nodes);
      if (ret != SUCCESS) {
        return ret;
      }
    }
  }
  return SUCCESS;
}
}  // namespace ge
