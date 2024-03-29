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

#ifndef GE_HYBRID_MODEL_NODE_ITEM_H_
#define GE_HYBRID_MODEL_NODE_ITEM_H_

#include <vector>
#include "external/ge/ge_api_error_codes.h"
#include "graph/node.h"
#include "graph/op_desc.h"
#include "framework/common/types.h"
#include "hybrid/common/tensor_value.h"

namespace ge {
namespace hybrid {
class NodeTask;
class NodeExecutor;

struct FusedSubgraph {
  std::map<int, std::vector<GeTensorDescPtr>> input_mapping;
  std::map<int, OpDescPtr> output_mapping;
  std::vector<NodePtr> nodes;
  ComputeGraphPtr graph;
};

bool IsControlOp(const std::string &op_type);

// for caching static information across execution
struct NodeItem {
  ~NodeItem() = default;
  static Status Create(const NodePtr &node, std::unique_ptr<NodeItem> &node_item);

  const std::string &NodeName() const {
    return node_name;
  }

  const std::string &NodeType() const {
    return node_type;
  }

  OpDescPtr GetOpDesc() const {
    return node->GetOpDesc();
  }

  bool IsInputShapeStatic(int index) const;

  GeTensorDescPtr MutableOutputDesc(int index) const {
    return op_desc->MutableOutputDesc(static_cast<uint32_t>(index));
  }

  GeTensorDescPtr MutableInputDesc(int index) const;

  Status GetCanonicalInputIndex(uint32_t index, int &canonical_index) const;

  bool IsControlOp() const;

  void SetToDynamic();

  std::string DebugString() const;

  NodePtr node;
  OpDesc *op_desc;
  int node_id = -1;
  int group = -1;
  int num_inputs = 0;
  int num_outputs = 0;
  int input_start = -1;
  int output_start = -1;
  bool is_dynamic = false;
  bool has_observer = false;
  bool has_optional_inputs = false;
  bool is_output_shape_static = true;
  UnknowShapeOpType shape_inference_type = DEPEND_IN_SHAPE;
  std::string node_name;
  std::string node_type;
  std::vector<ge::NodePtr> dependents_for_shape_inference;
  std::vector<ge::NodePtr> dependents_for_execution;
  std::set<int> to_const_output_id_list;

  // src_output_id, dst_anchor_id, dst_node
  vector<vector<pair<int, NodeItem *>>> outputs;

  std::shared_ptr<NodeTask> kernel_task;
  std::unique_ptr<FusedSubgraph> fused_subgraph;
  const NodeExecutor *node_executor = nullptr;
  std::map<int, ge::NodePtr> ref_outputs;
  std::map<int, int> reuse_inputs;
  std::map<int, int> reuse_outputs;
  int num_static_input_shapes = 0;
  bool is_profiling_report = false;

 private:
  explicit NodeItem(NodePtr node);
  Status Init();
  Status InitInputsAndOutputs();
  void ResolveOptionalInputs();
  Status ResolveDynamicState();
  Status ResolveStaticInputsAndOutputs();
  void ResolveUnknownShapeType();

  std::vector<bool> is_input_shape_static_;
  std::vector<uint32_t> input_desc_indices_;
};
}  // namespace hybrid
}  // namespace ge

#endif // GE_HYBRID_MODEL_NODE_ITEM_H_
