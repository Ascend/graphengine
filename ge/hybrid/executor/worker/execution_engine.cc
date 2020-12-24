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

#include "hybrid/executor/worker/execution_engine.h"
#include "graph/runtime_inference_context.h"
#include "graph/utils/tensor_utils.h"
#include "graph/utils/tensor_adapter.h"
#include "graph/debug/ge_attr_define.h"
#include "hybrid/node_executor/node_executor.h"
#include "common/dump/dump_manager.h"
#include "common/dump/dump_op.h"
#include "common/types.h"
#include "common/ge_types.h"
#include "common/profiling/profiling_manager.h"
#include "runtime/base.h"

namespace ge {
namespace hybrid {
namespace {
constexpr int64_t kMaxPadding = 63;

Status LogInputs(const NodeItem &node_item, const TaskContext &task_context) {
  for (auto i = 0; i < task_context.NumInputs(); ++i) {
    const auto &input_tensor = task_context.GetInput(i);
    GE_CHECK_NOTNULL(input_tensor);
    const auto &tensor_desc = task_context.GetInputDesc(i);
    GE_CHECK_NOTNULL(tensor_desc);
    GELOGD("[%s] Print task args. input[%d] = %s, shape = [%s]",
           node_item.NodeName().c_str(),
           i,
           input_tensor->DebugString().c_str(),
           tensor_desc->GetShape().ToString().c_str());
  }

  return SUCCESS;
}

Status LogOutputs(const NodeItem &node_item, const TaskContext &task_context) {
  for (auto i = 0; i < task_context.NumOutputs(); ++i) {
    const auto &output_tensor = task_context.GetOutput(i);
    GE_CHECK_NOTNULL(output_tensor);
    const auto &tensor_desc = node_item.MutableOutputDesc(i);
    GE_CHECK_NOTNULL(tensor_desc);
    GELOGD("[%s] Print task args. output[%d] = %s, shape = [%s]",
           node_item.NodeName().c_str(),
           i,
           output_tensor->DebugString().c_str(),
           tensor_desc->MutableShape().ToString().c_str());
  }

  return SUCCESS;
}
}  // namespace
class NodeDoneCallback {
 public:
  NodeDoneCallback(GraphExecutionContext *graph_context, std::shared_ptr<TaskContext> task_context);
  ~NodeDoneCallback() = default;
  Status OnNodeDone();
 private:
  Status PrepareConstInputs(const NodeItem &node_item);
  Status DumpDynamicNode();
  Status ProfilingReport();
  Status GetGraphDescInfo(const NodePtr node, const HybridModel *model,
                          std::vector<ComputeGraphDescInfo> &compute_graph_info);
  Status GetTaskDescInfo(const NodePtr node, const HybridModel *model,
                         std::vector<TaskDescInfo> &task_desc_info);
  GraphExecutionContext *graph_context_;
  std::shared_ptr<TaskContext> context_;
  DumpOp dump_op_;
};

NodeDoneCallback::NodeDoneCallback(GraphExecutionContext *graph_context,
                                   std::shared_ptr<TaskContext> task_context)
    : graph_context_(graph_context), context_(std::move(task_context)) {
}

Status NodeDoneCallback::PrepareConstInputs(const NodeItem &node_item) {
  for (auto output_idx : node_item.to_const_output_id_list) {
    RECORD_CALLBACK_EVENT(graph_context_, node_item.NodeName().c_str(),
                          "[PrepareConstInputs] [index = %d] Start",
                          output_idx);

    auto output_tensor = context_->GetOutput(output_idx);
    GE_CHECK_NOTNULL(output_tensor);

    Tensor tensor;
    auto ge_tensor_desc = node_item.MutableOutputDesc(output_idx);
    GE_CHECK_NOTNULL(ge_tensor_desc);
    tensor.SetTensorDesc(TensorAdapter::GeTensorDesc2TensorDesc(*ge_tensor_desc));

    int64_t tensor_size;
    GE_CHK_GRAPH_STATUS_RET(TensorUtils::GetTensorSizeInBytes(*ge_tensor_desc, tensor_size),
                            "Failed to invoke GetTensorSizeInBytes");

    if (output_tensor->GetSize() < static_cast<size_t>(tensor_size)) {
      GELOGE(INTERNAL_ERROR,
             "[%s] Tensor size is not enough. output index = %d, required size = %ld, tensor = %s",
             node_item.NodeName().c_str(),
             output_idx,
             tensor_size,
             output_tensor->DebugString().c_str());
      return INTERNAL_ERROR;
    }

    vector<uint8_t> host_buffer(static_cast<unsigned long>(tensor_size));
    GELOGD("[%s] To cache output[%d] to host, size = %zu",
           node_item.NodeName().c_str(),
           output_idx,
           output_tensor->GetSize());
    if (tensor_size > 0) {
      GE_CHK_RT_RET(rtMemcpy(host_buffer.data(),
                             tensor_size,
                             output_tensor->GetData(),
                             tensor_size,
                             RT_MEMCPY_DEVICE_TO_HOST));
    }
    tensor.SetData(std::move(host_buffer));
    string session_id = std::to_string(context_->GetSessionId());
    RuntimeInferenceContext *runtime_infer_ctx = nullptr;
    GE_CHK_GRAPH_STATUS_RET(RuntimeInferenceContext::GetContext(session_id, &runtime_infer_ctx),
                            "Failed to get RuntimeInferenceContext, session_id = %s", session_id.c_str());
    GE_CHK_STATUS_RET(runtime_infer_ctx->SetTensor(node_item.node_id, output_idx, std::move(tensor)),
                      "Failed to SetTensor, node = %s, output_index = %d", node_item.NodeName().c_str(), output_idx);
    GELOGD("[%s] Output[%d] cached successfully in session: %s. node_id = %d, shape = [%s]",
           node_item.NodeName().c_str(),
           output_idx,
           session_id.c_str(),
           node_item.node_id,
           ge_tensor_desc->GetShape().ToString().c_str());

    RECORD_CALLBACK_EVENT(graph_context_, node_item.NodeName().c_str(),
                          "[PrepareConstInputs] [index = %d] End",
                          output_idx);
  }

  return SUCCESS;
}

Status NodeDoneCallback::GetTaskDescInfo(const NodePtr node, const HybridModel *model,
                                         std::vector<TaskDescInfo> &task_desc_info) {
  GE_CHECK_NOTNULL(node);
  GE_CHECK_NOTNULL(model);

  GELOGD("GetTaskDescInfo of node [%s] start.", node->GetName().c_str());
  auto op_desc = node->GetOpDesc();
  std::string op_name = op_desc->GetName();
  std::string dynamic_model_name = model->GetModelName();

  uint32_t task_id = 0;
  uint32_t stream_id = 0;
  if (rtGetTaskIdAndStreamID(&task_id, &stream_id) != RT_ERROR_NONE) {
    GELOGE(PARAM_INVALID, "Get task_id and stream_id failed.");
    return PARAM_INVALID;
  }

  TaskDescInfo tmp_task_desc_info;
  tmp_task_desc_info.model_name = dynamic_model_name;
  tmp_task_desc_info.op_name = op_name;
  tmp_task_desc_info.block_dim = 0;
  auto task_defs = model->GetTaskDefs(node);
  if (task_defs != nullptr && (*task_defs).size() > 0) {
    const auto &task_def = (*task_defs)[0];
    tmp_task_desc_info.block_dim = task_def.kernel().block_dim();
  }
  tmp_task_desc_info.task_id = task_id;
  tmp_task_desc_info.stream_id = stream_id;
  GELOGD("GetTaskDescInfo of node [%s] end, task_id[%u], stream_id[%u]",
         node->GetName().c_str(), task_id, stream_id);
  task_desc_info.emplace_back(tmp_task_desc_info);
  return SUCCESS;
}

Status NodeDoneCallback::GetGraphDescInfo(const NodePtr node, const HybridModel *model,
                                          std::vector<ComputeGraphDescInfo> &compute_graph_info) {
  GE_CHECK_NOTNULL(node);
  GE_CHECK_NOTNULL(model);

  GELOGD("GetComputeGraphInfo of node [%s] start.", node->GetName().c_str());

  std::string dynamic_model_name = model->GetModelName();
  auto op_desc = node->GetOpDesc();
  if (op_desc == nullptr) {
    GELOGE(PARAM_INVALID, "op_desc is nullptr.");
    return PARAM_INVALID;
  }

  auto op_mode = static_cast<uint32_t>(domi::ImplyType::INVALID);
  if (AttrUtils::GetInt(op_desc, ATTR_NAME_IMPLY_TYPE, op_mode) &&
      op_mode == static_cast<uint32_t>(domi::ImplyType::TVM)) {
    ComputeGraphDescInfo tmp_compute_graph_info;
    tmp_compute_graph_info.model_name = dynamic_model_name;
    tmp_compute_graph_info.op_name = op_desc->GetName();
    tmp_compute_graph_info.op_type = op_desc->GetType();

    for (size_t i = 0; i < op_desc->GetAllInputsSize(); ++i) {
      GeTensorDescPtr input_desc = op_desc->MutableInputDesc(i);
      if (input_desc == nullptr) {
        continue;
      }
      tmp_compute_graph_info.input_format.emplace_back(input_desc->GetFormat());
      tmp_compute_graph_info.input_shape.emplace_back(input_desc->GetShape().GetDims());
      tmp_compute_graph_info.input_data_type.emplace_back(input_desc->GetDataType());
    }

    for (size_t j = 0; j < op_desc->GetOutputsSize(); ++j) {
      GeTensorDesc output_desc = op_desc->GetOutputDesc(j);
      tmp_compute_graph_info.output_format.emplace_back(output_desc.GetFormat());
      tmp_compute_graph_info.output_shape.emplace_back(output_desc.GetShape().GetDims());
      tmp_compute_graph_info.output_data_type.emplace_back(output_desc.GetDataType());
    }
    compute_graph_info.emplace_back(tmp_compute_graph_info);
    GELOGD("GetComputeGraphInfo of node [%s] end.", node->GetName().c_str());
  }
  return SUCCESS;
}

Status NodeDoneCallback::ProfilingReport() {
  auto node = context_->GetNodeItem().node;
  if (node == nullptr) {
    GELOGE(PARAM_INVALID, "Get node is nullptr");
    return PARAM_INVALID;
  }

  const auto &op_type = node->GetType();
  if (op_type == PARTITIONEDCALL) {
    return SUCCESS;
  }

  GE_CHECK_NOTNULL(graph_context_);
  const HybridModel *model = graph_context_->model;
  GE_CHECK_NOTNULL(model);

  GELOGD("ProfilingReport of node [%s] model [%s] start.", node->GetName().c_str(), model->GetModelName().c_str());
  std::vector<TaskDescInfo> task_desc_info;
  TaskDescInfo tmp_task_desc_info;
  auto profiling_ret = GetTaskDescInfo(node, model, task_desc_info);
  if (profiling_ret != RT_ERROR_NONE) {
    GELOGE(profiling_ret, "Get task info of node[%s] failed.", node->GetName().c_str());
    return profiling_ret;
  }

  std::vector<ComputeGraphDescInfo> compute_graph_info;
  profiling_ret = GetGraphDescInfo(node, model, compute_graph_info);
  if (profiling_ret != RT_ERROR_NONE) {
    GELOGE(profiling_ret, "Get graph info of node[%s] failed.", node->GetName().c_str());
    return profiling_ret;
  }

  auto &profiling_manager = ProfilingManager::Instance();
  profiling_manager.ReportProfilingData(model->GetModelId(), task_desc_info, compute_graph_info);
  return SUCCESS;
}

Status NodeDoneCallback::DumpDynamicNode() {
  auto node = context_->GetNodeItem().node;
  if (node == nullptr) {
    GELOGE(PARAM_INVALID, "Get node is nullptr");
    return PARAM_INVALID;
  }
  auto op_desc = node->GetOpDesc();
  auto stream = context_->GetStream();
  vector<uintptr_t> input_addrs;
  vector<uintptr_t> output_addrs;
  for (int i = 0; i < context_->NumInputs(); i++) {
    auto tensor_value = context_->GetInput(i);
    GE_CHK_BOOL_RET_STATUS(tensor_value != nullptr, PARAM_INVALID, "Tensor value is nullptr");
    uint64_t input_addr = reinterpret_cast<uintptr_t>(tensor_value->GetData());
    input_addrs.emplace_back(input_addr);
  }
  for (int j = 0; j < context_->NumOutputs(); j++) {
    auto tensor_value = context_->GetOutput(j);
    GE_CHK_BOOL_RET_STATUS(tensor_value != nullptr, PARAM_INVALID, "Tensor value is nullptr");
    uint64_t output_addr = reinterpret_cast<uintptr_t>(tensor_value->GetData());
    output_addrs.emplace_back(output_addr);
  }

  dump_op_.SetDumpInfo(context_->GetDumpProperties(), op_desc, input_addrs, output_addrs, stream);

  GE_CHECK_NOTNULL(graph_context_);
  const HybridModel *model = graph_context_->model;
  GE_CHECK_NOTNULL(model);
  std::string dynamic_model_name = model->GetModelName();
  uint32_t model_id = model->GetModelId();
  dump_op_.SetDynamicModelInfo(dynamic_model_name, model_id);

  void *global_step = nullptr;
  TensorValue *varible_global_step = context_->GetVariable(NODE_NAME_GLOBAL_STEP);
  if (varible_global_step != nullptr) {
    global_step = const_cast<void *>(varible_global_step->GetData());
  }

  void *loop_per_iter = nullptr;
  TensorValue *varible_loop_per_iter = context_->GetVariable(NODE_NAME_FLOWCTRL_LOOP_PER_ITER);
  if (varible_loop_per_iter != nullptr) {
    loop_per_iter = const_cast<void *>(varible_loop_per_iter->GetData());
  }

  void *loop_cond = nullptr;
  TensorValue *varible_loop_cond = context_->GetVariable(NODE_NAME_FLOWCTRL_LOOP_COND);
  if (varible_loop_cond != nullptr) {
    loop_cond = const_cast<void *>(varible_loop_cond->GetData());
  }
  dump_op_.SetLoopAddr(global_step, loop_per_iter, loop_cond);

  GE_CHK_STATUS_RET(dump_op_.LaunchDumpOp(), "Failed to launch dump op in hybird model");

  auto rt_ret = rtStreamSynchronize(stream);
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(rt_ret, "rtStreamSynchronize failed");
    return rt_ret;
  }
  return SUCCESS;
}

Status NodeDoneCallback::OnNodeDone() {
  auto &node_item = context_->GetNodeItem();
  GELOGI("[%s] Start callback process.", node_item.NodeName().c_str());
  RECORD_CALLBACK_EVENT(graph_context_, context_->GetNodeName(), "[Compute] End");
  RECORD_CALLBACK_EVENT(graph_context_, context_->GetNodeName(), "[Callback] Start");

  auto dump_path = context_->GetDumpProperties().GetDumpPath();
  if (!dump_path.empty()) {
    GELOGI("Start to dump dynamic shape,dump_path is %s", dump_path.c_str());
    GE_CHK_STATUS_RET(DumpDynamicNode(), "Failed to dump dynamic node");
  }

  if (ProfilingManager::Instance().ProfilingModelExecuteOn()) {
    GE_CHK_STATUS_RET(ProfilingReport(), "Report node[%s] to profiling failed.",
                      node_item.NodeName().c_str());
  }

  // release inputs
  for (int i = 0; i < context_->NumInputs(); ++i) {
    context_->ReleaseInput(i);
  }

  GE_CHK_STATUS_RET_NOLOG(PrepareConstInputs(node_item));
  // PropagateOutputs for type == DEPEND_COMPUTE
  if (node_item.shape_inference_type == DEPEND_COMPUTE) {
    if (graph_context_->trace_enabled) {
      (void) LogOutputs(node_item, *context_);
    }

    GE_CHK_STATUS_RET(context_->PropagateOutputs(),
                      "[%s] Failed to propagate outputs failed",
                      node_item.NodeName().c_str());

    RECORD_CALLBACK_EVENT(graph_context_, context_->GetNodeName(), "[PropagateOutputs] End");
  }

  // release condition variable
  if (node_item.has_observer) {
    GELOGI("[%s] Notify observer. node_id = %d", node_item.NodeName().c_str(), node_item.node_id);
    context_->NodeDone();
  }

  RECORD_CALLBACK_EVENT(graph_context_, context_->GetNodeName(), "[Callback] End");
  return SUCCESS;
}

Status ExecutionEngine::ExecuteAsync(NodeState &node_state,
                                     const std::shared_ptr<TaskContext> &task_context,
                                     GraphExecutionContext &execution_context) {
  GELOGI("[%s] Node is ready for execution", task_context->GetNodeName());
  RECORD_EXECUTION_EVENT(&execution_context, task_context->GetNodeName(), "Start");
  auto cb = std::shared_ptr<NodeDoneCallback>(new(std::nothrow) NodeDoneCallback(&execution_context, task_context));
  GE_CHECK_NOTNULL(cb);
  auto callback = [&, cb]() {
    auto ret = cb->OnNodeDone();
    if (ret != SUCCESS) {
      task_context->OnError(ret);
    }
  };

  GE_CHK_STATUS_RET_NOLOG(DoExecuteAsync(node_state, *task_context, execution_context, callback));
  GE_CHK_STATUS_RET_NOLOG(PropagateOutputs(*node_state.GetNodeItem(), *task_context, execution_context));
  return SUCCESS;
}

Status ExecutionEngine::DoExecuteAsync(NodeState &node_state,
                                       TaskContext &task_context,
                                       GraphExecutionContext &context,
                                       const std::function<void()> &callback) {
  const auto &task = node_state.GetKernelTask();
  if (task == nullptr) {
    GELOGE(INTERNAL_ERROR, "[%s] NodeTask is null.", node_state.GetName().c_str());
    return INTERNAL_ERROR;
  }

  // Wait for dependent nodes(DEPEND_COMPUTE), so that the input tensors are valid.
  RECORD_EXECUTION_EVENT(&context, task_context.GetNodeName(), "[AwaitDependents] Start");
  GE_CHK_STATUS_RET(node_state.AwaitInputTensors(context),
                    "[%s] Failed to wait for dependent nodes.",
                    node_state.GetName().c_str());

  const auto &node_item = *node_state.GetNodeItem();
  auto executor = node_item.node_executor;
  GE_CHECK_NOTNULL(executor);
  RECORD_EXECUTION_EVENT(&context, task_context.GetNodeName(), "[PrepareTask] Start");
  GE_CHK_STATUS_RET(executor->PrepareTask(*task, task_context),
                    "[%s] Failed to prepare task",
                    node_state.GetName().c_str());
  RECORD_EXECUTION_EVENT(&context, task_context.GetNodeName(), "[PrepareTask] End");
  GELOGD("[%s] Done task preparation successfully.", node_state.GetName().c_str());

  if (context.trace_enabled) {
    LogInputs(node_item, task_context);
    if (node_item.shape_inference_type != DEPEND_COMPUTE) {
      LogOutputs(node_item, task_context);
    }
  }

  GE_CHK_STATUS_RET(ValidateInputTensors(node_state, task_context), "Failed to validate input tensors.");
  RECORD_EXECUTION_EVENT(&context, task_context.GetNodeName(), "[ValidateInputTensors] End");

  if (context.profiling_level > 0) {
    auto *ctx = &context;
    const string &name = node_state.GetName();
    (void)task_context.RegisterCallback([ctx, name]() {
      RECORD_CALLBACK_EVENT(ctx, name.c_str(), "[Compute] Start");
    });
  }
  RECORD_EXECUTION_EVENT(&context, task_context.GetNodeName(), "[ExecuteTask] Start");
  GE_CHK_STATUS_RET(node_item.node_executor->ExecuteTask(*task, task_context, callback),
                    "[%s] Failed to execute task",
                    node_state.GetName().c_str());
  RECORD_EXECUTION_EVENT(&context, task_context.GetNodeName(), "[ExecuteTask] End");

  GELOGD("[%s] Done task launch successfully.", node_state.GetName().c_str());
  return SUCCESS;
}

Status ExecutionEngine::ValidateInputTensors(const NodeState &node_state, const TaskContext &task_context) {
  for (auto i = 0; i < task_context.NumInputs(); ++i) {
    const auto &input_tensor = task_context.GetInput(i);
    GE_CHECK_NOTNULL(input_tensor);
    if (input_tensor->GetData() == nullptr) {
      GELOGD("[%s] Skipping null input, index = %d", task_context.GetNodeName(), i);
      continue;
    }

    const auto &tensor_desc = task_context.MutableInputDesc(i);
    GE_CHECK_NOTNULL(tensor_desc);
    if (tensor_desc->GetDataType() == DT_STRING) {
      GELOGD("[%s] Skipping DT_STRING input, index = %d", task_context.GetNodeName(), i);
      continue;
    }

    if (input_tensor->GetData() == nullptr) {
      GELOGD("[%s] Skipping null input, index = %d", task_context.GetNodeName(), i);
      continue;
    }

    int64_t expected_size;
    GE_CHK_GRAPH_STATUS_RET(TensorUtils::GetTensorMemorySizeInBytes(*tensor_desc, expected_size));
    GELOGD("[%s] Input[%d] expects [%ld] bytes.", task_context.GetNodeName(), i, expected_size);
    auto size_diff = expected_size - static_cast<int64_t>(input_tensor->GetSize());
    if (size_diff > 0) {
      if (size_diff <= kMaxPadding) {
        GELOGW("[%s] Input[%d]: tensor size mismatches. expected: %ld, but given %zu",
               task_context.GetNodeName(),
               i,
               expected_size,
               input_tensor->GetSize());
      } else {
        GELOGE(INTERNAL_ERROR,
               "[%s] Input[%d]: tensor size mismatches. expected: %ld, but given %zu",
               task_context.GetNodeName(),
               i,
               expected_size,
               input_tensor->GetSize());
        return INTERNAL_ERROR;
      }
    }
  }

  return SUCCESS;
}

Status ExecutionEngine::PropagateOutputs(const NodeItem &node_item,
                                         TaskContext &task_context,
                                         GraphExecutionContext &context) {
  if (node_item.shape_inference_type != DEPEND_COMPUTE) {
    GE_CHK_STATUS_RET(task_context.PropagateOutputs(),
                      "[%s] Failed to propagate outputs.",
                      node_item.NodeName().c_str());
    RECORD_EXECUTION_EVENT(&context, task_context.GetNodeName(), "[PropagateOutputs] End");
    GELOGD("[%s] Done propagating outputs successfully.", node_item.NodeName().c_str());
  }

  return SUCCESS;
}
}  // namespace hybrid
}  // namespace ge
