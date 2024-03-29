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

#ifndef GE_HYBRID_EXECUTOR_NODE_STATE_H_
#define GE_HYBRID_EXECUTOR_NODE_STATE_H_

#include <condition_variable>
#include <future>
#include <mutex>
#include "external/ge/ge_api_error_codes.h"
#include "hybrid/model/node_item.h"
#include "node_done_manager.h"

namespace ge {
namespace hybrid {
class NodeTask;
struct GraphExecutionContext;
class SubgraphContext;
class TaskContext;
class NodeState;

class ShapeFuture {
 public:
  ShapeFuture(NodeState *src_node, uint32_t src_index, SubgraphContext *subgraph_context);
  ~ShapeFuture() = default;
  Status Get(GeShape &ori_shape, GeShape &shape);
  Status GetTensorDesc(const GeTensorDesc **tensor_desc);

 private:
  NodeState *src_node_;
  uint32_t src_index_;
  SubgraphContext *subgraph_context_;
};

struct ShapeInferenceState {
  explicit ShapeInferenceState(const NodeItem &node_item);

  Status UpdateInputShape(int idx, const GeTensorDesc &tensor_desc);

  void UpdateInputShapeFuture(int idx, ShapeFuture &&future);

  Status AwaitShapesReady(const GraphExecutionContext &context);

  Status UpdateOutputDesc();

  const vector<GeTensorDesc> &GetOutputTensorDesc() const;

  const NodeItem &node_item;

 private:
  friend struct NodeState;
  std::vector<std::pair<int, ShapeFuture>> shape_futures;
  // do not directly update op_desc, in case race condition across pipelines
  std::vector<GeTensorDesc> input_tensor_desc;
  std::vector<GeTensorDesc> output_tensor_desc;

  int num_pending_shapes_ = 0;
  std::condition_variable ready_cv_;
  std::mutex mu_;
};

// saving sth. dynamic during execution
struct NodeState {
 public:
  NodeState(const NodeItem &node_item, SubgraphContext *subgraph_context);
  ~NodeState() = default;

  OpDesc *GetOpDesc() const {
    return op_desc_.get();
  }

  inline const NodeItem *GetNodeItem() const {
    return node_item_;
  }

  inline const string &GetName() const {
    return node_item_->NodeName();
  }

  inline const string &GetType() const {
    return node_item_->NodeType();
  }

  ShapeInferenceState &GetShapeInferenceState() {
    return shape_inference_state_;
  }

  Status UpdateOutputShapes(int index, const GeShape &shape, const GeShape &ori_shape);

  const shared_ptr<NodeTask> &GetKernelTask() const {
    return kernel_task_;
  }

  void SetKernelTask(const shared_ptr<NodeTask> &kernel_task) {
    kernel_task_ = kernel_task;
  }

  Status WaitForPrepareDone();

  void SetPrepareFuture(std::future<Status> &&prepare_future) {
    this->prepare_future_ = std::move(prepare_future);
  }

  Status AwaitInputTensors(GraphExecutionContext &context) const;

  void SetTaskContext(std::shared_ptr<TaskContext> &task_context);
  std::shared_ptr<TaskContext> GetTaskContext();

 private:
  const NodeItem *node_item_ = nullptr;
  std::shared_ptr<NodeTask> kernel_task_ = nullptr;
  std::future<Status> prepare_future_;
  OpDescPtr op_desc_;
  ShapeInferenceState shape_inference_state_;
  SubgraphContext *subgraph_context_;
  std::shared_ptr<TaskContext> task_context_ = nullptr;
  std::mutex mu_;
};

using NodeStatePtr = std::shared_ptr<NodeState>;
}  // namespace hybrid
}  // namespace ge

#endif // GE_HYBRID_EXECUTOR_NODE_STATE_H_
