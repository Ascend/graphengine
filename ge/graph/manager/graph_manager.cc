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

#include "graph/manager/graph_manager.h"

#include <pthread.h>
#include <algorithm>
#include <future>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#include "common/math/math_util.h"
#include "common/thread_pool.h"
#include "analyzer/analyzer.h"
#include "graph/common/ge_call_wrapper.h"
#include "graph/common/local_context.h"
#include "graph/common/transop_util.h"
#include "graph/ge_context.h"
#include "graph/ge_global_options.h"
#include "graph/manager/util/rt_context_util.h"
#include "graph/partition/dynamic_shape_partition.h"
#include "graph/passes/enter_pass.h"
#include "graph/partition/stage_partition.h"
#include "graph/passes/addn_pass.h"
#include "graph/passes/bitcast_pass.h"
#include "graph/passes/assign_remove_pass.h"
#include "graph/passes/inplace_support_check_pass.h"
#include "graph/passes/atomic_addr_clean_pass.h"
#include "graph/passes/attach_stream_label_pass.h"
#include "graph/passes/cast_remove_pass.h"
#include "graph/passes/common_subexpression_elimination_pass.h"
#include "graph/passes/compile_nodes_pass.h"
#include "graph/passes/cond_remove_pass.h"
#include "graph/passes/constant_folding_pass.h"
#include "graph/passes/constant_fuse_same_pass.h"
#include "graph/passes/control_trigger_pass.h"
#include "graph/passes/ctrl_edge_transfer_pass.h"
#include "graph/passes/dimension_adjust_pass.h"
#include "graph/passes/dimension_compute_pass.h"
#include "graph/passes/flow_ctrl_pass.h"
#include "graph/passes/fuse_data_nodes_with_common_input_pass.h"
#include "graph/passes/identity_pass.h"
#include "graph/passes/input_output_connection_identify_pass.h"
#include "graph/passes/iterator_op_pass.h"
#include "graph/passes/link_gen_mask_nodes_pass.h"
#include "graph/passes/mark_graph_unknown_status_pass.h"
#include "graph/passes/merge_pass.h"
#include "graph/passes/merge_input_memcpy_pass.h"
#include "graph/passes/merge_to_stream_merge_pass.h"
#include "graph/passes/multi_batch_pass.h"
#include "graph/passes/next_iteration_pass.h"
#include "graph/passes/permute_pass.h"
#include "graph/passes/prune_pass.h"
#include "graph/passes/ref_identity_delete_op_pass.h"
#include "graph/passes/remove_same_const_pass.h"
#include "graph/passes/reshape_recovery_pass.h"
#include "graph/passes/reshape_remove_pass.h"
#include "graph/passes/same_transdata_breadth_fusion_pass.h"
#include "graph/passes/subgraph_pass.h"
#include "graph/passes/switch_data_edges_bypass.h"
#include "graph/passes/switch_dead_branch_elimination.h"
#include "graph/passes/switch_logic_remove_pass.h"
#include "graph/passes/switch_to_stream_switch_pass.h"
#include "graph/passes/transop_breadth_fusion_pass.h"
#include "graph/passes/transop_nearby_allreduce_fusion_pass.h"
#include "graph/passes/transop_symmetry_elimination_pass.h"
#include "graph/passes/transop_without_reshape_fusion_pass.h"
#include "graph/passes/transpose_transdata_pass.h"
#include "graph/passes/useless_control_out_remove_pass.h"
#include "graph/passes/variable_op_pass.h"
#include "graph/passes/variable_ref_delete_op_pass.h"
#include "graph/passes/variable_ref_useless_control_out_delete_pass.h"
#include "graph/passes/end_of_sequence_add_control_pass.h"
#include "graph/passes/subexpression_migration_pass.h"
#include "graph/passes/subgraph_const_migration_pass.h"
#include "graph/passes/unused_args_clean_pass.h"
#include "graph/passes/global_step_insert_pass.h"
#include "graph/passes/memcpy_addr_async_pass.h"
#include "graph/passes/hccl_continuous_memcpy_pass.h"
#include "graph/build/label_allocator.h"
#include "graph/utils/tensor_adapter.h"
#include "inc/pass_manager.h"
#include "init/gelib.h"
#include "ir_build/atc_ir_common.h"
#include "graph/common/local_context.h"
#include "graph/common/omg_util.h"
#include "common/formats/utils/formats_trans_utils.h"
#include "register/custom_pass_helper.h"

namespace {
const char *const kSummary = "Summary";
const char *const kSave = "Save";
const char *const kNetOutput = "NetOutput";
const char *const kVariable = "Variable";
const char *const kSend = "Send";
const char *const kRecv = "Recv";
const char *const kCheckPointForGetVar = "CheckPointGraphForGetVar";
const char *const kCheckPointGraph = "checkpoint_graph";
const char *const kVectorEngine = "VectorEngine";
const char *const kAIcoreEngine = "AIcoreEngine";
const int32_t kDynamicDimsTypeIsGetNext = 0;
const int32_t kDynamicDimsTypeIsData = 1;
const char *const kGetNextName = "IteratorV2";

bool IsTailingOptimization() {
  string is_tailing_optimization_option;
  auto ret = ge::GetContext().GetOption(ge::OPTION_EXEC_ENABLE_TAILING_OPTIMIZATION, is_tailing_optimization_option);
  if (ret == ge::GRAPH_SUCCESS) {
    GELOGI("Option ge.exec.isTailingOptimization is %s", is_tailing_optimization_option.c_str());
    // "1" means it's True from frontend option
    return is_tailing_optimization_option == "1";
  }
  GELOGW("OPTION_EXEC_ENABLE_TAILING_OPTIMIZATION not set, use BFSTopologicalSorting by default.");
  return false;
}

ge::Status CheckFpCeilingMode() {
  static const std::set<std::string> kValidFpCeilingMode = {"0", "1", "2"};
  string mode;
  auto ret = ge::GetContext().GetOption("ge.fpCeilingMode", mode);
  if (ret == ge::GRAPH_SUCCESS) {
    if (kValidFpCeilingMode.count(mode) == 0) {
      GELOGE(ge::GE_GRAPH_OPTIONS_INVALID, "The fp_ceiling_mode %s is invalid, options are 0, 1, and 2.", mode.c_str());
      return ge::GE_GRAPH_OPTIONS_INVALID;
    }
    GELOGI("The parameter fp_ceiling_mode is set to %s.", mode.c_str());
    return ge::SUCCESS;
  }
  GELOGW("The parameter fp_ceiling_mode is not set.");
  return ge::SUCCESS;
}
}  // namespace

namespace ge {
GraphManager::GraphManager()
    : thread_run_flag_(false),
      graph_run_listener_(nullptr),
      init_flag_(false) {
}

Status GraphManager::Initialize(const std::map<string, string> &options) {
  if (init_flag_) {
    GELOGW("[Initialize] GraphManager already initialized.");
    return SUCCESS;
  }

  // malloc
  graph_run_listener_ = MakeShared<GraphModelListener>(sync_run_mutex_, condition_);
  if (graph_run_listener_ == nullptr) {
    GELOGE(MEMALLOC_FAILED, "Make shared failed");
    return MEMALLOC_FAILED;
  }
  // graph context
  graph_context_ = MakeShared<GraphContext>();
  if (graph_context_ == nullptr) {
    GELOGE(MEMALLOC_FAILED, "Make shared failed.");
    return MEMALLOC_FAILED;
  }

  // parse option parameters
  Status ret = ParseOptions(options);
  if (ret != SUCCESS) {
    GELOGE(ret, "[Initialize] parse options failed.");
    return ret;
  }

  ret = CheckFpCeilingMode();
  if (ret != SUCCESS) {
    GELOGE(ret, "[Initialize] Check fp-ceiling-mode options failed.");
    return ret;
  }

  ret = graph_context_->Initialize(options);
  if (ret != SUCCESS) {
    GELOGE(ret, "[Initialize] GraphContext initialize failed.");
    return ret;
  }

  graph_map_.clear();
  cache_helper_map_.clear();
  init_flag_ = true;

  thread_run_flag_ = true;
  prerun_thread_ = std::thread(GraphManager::PreRunThread, this);
  run_thread_ = std::thread(GraphManager::RunThread, this);

  return SUCCESS;
}

Status GraphManager::Finalize() {
  if (!init_flag_) {
    GELOGW("GraphManager has not been initialized.");
    return SUCCESS;
  }

  if (graph_executor_.FreeExecuteMemory() != SUCCESS) {
    GELOGW("Graph executor FreeExecuteMemory failed, resources may not be released correctly.");
  }

  StopQueue(this);

  if (prerun_thread_.joinable()) {
    prerun_thread_.join();
  }
  if (run_thread_.joinable()) {
    run_thread_.join();
  }

  // check graph whether running or not
  Status unload_model_ret = SUCCESS;
  Status ret;
  rtError_t rt_ret;
  for (auto iter = graph_map_.begin(); iter != graph_map_.end(); ++iter) {
    GraphNodePtr graph_node = iter->second;
    if (graph_node->GetRunFlag()) {
      GELOGW("[GraphManager] finalize failed, graphId=%u.", iter->first);
      unload_model_ret = GE_GRAPH_GRAPH_IS_RUNNING;
      continue;
    }

    // unload model
    auto ge_root_model = graph_node->GetGeRootModel();
    if (ge_root_model != nullptr && ge_root_model->GetModelId() != INVALID_MODEL_ID && graph_node->GetLoadFlag()) {
      rt_ret = rtSetDevice(GetContext().DeviceId());
      if (rt_ret != RT_ERROR_NONE) {
        GELOGW("[GraphManager] rtSetDevice failed, modelId=%u, graphId=%u.", ge_root_model->GetModelId(), iter->first);
        unload_model_ret = FAILED;
        continue;
      }
      ret = GraphLoader::UnloadModel(ge_root_model->GetModelId());
      if (ret != SUCCESS) {
        GELOGW("[GraphManager] unload model failed, modelId=%u, graphId=%u.", ge_root_model->GetModelId(), iter->first);
        unload_model_ret = ret;
      }
      rt_ret = rtDeviceReset(GetContext().DeviceId());
      if (rt_ret != RT_ERROR_NONE) {
        GELOGW("[GraphManager] rtDeviceReset failed, modelId=%u, graphId=%u.", ge_root_model->GetModelId(),
               iter->first);
        unload_model_ret = FAILED;
        continue;
      }
    }

    // clear analyzer saved info(graph level)
    auto compute_graph = GraphUtils::GetComputeGraph(*graph_node->GetGraph());
    GE_CHECK_NOTNULL(compute_graph);
    auto session_id = compute_graph->GetSessionID();
    auto graph_id = compute_graph->GetGraphID();
    Analyzer::GetInstance()->DestroyGraphJsonObject(session_id, graph_id);
  }
  graph_map_.clear();
  cache_helper_map_.clear();

  // graph context
  if (graph_context_ != nullptr) {
    Status ret_final = graph_context_->Finalize();
    if (ret_final != SUCCESS) {
      GELOGE(ret_final, "[GraphManager] graph context Finalize failed!");
      unload_model_ret = ret_final;
    }
  }

  init_flag_ = false;
  return unload_model_ret;
}

Status GraphManager::InitDynamicParams(ComputeGraphPtr &compute_graph) {
  for (const auto &node : compute_graph->GetAllNodes()) {
    auto op_desc = node->GetOpDesc();
    if (op_desc == nullptr) {
      continue;
    }
    GetLocalOmgContext().need_multi_batch = false;
    std::string op_type;
    auto ret = GetOriginalType(node, op_type);
    if (ret != SUCCESS) {
      GELOGE(FAILED, "Failed to get node %s original type.", node->GetName().c_str());
      return FAILED;
    }
    if ((op_desc->GetType() == DATA) || (op_type == kGetNextName)) {
      GELOGI("Need to process multi batch for compute graph.");
      GetLocalOmgContext().need_multi_batch = true;
      break;
    }
  }
  if (!options_.input_shape.empty() && !options_.dynamic_dims.empty()) {
    if (!ge::ParseInputShape(options_.input_shape, GetLocalOmgContext().input_dims,
                             GetLocalOmgContext().user_input_dims, true)) {
      GELOGE(GRAPH_PARAM_INVALID, "Failed to parse input shape: %s.", options_.input_shape.c_str());
      return GRAPH_PARAM_INVALID;
    }
    GetLocalOmgContext().dynamic_dims = options_.dynamic_dims;
  }
  if (options_.dynamic_node_type == kDynamicDimsTypeIsGetNext) {
    GetLocalOmgContext().dynamic_node_type = GETNEXT;
  }
  if (options_.dynamic_node_type == kDynamicDimsTypeIsData) {
    GetLocalOmgContext().dynamic_node_type = DATA;
  }
  return SUCCESS;
}

Status GraphManager::AddGraph(const GraphId &graph_id, const Graph &graph,
                              const std::map<std::string, std::string> &options,
                              const OmgContext &omg_context) {
  if (HasGraphNode(graph_id)) {
    GELOGE(GE_GRAPH_GRAPH_ALREADY_EXIST, "[GraphManager] graph exists, graph_id = %u.", graph_id);
    return GE_GRAPH_GRAPH_ALREADY_EXIST;
  }

  auto compute_graph = GraphUtils::GetComputeGraph(graph);
  if (compute_graph != nullptr) {
    compute_graph->SetGraphID(graph_id);
    bool graph_has_been_added = false;
    if (AttrUtils::GetBool(*compute_graph, ATTR_NAME_GRAPH_HAS_BEEN_ADDED, graph_has_been_added)
        && graph_has_been_added) {
      GELOGE(GE_GRAPH_GRAPH_ALREADY_EXIST,
             "[GraphManager] same graph object can not be added again, graph_id = %u.", graph_id);
      return GE_GRAPH_GRAPH_ALREADY_EXIST;
    }
    (void)AttrUtils::SetBool(*compute_graph, ATTR_NAME_GRAPH_HAS_BEEN_ADDED, true);
    compute_graph_ = compute_graph;
  } else {
    GELOGE(FAILED, "compute graph is null");
    return FAILED;
  }
  std::string session_graph_id;
  if (!AttrUtils::GetStr(*compute_graph, ATTR_NAME_SESSION_GRAPH_ID, session_graph_id) || session_graph_id.empty()) {
    session_graph_id = "-1_" + to_string(graph_id);
    if (!AttrUtils::SetStr(*compute_graph, ATTR_NAME_SESSION_GRAPH_ID, session_graph_id)) {
      GELOGW("Set attribute of compute graph failed.");
    }
    for (auto &subgraph : compute_graph->GetAllSubgraphs()) {
      (void)AttrUtils::SetStr(*subgraph, ATTR_NAME_SESSION_GRAPH_ID, session_graph_id);
    }
    GELOGD("Get graph session_graph_id attr failed, set session id to default value: [0]");
  }

  GraphNodePtr graph_node = MakeShared<ge::GraphNode>(graph_id);
  GE_IF_BOOL_EXEC(graph_node == nullptr, GELOGE(FAILED, "GraphNode make shared failed");
                  return FAILED);
  std::shared_ptr<Graph> graph_ptr = MakeShared<ge::Graph>(graph);
  GE_IF_BOOL_EXEC(graph_ptr == nullptr, GELOGE(FAILED, "GraphPtr make shared failed");
                  return FAILED);

  graph_node->SetGraph(graph_ptr);
  graph_node->SetOptions(options);
  AddGraphNode(graph_id, graph_node);

  AddLocalOmgContext(graph_id, omg_context);
  if (!options_.output_datatype.empty()) {
    GetLocalOmgContext().output_type = options_.output_datatype;
  }
  if (InitDynamicParams(compute_graph) != SUCCESS) {
    GELOGE(GRAPH_PARAM_INVALID, "Failed to init params when online infer is dynamic.");
    return GRAPH_PARAM_INVALID;
  }

  CompilerStages &stages = GetCompilerStages(graph_id);
  stages.preparer.SetOptions(options_);
  Status status = stages.optimizer.SetOptions(options_);
  if (status != SUCCESS) {
    GELOGE(status, "Graph optimizer set options failed.");
    return status;
  }
  stages.builder.SetOptions(options_);

  var_acc_ctrl_.AddGraph(graph_id, compute_graph);
  return SUCCESS;
}

Status GraphManager::AddGraphWithCopy(const GraphId &graph_id, const Graph &graph,
                                      const std::map<std::string, std::string> &options,
                                      const OmgContext &omg_context) {
  if (HasGraphNode(graph_id)) {
    GELOGE(GE_GRAPH_GRAPH_ALREADY_EXIST, "[GraphManager] graph exists, graph_id = %u.", graph_id);
    return GE_GRAPH_GRAPH_ALREADY_EXIST;
  }
  auto compute_graph = GraphUtils::GetComputeGraph(graph);
  if (compute_graph != nullptr) {
    compute_graph->SetGraphID(graph_id);
    bool graph_has_been_added = false;
    if (AttrUtils::GetBool(*compute_graph, ATTR_NAME_GRAPH_HAS_BEEN_ADDED, graph_has_been_added)
        && graph_has_been_added) {
      GELOGE(GE_GRAPH_GRAPH_ALREADY_EXIST,
             "[GraphManager] same graph object can not be added again, graph_id = %u.", graph_id);
      return GE_GRAPH_GRAPH_ALREADY_EXIST;
    }
  } else {
    GELOGE(FAILED, "compute graph is null");
    return FAILED;
  }
  std::vector<NodePtr> input_nodes;
  std::vector<NodePtr> output_nodes;
  auto new_compute_graph = GraphUtils::CloneGraph(compute_graph, "", input_nodes, output_nodes);
  std::string session_graph_id;
  if (!AttrUtils::GetStr(*new_compute_graph, ATTR_NAME_SESSION_GRAPH_ID, session_graph_id) ||
      session_graph_id.empty()) {
    session_graph_id = "-1_" + to_string(graph_id);
    if (!AttrUtils::SetStr(*new_compute_graph, ATTR_NAME_SESSION_GRAPH_ID, session_graph_id)) {
      GELOGW("Set attribute of compute graph failed.");
    }
    for (auto &subgraph : new_compute_graph->GetAllSubgraphs()) {
      (void)AttrUtils::SetStr(*subgraph, ATTR_NAME_SESSION_GRAPH_ID, session_graph_id);
    }
    GELOGD("Get graph session_graph_id attr failed, set session id to default value: [0]");
  }

  GraphNodePtr graph_node = MakeShared<ge::GraphNode>(graph_id);
  if (graph_node == nullptr) {
    GELOGE(FAILED, "GraphNode make shared failed");
    return FAILED;
  }
  std::shared_ptr<Graph> graph_ptr = GraphUtils::CreateGraphPtrFromComputeGraph(new_compute_graph);
  if (graph_ptr == nullptr) {
    GELOGE(FAILED, "GraphPtr make shared failed");
    return FAILED;
  }

  graph_node->SetGraph(graph_ptr);
  graph_node->SetOptions(options);
  AddGraphNode(graph_id, graph_node);

  AddLocalOmgContext(graph_id, omg_context);
  if (!options_.output_datatype.empty()) {
    GetLocalOmgContext().output_type = options_.output_datatype;
  }

  CompilerStages &stages = GetCompilerStages(graph_id);
  stages.preparer.SetOptions(options_);
  Status status = stages.optimizer.SetOptions(options_);
  if (status != SUCCESS) {
    GELOGE(status, "Graph optimizer set options failed.");
    return status;
  }
  stages.builder.SetOptions(options_);

  var_acc_ctrl_.AddGraph(graph_id, new_compute_graph);
  return SUCCESS;
}

Status GraphManager::MergeSubGraph(ComputeGraphPtr &compute_graph, const ge::ComputeGraphPtr &original_compute_graph,
                                   GraphId root_graph_id) {
  std::shared_ptr<GELib> instance_ptr = ge::GELib::GetInstance();
  GraphPartitioner &partitioner = GetCompilerStages(root_graph_id).partitioner;
  if (instance_ptr != nullptr && instance_ptr->InitFlag()) {
    Status ret = partitioner.MergeAfterSubGraphOptimization(compute_graph, original_compute_graph);
    if (ret != SUCCESS) {
      GELOGE(ret, "merge end and placeholder after subGraph optimization failed.");
      return FAILED;
    }

    Status ret_topo = compute_graph->TopologicalSorting();
    if (ret_topo != SUCCESS) {
      GELOGE(ret_topo, "[GraphManager]: TopologicalSorting the merged graph failed.");
      return ret_topo;
    }
  } else {
    auto subgraph_list = partitioner.GetSubGraphMap();
    if (subgraph_list.find(original_compute_graph) != subgraph_list.end() &&
        !subgraph_list[original_compute_graph].empty() && subgraph_list[original_compute_graph][0] != nullptr) {
      compute_graph = subgraph_list[original_compute_graph][0]->GetSubGraph();
    }
  }

  return SUCCESS;
}

Status GraphManager::CopySubGraphAndMarkFusion(const ComputeGraphPtr &compute_graph,
                                               Graph2SubGraphInfoList &sub_graph_map,
                                               std::unordered_map<std::string, ComputeGraphPtr> &copy_graphs) {
  GE_CHECK_NOTNULL(compute_graph);
  vector<ComputeGraphPtr> old_compute_graphs;
  const auto &root_subgraph_list = sub_graph_map[compute_graph];
  for (const auto &subgraph : root_subgraph_list) {
    old_compute_graphs.emplace_back(subgraph->GetSubGraph());
  }
  for (const auto &function_graph : compute_graph->GetAllSubgraphs()) {
    const auto &subgraph_list = sub_graph_map[function_graph];
    for (const auto &subgraph : subgraph_list) {
      old_compute_graphs.emplace_back(subgraph->GetSubGraph());
    }
  }

  for (const auto &old_compute_graph : old_compute_graphs) {
    std::vector<NodePtr> input_nodes;
    std::vector<NodePtr> output_nodes;
    ComputeGraphPtr new_compute_graph = GraphUtils::CloneGraph(old_compute_graph, "", input_nodes, output_nodes);
    if (new_compute_graph == nullptr) {
      GELOGE(INTERNAL_ERROR, "Clone graph failed.");
      return INTERNAL_ERROR;
    }
    copy_graphs.emplace(old_compute_graph->GetName(), new_compute_graph);
    if (!AttrUtils::SetBool(old_compute_graph, ATTR_NAME_NEED_LX_FUSION, true)) {
      GELOGE(INTERNAL_ERROR, "Set attr lx_fusion to graph failed.");
      return INTERNAL_ERROR;
    }
  }

  GELOGI("Copy %zu graphs successfully.", copy_graphs.size());
  return SUCCESS;
}

Status GraphManager::OptimizeSubGraphWithMultiThreads(ComputeGraphPtr compute_graph,
                                                      Graph2SubGraphInfoList &sub_graph_map, uint64_t session_id) {
  GE_CHECK_NOTNULL(compute_graph);
  // use default 16 multi thread
  uint32_t thread_num = 16;

  char *env = std::getenv("THREAD_MULTI_NUM");
  if (env != nullptr) {
    thread_num = atoi(env);
    GEEVENT("OptimizeSubGraphWithMultiThreads thread num: %u", thread_num);
  }


  ThreadPool executor(thread_num);
  std::vector<std::future<Status>> vector_future;
  const auto &root_subgraph_list = sub_graph_map[compute_graph];
  std::string op_compile_strategy;
  (void)AttrUtils::GetStr(compute_graph, ATTR_NAME_OP_COMPILE_STRATEGY, op_compile_strategy);
  GELOGD("OptimizeSubGraphWithMultiThreads Process op_compile_strategy:%s", op_compile_strategy.c_str());
  for (const auto &subgraph : root_subgraph_list) {
    if (!op_compile_strategy.empty()) {
      (void) AttrUtils::SetStr(subgraph->GetSubGraph(), ATTR_NAME_OP_COMPILE_STRATEGY, op_compile_strategy);
    }
    std::future<Status> f = executor.commit(GraphManager::ProcessSubGraphWithMultiThreads, this,
                                            compute_graph->GetGraphID(), subgraph,
                                            compute_graph->GetName(), session_id,
                                            GetThreadLocalContext());
    if (!f.valid()) {
      GELOGE(FAILED, "Future is invalid");
      return FAILED;
    }
    vector_future.emplace_back(std::move(f));
  }
  for (auto &function_graph : compute_graph->GetAllSubgraphs()) {
    auto subgraph_list = sub_graph_map[function_graph];
    for (const auto &subgraph : subgraph_list) {
      if (!op_compile_strategy.empty()) {
        (void) AttrUtils::SetStr(subgraph->GetSubGraph(), ATTR_NAME_OP_COMPILE_STRATEGY, op_compile_strategy);
      }
      std::future<Status> f = executor.commit(GraphManager::ProcessSubGraphWithMultiThreads, this,
                                              compute_graph->GetGraphID(), subgraph,
                                              compute_graph->GetName(), session_id,
                                              GetThreadLocalContext());
      if (!f.valid()) {
        GELOGE(FAILED, "Future is invalid");
        return FAILED;
      }
      vector_future.emplace_back(std::move(f));
    }
  }
  GELOGD("All sub graph num is %zu", vector_future.size());
  for (size_t i = 0; i < vector_future.size(); ++i) {
    Status ret_status = vector_future[i].get();
    if (ret_status != SUCCESS) {
      GELOGE(ret_status, "subgraph %zu optimize failed", i);
      return ret_status;
    }
  }
  return SUCCESS;
}

bool GraphManager::CheckAllFusionOptimizeSuccess(const ComputeGraphPtr &compute_graph,
                                                 Graph2SubGraphInfoList &sub_graph_map) {
  if (compute_graph == nullptr) {
    GELOGE(PARAM_INVALID, "Input param compute_graph is nullptr.");
    return false;
  }

  /// 1. FE will set attr optimize_group with true(false) while lx fusion is success(fail);
  /// 2. FE will not set attr optimize_group while fe.ini set l2fusion enable false;
  /// 3. Other engine will not set attr optimize_group.
  const auto &root_subgraph_list = sub_graph_map[compute_graph];
  for (const auto &subgraph : root_subgraph_list) {
    bool optimize_group = true;
    (void) AttrUtils::GetBool(subgraph->GetSubGraph(), ATTR_NAME_OPTIMIZE_GROUP, optimize_group);
    if (!optimize_group) {
      GELOGW("Run lx optimize for subgraph:%s failed.", subgraph->GetSubGraph()->GetName().c_str());
      return false;
    }
  }
  for (auto &function_graph : compute_graph->GetAllSubgraphs()) {
    const auto &subgraph_list = sub_graph_map[function_graph];
    for (const auto &subgraph : subgraph_list) {
      bool optimize_group = true;
      (void) AttrUtils::GetBool(subgraph->GetSubGraph(), ATTR_NAME_OPTIMIZE_GROUP, optimize_group);
      if (!optimize_group) {
        GELOGW("Run lx optimize for subgraph:%s failed.", subgraph->GetSubGraph()->GetName().c_str());
        return false;
      }
    }
  }
  GELOGI("All subgraph are optimized successfully, no need to reuse buffer optimize.");
  return true;
}

Status GraphManager::ReplaceSubgraphWithOriGraph(const ComputeGraphPtr &compute_graph,
                                                 Graph2SubGraphInfoList &sub_graph_map,
                                                 std::unordered_map<std::string, ComputeGraphPtr> &copy_graphs) {
  GE_CHECK_NOTNULL(compute_graph);
  const auto &root_subgraph_list = sub_graph_map[compute_graph];
  for (const auto &subgraph : root_subgraph_list) {
    auto iter = copy_graphs.find(subgraph->GetSubGraph()->GetName());
    if (iter == copy_graphs.end()) {
      GELOGE(FAILED, "Can not find subgraph:%s in copy graphs.", subgraph->GetSubGraph()->GetName().c_str());
      return FAILED;
    }
    subgraph->SetSubGraph(iter->second);
  }

  for (auto &function_graph : compute_graph->GetAllSubgraphs()) {
    const auto &subgraph_list = sub_graph_map[function_graph];
    for (const auto &subgraph : subgraph_list) {
      auto iter = copy_graphs.find(subgraph->GetSubGraph()->GetName());
      if (iter == copy_graphs.end()) {
        GELOGE(FAILED, "Can not find subgraph:%s in copy graphs.", subgraph->GetSubGraph()->GetName().c_str());
        return FAILED;
      }
      subgraph->SetSubGraph(iter->second);
    }
  }
  GELOGI("All subgraphs are successfully replaced.");
  return SUCCESS;
}

Status GraphManager::SetSubgraph(uint64_t session_id, ComputeGraphPtr compute_graph, GraphPartitioner &partitioner) {
  GE_CHECK_NOTNULL(compute_graph);
  auto sub_graph_map = partitioner.GetSubGraphMap();
  GELOGD("Directly optimize subgraph with build mode:%s, and step:%s.",
         options_.build_mode.c_str(),
         options_.build_step.c_str());
  Status ret = OptimizeSubGraphWithMultiThreads(compute_graph, sub_graph_map, session_id);
  if (ret != SUCCESS) {
    GELOGE(ret, "Multiply optimize subgraph failed");
    return ret;
  }
  return SUCCESS;
}

#define GM_RUN_AND_DUMP_PERF(name, func, ...)                                                                    \
  do {                                                                                                           \
    GE_RUN_PERF(GraphManager, func, __VA_ARGS__);                                                                \
    GE_DUMP(compute_graph, "PreRunAfter" name);                                                                  \
    GELOGI("Run %s on graph %s(%u) success.", name, compute_graph->GetName().c_str(), graph_node->GetGraphId()); \
  } while (0)

Status GraphManager::PreRunOptimizeOriginalGraph(const GraphNodePtr &graph_node, const std::vector<GeTensor> &inputs,
                                                 ge::ComputeGraphPtr &compute_graph, uint64_t session_id) {
  GE_CHECK_NOTNULL(graph_node);
  GE_CHECK_NOTNULL(compute_graph);

  CompilerStages &stages = GetCompilerStages(graph_node->GetGraphId());
  GM_RUN_AND_DUMP_PERF("OptimizeGraphPrepare", stages.optimizer.OptimizeOriginalGraphForQuantize, compute_graph);
  GM_RUN_AND_DUMP_PERF("HandleSummaryOp", stages.optimizer.HandleSummaryOp, compute_graph);
  GM_RUN_AND_DUMP_PERF("Prepare", stages.preparer.PrepareDynShape, graph_node, inputs, compute_graph,
                       session_id);
  GM_RUN_AND_DUMP_PERF("OptimizeOriginalGraph", stages.optimizer.OptimizeOriginalGraph, compute_graph);

  GM_RUN_AND_DUMP_PERF("PrepareRunningFormatRefiner", stages.preparer.PrepareRunningFormatRefiner);
  GM_RUN_AND_DUMP_PERF("RefineRunningFormat", stages.optimizer.OptimizeOriginalGraphJudgeInsert, compute_graph);
  GM_RUN_AND_DUMP_PERF("SubexpressionMigration", SubexpressionMigration, compute_graph);
  GE_RUN(GraphManager, stages.preparer.RecordAIPPInfo, compute_graph);
  if (IsTailingOptimization()) {
    GM_RUN_AND_DUMP_PERF("OptimizeSwitchOp", stages.preparer.SwitchOpOptimize, compute_graph);
  }
  GM_RUN_AND_DUMP_PERF("Optimize1", OptimizeStage1, compute_graph);
  GM_RUN_AND_DUMP_PERF("InferShape2", compute_graph->InferShapeInNeed);

  PassManager graph_pass;
  GE_CHK_STATUS_RET(graph_pass.AddPass("PreRun::CtrlEdgeTransferPass", new (std::nothrow) CtrlEdgeTransferPass))
  GE_CHK_STATUS_RET(graph_pass.Run(compute_graph));

  GE_CHK_STATUS_RET(stages.optimizer.IdentifyReference(compute_graph), "Identify reference failed.");
  GELOGD("PreRun:PreRunOptimizeOriginalGraph success.");
  return SUCCESS;
}

Status GraphManager::PreRunOptimizeSubGraph(const GraphNodePtr &graph_node,
                                            ge::ComputeGraphPtr &compute_graph,
                                            uint64_t session_id) {
  GE_CHECK_NOTNULL(graph_node);
  GE_CHECK_NOTNULL(compute_graph);
  GM_RUN_AND_DUMP_PERF("OptimizeSubgraph", OptimizeSubgraph, graph_node, compute_graph, session_id);

  // Dump graph to tuning path
  if (options_.build_mode == BUILD_MODE_TUNING && options_.build_step == BUILD_STEP_AFTER_UB_MATCH) {
    std::string tuning_path;
    (void) GetContext().GetOption(TUNING_PATH, tuning_path);
    GELOGD("Dump path:%s.", tuning_path.c_str());
    GraphUtils::DumpGEGraph(compute_graph, "", true, tuning_path);
  }
  GELOGD("PreRun:PreRunOptimizeSubGraph success.");
  return SUCCESS;
}

Status GraphManager::PreRunAfterOptimizeSubGraph(const GraphNodePtr &graph_node, ComputeGraphPtr &compute_graph,
                                                 GeRootModelPtr &ge_root_model, uint64_t session_id) {
  GE_CHECK_NOTNULL(graph_node);
  GE_CHECK_NOTNULL(compute_graph);

  CompilerStages &stages = GetCompilerStages(graph_node->GetGraphId());
  GM_RUN_AND_DUMP_PERF("OptimizeWholeGraph", stages.optimizer.OptimizeWholeGraph, compute_graph);
  GM_RUN_AND_DUMP_PERF("Optimize2", OptimizeStage2, compute_graph);
  GM_RUN_AND_DUMP_PERF("OptimizeGraphBeforeBuildForRts",
                       GetCompilerStages(graph_node->GetGraphId()).optimizer.OptimizeGraphBeforeBuildForRts,
                       compute_graph);

  Status ret = compute_graph->TopologicalSorting();
  if (ret != SUCCESS) {
    GELOGE(ret, "Graph topological sort failed, ret:%d.", ret);
    return ret;
  }

  GM_RUN_AND_DUMP_PERF("Build", Build, graph_node, compute_graph, ge_root_model, session_id);
  GELOGD("PreRun:PreRunAfterOptimizeSubGraph success.");
  return SUCCESS;
}

Status GraphManager::SetRtContext(rtContext_t rt_context, rtCtxMode_t mode, uint64_t session_id, uint32_t graph_id) {
  GELOGD("set rt_context, session id: %lu, graph id: %u, mode %d, device id:%u.", session_id, graph_id,
         static_cast<int>(mode), ge::GetContext().DeviceId());

  rtError_t rt_ret = rtCtxCreate(&rt_context, mode, ge::GetContext().DeviceId());
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(FAILED, "Call rt api failed, ret: 0x%X", rt_ret);
    return FAILED;
  }
  rt_ret = rtCtxSetCurrent(rt_context);
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(FAILED, "Call rt api failed, ret: 0x%X", rt_ret);
    return FAILED;
  }
  RtContextUtil::GetInstance().AddRtContext(session_id, graph_id, rt_context);
  return SUCCESS;
}

Status GraphManager::RunCustomPass(const GraphNodePtr &graph_node) {
  ConstGraphPtr const_graph = graph_node->GetGraph();
  auto comp_graph = GraphUtils::GetComputeGraph(*const_graph);
  GE_DUMP(comp_graph, "RunCustomPassBegin");

  GE_TIMESTAMP_START(RunCustomPass);
  GraphPtr graph = std::const_pointer_cast<Graph>(const_graph);
  GE_CHK_STATUS_RET(CustomPassHelper::Instance().Run(graph), "Graph[%s] run custom pass fail.",
                    comp_graph->GetName().c_str());
  GE_TIMESTAMP_END(RunCustomPass, "GraphBuilder::RunCustomPass");
  return SUCCESS;
}

Status GraphManager::PreRun(const GraphNodePtr &graph_node, const std::vector<GeTensor> &inputs,
                            GeRootModelPtr &ge_root_model, uint64_t session_id) {
  GE_CHECK_NOTNULL(graph_node);
  GE_CHECK_NOTNULL(graph_node->GetGraph());
  GE_CHK_STATUS_RET_NOLOG(RunCustomPass(graph_node));
  auto compute_graph = GraphUtils::GetComputeGraph(*graph_node->GetGraph());
  GE_CHECK_NOTNULL(compute_graph);
  compute_graph->SetSessionID(session_id);
  auto analyzer_instance = Analyzer::GetInstance();
  GE_CHK_STATUS_RET(analyzer_instance->BuildJsonObject(session_id, compute_graph->GetGraphID()),
                    "BuildJsonObject Failed")

  GEEVENT("PreRun start, graph node size %zu, session id %lu, graph id %u, graph name %s",
          compute_graph->GetDirectNodesSize(), session_id, compute_graph->GetGraphID(),
          compute_graph->GetName().c_str());
  GE_DUMP(compute_graph, "PreRunBegin");
  // rtContext_t
  Status ret = SetRtContext(rtContext_t(), RT_CTX_GEN_MODE, session_id, compute_graph->GetGraphID());
  if (ret != SUCCESS) {
    GELOGE(ret, "Set rt context failed.");
    return ret;
  }

  /// 1. BUILD_MODE_TUNING with BUILD_STEP_AFTER_UB_MATCH no need PreRunOptimizeOriginalGraph;
  /// 2. BUILD_MODE_TUNING with BUILD_STEP_AFTER_MERGE no need PreRunOptimizeOriginalGraph.
  /// 3. BUILD_MODE_TUNING with BUILD_STEP_AFTER_BUILDER_SUB no need PreRunOptimizeOriginalGraph.
  bool run_optimize_original_graph = !((options_.build_mode == BUILD_MODE_TUNING) &&
                                        (options_.build_step == BUILD_STEP_AFTER_UB_MATCH ||
                                         options_.build_step == BUILD_STEP_AFTER_MERGE ||
                                         options_.build_step == BUILD_STEP_AFTER_BUILDER_SUB));
  if (run_optimize_original_graph) {
    Status ret = PreRunOptimizeOriginalGraph(graph_node, inputs, compute_graph, session_id);
    if (ret != SUCCESS) {
      GELOGE(ret, "Run PreRunOptimizeOriginalGraph failed for graph:%s.", compute_graph->GetName().c_str());
      return ret;
    }
  }

  ret = PreRunOptimizeSubGraph(graph_node, compute_graph, session_id);
  if (ret != SUCCESS) {
    GELOGE(ret, "Run PreRunOptimizeSubGraph failed for graph:%s.", compute_graph->GetName().c_str());
    return ret;
  }

  /// 1. BUILD_MODE_TUNING with BUILD_STEP_BEFORE_UB_MATCH no need PreRunAfterOptimizeSubGraph;
  /// 2. BUILD_MODE_TUNING with BUILD_STEP_AFTER_BUILDER no need PreRunAfterOptimizeSubGraph.
  /// 3. BUILD_MODE_TUNING with BUILD_STEP_AFTER_BUILDER_SUB no need PreRunAfterOptimizeSubGraph.
  bool run_after_optimize_subgraph = !((options_.build_mode == BUILD_MODE_TUNING) &&
                                        (options_.build_step == BUILD_STEP_BEFORE_UB_MATCH ||
                                         options_.build_step == BUILD_STEP_AFTER_BUILDER ||
                                         options_.build_step == BUILD_STEP_AFTER_BUILDER_SUB));
  if (run_after_optimize_subgraph) {
    Status ret = PreRunAfterOptimizeSubGraph(graph_node, compute_graph, ge_root_model, session_id);
    if (ret != SUCCESS) {
      GELOGE(ret, "Run PreRunAfterOptimizeSubGraph failed for graph:%s.", compute_graph->GetName().c_str());
      return ret;
    }
  }

  // when set incre build, save om model and var manager
  GeModelPtr ge_model = nullptr;
  auto save_ret = SaveCacheAfterBuild(graph_node->GetGraphId(), compute_graph, ge_model);
  if (save_ret != SUCCESS) {
    GELOGW("Fail to save cache.");
  }
  GEEVENT("[GEPERFTRACE] GE PreRun End");
  return SUCCESS;
}

Status GraphManager::SubexpressionMigration(ComputeGraphPtr &compute_graph) {
  PassManager pass_manager;
  GE_CHK_STATUS_RET(pass_manager.AddPass("SubexpressionMigrationPass", new (std::nothrow) SubexpressionMigrationPass));
  GE_CHK_STATUS_RET(pass_manager.AddPass("UnusedArgsCleanPass", new (std::nothrow) UnusedArgsCleanPass));

  GE_TIMESTAMP_START(SubexpressionMigrationPass);
  auto ret = pass_manager.Run(compute_graph);
  GE_TIMESTAMP_END(SubexpressionMigrationPass, "GraphManager::SubexpressionMigration");
  if (ret != SUCCESS && ret != NOT_CHANGED) {
    GELOGE(ret, "Run SubexpressionMigrationPass failed, ret:%u.", ret);
    return ret;
  }

  return SUCCESS;
}

Status GraphManager::StartForRunGraph(const GraphNodePtr &graph_node, const std::vector<GeTensor> &inputs,
                                      GeRootModelPtr &ge_root_model, uint64_t session_id) {
  // it will not execute graph prreprocess, optimize, parition, build if the graph has built successful.
  Status ret = SUCCESS;
  if (IsGraphNeedBuild(graph_node)) {
    if (graph_node->GetBuildFlag()) {
      GELOGE(PARAM_INVALID,
             "The graph %u need to re-build, you should remove it from GE "
             "first, then AddGraph again and rebuild it.",
             graph_node->GetGraphId());
      return PARAM_INVALID;
    }
    GeModelPtr ge_model = nullptr;
    // check need incre build.
    ret = IncreBuild(graph_node, ge_model);
    if (ret != SUCCESS) {
      ret = PreRun(graph_node, inputs, ge_root_model, session_id);
      // release rts generate context
      RtContextUtil::GetInstance().DestroyRtContexts(session_id, graph_node->GetGraphId());
      if (ret != SUCCESS) {
        GELOGE(ret, "PreRun Failed.");
        return ret;
      }
    }
    if (!graph_node->IsAsync()) {
      ret = LoadGraph(ge_root_model, graph_node);
    } else {
      ret = LoadGraphAsync(ge_root_model, graph_node);
    }
    if (ret != SUCCESS) {
      GELOGE(ret, "LoadGraph Failed.");
      return ret;
    }
    graph_node->SetBuildFlag(true);
    var_acc_ctrl_.SetGraphBuildEnd(graph_node->GetGraphId());
  } else if (!graph_node->GetLoadFlag()) {
    GeRootModelPtr ge_root_model_ptr = graph_node->GetGeRootModel();
    if (!graph_node->IsAsync()) {
      ret = LoadGraph(ge_root_model_ptr, graph_node);
    } else {
      ret = LoadGraphAsync(ge_root_model_ptr, graph_node);
    }
    if (ret != SUCCESS) {
      GELOGE(ret, "LoadGraph Failed.");
      return ret;
    }
  }
  return ret;
}
Status GraphManager::LoadGraph(const GeRootModelPtr &ge_root_model, const GraphNodePtr &graph_node) {
  GELOGI("[LoadGraph] run_graph_flag[%d], graph_id[%u]", options_.run_graph_flag, graph_node->GetGraphId());
  if (options_.run_graph_flag && ge_root_model != nullptr) {
    // synchronization run graph with model
    std::shared_ptr<GraphModelListener> model_listener = GetModelListener();
    ModelIdInfo model_id_info;
    bool is_unknown_shape = false;
    GE_CHK_STATUS_RET(ge_root_model->CheckIsUnknownShape(is_unknown_shape));
    if (!is_unknown_shape) {
      if (getenv(kEnvGeuseStaticMemory) != nullptr) {
        GELOGI("[LoadGraph] GE_USE_STATIC_MEMORY is seted.");
      } else {
        auto root_graph = ge_root_model->GetRootGraph();
        GE_CHECK_NOTNULL(root_graph);
        auto name_to_model = ge_root_model->GetSubgraphInstanceNameToModel();
        GeModelPtr ge_model = name_to_model[root_graph->GetName()];
        GE_CHK_STATUS_RET(CheckAndReleaseMemory(ge_model, graph_node));
      }
    }
    GE_TIMESTAMP_START(LoadGraph);
    Status ret = GraphLoader::LoadModelOnline(model_id_info.model_id, ge_root_model, model_listener);
    GE_TIMESTAMP_EVENT_END(LoadGraph, "GraphManager::LoadGraph");
    if (ret != SUCCESS) {
      GELOGE(ret, "[StartForRunGraph] LoadGraph Failed");
      graph_node->SetRunFlag(false);
      return ret;
    }
    graph_node->SetLoadFlag(true);
    ge_root_model->SetModelId(model_id_info.model_id);
    graph_node->SetGeRootModel(ge_root_model);
  }
  return SUCCESS;
}

Status GraphManager::LoadFromCache(const GraphNodePtr &graph_node, const ModelCacheHelperPtr &cache_helper,
                                   GeModelPtr &ge_model) {
  auto graph_id = graph_node->GetGraphId();
  auto ret = cache_helper->LoadOmModelFromCache(ge_model);
  if (ret != SUCCESS) {
    GELOGW("Fail to load om model from cache.");
    if (cache_helper->ClearCache(graph_id) != SUCCESS) {
      GELOGW("Fail to clear cache of graph %u.", graph_id);
    }
    return FAILED;
  }
  ret = cache_helper->RecoverVarManagerFromCache();
  if (ret != SUCCESS) {
    GELOGW("Fail to recover VarManager from cache.");
    if (cache_helper->ClearCache(graph_id) != SUCCESS) {
      GELOGW("Fail to clear cache of graph %u.", graph_id);
    }
    return FAILED;
  }
  ComputeGraphPtr compute_graph_in_model = GraphUtils::GetComputeGraph(ge_model->GetGraph());
  if (compute_graph_in_model == nullptr) {
    GELOGW("Error occurred when get compute graph from om, abandon.");
    return FAILED;
  } else {
    graph_node->SetComputeGraph(compute_graph_in_model);
    graph_node->SetGeModel(ge_model);
    GELOGI("Load model and graph form cache om file.");
  }
  return SUCCESS;
}

Status GraphManager::SaveCacheBeforeBuild(uint32_t graph_id, const ModelCacheHelperPtr &cache_helper) {
  auto ret = cache_helper->SaveCacheInfoToCache();
  if (ret != SUCCESS) {
    GELOGW("Fail to save cache info of graph[%d] to cache.", graph_id);
    return FAILED;
  }
  ret = cache_helper->SaveVarManagerToCache(true);
  if (ret != SUCCESS) {
    GELOGW("Fail to save var manager to cache.");
    cache_helper->ClearCache(graph_id);
    return FAILED;
  }
  GELOGI("Cache files have been saved.");
  return SUCCESS;
}

Status GraphManager::SaveCacheAfterBuild(uint32_t graph_id, ge::ComputeGraphPtr graph, GeModelPtr &ge_model) {
  std::shared_ptr<GELib> instance_ptr = ge::GELib::GetInstance();
  if ((instance_ptr == nullptr) || !instance_ptr->InitFlag()) {
    GELOGW("GELib not initialized.");
    return FAILED;
  }

  if (instance_ptr->IsIncreBuild()) {
    std::lock_guard<std::mutex> lock(member_mutex_);
    auto iter = cache_helper_map_.find(graph_id);
    if (iter == cache_helper_map_.end()) {
      GELOGW("Can not find ModelCacheHelper of graph[%u]", graph_id);
      return FAILED;
    } else {
      ModelCacheHelperPtr cache_helper = iter->second;
      auto ret = cache_helper->RefreshComputeGraph(graph);
      if (ret != SUCCESS) {
        cache_helper->ClearCache(graph_id);
        GELOGW("Fail to refresh cache helper's compute graph");
        return FAILED;
      }
      ret = cache_helper->SaveVarManagerToCache(false);
      if (ret != SUCCESS) {
        cache_helper->ClearCache(graph_id);
        GELOGW("Fail to save VarManager to cache");
        return FAILED;
      }
      ret = cache_helper->SaveOmModelToCache(ge_model);
      if (ret != SUCCESS) {
        cache_helper->ClearCache(graph_id);
        GELOGW("Fail to save om model to cache");
        return FAILED;
      }
    }
  }
  return SUCCESS;
}

Status GraphManager::InnerRunGraph(GraphNodePtr &graph_node, const GraphId &graph_id,
                                   const std::vector<GeTensor> &inputs, std::vector<GeTensor> &outputs) {
  Status ret = graph_executor_.SetCondition(&sync_run_mutex_, &condition_, graph_run_listener_);
  if (ret != SUCCESS) {
    GELOGE(GE_GRAPH_RUNGRAPH_FAILED, "[RunGraph] set condition failed, graph_id = %u.", graph_id);
    graph_node->SetRunFlag(false);
    return GE_GRAPH_RUNGRAPH_FAILED;
  }

  if (GetTrainFlag()) {
    GE_CHK_STATUS_RET(graph_executor_.SetGraphContext(GetGraphContext()));
    graph_executor_.SetTrainFlag(options_.train_graph_flag);
  }
  ret = graph_executor_.ExecuteGraph(graph_id, graph_node->GetGeRootModel(), inputs, outputs);

  graph_node->SetRunFlag(false);
  if (ret != SUCCESS) {
    GELOGE(ret, "[RunGraph] execute graph failed, graph_id = %u.", graph_id);
    return ret;
  }
  return SUCCESS;
}

Status GraphManager::RunGraph(const GraphId &graph_id, const std::vector<GeTensor> &inputs,
                              std::vector<GeTensor> &outputs, uint64_t session_id) {
  std::lock_guard<std::mutex> lock(run_mutex_);
  GELOGI("[RunGraph] start to run graph, graph_id = %u, is_train_graph: %d", graph_id, GetTrainFlag());

  if (inputs.empty()) {
    GELOGI("[RunGraph] initialize sub graph has no inputs");
  }

  // find graph
  GraphNodePtr graph_node = nullptr;
  Status ret = GetGraphNode(graph_id, graph_node);
  if (ret != SUCCESS) {
    GELOGE(ret, "[RunGraph] graph not exist, graph_id = %u.", graph_id);
    return ret;
  }

  if (graph_node == nullptr) {
    GELOGE(GE_GRAPH_GRAPH_NODE_NULL, "[RunGraph] graph node is NULL, graph_id = %u.", graph_id);
    return GE_GRAPH_GRAPH_NODE_NULL;
  }

  if (graph_node->GetRunFlag()) {
    GELOGE(GE_GRAPH_ALREADY_RUNNING, "[RunGraph] graph already running, graph id = %u", graph_id);
    return GE_GRAPH_ALREADY_RUNNING;
  }

  UpdateLocalOmgContext(graph_id);

  // set graph's run flag
  graph_node->SetRunFlag(true);
  ComputeGraphPtr compute_graph_tmp = GraphUtils::GetComputeGraph(*(graph_node->GetGraph()));

  GE_IF_BOOL_EXEC(GetTrainFlag(),
                  GE_IF_BOOL_EXEC(compute_graph_tmp == nullptr,
                                  GELOGE(GE_GRAPH_GRAPH_NODE_NULL,
                                         "[RunGraph] compute_graph_tmp is NULL, graph id = %u.", graph_id);
                                  return GE_GRAPH_GRAPH_NODE_NULL;))

  // when set incre build, add cache helper map
  AddModelCacheHelperToMap(graph_id, session_id, compute_graph_tmp);

  if (options_.local_fmk_op_flag) {
    GetCompilerStages(graph_id).optimizer.TranFrameOp(compute_graph_tmp);
  }

  GeRootModelPtr ge_root_model = nullptr;
  ret = StartForRunGraph(graph_node, inputs, ge_root_model, session_id);
  if (ret != SUCCESS) {
    GELOGE(ret, "[RunGraph] StartForRunGraph failed!");
    graph_node->SetRunFlag(false);
    return ret;
  }

  // excute graph
  ret = InnerRunGraph(graph_node, graph_id, inputs, outputs);
  if (ret != SUCCESS) {
    return ret;
  }

  if (GetTrainFlag()) {
    if (compute_graph_tmp->IsSummaryGraph()) {
      ret = SummaryHandle(graph_id, outputs);
      if (ret != SUCCESS) {
        GELOGE(ret, "[RunGraph] SummaryHandle failed!");
      }
    }

    GeRootModelPtr root_model = graph_node->GetGeRootModel();
    if (root_model != nullptr) {
      GELOGI("Start CheckpointHandle.");
      auto checkPointGraph = root_model->GetRootGraph();
      if (IsCheckpointGraph(checkPointGraph)) {
        ret = CheckpointHandle(graph_id, checkPointGraph, outputs);
        if (ret != SUCCESS) {
          GELOGE(ret, "[RunGraph] CheckpointHandle failed!");
        }
      }
    }
  }

  GELOGI("[RunGraph] run graph success, graph_id = %u.", graph_id);
  return SUCCESS;
}

Status GraphManager::GenerateInfershapeGraph(GraphId &graph_id) {
  GELOGI("[DumpInfershapeJson] start to DumpInfershapeJson graph, graph_id=%u.", graph_id);
  // find graph
  GraphNodePtr graph_node = nullptr;
  Status ret = GetGraphNode(graph_id, graph_node);
  if (ret != SUCCESS) {
    GELOGE(ret, "[BuildGraph] graph not exist, graph_id = %u.", graph_id);
    return ret;
  }

  if (graph_node == nullptr) {
    GELOGE(GE_GRAPH_GRAPH_NODE_NULL, "[BuildGraph] graph node is NULL, graphId = %u.", graph_id);
    return GE_GRAPH_GRAPH_NODE_NULL;
  }

  UpdateLocalOmgContext(graph_id);

  ret = GetCompilerStages(graph_id).preparer.GenerateInfershapeGraph(graph_node->GetGraph());
  if (ret != SUCCESS) {
    GELOGE(ret, "ATC dump infershape json failed");
    return ret;
  }

  GELOGI("[DumpInfershapeJson] Dump infershape json success, graph_id=%u.", graph_id);
  return ret;
}

Status GraphManager::BuildGraphForUnregisteredOp(const GraphId &graph_id, const std::vector<GeTensor> &inputs,
                                                 GeRootModelPtr &ge_root_model, uint64_t session_id) {
  // find graph
  GraphNodePtr graph_node = nullptr;
  Status ret = GetGraphNode(graph_id, graph_node);
  if (ret != SUCCESS) {
    GELOGE(ret, "[BuildGraph] graph not exist, graph_id = %u.", graph_id);
    return ret;
  }

  if (graph_node == nullptr) {
    GELOGE(GE_GRAPH_GRAPH_NODE_NULL, "[BuildGraph] graph node is NULL, graphId = %u.", graph_id);
    return GE_GRAPH_GRAPH_NODE_NULL;
  }

  UpdateLocalOmgContext(graph_id);

  auto compute_graph = GraphUtils::GetComputeGraph(*graph_node->GetGraph());
  GE_CHECK_NOTNULL(compute_graph);

  GM_RUN_AND_DUMP_PERF("Prepare", GetCompilerStages(graph_id).preparer.PrepareDynShape, graph_node, inputs,
                       compute_graph, session_id);

  for (auto &node : compute_graph->GetAllNodes()) {
    OpDescPtr op_desc = node->GetOpDesc();
    GE_CHECK_NOTNULL(op_desc);
    if (op_desc->HasAttr(ATTR_NAME_UNREGST_OPPATH)) {
      vector<ge::NodePtr> node_vec = {node};

      auto instance_ptr = ge::GELib::GetInstance();
      if (instance_ptr == nullptr || !instance_ptr->InitFlag()) {
        GELOGE(GE_CLI_GE_NOT_INITIALIZED, "GE is not initialized");
        return GE_CLI_GE_NOT_INITIALIZED;
      }

      OpsKernelInfoStorePtr kernel_info =
          instance_ptr->OpsKernelManagerObj().GetOpsKernelInfoStore(op_desc->GetOpKernelLibName());
      if (kernel_info == nullptr) {
        GELOGE(FAILED, "Get op kernel info store failed");
        return FAILED;
      }

      ret = kernel_info->CompileOp(node_vec);
      if (ret != SUCCESS) {
        GELOGE(ret, "Compile op failed, op = %s, graph_id = %u.", op_desc->GetName().c_str(), graph_id);
        return ret;
     }
    }
  }

  GM_RUN_AND_DUMP_PERF("Build", Build, graph_node, compute_graph, ge_root_model, session_id);

  return SUCCESS;
}

Status GraphManager::BuildGraph(const GraphId &graph_id, const std::vector<GeTensor> &inputs,
                                GeRootModelPtr &ge_root_model, uint64_t session_id, bool async) {
  GELOGD("[BuildGraph] start to build graph, graph_id=%u.", graph_id);
  if (inputs.empty()) {
    GELOGW("[BuildGraph] BuildGraph warning: empty GeTensor inputs");
  }

  // find graph
  GraphNodePtr graph_node = nullptr;
  Status ret = GetGraphNode(graph_id, graph_node);
  if (ret != SUCCESS) {
    GELOGE(ret, "[BuildGraph] graph not exist, graph_id = %u.", graph_id);
    return ret;
  }

  if (graph_node == nullptr) {
    GELOGE(GE_GRAPH_GRAPH_NODE_NULL, "[BuildGraph] graph node is NULL, graphId = %u.", graph_id);
    return GE_GRAPH_GRAPH_NODE_NULL;
  }

  if (graph_node->GetRunFlag()) {
    GELOGE(GE_GRAPH_ALREADY_RUNNING, "[BuildGraph] graph already running, graph id = %u", graph_node->GetGraphId());
    return GE_GRAPH_ALREADY_RUNNING;
  }

  UpdateLocalOmgContext(graph_id);

  graph_node->SetAsync(async);
  // set graph's run flag
  graph_node->SetRunFlag(true);

  ret = StartForRunGraph(graph_node, inputs, ge_root_model, session_id);
  graph_node->SetRunFlag(false);
  if (ret != SUCCESS) {
    GELOGE(GE_GRAPH_PRERUN_FAILED, "[BuildGraph] StartForRunGraph failed!");
    return GE_GRAPH_PRERUN_FAILED;
  }

  GELOGI("[BuildGraph] build graph success, graph_id=%u.", graph_id);
  return ret;
}

///
/// @ingroup ge_graph
/// @brief Save extra attribute to Model
/// @param [in] model: Model attribues will save to.
/// @param [in] type: type of OpDesc.
/// @param [in] attrs: attributes of OpDesc.
/// @param [in] inputs: inputs tensor.
/// @param [in] outputs: outputs tensor.
/// @return: Status
///
Status GraphManager::SaveParams(ge::GeModel &model, const std::string &type, const std::map<string, GeAttrValue> &attrs,
                                const std::vector<GeTensor> &inputs, const std::vector<GeTensor> &outputs) {
  GE_CHK_BOOL_EXEC(ge::AttrUtils::SetStr(&model, "ATTR_MODEL_OP_TYPE", type), return FAILED, "Set Op[%s] type fail",
                   type.c_str());

  for (const auto &it : attrs) {
    GE_CHK_BOOL_EXEC(model.SetAttr("ATTR_MODEL_" + it.first, it.second) == GRAPH_SUCCESS, return FAILED,
                     "Set OpDesc attribute[%s] fail", it.first.c_str());
  }

  GE_CHK_BOOL_EXEC(ge::AttrUtils::SetListTensor(&model, "ATTR_MODEL_TENSOR_INPUTS", inputs), return FAILED,
                   "Set Inputs tensor list fail");
  GE_CHK_BOOL_EXEC(ge::AttrUtils::SetListTensor(&model, "ATTR_MODEL_TENSOR_OUTPUTS", outputs), return FAILED,
                   "Set Outputs tensor list fail");

  return SUCCESS;
}

void GraphManager::RemoveModelCacheHelper(const GraphId &graph_id) {
  std::lock_guard<std::mutex> lock(member_mutex_);
  auto iter = cache_helper_map_.find(graph_id);
  if (iter != cache_helper_map_.end()) {
    cache_helper_map_.erase(iter);
  } else {
    GELOGW("[GraphManager] cache helper does not exist, graph_id = %u", graph_id);
  }
}

bool GraphManager::CheckModelLoad(const GeRootModelPtr &ge_root_model, bool load_flag) {
  return ((ge_root_model != nullptr) && (ge_root_model->GetModelId() != INVALID_MODEL_ID) && load_flag);
}

Status GraphManager::RemoveGraph(const GraphId &graph_id) {
  GraphNodePtr graph_node = nullptr;
  Status ret = GetGraphNode(graph_id, graph_node);
  if (ret != SUCCESS) {
    GELOGE(GE_GRAPH_GRAPH_NOT_EXIST, "[GraphManager] Id %u does not exists.", graph_id);
    return GE_GRAPH_GRAPH_NOT_EXIST;
  }

  if ((graph_node == nullptr) || (graph_node->GetRunFlag())) {
    GELOGE(GE_GRAPH_GRAPH_IS_RUNNING, "[GraphManager] Id %u is running, can't be deleted.", graph_id);
    return GE_GRAPH_GRAPH_IS_RUNNING;
  }

  std::lock_guard<std::mutex> lock(unload_model_mutex_);

  Status middle_ret;
  rtError_t rt_ret;
  const std::vector<SubGraphInfoPtr> &all_sub_graph = graph_node->GetAllSubGraph();
  for (size_t i = 0; i < all_sub_graph.size(); ++i) {
    // must free buffer firstly
    middle_ret = all_sub_graph[i]->FreeInOutBuffer();
    if (middle_ret != SUCCESS) {
      GELOGE(middle_ret, "[GraphManager] RemoveGraph free mem failed, graph_id=%u.", graph_id);
      ret = middle_ret;
    }
    if (all_sub_graph[i]->GeModelIsValid() && all_sub_graph[i]->GetModelIdInfo().model_id != INVALID_MODEL_ID) {
      // unload model
      GELOGI("UnloadModel via new ome.");
      rt_ret = rtSetDevice(GetContext().DeviceId());
      if (rt_ret != RT_ERROR_NONE) {
        GELOGE(RT_FAILED, "[GraphManager:] rtSetDevice failed, modelId=%u, graphId=%u.",
               all_sub_graph[i]->GetModelIdInfo().model_id, graph_id);
        ret = FAILED;
        continue;
      }
      middle_ret = GraphLoader::UnloadModel(all_sub_graph[i]->GetModelIdInfo().model_id);
      if (middle_ret != SUCCESS) {
        GELOGE(middle_ret, "[GraphManager:] unload model failed, modelId=%u, graph_id=%u.",
               all_sub_graph[i]->GetModelIdInfo().model_id, graph_id);
        ret = middle_ret;
      }
      rt_ret = rtDeviceReset(GetContext().DeviceId());
      if (rt_ret != RT_ERROR_NONE) {
        GELOGE(RT_FAILED, "[GraphManager:] unload model failed, modelId=%u, graphId=%u.",
               all_sub_graph[i]->GetModelIdInfo().model_id, graph_id);
        ret = FAILED;
      }
    }
  }
  var_acc_ctrl_.RemoveGraph(graph_id);
  RemoveGraphNode(graph_id);

  RemoveModelCacheHelper(graph_id);

  auto ge_root_model = graph_node->GetGeRootModel();
  if (CheckModelLoad(ge_root_model, graph_node->GetLoadFlag())) {
    GELOGI("Unload model %u.", ge_root_model->GetModelId());
    rt_ret = rtSetDevice(GetContext().DeviceId());
    if (rt_ret != RT_ERROR_NONE) {
      GELOGE(RT_FAILED, "[GraphManager:] rtSetDevice failed, modelId=%u, graphId=%u.", ge_root_model->GetModelId(),
             graph_id);
      return FAILED;
    }
    middle_ret = GraphLoader::UnloadModel(ge_root_model->GetModelId());
    if (middle_ret != SUCCESS) {
      GELOGE(middle_ret, "[GraphManager:] unload model failed, modelId=%u, graph_id=%u.", ge_root_model->GetModelId(),
             graph_id);
      ret = middle_ret;
    }
    rt_ret = rtDeviceReset(GetContext().DeviceId());
    if (rt_ret != RT_ERROR_NONE) {
      GELOGE(RT_FAILED, "[GraphManager:] rtDeviceReset failed, modelId=%u, graphId=%u.", ge_root_model->GetModelId(),
             graph_id);
      ret = FAILED;
    }
  }

  RemoveCompilerStages(graph_id);

  GE_CHK_STATUS_RET(ret, "[GraphManager:] Remove graph failed, graph_id=%u.", graph_id);
  GELOGI("[GraphManager] remove graph success, graph_id=%u.", graph_id);
  return SUCCESS;
}

Status GraphManager::ParseOptions(const std::map<std::string, std::string> &options) {
  Status ret;

  ParseOption(options, "ge.INPUT_NODES_SET_FP16", options_.input_nodes_set_fp16);
  // parse streams max parallel num
  ret = ParseOption(options, STREAM_MAX_PARALLEL_NUM, options_.stream_max_parallel_num);
  if (ret != SUCCESS) {
    GELOGE(GE_GRAPH_OPTIONS_INVALID,
           "parse Key:%s value failed, it must be same format as "
           "DNN_V100:2,DNN_HCCL:3",
           STREAM_MAX_PARALLEL_NUM.c_str());
    return GE_GRAPH_OPTIONS_INVALID;
  }

  // get stream num
  ret = ParseOption(options, STREAM_NUM, options_.stream_num);
  if ((ret != SUCCESS) || (options_.stream_num == 0)) {
    GELOGE(GE_GRAPH_OPTIONS_INVALID, "Key:ge.stream_num, its value %d is invalid, must be not equal zero.",
           options_.stream_num);
    return GE_GRAPH_OPTIONS_INVALID;
  }

  // get perf level, its value please see enum PerfLevel
  ret = ParseOption(options, PERF_LEVEL, options_.perf_level);
  if ((ret != SUCCESS) || IsPerfLevelInvalid(options_.perf_level)) {
    GELOGE(GE_GRAPH_OPTIONS_INVALID, "Key:ge.perfLevel, its value %d is invalid, must be enum PerfLevel type.",
           options_.perf_level);
    return GE_GRAPH_OPTIONS_INVALID;
  }

  // get encrypt mode
  ret = ParseOption(options, ENCRYPT_MODE, options_.encrypt_mode);
  GE_IF_BOOL_EXEC(ret != SUCCESS,
                  GELOGE(GE_GRAPH_OPTIONS_INVALID, "Key:ge.encryptMode value invalid.");
                  return GE_GRAPH_OPTIONS_INVALID);

  // get ek file
  ParseOption(options, EK_FILE, options_.ek_file);

  // get cert file
  ParseOption(options, CERT_FILE, options_.cert_file);

  // get hw key file
  ParseOption(options, HW_KEY_FILE, options_.hw_key_file);

  // get private file
  ParseOption(options, PRIVATE_KEY_FILE, options_.private_key_file);

  // get framework type, its value please see enum FrameworkType
  ret = ParseOption(options, FRAMEWORK_TYPE, options_.framework_type);
  if (ret != SUCCESS) {
    // print error log in ParseOption
    return GE_GRAPH_OPTIONS_INVALID;
  }

  // get calibration info file
  ParseOption(options, CALIBRATION_CONF_FILE, options_.calibration_conf_file);

  // get insert op info file
  ParseOption(options, INSERT_OP_FILE, options_.insert_op_file);

  // get output node name
  ParseOption(options, OUTPUT_NODE_NAME, options_.output_node_name);

  // get function bin path
  ParseOption(options, "ge.func_bin_path", options_.func_bin_path);

  // get core type
  ParseOption(options, CORE_TYPE, options_.core_type);

  // get weight compress flag
  ret = ParseOption(options, COMPRESS_FLAG, options_.compress_flag);
  GE_IF_BOOL_EXEC(ret != SUCCESS,
                  GELOGE(GE_GRAPH_OPTIONS_INVALID, "Key:ge.compressFlag value is invalid, must be 0 or 1.");
                  return GE_GRAPH_OPTIONS_INVALID);

  // ge.graphType.
  options_.run_graph_flag = true;
  ret = ParseOption(options, RUN_FLAG, options_.run_graph_flag);
  GE_IF_BOOL_EXEC(ret != SUCCESS,
                  GELOGE(GE_GRAPH_OPTIONS_INVALID, "Key:ge.runFlag value is invalid, must be 0 or 1.");
                  return GE_GRAPH_OPTIONS_INVALID);

  // ge.graphType
  ret = ParseTrainGraphFlag(options_.run_graph_flag, options_.train_graph_flag);
  GE_IF_BOOL_EXEC(ret != SUCCESS,
                  GELOGE(GE_GRAPH_OPTIONS_INVALID, "Key:ge.runFlag value is invalid");
                  return GE_GRAPH_OPTIONS_INVALID);

  // parse FmkOp
  options_.local_fmk_op_flag = false;
  ret = ParseOption(options, LOCAL_FMKOP_FLAG, options_.local_fmk_op_flag);
  GE_IF_BOOL_EXEC(ret != SUCCESS,
                  GELOGE(GE_GRAPH_OPTIONS_INVALID, "Key:ge.localFmkopFlag value is invalid, must be 0 or 1.");
                  return GE_GRAPH_OPTIONS_INVALID);
  options_.enable_print_op_pass = true;
  ret = ParseOption(options, ENABLE_PRINT_OP_PASS, options_.enable_print_op_pass);

  options_.is_single_op = false;
  ret = ParseOption(options, SINGLE_OP_FLAG, options_.is_single_op);
  GE_IF_BOOL_EXEC(ret != SUCCESS,
                  GELOGE(GE_GRAPH_OPTIONS_INVALID, "Key:ge.enablePrintOpPass value is invalid, must be 0 or 1.");
                  return GE_GRAPH_OPTIONS_INVALID);
  // parse hcom parallel
  options_.hcom_parallel = false;
  ret = ParseOption(options, HCOM_PARALLEL, options_.hcom_parallel);
  GE_IF_BOOL_EXEC(ret != SUCCESS,
                  GELOGE(GE_GRAPH_OPTIONS_INVALID, "Key:ge.hcomParallel value is invalid, must be 0 or 1.");
                  return GE_GRAPH_OPTIONS_INVALID);
  // net output node dataType
  ParseOption(options, OUTPUT_DATATYPE, options_.output_datatype);

  // Set save_original_model flag (ge.save_original_model)
  ParseOption(options, SAVE_ORIGINAL_MODEL, options_.save_original_model);
  // Original model file name
  ParseOption(options, ORIGINAL_MODEL_FILE, options_.original_model_file);

  ParseOption(options, INPUT_SHAPE, options_.input_shape);
  ParseOption(options, kDynamicDims, options_.dynamic_dims);
  ParseOption(options, DYNAMIC_NODE_TYPE, options_.dynamic_node_type);
  GELOGD("Dynamic dims params: input shape is %s, dynamic dims is %s, dynamic node type is %d.",
         options_.input_shape.c_str(), options_.dynamic_dims.c_str(), options_.dynamic_node_type);

  // Set Build model and step
  ParseOption(options, BUILD_MODE, options_.build_mode);
  ParseOption(options, BUILD_STEP, options_.build_step);

  return SUCCESS;
}

Status GraphManager::ParseTrainGraphFlag(bool &options, bool &option) {
  std::shared_ptr<GELib> ge_instance_ptr = ge::GELib::GetInstance();
  if (ge_instance_ptr == nullptr) {
    GELOGW("[Initialize] set train_graph_flag_ to 0 when GE is not initialized or finalized.");
    option = false;
  } else if (!ge_instance_ptr->isTrainMode()) {
    option = false;
  } else {  //  ge_instance_ptr->isTrainMode() is true
    if (!options) {
      GELOGE(GE_GRAPH_OPTIONS_INVALID,
             "Key:ge.runFlag, its value %d is invalid, it must be 1 when GElib::is_train_mode_ flag is 1", options);
      return GE_GRAPH_OPTIONS_INVALID;
    }
    option = true;
  }
  return SUCCESS;
}

bool GraphManager::IsPerfLevelInvalid(int32_t perf_level) {
  return ((perf_level != static_cast<int32_t>(GEN_TASK_WITHOUT_L2FUSION)) &&
          (perf_level != static_cast<int32_t>(GEN_TASK_WITHOUT_FUSION)) && (perf_level != -1));
}

void GraphManager::ParseOption(const std::map<std::string, std::string> &options, const std::string &key,
                               std::string &option) {
  auto iter = options.find(key);
  if (iter != options.end()) {
    option = iter->second;
  }
}

Status GraphManager::ParseOption(const std::map<std::string, std::string> &options, const std::string &key,
                                 bool &option) {
  auto iter = options.find(key);
  if (iter != options.end()) {
    string flag = iter->second;
    if (flag == "0") {
      option = false;
    } else if (flag == "1") {
      option = true;
    } else {
      GELOGE(GE_GRAPH_OPTIONS_INVALID, "Key:%s, its value %s is invalid, it must be 0 or 1.", key.c_str(),
             flag.c_str());
      return GE_GRAPH_OPTIONS_INVALID;
    }
  }
  return SUCCESS;
}

Status GraphManager::ParseOption(const std::map<std::string, std::string> &options, const std::string &key,
                                 int &option) {
  const int kDecimal = 10;
  char *ptr = nullptr;
  auto iter = options.find(key);
  if (iter != options.end()) {
    option = static_cast<int32_t>(std::strtol(iter->second.c_str(), &ptr, kDecimal));
    if (ptr != nullptr && *ptr != '\0') {
      GELOGE(GE_GRAPH_OPTIONS_INVALID, "Key:%s, its value %s is invalid, must be int32_t type.", key.c_str(),
             iter->second.c_str());
      return GE_GRAPH_OPTIONS_INVALID;
    }
  }
  return SUCCESS;
}

void GraphManager::Trim(std::string &str) {
  if (!str.empty()) {
    auto it = str.find_first_not_of(" ");
    if (it != std::string::npos) {
      str.erase(0, it);
    }
    it = str.find_last_not_of(" ");
    if (it != std::string::npos) {
      str.erase(it + 1);
    }
  }
}

Status GraphManager::ParseOption(const std::map<std::string, std::string> &options, const std::string &key,
                                 std::map<std::string, int> &option) {
  auto iter = options.find(key);
  if (iter == options.end()) {
    return SUCCESS;
  }
  GELOGI("Start to parse %s", key.c_str());
  option.clear();
  std::string op_num = iter->second;

  // split string by ','
  std::vector<std::string> split;
  std::istringstream f(op_num);
  std::string str_tmp;
  while (getline(f, str_tmp, ',')) {
    split.push_back(str_tmp);
  }

  for (const std::string &engine_parallel : split) {
    // split engine and num by :
    size_t pos = engine_parallel.find(':');
    if (pos == string::npos) {
      GELOGE(GE_GRAPH_OPTIONS_INVALID,
             "engine and num must be connected by :, "
             "while your input is %s",
             engine_parallel.c_str());
      return GE_GRAPH_OPTIONS_INVALID;
    }
    std::string engine_name = engine_parallel.substr(0, pos);
    std::string parallel_num = engine_parallel.substr(pos + 1);
    Trim(engine_name);
    Trim(parallel_num);

    Status ret = CheckEngineName(engine_name, key, option);
    if (ret != SUCCESS) {
      GELOGE(GE_GRAPH_OPTIONS_INVALID, "check engine name : %s failed, ", engine_name.c_str());
      return GE_GRAPH_OPTIONS_INVALID;
    }

    int num = 0;
    ret = ParseParallelNum(parallel_num, key, num);
    if (ret != SUCCESS) {
      GELOGE(GE_GRAPH_OPTIONS_INVALID, "parse parallel num failed");
      return GE_GRAPH_OPTIONS_INVALID;
    }

    option.insert(std::make_pair(engine_name, num));
  }
  GELOGI("Parse %s successfully", key.c_str());
  return SUCCESS;
}

Status GraphManager::CheckEngineName(const std::string &engine_name, const std::string &key,
                                     const std::map<std::string, int> &option) {
  if (engine_name.empty()) {
    GELOGE(GE_GRAPH_OPTIONS_INVALID, "engine name of %s is empty", key.c_str());
    return GE_GRAPH_OPTIONS_INVALID;
  }
  // judge whether exist in engine list
  if (!GELib::GetInstance()->DNNEngineManagerObj().IsEngineRegistered(engine_name)) {
    GELOGW("engine : %s is not registered in %s", engine_name.c_str(), key.c_str());
  }

  auto it_stream_repeat = option.find(engine_name);
  if (it_stream_repeat != option.end()) {
    GELOGE(GE_GRAPH_OPTIONS_INVALID, "engine : %s of %s is repeated", engine_name.c_str(), key.c_str());
    return GE_GRAPH_OPTIONS_INVALID;
  }
  return SUCCESS;
}

Status GraphManager::ParseParallelNum(const std::string &parallel_num, const std::string &key, int &num) {
  if (parallel_num.empty()) {
    GELOGE(GE_GRAPH_OPTIONS_INVALID, "parallel num of %s is empty", key.c_str());
    return GE_GRAPH_OPTIONS_INVALID;
  }
  for (char c : parallel_num) {
    if (!isdigit(c)) {
      GELOGE(GE_GRAPH_OPTIONS_INVALID, "%s input is invalid ", key.c_str());
      return GE_GRAPH_OPTIONS_INVALID;
    }
  }

  try {
    num = std::stoi(parallel_num);
  } catch (std::invalid_argument &) {
    GELOGE(GE_GRAPH_OPTIONS_INVALID, "parallel num : %s of %s is invalid argument", parallel_num.c_str(), key.c_str());
    return GE_GRAPH_OPTIONS_INVALID;
  } catch (std::out_of_range &) {
    GELOGE(GE_GRAPH_OPTIONS_INVALID, "parallel num : %s of %s is out of range", parallel_num.c_str(), key.c_str());
    return GE_GRAPH_OPTIONS_INVALID;
  } catch (...) {
    GELOGE(GE_GRAPH_OPTIONS_INVALID, "parallel num : %s of %s is invalid argument", parallel_num.c_str(), key.c_str());
    return GE_GRAPH_OPTIONS_INVALID;
  }

  if (num < 1) {
    GELOGE(GE_GRAPH_OPTIONS_INVALID, "parallel num : %s of %s must bigger than 0", parallel_num.c_str(), key.c_str());
    return GE_GRAPH_OPTIONS_INVALID;
  }
  return SUCCESS;
}


void GraphManager::AddGraphNode(GraphId graph_id, const GraphNodePtr &graph_node) {
  std::lock_guard<std::mutex> lock(member_mutex_);
  graph_map_.emplace(graph_id, graph_node);
}

void GraphManager::RemoveGraphNode(GraphId graph_id) {
  std::lock_guard<std::mutex> lock(member_mutex_);
  graph_map_.erase(graph_id);
}

bool GraphManager::HasGraphNode(GraphId graph_id) {
  std::lock_guard<std::mutex> lock(member_mutex_);
  return graph_map_.find(graph_id) != graph_map_.end();
}

Status GraphManager::GetGraphNode(const GraphId &graph_id, GraphNodePtr &out) {
  std::lock_guard<std::mutex> lock(member_mutex_);
  auto iter = graph_map_.find(graph_id);
  if (iter == graph_map_.end()) {
    out = nullptr;
    GELOGE(GE_GRAPH_GRAPH_NOT_EXIST, "[GraphManager] graph not exist, graph_id= %u.", graph_id);
    return GE_GRAPH_GRAPH_NOT_EXIST;
  }
  out = iter->second;
  return SUCCESS;
}

Status GraphManager::GetVariable(const std::string &name, Tensor &val) {
  GeTensorPtr ge_tensor_ptr = TensorAdapter::AsGeTensorPtr(val);
  GE_CHECK_NOTNULL(ge_tensor_ptr);
  return GetGraphContext()->GetVariableTensor(name, *(ge_tensor_ptr.get()));
}

Status GraphManager::SummaryHandle(const GraphId &graph_id, std::vector<GeTensor> &outputs) {
  std::vector<GeTensor> without_summary_outputs;
  std::set<int> summary_output_index;
  GELOGI("[GraphManager] SummaryHandle, outputsSize=%zu.", outputs.size());
  const std::map<uint32_t, std::map<string, size_t>> &whole_summary_output_indexes =
      GetCompilerStages(graph_id).optimizer.GetSummaryOutputIndexes();
  if (whole_summary_output_indexes.find(graph_id) == whole_summary_output_indexes.end()) {
    GELOGE(FAILED, "No Summary graph found in map.");
    return FAILED;
  }
  const std::map<string, size_t> &summary_output_indexes = whole_summary_output_indexes.at(graph_id);
  GELOGI("[GraphManager] SummaryHandle, summaryOutputIndexesSize=%zu.", summary_output_indexes.size());
  std::map<string, Tensor> summary_results;
  for (auto iter = summary_output_indexes.begin(); iter != summary_output_indexes.end(); ++iter) {
    GELOGI("[GraphManager] SummaryHandle, summaryName=%s, outputIndex=%zu.", iter->first.c_str(), iter->second);
    summary_results.emplace(iter->first, TensorAdapter::AsTensor(outputs.at(iter->second)));
    summary_output_index.emplace(iter->second);
  }

  // remove summary data from outputs
  if (!summary_output_index.empty()) {
    for (size_t j = 0; j < outputs.size(); ++j) {
      if (summary_output_index.count(j) == 0) {
        without_summary_outputs.emplace_back(outputs.at(j));
      }
    }
    outputs.swap(without_summary_outputs);
    GELOGI("[GraphManager] SummaryHandle, after swap outputsSize=%zu.", outputs.size());
  }

  if (!summary_results.empty()) {
    return PushSummaryData2ME(graph_id, summary_results);
  }

  return SUCCESS;
}

Status GraphManager::CheckpointHandle(const GraphId &graph_id, const ComputeGraphPtr &compute_graph,
                                      const std::vector<GeTensor> &outputs) {
  GELOGI("[GraphManager] CheckpointHandle, outputsSize=%zu.", outputs.size());
  std::vector<InputOutputDescInfo> outputs_desc = graph_executor_.GetOutputsDesc();
  GELOGI("[GraphManager] CheckpointHandle, outputsDescSize=%zu.", outputs_desc.size());

  std::map<string, Tensor> save_results;
  NodePtr netoutput = nullptr;
  for (const auto &node : compute_graph->GetAllNodes()) {
    if (node->GetType() == kNetOutput) {
      netoutput = node;
      break;
    }
  }
  if (netoutput == nullptr) {
    GELOGE(FAILED, "Netoutput is null.");
    return FAILED;
  }
  for (const auto &in : netoutput->GetAllInDataAnchors()) {
    std::string desc_name;
    auto out_anchor = in->GetPeerOutAnchor();
    if (out_anchor == nullptr) {
      GELOGE(FAILED, "out_anchor is null.");
      return FAILED;
    }
    ge::NodePtr peer_node = out_anchor->GetOwnerNode();
    // find the variable node in graph
    while (peer_node != nullptr && peer_node->GetType() != kVariable) {
      if (peer_node->GetAllInDataAnchors().size() != 1) {
        GELOGE(FAILED, "More than one prior nodes of peer_node %s in checkpoint Graph.", peer_node->GetName().c_str());
        return FAILED;
      }
      auto peer_node_in = peer_node->GetAllInDataAnchors().at(0);
      auto peer_node_out_anchor = peer_node_in->GetPeerOutAnchor();
      if (peer_node_out_anchor != nullptr) {
        peer_node = peer_node_out_anchor->GetOwnerNode();
        if (peer_node->GetType() == kVariable) {
          break;
        }
      }
    }
    if (peer_node == nullptr) {
      GELOGE(FAILED, "No variable op found in one branch, checkpoint graph illegal.");
      return FAILED;
    }
    desc_name = peer_node->GetName();
    GELOGI("[GraphManager] CheckpointHandle, descName=%s.", desc_name.c_str());
    if (in->GetIdx() >= static_cast<int>(outputs.size())) {
      GELOGE(FAILED, "variable index out of range.");
      return FAILED;
    }
    save_results.emplace(desc_name, TensorAdapter::AsTensor(outputs.at(in->GetIdx())));
  }

  if (!save_results.empty()) {
    return PushSaveData2ME(graph_id, save_results);
  }

  return SUCCESS;
}

Status GraphManager::RegisterCallBackFunc(
    const std::string &key,
    const std::function<Status(uint32_t, const std::map<std::string, ge::Tensor> &)> &callback) {
  std::lock_guard<std::mutex> lock(member_mutex_);
  GELOGI("[GraphManager] RegisterCallBackFunc, key=%s.", key.c_str());
  me_callback_map_[key] = callback;
  return SUCCESS;
}

Status GraphManager::RegisterCallBackFunc(
  const std::string &key,
  const std::function<Status(uint32_t, const std::map<AscendString, ge::Tensor> &)> &callback) {
  std::lock_guard<std::mutex> lock(member_mutex_);
  GELOGI("[GraphManager] RegisterCallBackFunc, key=%s.", key.c_str());
  callback_map_[key] = callback;
  return SUCCESS;
}

Status GraphManager::PushSummaryData2ME(const GraphId &graph_id,
                                        const std::map<std::string, ge::Tensor> &summary_data) {
  std::lock_guard<std::mutex> lock(member_mutex_);
  GELOGI("[GraphManager] PushSummaryData2ME, dataSize=%zu.", summary_data.size());
  auto itr = me_callback_map_.find(kSummary);
  if (itr == me_callback_map_.end()) {
    auto iter = callback_map_.find(kSummary);
    if (iter != callback_map_.end()) {
      std::map<AscendString, ge::Tensor> tmp_summary_data;
      for (auto &data : summary_data) {
        AscendString tmp(data.first.c_str());
        tmp_summary_data[tmp] = data.second;
      }
      return iter->second(graph_id, tmp_summary_data);
    }
    GELOGE(FAILED, "[GraphManager] PushSummaryData2ME failed, not found summary callback.");
    return FAILED;
  }
  return itr->second(graph_id, summary_data);
}

Status GraphManager::PushSaveData2ME(const GraphId &graph_id, const std::map<std::string, ge::Tensor> &save_data) {
  std::lock_guard<std::mutex> lock(member_mutex_);
  GELOGI("[GraphManager] PushSaveData2ME, dataSize=%zu.", save_data.size());
  auto itr = me_callback_map_.find(kSave);
  if (itr == me_callback_map_.end()) {
    auto iter = callback_map_.find(kSave);
    if (iter != callback_map_.end()) {
      std::map<AscendString, ge::Tensor> tmp_save_data;
      for (auto &data : save_data) {
        AscendString tmp(data.first.c_str());
        tmp_save_data[tmp] = data.second;
      }
      return iter->second(graph_id, tmp_save_data);
    }
    GELOGE(FAILED, "[GraphManager] PushSaveData2ME failed, not found checkpoint callback.");
    return FAILED;
  }
  return itr->second(graph_id, save_data);
}

bool GraphManager::CheckNetOutputForCheckpointGraph(NodePtr &node) {
  size_t in_data_anchor_size = node->GetAllInDataAnchors().size();
  for (size_t i = 0; i < in_data_anchor_size; ++i) {
    auto in = node->GetInDataAnchor(i);
    if (in == nullptr) {
      return false;
    }
    auto peerin = in->GetPeerOutAnchor();
    GE_IF_BOOL_EXEC(peerin == nullptr, return false);
    if (peerin->GetOwnerNode()->GetType() != kVariable && (!TransOpUtil::IsTransOp(peerin->GetOwnerNode()))) {
      return false;
    }
  }
  return true;
}

bool GraphManager::CheckVariableForCheckpointGraph(NodePtr &node) {
  if (node->GetOpDesc()->HasAttr(kCheckPointForGetVar)) {
    return false;
  }
  auto out = node->GetOutDataAnchor(0);
  if (out == nullptr) {
    GELOGE(GE_GRAPH_PARAM_NULLPTR, "out is nullptr.");
    return false;
  }
  auto peer_out = out->GetPeerInDataAnchors();
  for (size_t i = 0; i < peer_out.size(); ++i) {
    if (peer_out.at(i)->GetOwnerNode()->GetType() != kNetOutput &&
        (!TransOpUtil::IsTransOp(peer_out.at(i)->GetOwnerNode()))) {
      return false;
    }
  }
  return true;
}

bool GraphManager::CheckTransOpForCheckpointGraph(NodePtr &node) {
  for (const auto &out_node : node->GetOutAllNodes()) {
    if ((!TransOpUtil::IsTransOp(out_node)) && (out_node->GetType() != kNetOutput) && (out_node->GetType() != kSend)) {
      return false;
    }
  }

  for (const auto &in_node : node->GetInAllNodes()) {
    if ((!TransOpUtil::IsTransOp(in_node)) && (in_node->GetType() != kVariable) && (in_node->GetType() != kRecv)) {
      return false;
    }
  }
  return true;
}

static inline bool CheckConstanOpForCheckpointGraph(NodePtr &node) { return node->GetOutDataNodes().empty(); }

bool GraphManager::IsCheckpointGraph(ComputeGraphPtr &compute_graph) {
  if (compute_graph == nullptr) {
    GELOGE(GE_GRAPH_PARAM_NULLPTR, "[IsCheckpointGraph] computeGraph is nullptr.");
    return false;
  }
  for (auto &node : compute_graph->GetAllNodes()) {
    OpDescPtr op = node->GetOpDesc();
    GE_RT_FALSE_CHECK_NOTNULL(op);
    if (op->GetType() == kNetOutput) {
      if (!CheckNetOutputForCheckpointGraph(node)) {
        return false;
      }
    } else if (op->GetType() == kVariable) {
      if (!CheckVariableForCheckpointGraph(node)) {
        return false;
      }
    } else if ((TransOpUtil::IsTransOp(node))) {
      if (!CheckTransOpForCheckpointGraph(node)) {
        return false;
      }
    } else if (op->GetType() == CONSTANTOP) {
      if (!CheckConstanOpForCheckpointGraph(node)) {
        return false;
      }
    } else if (op->GetType() != kSend && op->GetType() != kRecv) {
      GELOGI("this node is not allow in checkpoint sub graph, node_type: %s, node_name: %s.", op->GetType().c_str(),
             op->GetName().c_str());
      return false;
    }
  }
  GELOGI("current graph %s is checkpoint sub graph.", compute_graph->GetName().c_str());
  return true;
}

bool GraphManager::IsBroadCastOpData(const ge::NodePtr &var_node) {
  for (auto &out_anchor : var_node->GetAllOutDataAnchors()) {
    GE_RT_FALSE_CHECK_NOTNULL(out_anchor);
    for (auto &in_anchor : out_anchor->GetPeerInDataAnchors()) {
      GE_RT_FALSE_CHECK_NOTNULL(in_anchor);
      ge::NodePtr dst_node = in_anchor->GetOwnerNode();
      GE_RT_FALSE_CHECK_NOTNULL(dst_node);
      if (dst_node->GetType() == HCOMBROADCAST || dst_node->GetType() == HVDCALLBACKBROADCAST) {
        return true;
      }
    }
  }
  return false;
}

void GraphManager::SetAttrForHcomBroadCastOp(ge::ComputeGraphPtr &compute_graph) {
  // add variable attr for hccl broadcast,need to be removed after variable pass online
  for (const ge::NodePtr &node : compute_graph->GetDirectNode()) {
    if (node->GetOpDesc()->GetType() != ge::VARIABLE) {
      continue;
    }
    if (IsBroadCastOpData(node)) {
      AdjustBroadCastOpData(node);
    }
    if (IsAssignOpData(node)) {
      AdjustAssignOpData(node);
    }
  }
}

void GraphManager::AdjustBroadCastOpData(const ge::NodePtr &var_node) {
  if (!ge::AttrUtils::SetStr(var_node->GetOpDesc(), VAR_ATTR_VAR_IS_BROADCAST, "var_is_restore")) {
    GELOGW("set var_is_restore failed");
  }
}

bool GraphManager::IsAssignOpData(const ge::NodePtr &var_node) {
  GELOGD("IsAssignOpData var_node %s", var_node->GetName().c_str());
  std::map<std::string, std::set<int>> assign_ops = {{ASSIGN, {0}}};

  ge::NodePtr assign_node = nullptr;
  if (ConfirmUseOpAndIndexByNode(var_node, assign_ops, assign_node)) {
    return true;
  }

  return false;
}

void GraphManager::AdjustAssignOpData(const ge::NodePtr &var_node) {
  if (!ge::AttrUtils::SetStr(var_node->GetOpDesc(), VAR_ATTR_VAR_IS_RESTORE, "var_is_restore")) {
    GELOGW("SetStr var_is_restore failed");
  }
}

bool GraphManager::ConfirmUseOpAndIndexByAnchor(const ge::InDataAnchorPtr &in_anchor,
                                                const map<string, std::set<int>> &confirm_ops, ge::NodePtr &use_node) {
  GE_RT_FALSE_CHECK_NOTNULL(in_anchor);
  ge::NodePtr dst_node = in_anchor->GetOwnerNode();
  GE_RT_FALSE_CHECK_NOTNULL(dst_node);
  ge::OpDescPtr dst_op_desc = dst_node->GetOpDesc();
  GE_RT_FALSE_CHECK_NOTNULL(dst_op_desc);
  const string &dst_type = dst_op_desc->GetType();
  int input_index = in_anchor->GetIdx();

  GELOGD("ConfirmUseOpAndIndex, var name %s, dst_type = %s, input index %d", dst_node->GetName().c_str(),
         dst_type.c_str(), input_index);

  if (confirm_ops.count(dst_type) > 0) {
    if (confirm_ops.at(dst_type).count(input_index) > 0) {
      use_node = dst_node;
      return true;
    }
  }
  return false;
}

bool GraphManager::ConfirmUseOpAndIndexByNode(const ge::NodePtr &var_node,
                                              const map<string, std::set<int>> &confirm_ops, ge::NodePtr &use_node) {
  GE_RT_FALSE_CHECK_NOTNULL(var_node);
  for (auto &out_anchor : var_node->GetAllOutDataAnchors()) {
    GE_RT_FALSE_CHECK_NOTNULL(out_anchor);
    for (auto &in_anchor : out_anchor->GetPeerInDataAnchors()) {
      GE_RT_FALSE_CHECK_NOTNULL(in_anchor);
      if (ConfirmUseOpAndIndexByAnchor(in_anchor, confirm_ops, use_node)) {
        return true;
      }
    }
  }
  return false;
}

Status GraphManager::RemoveIsolatedConstInThisGraph(ge::ComputeGraphPtr &compute_graph) {
  for (ge::NodePtr &n : compute_graph->GetDirectNode()) {
    if (n->GetOpDesc() == nullptr) {
      continue;
    }
    if (n->GetOpDesc()->GetType() == CONSTANT || n->GetOpDesc()->GetType() == CONSTANTOP) {
      // reset const type depend on train_flag
      options_.train_graph_flag ? n->GetOpDesc()->SetType(CONSTANTOP) : n->GetOpDesc()->SetType(CONSTANT);
      if (n->GetOutAllNodes().empty() && n->GetInAllNodes().empty()) {
        // it is an isolated constant, just remove it
        if (GraphUtils::RemoveJustNode(compute_graph, n) != GRAPH_SUCCESS) {
          GELOGE(FAILED, "remove constant %s failed.", n->GetName().c_str());
          return FAILED;
        }
      }
    }
  }
  return SUCCESS;
}

Status GraphManager::RemoveIsolatedConst(ge::ComputeGraphPtr &compute_graph) {
  GE_CHK_STATUS_RET(RemoveIsolatedConstInThisGraph(compute_graph));
  for (auto &sub_graph : compute_graph->GetAllSubgraphs()) {
    GE_CHK_STATUS_RET(RemoveIsolatedConstInThisGraph(sub_graph));
  }
  return SUCCESS;
}

Status GraphManager::OptimizeStage1(ge::ComputeGraphPtr &compute_graph) {
  string options = "default";
  if (GetContext().GetOption("ge.exec.variable_acc", options) != SUCCESS) {
    GELOGI("get ge.exec.variable_acc failed. set default value.");
  }
  PassManager after_merge_passes;
  GE_CHK_STATUS_RET(
      after_merge_passes.AddPass("OptimizeStage1_1::MergeInputMemcpyPass", new (std::nothrow) MergeInputMemcpyPass));
  GE_CHK_STATUS_RET(
      after_merge_passes.AddPass("OptimizeStage1_1::SwitchDataEdgesBypass", new (std::nothrow) SwitchDataEdgesBypass));
  GE_CHK_STATUS_RET(
      after_merge_passes.AddPass("OptimizeStage1_1::ConstantFuseSamePass", new (std::nothrow) ConstantFuseSamePass));
  /*
   * Do CSE before FuseDataNodesWithCommonInputPass to resolve the scene in bertlarge as following:
   *            const
   *    /        |        \
   * cast1      cast2     cast3
   *    \         |         /
   *             case
   * the node `const` is the fused const node after ConstantFuseSamePass
   * the nodes `cast1`, `cast2` and 'cast3' will be fused by CSE.
   * in order to eliminate hard code in FuseDataNodesWithCommonInputPass,
   * we do CSE before FuseDataNodesWithCommonInputPass
   * But it is a temp solution, this CSE will be deleted after change pass from graph pass to node pass
   */
  GE_CHK_STATUS_RET(after_merge_passes.AddPass("OptimizeStage1_1::CSEBeforeFuseDataNodesWithCommonInputPass",
                                               new (std::nothrow) CommonSubexpressionEliminationPass));
  // FuseDataNodesWithCommonInputPass: fuse same data with common input in same graph
  GE_CHK_STATUS_RET(after_merge_passes.AddPass("OptimizeStage1_1::FuseDataNodesWithCommonInputPass",
                                               new (std::nothrow) FuseDataNodesWithCommonInputPass));
  GE_CHK_STATUS_RET(after_merge_passes.AddPass("OptimizeStage1_1::CommonSubexpressionEliminationPass",
                                               new (std::nothrow) CommonSubexpressionEliminationPass));
  GE_CHK_STATUS_RET(after_merge_passes.AddPass("OptimizeStage1_1::PermutePass", new (std::nothrow) PermutePass))
  /*
   * The SameTransdataBreadthFusionPass should be called before VariableOpPass, because of the scene following:
   *   node3
   *    |
   * transdata1   node2
   *    |         |
   *   cast1  transdata2
   *      \    /
   *        var
   * the node `transdata1` should be moved to the front of the ndoe `cast1`,
   * to ensure that `transdata1` and `transdata2` can be fusion with `var`.
   * But it is a temp solution, because the `SameTransdataBreadthFusionPass`
   * can only move `TransData` but not `Cast` nodes.
   * So if we exchange Cast and TransData, the fusion mechanism will fail.
   */
  GE_CHK_STATUS_RET(after_merge_passes.AddPass("OptimizeStage1_1::SameTransdataBreadthFusionPass",
                                               new (std::nothrow) SameTransdataBreadthFusionPass))
  GE_IF_BOOL_EXEC(options == "default" || options == "1", GELOGI("turn on variable accelerator");
                  GE_CHK_STATUS_RET(after_merge_passes.AddPass("OptimizeStage1_1::VariableOpPass",
                                                               new (std::nothrow) VariableOpPass(&var_acc_ctrl_))))
  GE_CHK_STATUS_RET(after_merge_passes.AddPass("OptimizeStage1_1::TransOpWithoutReshapeFusionPass",
                                               new (std::nothrow) TransOpWithoutReshapeFusionPass))
  GE_CHK_STATUS_RET(after_merge_passes.AddPass("OptimizeStage1_1::TransOpBreadthFusionPass",
                                               new (std::nothrow) TransOpBreadthFusionPass))

  GE_TIMESTAMP_START(after_merge_passes);
  auto ret = after_merge_passes.Run(compute_graph);
  GE_TIMESTAMP_END(after_merge_passes, "GraphManager::OptimizeStage1_1");
  if (ret != SUCCESS && ret != NOT_CHANGED) {
    GELOGE(ret, "Run passes when OptimizeStage1_1 failed, ret:%u.", ret);
    return ret;
  }

  GE_DUMP(compute_graph, "OptimizeStage1_1");

  NamesToPass names_to_passes;
  TransOpNearbyAllreduceFusionPass trans_op_nearby_allreduce_fusion_pass;
  ReshapeRemovePass reshape_remove_pass;
  ConstantFoldingPass constant_folding_pass;
  DimensionAdjustPass dimension_adjust_pass;
  EnterPass enter_pass;
  AddNPass addn_pass;
  SwitchDeadBranchElimination switch_dead_branch_elimination;
  SwitchLogicRemovePass switch_logic_remove_pass;
  MergePass merge_pass;
  CastRemovePass cast_remove_pass;
  TransposeTransDataPass transpose_transdata_pass;
  TransOpSymmetryEliminationPass symmetry_elimination_pass;
  DimensionComputePass dimension_compute_pass;
  UselessControlOutRemovePass useless_control_out_remove_pass;
  names_to_passes.emplace_back("EnterPass", &enter_pass);
  names_to_passes.emplace_back("AddNPass", &addn_pass);
  names_to_passes.emplace_back("SwitchDeadBranchElimination", &switch_dead_branch_elimination);
  names_to_passes.emplace_back("SwitchLogicRemovePass", &switch_logic_remove_pass);
  names_to_passes.emplace_back("MergePass", &merge_pass);
  names_to_passes.emplace_back("CastRemovePass", &cast_remove_pass);
  names_to_passes.emplace_back("TransposeTransDataPass", &transpose_transdata_pass);
  names_to_passes.emplace_back("ReshapeRemovePass", &reshape_remove_pass);
  names_to_passes.emplace_back("TransOpSymmetryEliminationPass", &symmetry_elimination_pass);
  names_to_passes.emplace_back("TransOpNearbyAllreduceFusionPass", &trans_op_nearby_allreduce_fusion_pass);
  names_to_passes.emplace_back("DimensionComputePass", &dimension_compute_pass);
  names_to_passes.emplace_back("ConstantFoldingPass", &constant_folding_pass);
  names_to_passes.emplace_back("DimensionAdjustPass", &dimension_adjust_pass);
  names_to_passes.emplace_back("UselessControlOutRemovePass", &useless_control_out_remove_pass);
  GE_TIMESTAMP_START(names_to_passes);
  ret = GEPass(compute_graph).Run(names_to_passes);
  GE_TIMESTAMP_END(names_to_passes, "GraphManager::OptimizeStage1_2");
  if (ret != SUCCESS) {
    GELOGE(ret, "Run passes when OptimizeStage1_2 failed, ret:%u.", ret);
    return ret;
  }
  // Calculate Op/Fe constantfolding cost
  uint64_t op_constant_folding_cost = 0;
  for (auto &it : constant_folding_pass.GetOpConstantFoldingPerfStatistic()) {
    op_constant_folding_cost += it.second.second;
    GELOGI("The time cost of %s constant folding is [%lu] micro second, calls is %lu.",
           it.first.c_str(), it.second.second, it.second.first);
  }
  GEEVENT("[GEPERFTRACE] The time cost of extern constant folding is [%lu] micro second.", op_constant_folding_cost);
  for (auto &it : constant_folding_pass.GetGeConstantFoldingPerfStatistic()) {
    op_constant_folding_cost += it.second.second;
    GELOGI("The time cost of %s constant folding is [%lu] micro second, calls is %lu.",
           it.first.c_str(), it.second.second, it.second.first);
  }

  GE_DUMP(compute_graph, "OptimizeStage1_2");
  PassManager graph_pass;
  // the prune pass should between SwitchPass and SwitchToStreamSwitchPass
  GE_CHK_STATUS_RET(graph_pass.AddPass("OptimizeStage1_3::Migration", new (std::nothrow) SubgraphConstMigrationPass));
  GE_CHK_STATUS_RET(graph_pass.AddPass("OptimizeStage1_3::ArgsClean", new (std::nothrow) UnusedArgsCleanPass));
  GE_CHK_STATUS_RET(graph_pass.AddPass("OptimizeStage1_3::PrunePass", new (std::nothrow) PrunePass))
  GE_CHK_STATUS_RET(graph_pass.AddPass("OptimizeStage1_3::NextIterationPass", new (std::nothrow) NextIterationPass))
  GE_CHK_STATUS_RET(graph_pass.AddPass("OptimizeStage1_3::ControlTriggerPass", new (std::nothrow) ControlTriggerPass))
  GE_CHK_STATUS_RET(
      graph_pass.AddPass("OptimizeStage1_3::MergeToStreamMergePass", new (std::nothrow) MergeToStreamMergePass))
  GE_CHK_STATUS_RET(
      graph_pass.AddPass("OptimizeStage1_3::SwitchToStreamSwitchPass", new (std::nothrow) SwitchToStreamSwitchPass))
  GE_CHK_STATUS_RET(
      graph_pass.AddPass("OptimizeStage1_3::AttachStreamLabelPass", new (std::nothrow) AttachStreamLabelPass))
  GE_CHK_STATUS_RET(graph_pass.AddPass("OptimizeStage1_3::MultiBatchPass", new (std::nothrow) MultiBatchPass(true)))
  GE_CHK_STATUS_RET(graph_pass.AddPass("OptimizeStage1_3::IteratorOpPass", new (std::nothrow) IteratorOpPass))
  GE_CHK_STATUS_RET(graph_pass.AddPass("OptimizeStage1_3::VariableRefUselessControlOutDeletePass",
                                       new (std::nothrow) VariableRefUselessControlOutDeletePass))
  GE_CHK_STATUS_RET(graph_pass.AddPass("OptimizeStage1_3::ReshapeRecoveryPass", new (std::nothrow) ReshapeRecoveryPass))
  GE_CHK_STATUS_RET(
      graph_pass.AddPass("OptimizeStage1_3::RemoveSameConstPass", new (std::nothrow) RemoveSameConstPass))
  if (options_.train_graph_flag) {
    // Priority: The GlobalStepInsertPass should work before graph partitioner.
    // Reason: Make sure that the var "global_step" can be partitioned to known sub graph and allocated memory
    GE_CHK_STATUS_RET(
        graph_pass.AddPass("OptimizeStage1_3::GlobalStepInsertPass", new (std::nothrow) GlobalStepInsertPass))
  }
  GE_TIMESTAMP_START(graph_pass);
  ret = graph_pass.Run(compute_graph);
  GE_TIMESTAMP_END(graph_pass, "GraphManager::OptimizeStage1_3");
  if (ret != SUCCESS && ret != NOT_CHANGED) {
    GELOGE(ret, "Run passes when OptimizeStage1_3 failed, ret:%u.", ret);
    return ret;
  }
  NamesToPass node_pass;
  GE_TIMESTAMP_START(node_pass);
  IdentityPass identity_force_pass(false);  // after SwitchToStreamSwitchPass
  node_pass.emplace_back("IdentityPass", &identity_force_pass);
  ret = GEPass(compute_graph).Run(node_pass);
  GE_TIMESTAMP_END(node_pass, "GraphPrepare::node_pass");
  if (ret != SUCCESS) {
    GELOGE(ret, "Run identity remove pass for preprocess failed, ret:%u.", ret);
    return ret;
  }
  return SUCCESS;
}

Status GraphManager::OptimizeStage2(ge::ComputeGraphPtr &compute_graph) {
  GELOGD("Start optimize after merge sub graph.");

  PassManager after_merge_passes;
  GE_CHK_STATUS_RET(after_merge_passes.AddPass("OptimizeStage2::AfterMergePasses::LinkGenMaskNodesPass",
                                               new (std::nothrow)
                                                   LinkGenMaskNodesPass(options_.stream_max_parallel_num)));
  GE_CHK_STATUS_RET(
    after_merge_passes.AddPass("OptimizeStage2::HcclContinuousMemcpyPass",
                                 new (std::nothrow) HcclContinuousMemcpyPass));

  GE_TIMESTAMP_START(after_merge_passes);
  auto ret = after_merge_passes.Run(compute_graph);
  GE_TIMESTAMP_END(after_merge_passes, "OptimizeStage2::AfterMergePasses");
  if (ret != SUCCESS && ret != NOT_CHANGED) {
    GELOGE(ret, "Run passes after merge sub graph failed, ret:%d.", ret);
    return ret;
  }
  SetAttrForHcomBroadCastOp(compute_graph);

  NamesToPass names_to_passes;
  ConstantFoldingPass constant_folding_pass;
  ReshapeRemovePass reshape_remove_pass;
  CondRemovePass condition_remove_pass;
  BitcastPass bitcast_pass;
  AssignRemovePass assign_remove_pass;
  InplaceSupportCheckPass inplace_support_check_pass;
  names_to_passes.emplace_back("ConstantFoldingPass", &constant_folding_pass);
  names_to_passes.emplace_back("ReshapeRemovePass", &reshape_remove_pass);
  names_to_passes.emplace_back("CondRemovePass", &condition_remove_pass);
  names_to_passes.emplace_back("BitcastPass", &bitcast_pass);
  if (GetContext().GetHostExecFlag()) {
    names_to_passes.emplace_back("AssignRemovePass", &assign_remove_pass);
    names_to_passes.emplace_back("InplaceSupportCheckPass", &inplace_support_check_pass);
  }
  GE_TIMESTAMP_START(names_to_passes);
  ret = GEPass(compute_graph).Run(names_to_passes);
  GE_TIMESTAMP_END(names_to_passes, "OptimizeStage2::MergedGraphNameToPasses");
  if (ret != SUCCESS) {
    GELOGE(ret, "Run ge_passes optimize for OptimizeAfterMergeSubGraph failed, ret:%d.", ret);
    return ret;
  }

  ret = RemoveIsolatedConst(compute_graph);
  if (ret != SUCCESS) {
    GELOGE(ret, "Remove isolated Constant failed, ret:%d.", ret);
    return ret;
  }

  PassManager pass_for_control_attr_optimize;
  if (options_.train_graph_flag) {
    GE_CHK_STATUS_RET(pass_for_control_attr_optimize.AddPass("OptimizeStage2::ControlAttrOptimize::FlowCtrlPass",
                                                             new (std::nothrow) FlowCtrlPass))
  }

  GE_CHK_STATUS_RET(pass_for_control_attr_optimize.AddPass("OptimizeStage2::ControlAttrOptimize::MultiBatchPass",
                                                           new (std::nothrow) MultiBatchPass))
  GE_CHK_STATUS_RET(pass_for_control_attr_optimize.AddPass("OptimizeStage2::AfterMergePasses::RefIdentityDeleteOpPass",
                                                           new (std::nothrow) RefIdentityDeleteOpPass))
  // the value of the attr is the original variable name the ref-variable ref from.
  // The attr will be used when allocating memory,
  // the node marked attr will be output to a variable instead of new-allocated memory.
  // Therefore, ComputeGraph should not delete nodes after `VariableRefDeleteOpPass`
  // to prevent unexpected deletion of nodes marked with attr
  GE_CHK_STATUS_RET(pass_for_control_attr_optimize.AddPass("OptimizeStage2::AfterMergePasses::VariableRefDeleteOpPass",
                                                           new (std::nothrow) VariableRefDeleteOpPass))
  GE_CHK_STATUS_RET(pass_for_control_attr_optimize.AddPass("OptimizeStage2::ControlAttrOptimize::CompileNodesPass",
                                                           new (std::nothrow) CompileNodesPass))
  GE_CHK_STATUS_RET(pass_for_control_attr_optimize.AddPass(
      "OptimizeStage2::AfterMergePasses::MarkGraphUnknownStatusPass", new(std::nothrow) MarkGraphUnknownStatusPass))
  GE_CHK_STATUS_RET(
          pass_for_control_attr_optimize.AddPass("OptimizeStage2::AfterMergePasses::InputOutputConnectionIdentifyPass",
                                                 new (std::nothrow) InputOutputConnectionIdentifyPass))
  // When the input node to be cleared is after a `Data` node, the atomic-clean-node should not be inserted.
  // So The ComputeGraph should not delete nodes after `AtomicAddrCleanPass`
  // to prevent unexpected deletion of nodes after a `Data` node
  GE_CHK_STATUS_RET(pass_for_control_attr_optimize.AddPass("OptimizeStage2::AfterMergePasses::AtomicAddrCleanPass",
                                                           new (std::nothrow) AtomicAddrCleanPass))
  GE_CHK_STATUS_RET(pass_for_control_attr_optimize.AddPass("OptimizeStage2::AfterMergePasses::"
                                                           "EndOfSequenceAddControlPass",
                                                           new (std::nothrow) EndOfSequenceAddControlPass))
  // 'SubgraphPass' solves memory_assign_conflicts by insert MemcpyAsync node, which depends on multi attrs and
  // graph-structure. Passes after 'SubgraphPass' MUST NOT remove MemcpyAsync/Identity nodes in subgraphs.
  GE_CHK_STATUS_RET(pass_for_control_attr_optimize.AddPass("OptimizeStage2::ControlAttrOptimize::SubgraphPass",
                                                           new (std::nothrow) SubgraphPass))
  // 'AttachStreamLabelPass' modifies attr without changing structure of compute_graph
  // All passes after 'AttachStreamLabelPass' MUST mark stream_label on new nodes by self.
  GE_CHK_STATUS_RET(pass_for_control_attr_optimize.AddPass("OptimizeStage2::ControlAttrOptimize::AttachStreamLabelPass",
                                                           new (std::nothrow) AttachStreamLabelPass))

  GE_TIMESTAMP_START(pass_for_control_attr_optimize);
  ret = pass_for_control_attr_optimize.Run(compute_graph);
  GE_TIMESTAMP_END(pass_for_control_attr_optimize, "OptimizeStage2::ControlAttrOptimize");
  if (ret != SUCCESS && ret != NOT_CHANGED) {
    GELOGE(ret, "Run passes when optimize stage 2 failed");
    return ret;
  }

  // Assign functional op labels.
  GE_TIMESTAMP_START(AssignFunctionalLabels);
  LabelAllocator label_allocator(compute_graph);
  GE_CHK_STATUS_RET(label_allocator.AssignFunctionalLabels(), "Assign label failed.");
  GE_TIMESTAMP_END(AssignFunctionalLabels, "ModelBuilder::AssignFunctionalLabels");

  // Add memcpy addr asynchronous node.
  GE_TIMESTAMP_START(AddMemcpyAddrAsyncNode);
  MemcpyAddrAsyncPass memcpy_addr;
  GE_CHK_STATUS_RET(memcpy_addr.Run(compute_graph), "Add memcpy_addr_async node failed.");
  GE_TIMESTAMP_END(AddMemcpyAddrAsyncNode, "MemcpyAddrAsyncPass::Run.");

  // After while sub graph handle, mark all node rw type
  auto result = GetCompilerStages(compute_graph->GetGraphID()).optimizer.HandleMemoryRWConflict(compute_graph);
  if (result != SUCCESS) {
    GELOGW(
        "Mark node rw type failed. It will take some effect on memory_assign_conflicts handling."
        "Please pay attention to it.");
  }

  ChangeConstTypeWhenTraining(compute_graph);

  GELOGI("End optimize after merge sub graph.");
  return SUCCESS;
}
void GraphManager::ChangeConstTypeWhenTraining(const ComputeGraphPtr &compute_graph) {
  // The constant for train is CONSTANTOP, and is CONSTANT for inference. They will be unified in future.
  if (options_.train_graph_flag) {
    for (NodePtr &n : compute_graph->GetAllNodes()) {
      // This can ensure that n is not a null pointer
      if (n->GetOpDesc()->GetType() == CONSTANT) {
        n->GetOpDesc()->SetType(CONSTANTOP);
      }
    }
  }
}

Status GraphManager::LoadGraphAsync(const GeRootModelPtr &ge_root_model, const GraphNodePtr &graph_node) {
  GELOGI("[LoadGraphAsync] run_graph_flag[%d], graph_id[%u]", options_.run_graph_flag, graph_node->GetGraphId());
  if (options_.run_graph_flag && ge_root_model != nullptr) {
    // synchronization run graph with model
    ModelIdInfo model_id_info;
    bool is_unknown_shape = false;
    GE_CHK_STATUS_RET(ge_root_model->CheckIsUnknownShape(is_unknown_shape));
    if (!is_unknown_shape) {
      if (getenv(kEnvGeuseStaticMemory) != nullptr) {
        GELOGI("[LoadGraphAsync] GE_USE_STATIC_MEMORY is seted.");
      } else {
        auto root_graph = ge_root_model->GetRootGraph();
        GE_CHECK_NOTNULL(root_graph);
        auto name_to_model = ge_root_model->GetSubgraphInstanceNameToModel();
        GeModelPtr ge_model = name_to_model[root_graph->GetName()];
        GE_CHK_STATUS_RET(CheckAndReleaseMemory(ge_model, graph_node));
      }
    }
    GE_TIMESTAMP_START(LoadGraph);
    GE_CHECK_NOTNULL(graph_node->graph_run_async_listener_);
    Status ret =
        GraphLoader::LoadModelOnline(model_id_info.model_id, ge_root_model, graph_node->graph_run_async_listener_);
    GE_TIMESTAMP_EVENT_END(LoadGraph, "GraphManager::LoadGraphAsync");
    if (ret != SUCCESS) {
      GELOGE(ret, "[LoadGraphAsync] LoadGraphAsync Failed");
      graph_node->SetRunFlag(false);
      return ret;
    }
    graph_node->SetLoadFlag(true);
    ge_root_model->SetModelId(model_id_info.model_id);
    graph_node->SetGeRootModel(ge_root_model);
  }
  return SUCCESS;
}

Status GraphManager::CheckAndReleaseMemory(const GeModelPtr &ge_model, const GraphNodePtr &graph_node) {
  GELOGI("CheckAndReleaseMemory graph_id[%u]", graph_node->GetGraphId());
  int64_t value = 0;
  bool ret = ge::AttrUtils::GetInt(ge_model, ATTR_MODEL_MEMORY_SIZE, value);
  int64_t memory_size = ret ? value : 0;
  ret = ge::AttrUtils::GetInt(ge_model, ATTR_MODEL_WEIGHT_SIZE, value);
  int64_t weight_size = ret ? value : 0;
  ret = ge::AttrUtils::GetInt(ge_model, MODEL_ATTR_SESSION_ID, value);
  uint64_t session_id = ret ? value : 0;

  int64_t free_memory = 0;
  Status result = GraphLoader::GetMemoryInfo(free_memory);
  if (result != SUCCESS) {
    return result;
  }

  GELOGI(
      "CheckAndReleaseMemory Graph[%u] need memory_size[%ld], weight_size[%ld],"
      " Device[%u] free_memory_size[%ld]",
      graph_node->GetGraphId(), memory_size, weight_size, GetContext().DeviceId(), free_memory);
  if (ge::CheckInt64AddOverflow(memory_size, weight_size) != SUCCESS) {
    GELOGE(INTERNAL_ERROR, "The sum of Memory size and weight size exceeds INT64_MAX");
    return INTERNAL_ERROR;
  }
  if (free_memory >= (memory_size + weight_size)) {
    return SUCCESS;
  }

  std::lock_guard<std::mutex> lock(unload_model_mutex_);

  std::map<GraphId, GraphNodePtr> graph_map;
  {
    std::lock_guard<std::mutex> lock(member_mutex_);
    graph_map = graph_map_;
  }

  for (auto &it : graph_map) {
    auto graph_id = it.second->GetGraphId();
    auto model = it.second->GetGeRootModel();
    if (model == nullptr) {
      continue;
    }
    auto model_id = model->GetModelId();
    // unload model not release
    bool is_unknown_shape = false;
    GE_CHK_STATUS_RET(model->CheckIsUnknownShape(is_unknown_shape));
    if (is_unknown_shape) {
      GELOGD("model_id[%u] graph_id[%u] is unknown model, not release memory", model_id, graph_id);
      continue;
    }
    // not loaded,no need unload
    if (!it.second->GetLoadFlag()) {
      GELOGI("CheckAndReleaseMemory graph[%u] has not been loaded.", graph_id);
      continue;
    }
    uint64_t max_memory_size = 0;
    result = GraphLoader::GetMaxUsedMemory(model_id, max_memory_size);
    if (result != SUCCESS) {
      continue;
    }
    GELOGI("CheckAndReleaseMemory try to UnloadGraph[%u], model[%u] which MaxUsedMemory[%lu].", graph_id, model_id,
           max_memory_size);
    rtError_t rt_ret = rtSetDevice(GetContext().DeviceId());
    if (rt_ret != RT_ERROR_NONE) {
      GELOGE(RT_FAILED, "[GraphManager:] rtSetDevice failed, modelId=%u, graphId=%u.", model_id, graph_id);
      continue;
    }
    result = GraphLoader::DestroyAicpuKernel(session_id, model_id, 0);
    if (result != SUCCESS) {
      GELOGW("[GraphManager:] destroy aicpu kernel failed when dynamic memory, modelId=%u, graphId=%u.", model_id,
             graph_id);
    }
    result = GraphLoader::UnloadModel(model_id);
    if (result != SUCCESS) {
      GELOGW("[GraphManager:] unload model failed, modelId=%u, graphId=%u.", model_id, graph_id);
    }
    rt_ret = rtDeviceReset(GetContext().DeviceId());
    if (rt_ret != RT_ERROR_NONE) {
      GELOGE(RT_FAILED, "[GraphManager:] rtDeviceReset failed, modelId=%u, graphId=%u.", model_id, graph_id);
      continue;
    }
    it.second->SetLoadFlag(false);
    GELOGI("CheckAndReleaseMemory UnloadGraph[%u], model[%u] success and set LoadFlag to false.", graph_id, model_id);
  }

  return SUCCESS;
}

Status GraphManager::ProcessSubGraphWithMultiThreads(GraphManager *graph_manager, GraphId root_graph_id,
                                                     const SubGraphInfoPtr &sub_graph_info_ptr,
                                                     const std::string &root_graph_name,
                                                     uint64_t session_id,
                                                     const GEThreadLocalContext &ge_context) {
  if (sub_graph_info_ptr != nullptr && graph_manager != nullptr) {
    GetContext().SetSessionId(session_id);
    GetThreadLocalContext() = ge_context;
    graph_manager->UpdateLocalOmgContext(root_graph_id);
    ComputeGraphPtr compute_graph_tmp = sub_graph_info_ptr->GetSubGraph();
    const std::string &engine_name = sub_graph_info_ptr->GetEngineName();
    GELOGD("ProcessSubGraphWithMultiThreads start, graph name is %s, engine_name is %s, thread id is %lu",
           compute_graph_tmp != nullptr ? compute_graph_tmp->GetName().c_str() : "", engine_name.c_str(),
           pthread_self());
    GE_DUMP(compute_graph_tmp, "OptimizeSubGraphBefore");
    GE_CHECK_NOTNULL(compute_graph_tmp);
    if (!AttrUtils::SetInt(*compute_graph_tmp, ATTR_NAME_ROOT_GRAPH_ID, root_graph_id)) {
      GELOGE(FAILED, "Failed to set attr ATTR_NAME_ROOT_GRAPH_ID for subgraph, graph_id: %u.", root_graph_id);
      return FAILED;
    }
    if (!AttrUtils::SetStr(*compute_graph_tmp, ATTR_NAME_ROOT_GRAPH_NAME, root_graph_name)) {
      GELOGE(FAILED, "Failed to set attr ATTR_NAME_ROOT_GRAPH_NAME for subgraph, \
             root_graph_name: %s.", root_graph_name.c_str());
      return FAILED;
    }
    compute_graph_tmp->SetSessionID(session_id);
    Status ret = graph_manager->GetCompilerStages(root_graph_id).optimizer.OptimizeSubGraph(compute_graph_tmp,
                                                                                            engine_name);
    if (ret != SUCCESS) {
      GELOGE(ret, "SubGraph optimize Failed %s", engine_name.c_str());
      return ret;
    } else {
      GELOGD("SubGraph optimize success %s", engine_name.c_str());
    }
    GE_DUMP(compute_graph_tmp, "OptimizeSubGraphAfter");
    sub_graph_info_ptr->SetSubGraph(compute_graph_tmp);
    GELOGD("ProcessSubGraphWithMultiThreads end, graph name is %s, engine_name is %s, thread id is %lu",
           compute_graph_tmp != nullptr ? compute_graph_tmp->GetName().c_str() : "", engine_name.c_str(),
           pthread_self());
  } else {
    GELOGE(FAILED, "graph_manager or sub_graph_info_ptr is nullptr");
    return FAILED;
  }

  return SUCCESS;
}

// run graph async on session
Status GraphManager::RunGraphAsync(const GraphId &graph_id, const std::vector<ge::InputTensorInfo> &inputs,
                                   uint64_t session_id, RunAsyncCallback callback) {
  GELOGI("[GraphManager] Start to run graph async, graph_id=%u, inputsSize=%zu.", graph_id, inputs.size());

  bool ret = prerun_args_q_.Push(PreRunArgs({graph_id, inputs, session_id, GetThreadLocalContext(), callback}));
  if (!ret) {
    GELOGE(FAILED, "[GraphManager] Run graph async failed, graph_id=%u.", graph_id);
    return FAILED;
  }

  GELOGI("[GraphManager] Run graph async success, graph_id=%u.", graph_id);
  return SUCCESS;
}

void GraphManager::AddModelCacheHelperToMap(const GraphId &graph_id, uint64_t session_id,
                                            ComputeGraphPtr &compute_graph) {
  std::shared_ptr<GELib> instance_ptr = ge::GELib::GetInstance();
  if (instance_ptr != nullptr && instance_ptr->IsIncreBuild()) {
    std::lock_guard<std::mutex> lock(member_mutex_);
    auto iter = cache_helper_map_.find(graph_id);
    if (iter == cache_helper_map_.end()) {
      ModelCacheHelperPtr cache_helper = MakeShared<ge::ModelCacheHelper>(session_id, graph_id, compute_graph);
      if (cache_helper != nullptr) {
        cache_helper_map_.emplace(std::make_pair(graph_id, cache_helper));
      } else {
        GELOGW("Cache helper make shared failed, graph_id = %u.", graph_id);
      }
    }
  }
}

ModelCacheHelperPtr GraphManager::FindModelCacheHelper(GraphId graph_id) {
  std::lock_guard<std::mutex> lock(member_mutex_);
  auto iter = cache_helper_map_.find(graph_id);
  if (iter != cache_helper_map_.end()) {
    return iter->second;
  }

  return nullptr;
}

Status GraphManager::IncreBuild(const GraphNodePtr &graph_node, GeModelPtr &ge_model) {
  std::shared_ptr<GELib> instance_ptr = ge::GELib::GetInstance();
  if (instance_ptr == nullptr || !instance_ptr->IsIncreBuild()) {
    return FAILED;
  }
  const uint32_t graph_id = graph_node->GetGraphId();
  ModelCacheHelperPtr cache_helper = FindModelCacheHelper(graph_id);
  if (cache_helper == nullptr) {
    GELOGW("Can not find ModelCacheHelper of graph[%u]", graph_id);
    return FAILED;
  }
  if (cache_helper->IsModelCacheHit()) {
    GEEVENT("Model cache hit.");
    Status ret = LoadFromCache(graph_node, cache_helper, ge_model);
    if (ret == SUCCESS) {
      return SUCCESS;
    } else {
      GELOGW("Error occurred when load from cache, abandon.");
    }
  } else {
    GEEVENT("Model cache miss.");
  }
  if (SaveCacheBeforeBuild(graph_node->GetGraphId(), cache_helper) != SUCCESS) {
    GELOGW("Error occurred when save cache.");
  }
  return FAILED;
}

void GraphManager::ConstructGeInput(const vector<InputTensorInfo> &inputs, vector<GeTensor> &ge_inputs) {
  for (auto const &input : inputs) {
    GeTensorDesc input_tensor_desc(GeShape(input.dims));
    input_tensor_desc.SetDataType(static_cast<ge::DataType>(input.data_type));
    ge_inputs.emplace_back(input_tensor_desc);
  }
}

void GraphManager::PreRunThread(GraphManager *graph_manager) {
  if (prctl(PR_SET_NAME, ("GE_PreRun")) != 0) {
    GELOGW("Set thread name failed.");
  }

  PreRunArgs args;
  while (graph_manager->thread_run_flag_) {
    bool pop_status = graph_manager->prerun_args_q_.Pop(args);
    if (!pop_status) {
      continue;
    }

    GELOGI("A new loop start.");

    GetContext().SetSessionId(args.session_id);
    GetThreadLocalContext() = args.context;
    graph_manager->UpdateLocalOmgContext(args.graph_id);

    // find graph
    GraphNodePtr graph_node = nullptr;
    Status ret = graph_manager->GetGraphNode(args.graph_id, graph_node);
    if (ret != SUCCESS) {
      ReturnError(graph_manager, args.callback, GE_GRAPH_ALREADY_RUNNING,
                  "[RunGraph] graph not exist, graph_id=" + std::to_string(args.graph_id));
      return;
    }

    graph_node->Lock();

    if (graph_node->GetRunFlag()) {
      ReturnError(graph_manager, args.callback, GE_GRAPH_GRAPH_NODE_NULL,
                  "[RunGraph] graph already running, graph id=" + std::to_string(args.graph_id));
      graph_node->Unlock();
      return;
    }

    // set graph's run flag
    graph_node->SetRunFlag(true);

    ComputeGraphPtr compute_graph_tmp = GraphUtils::GetComputeGraph(*(graph_node->GetGraph()));
    if (compute_graph_tmp == nullptr) {
      ReturnError(graph_manager, args.callback, GE_GRAPH_GRAPH_NODE_NULL,
                  "[RunGraph] compute_graph_tmp is NULL, graph id = %u.");
      graph_node->Unlock();
      return;
    }
    // when set incre build, save cache helper.
    graph_manager->AddModelCacheHelperToMap(args.graph_id, args.session_id, compute_graph_tmp);

    std::vector<GeModelPtr> ge_models;

    if (graph_manager->options_.local_fmk_op_flag) {
      graph_manager->GetCompilerStages(graph_node->GetGraphId()).optimizer.TranFrameOp(compute_graph_tmp);
    }

    // it will not execute graph preprocess, optimize, parition, build if the graph has built successful.
    GELOGI("Start for run graph async.");
    GeRootModelPtr ge_root_model = nullptr;
    if (graph_manager->IsGraphNeedBuild(graph_node)) {
      if (graph_node->GetBuildFlag()) {
        ReturnError(graph_manager, args.callback, PARAM_INVALID,
                    "The graph " + std::to_string(graph_node->GetGraphId()) +
                        " need to re-build, you should remove it"
                        " from GE first, then AddGraph again and rebuild it.");
        graph_node->Unlock();
        return;
      }

      // check need incre build.
      GeModelPtr ge_model = nullptr;
      if (graph_manager->IncreBuild(graph_node, ge_model) != SUCCESS) {
        std::vector<GeTensor> ge_inputs;
        ConstructGeInput(args.input_tensor, ge_inputs);
        ret = graph_manager->PreRun(graph_node, ge_inputs, ge_root_model, args.session_id);
        // release rts generate context
        RtContextUtil::GetInstance().DestroyRtContexts(args.session_id, graph_node->GetGraphId());
        if (ret != SUCCESS) {
          graph_node->SetRunFlag(false);
          if (!ge::Analyzer::GetInstance()->IsEnableNetAnalyzeDebug()) {
            ReturnError(graph_manager, args.callback, ret, "PreRun Failed, thread exit..");
            graph_node->Unlock();
            return;
          } else {
            ReturnError(graph_manager, graph_node, args.callback, ret, "PreRun Failed, keep geop continue!");
            graph_node->Unlock();
            continue;
          }
        }
      }
      graph_node->SetBuildFlag(true);
      graph_manager->var_acc_ctrl_.SetGraphBuildEnd(graph_node->GetGraphId());
    } else {
      ge_root_model = graph_node->GetGeRootModel();
    }

    graph_manager->run_args_q_.Push(RunArgs( { graph_node, args.graph_id, args.session_id, args.input_tensor,
        ge_root_model, GetThreadLocalContext(), args.callback }));
    GELOGI("Loop end.");
  }
}

void GraphManager::ParseInputsDimsForData(const std::vector<InputTensorInfo> &input_tensor) {
  GELOGD("Start parse input dims from data.");
  for (size_t i = 0; i < input_tensor.size(); ++i) {
    std::vector<int64_t> dynamic_dim;
    for (size_t j = 0; j < input_tensor[i].dims.size(); ++j) {
      dynamic_dim.emplace_back(input_tensor[i].dims[j]);
    }
    GELOGD("Input tensor dims is %s.", formats::JoinToString(dynamic_dim).c_str());
    GetLocalOmgContext().user_real_input_dims.emplace_back(input_tensor[i].dims);
  }
}

Status GraphManager::ParseInputsDimsForGetNexNosinkAndData(const vector<NodePtr> &dynamic_nodes,
                                                           const std::vector<InputTensorInfo> &input_tensor) {
  GELOGD("Start parse inputs dims when coexist data and getnext sink.");
  for (size_t i = 0; i < dynamic_nodes.size(); ++i) {
    auto op_desc = dynamic_nodes.at(i)->GetOpDesc();
    if (op_desc == nullptr) {
      continue;
    }
    GeAttrValue::INT index = 0;
    if (!(AttrUtils::GetInt(op_desc, ATTR_NAME_INDEX, index))) {
      GELOGE(PARAM_INVALID, "Get index from attr failed");
      return PARAM_INVALID;
    }
    if (static_cast<size_t>(index) > input_tensor.size()) {
      GELOGE(PARAM_INVALID, "The count of input tensor should be equal to the count of data.");
      return PARAM_INVALID;
    }

    GetLocalOmgContext().user_real_input_dims.emplace_back(input_tensor.at(index).dims);
    GELOGI("Shape dims of %zu data is %s.", index, formats::JoinToString(input_tensor.at(index).dims).c_str());
  }
  return SUCCESS;
}

Status GraphManager::ParseInputsDims(const std::vector<InputTensorInfo> &input_tensor) {
  GELOGI("Start parse input dims of %zu input tensor.", input_tensor.size());
  GetLocalOmgContext().user_real_input_dims.clear();
  if (!GetLocalOmgContext().dynamic_node_type.empty()) {
    vector<NodePtr> data_nodes;
    vector<NodePtr> getnext_nosink_nodes;
    data_nodes = GetLocalOmgContext().data_nodes;
    getnext_nosink_nodes = GetLocalOmgContext().getnext_nosink_nodes;
    GELOGD("Data nodes count is %zu, getnext nosink nodes count is %zu.", data_nodes.size(),
           getnext_nosink_nodes.size());
    if (GetLocalOmgContext().dynamic_node_type == DATA) {
      if (getnext_nosink_nodes.empty()) {
        // just data or data+getnext_sink
        ParseInputsDimsForData(input_tensor);
      } else {
        // data+getnext_nosink, but only need to get shape_dims of data
        if (ParseInputsDimsForGetNexNosinkAndData(data_nodes, input_tensor) != SUCCESS) {
          GELOGE(PARAM_INVALID, "Failed to parse dims from data, when data coexist with getnext nosink.");
          return PARAM_INVALID;
        }
      }
    } else {
      if (getnext_nosink_nodes.empty()) {
        // just getnext_sink or getnext_sink+data, need to get shape_dims from aicpu op
        GELOGI("Need to get dims from aicpu op: GETDYNAMICDIMS.");
        return SUCCESS;
      } else {
        if (data_nodes.empty()) {
          // just getnext_nosink
          ParseInputsDimsForData(input_tensor);
        } else {
          // getnext_nosink + data, but only need to get shape_dims of getnext_nosink
          if (ParseInputsDimsForGetNexNosinkAndData(getnext_nosink_nodes, input_tensor) != SUCCESS) {
            GELOGE(PARAM_INVALID, "Failed to parse dims from getnext nosink, when data coexist with getnext nosink");
            return PARAM_INVALID;
          }
        }
      }
    }
  }
  GELOGI("Parse %zu inputs dims success.", GetLocalOmgContext().user_real_input_dims.size());
  return SUCCESS;
}

void GraphManager::RunThread(GraphManager *graph_manager) {
  if (prctl(PR_SET_NAME, ("GE_Run")) != 0) {
    GELOGW("Set thread name failed.");
  }

  RunArgs args;
  while (graph_manager->thread_run_flag_) {
    bool pop_status = graph_manager->run_args_q_.Pop(args);
    if (!pop_status) {
      continue;
    }

    GELOGI("A new loop start.");

    GetContext().SetSessionId(args.session_id);
    GetThreadLocalContext() = args.context;
    graph_manager->UpdateLocalOmgContext(args.graph_id);

    if (args.graph_node->graph_run_async_listener_ != nullptr) {
      args.graph_node->graph_run_async_listener_->SetCallback(args.callback);
    }
    Status ret;
    // parse inputs.dims to vector<vector<uint64_t>> dynamic_dims
    ret = graph_manager->ParseInputsDims(args.input_tensor);
    if (ret != SUCCESS) {
      ReturnError(graph_manager, args.callback, ret, "ParseInputsDims failed, thread exit.");
      args.graph_node->Unlock();
      return;
    }

    if (!args.graph_node->GetLoadFlag()) {
      ret = graph_manager->LoadGraphAsync(args.ge_root_model, args.graph_node);
      if (ret != SUCCESS || args.ge_root_model == nullptr) {
        StopQueue(graph_manager);
        ReturnError(graph_manager, args.callback, ret, "LoadGraphAsync failed, thread exit.");
        args.graph_node->Unlock();
        return;
      }
      args.graph_node->SetLoadFlag(true);
      GELOGI("LoadGraph[%u], model[%u] success and set LoadFlag to true.", args.graph_node->GetGraphId(),
             args.ge_root_model->GetModelId());
    }

    if (graph_manager->GetTrainFlag()) {
      ret = graph_manager->graph_executor_.SetGraphContext(graph_manager->GetGraphContext());
      if (ret != SUCCESS) {
        GELOGW("[GraphManager] SetGraphContext failed, graph_id=%u.", args.graph_id);
      }
      graph_manager->graph_executor_.SetTrainFlag(graph_manager->options_.train_graph_flag);
    }

    ret = graph_manager->graph_executor_.ExecuteGraphAsync(args.graph_id, args.graph_node->GetGeRootModel(),
                                                           args.input_tensor);
    args.graph_node->SetRunFlag(false);
    if (ret != SUCCESS) {
      ReturnError(graph_manager, args.callback, ret, "ExecuteGraphAsync failed, thread exit.");
      args.graph_node->Unlock();
      return;
    }
    args.graph_node->Unlock();
    GELOGI("[GraphManager] Run graph async success, graph_id=%u.", args.graph_id);
  }
}

void GraphManager::StopQueue(GraphManager *graph_manager) {
  if (graph_manager == nullptr) {
    return;
  }

  graph_manager->thread_run_flag_.store(false);
  graph_manager->prerun_args_q_.Stop();
  graph_manager->run_args_q_.Stop();
}

void GraphManager::ReturnError(GraphManager *graph_manager, RunAsyncCallback callback, Status ret, const string &log) {
  if (graph_manager == nullptr) {
    return;
  }
  StopQueue(graph_manager);
  GELOGE(ret, "%s.", log.c_str());
  std::vector<ge::OutputTensorInfo> outputs;
  callback(ret, outputs);
}

void GraphManager::ReturnError(GraphManager *graph_manager, GraphNodePtr &graph_node,
                               RunAsyncCallback callback, Status ret, const string &log) {
  std::vector<ge::OutputTensorInfo> outputs;
  auto compute_graph = GraphUtils::GetComputeGraph(*graph_node->GetGraph());
  if (graph_manager == nullptr || compute_graph == nullptr) {
    GELOGE(GRAPH_FAILED, "[Analyze Mode] compute graph is null!");
    callback(GRAPH_FAILED, outputs);
    return;
  }

  for (const auto &node : compute_graph->GetAllNodes()) {
    if (node->GetType() != "NetOutput") {
      continue;
    }
    for (size_t i = 0; i < node->GetAllInDataAnchorsSize(); i++) {
      auto input_desc = node->GetOpDesc()->MutableInputDesc(i);
      ge::OutputTensorInfo tensor;
      tensor.dims = input_desc->GetShape().GetDims();
      tensor.data_type = static_cast<uint32_t>(input_desc->GetDataType());
      int64_t len = 1;
      if (input_desc->GetShape().GetDims() != std::vector<int64_t>({})) {
        len = input_desc->GetShape().GetShapeSize();
      }
      if (len < 0) {
        GELOGE(GRAPH_FAILED, "Analyze Mode does not support GEOP output unknown shape!");
        callback(GRAPH_FAILED, outputs);
        return;
      } else if (len == 0) {
        GELOGI("getted shape size is 0.Do process as empty tensor!");
        len = 1;
      }
      auto size = GetSizeByDataType(input_desc->GetDataType());
      if (size <= 0) {
        GELOGE(PARAM_INVALID, "Failed to get cube size, the data type %s is invalid",
               ge::TypeUtils::DataTypeToSerialString(input_desc->GetDataType()).c_str());
        callback(GRAPH_FAILED, outputs);
        return;
      }
      if (CheckInt64MulOverflow(len, static_cast<int64_t>(size)) != true) {
        GELOGE(MEMALLOC_FAILED, "int64 multiply happens overflow! a:%ld b:%d", len, size);
        callback(GRAPH_FAILED, outputs);
        return;
      }
      tensor.length = len * size;
      tensor.data.reset(new(std::nothrow) uint8_t[tensor.length]);
      // To avoid global step too small and can not stop, totally set a bigger value
      for (int64_t i = 0; i < tensor.length; i++) {
        tensor.data[i] = 0x7F; // here stands for a positive max value
      }
      outputs.emplace_back(std::move(tensor));
    }
  }
  callback(SUCCESS, outputs);
  return;
}

bool GraphManager::IsGraphNeedRebuild(uint32_t graph_id) {
  // find graph
  GraphNodePtr graph_node = nullptr;
  Status ret = GetGraphNode(graph_id, graph_node);
  if (ret != SUCCESS) {
    GELOGE(ret, "[RunGraph] graph not exist, graph_id=%u.", graph_id);
    return true;
  }

  if (graph_node == nullptr) {
    GELOGE(GE_GRAPH_GRAPH_NODE_NULL, "[RunGraph] graph node is NULL, graphId=%u.", graph_id);
    return true;
  }

  return IsGraphNeedBuild(graph_node);
}

bool GraphManager::IsGraphNeedBuild(const GraphNodePtr &graph_node) {
  return !graph_node->GetBuildFlag() || var_acc_ctrl_.IsGraphNeedRebuild(graph_node->GetGraphId());
}
const map<std::string, std::string> *GraphManager::GetGraphOptions(uint32_t graph_id) {
  GraphNodePtr graph_node = nullptr;
  Status ret = GetGraphNode(graph_id, graph_node);
  if (ret != SUCCESS) {
    GELOGE(ret, "[RunGraph] graph not exist, graph_id=%u.", graph_id);
    return nullptr;
  }

  if (!graph_node) {
    GELOGE(GE_GRAPH_GRAPH_NODE_NULL, "[RunGraph] graph node is NULL, graph_id=%u.", graph_id);
    return nullptr;
  }
  return &(graph_node->GetOptions());
}

void GraphManager::SetOptionsRunGraphFlag(bool run_graph_flag) { options_.run_graph_flag = run_graph_flag; }

Status GraphManager::OptimizeSubgraph(const GraphNodePtr &graph_node, ComputeGraphPtr &compute_graph,
                                      uint64_t session_id) {
  // graph partition
  // Stage partition, only for root graph
  GE_TIMESTAMP_START(StagePartition);
  StagePartitioner stage_partitioner(compute_graph);
  auto ret = stage_partitioner.Partition();
  if (ret != SUCCESS) {
    GELOGE(ret, "Graph partition by stage Failed");
    return ret;
  }
  GE_TIMESTAMP_EVENT_END(StagePartition, "OptimizeSubgraph::StagePartition");
  // all sub graph list of root graph and sub graph
  GE_TIMESTAMP_START(GraphPartitionDynamicShape);
  DynamicShapePartitioner dynamic_shape_partitioner(compute_graph);
  ret = dynamic_shape_partitioner.Partition();
  if (ret != SUCCESS) {
    GELOGE(ret, "Graph partition by dynamic shape Failed");
    return ret;
  }
  bool dynamic_shape_partitioned = false;
  if (!AttrUtils::GetBool(*compute_graph, ATTR_NAME_DYNAMIC_SHAPE_PARTITIONED, dynamic_shape_partitioned)) {
    GELOGE(FAILED, "failed get dynamic shape partitioned flag on partitioned graph.");
    return FAILED;
  }
  GE_TIMESTAMP_EVENT_END(GraphPartitionDynamicShape, "OptimizeSubgraph::GraphPartitionDynamicShape");
  GE_DUMP(compute_graph, "AfterDynamicShapePartition");
  GE_TIMESTAMP_START(GraphPartition);
  GraphPartitioner &partitioner = GetCompilerStages(graph_node->GetGraphId()).partitioner;
  ret = partitioner.Partition(compute_graph, GraphPartitioner::kPartitioning);
  if (ret != SUCCESS) {
    GELOGE(ret, "Graph partition Failed");
    return ret;
  }
  GE_TIMESTAMP_EVENT_END(GraphPartition, "OptimizeSubgraph::Partition1");
  GE_TIMESTAMP_START(SetSubgraph);
  ret = SetSubgraph(session_id, compute_graph, partitioner);
  if (ret != SUCCESS) {
    GELOGE(ret, "Graph set subgraph Failed");
    return ret;
  }
  GE_TIMESTAMP_EVENT_END(SetSubgraph, "OptimizeSubgraph::SetSubGraph");
  if ((options_.build_mode == BUILD_MODE_TUNING) &&
      (options_.build_step == BUILD_STEP_BEFORE_UB_MATCH || options_.build_step == BUILD_STEP_AFTER_BUILDER ||
       options_.build_step == BUILD_STEP_AFTER_BUILDER_SUB)) {
    GE_TIMESTAMP_START(ConvertGraphToFile);
    std::string tuning_path;
    (void) GetContext().GetOption(TUNING_PATH, tuning_path);
    Status ret = ConvertGraphToFile(compute_graph, partitioner, tuning_path,
                                    (options_.build_step == BUILD_STEP_AFTER_BUILDER));
    if (ret != SUCCESS) {
      GELOGE(ret, "Convert graph[%s] to file failed", compute_graph->GetName().c_str());
      return ret;
    }
    GE_TIMESTAMP_EVENT_END(ConvertGraphToFile, "OptimizeSubgraph::ConvertGraphToFile");
    return SUCCESS;
  }

  ComputeGraphPtr merged_compute_graph = nullptr;
  std::vector<ComputeGraphPtr> merged_sub_graph_list;

  GE_TIMESTAMP_START(MergeSubgraph);
  ret = MergeSubGraph(merged_compute_graph, compute_graph, graph_node->GetGraphId());
  if (ret != SUCCESS) {
    GELOGE(ret, "Merge SubGraph Failed");
    return ret;
  }
  GE_CHECK_NOTNULL(merged_compute_graph);
  merged_compute_graph->SetSessionID(session_id);
  merged_compute_graph->SetGraphID(graph_node->GetGraphId());
  merged_compute_graph->SetNeedIteration(compute_graph->GetNeedIteration());
  for (auto &sub_graph : merged_compute_graph->GetAllSubgraphs()) {
    sub_graph->SetSessionID(session_id);
    sub_graph->SetGraphID(graph_node->GetGraphId());
  }
  GE_TIMESTAMP_EVENT_END(MergeSubgraph, "OptimizeSubgraph::MergeSubGraph");
  GE_DUMP(merged_compute_graph, "mergedComputeGraph");
  compute_graph = merged_compute_graph;
  if (!AttrUtils::SetBool(*compute_graph, ATTR_NAME_DYNAMIC_SHAPE_PARTITIONED, dynamic_shape_partitioned)) {
    GELOGE(FAILED, "failed set dynamic shape partitioned flag on partitioned graph.");
    return FAILED;
  }
  return SUCCESS;
}

Status GraphManager::ConvertGraphToFile(ComputeGraphPtr &compute_graph, GraphPartitioner &partitioner, std::string path,
                                        bool exe_flag) {
  GE_CHECK_NOTNULL(compute_graph);
  GELOGI("compute_graph [%s] path [%s] Enter ConvertGraphToFile.", compute_graph->GetName().c_str(), path.c_str());
  std::vector<ComputeGraphPtr> non_tuning_subgraphs;
  auto input_node_sub_graph_map = partitioner.graph_2_input_subgraph_;
  const auto &input_subgraph_info = input_node_sub_graph_map[compute_graph];
  GE_CHECK_NOTNULL(input_subgraph_info);
  ComputeGraphPtr input_graph_tmp = input_subgraph_info->GetSubGraph();
  non_tuning_subgraphs.push_back(input_graph_tmp);
  auto sub_graph_map = partitioner.GetSubGraphMap();
  const auto &subgraph_infos = sub_graph_map[compute_graph];
  std::vector<ComputeGraphPtr> tuning_subgraphs;
  for (const auto &sub_graph_info_ptr: subgraph_infos) {
    GE_CHECK_NOTNULL(sub_graph_info_ptr);
    ComputeGraphPtr sub_graph_tmp = sub_graph_info_ptr->GetSubGraph();
    // need to tuning
    if (sub_graph_info_ptr->GetEngineName() == kVectorEngine || sub_graph_info_ptr->GetEngineName() == kAIcoreEngine) {
      tuning_subgraphs.push_back(sub_graph_tmp);
    } else {
      non_tuning_subgraphs.push_back(sub_graph_tmp);
    }
  }
  return TuningUtils::ConvertGraphToFile(tuning_subgraphs, non_tuning_subgraphs, exe_flag, path);
}

Status GraphManager::Build(const GraphNodePtr &graph_node, ComputeGraphPtr &compute_graph,
                           GeRootModelPtr &ge_root_model, uint64_t session_id) {
  // build
  if (compute_graph != nullptr) {
    std::string graph_name = compute_graph->GetName();
    graph_name.append("_");
    graph_name.append(std::to_string(graph_node->GetGraphId()));
    compute_graph->SetName(graph_name);
  }

  auto ret = GetCompilerStages(graph_node->GetGraphId()).builder.Build(compute_graph, ge_root_model, session_id);
  if (ret != SUCCESS) {
    GELOGE(ret, "SubGraph build Failed.");
    return ret;
  }

  bool is_always_dump = false;
  if (!PropertiesManager::Instance().GetDumpProperties(session_id).GetDumpPath().empty()) {
    is_always_dump = true;
  }

  GraphUtils::DumpGEGraph(compute_graph, "Build", is_always_dump);
  GraphUtils::DumpGEGraphToOnnx(*compute_graph, "Build");

  graph_node->SetGeRootModel(ge_root_model);
  return SUCCESS;
}

Status GraphManager::GenCheckPointGraph(const std::map<std::string, GeTensorDesc> &all_variables, Graph &graph) {
  ge::ComputeGraphPtr compute_graph = MakeShared<ComputeGraph>(kCheckPointGraph);
  GE_CHECK_NOTNULL(compute_graph);
  OpDescPtr save_desc = MakeShared<ge::OpDesc>(compute_graph->GetName() + "_" + kSave, kSave);
  GE_CHECK_NOTNULL(save_desc);
  uint32_t save_index = 0;
  for (auto iter = all_variables.begin(); iter != all_variables.end(); ++iter) {
    GE_CHK_GRAPH_STATUS_RET(save_desc->AddInputDesc(save_index, iter->second));
    save_index++;
  }
  NodePtr save_node = compute_graph->AddNode(save_desc);

  uint32_t index = 0;
  for (auto iter = all_variables.begin(); iter != all_variables.end(); ++iter) {
    OpDescPtr var_desc = MakeShared<ge::OpDesc>(iter->first, VARIABLE);
    GE_CHECK_NOTNULL(var_desc);
    if (!AttrUtils::SetBool(var_desc, kCheckPointForGetVar, true)) {
      GELOGW("Set check point graph attr failed.");
    }
    GE_CHK_GRAPH_STATUS_RET(var_desc->AddOutputDesc(iter->second));
    NodePtr var_node = compute_graph->AddNode(var_desc);
    GE_CHK_STATUS(GraphUtils::AddEdge(var_node->GetOutDataAnchor(0), save_node->GetInDataAnchor(index)),
                  "Add edge[%s->%s] fail.", var_node->GetName().c_str(), save_node->GetName().c_str());
    index++;
  }
  compute_graph->Dump();
  graph = GraphUtils::CreateGraphFromComputeGraph(compute_graph);
  return SUCCESS;
}

Status GraphManager::SaveVariables(const Graph &graph, const std::vector<std::string> &var_names,
                                   const std::vector<Tensor> &outputs, std::vector<Tensor> &var_values) {
  map<string, Tensor> var_results;
  GE_CHK_STATUS_RET(SaveCheckPointResult(graph, outputs, var_results), "Save check point result failed.");
  if (!var_names.empty()) {
    for (const auto &var_name : var_names) {
      if (var_results.count(var_name) == 0) {
        GELOGE(FAILED, "Fetch var[%s] value failed.", var_name.c_str());
        return FAILED;
      } else {
        auto var_tensor = var_results[var_name].GetTensorDesc();
        var_tensor.SetName(var_name);
        var_results[var_name].SetTensorDesc(var_tensor);
        var_values.emplace_back(var_results[var_name]);
      }
    }
  } else {
    for (auto iter = var_results.begin(); iter != var_results.end(); ++iter) {
      string var_name = iter->first;
      auto var_tensor = iter->second.GetTensorDesc();
      var_tensor.SetName(var_name);
      iter->second.SetTensorDesc(var_tensor);
      var_values.emplace_back(iter->second);
    }
  }
  return SUCCESS;
}

Status GraphManager::SaveCheckPointResult(const Graph &graph, const std::vector<Tensor> &outputs,
                                          map<string, Tensor> &var_results) {
  auto compute_graph = GraphUtils::GetComputeGraph(graph);
  NodePtr netoutput_node = nullptr;
  for (const auto &node : compute_graph->GetAllNodes()) {
    if (node->GetType() == NETOUTPUT) {
      netoutput_node = node;
      break;
    }
  }
  GE_CHECK_NOTNULL(netoutput_node);
  for (const auto &in : netoutput_node->GetAllInDataAnchors()) {
    auto out_anchor = in->GetPeerOutAnchor();
    GE_CHECK_NOTNULL(out_anchor);
    auto peer_node = out_anchor->GetOwnerNode();
    while (peer_node->GetType() != VARIABLE) {
      if (peer_node->GetAllInDataAnchors().size() != 1) {
        GELOGE(FAILED, "peer_node [%s] has more than 1 input in checkpoint Graph.", peer_node->GetName().c_str());
        return FAILED;
      }
      auto peer_node_in_anchor = peer_node->GetAllInDataAnchors().at(0);
      auto peer_node_out_anchor = peer_node_in_anchor->GetPeerOutAnchor();
      if (peer_node_out_anchor != nullptr) {
        peer_node = peer_node_out_anchor->GetOwnerNode();
        if (peer_node->GetType() == VARIABLE) {
          break;
        }
      }
    }
    if (peer_node->GetType() != VARIABLE) {
      GELOGE(FAILED, " peer_node %s is not variable in checkpoint Graph.", peer_node->GetName().c_str());
      return FAILED;
    }
    auto var_name = peer_node->GetName();
    GELOGI("[GraphManager] SaveVariables, varName is %s.", var_name.c_str());
    if (in->GetIdx() >= static_cast<int>(outputs.size())) {
      GELOGE(FAILED, "variable index[%d] out of range[%zu].", in->GetIdx(), outputs.size());
      return FAILED;
    }
    var_results.emplace(var_name, outputs.at(in->GetIdx()));
  }
  return SUCCESS;
}

void GraphManager::AddLocalOmgContext(GraphId graph_id, const OmgContext &omg_context) {
  std::lock_guard<std::mutex> lock(member_mutex_);
  omg_contexts_.emplace(graph_id, omg_context);
  SetLocalOmgContext(omg_contexts_[graph_id]);
}

void GraphManager::UpdateLocalOmgContext(GraphId graph_id) {
  std::lock_guard<std::mutex> lock(member_mutex_);
  auto iter = omg_contexts_.find(graph_id);
  if (iter != omg_contexts_.end()) {
    SetLocalOmgContext(iter->second);
  } else {
    GELOGW("OmgContext of graph %u not found.", graph_id);
  }
}

GraphManager::CompilerStages &GraphManager::GetCompilerStages(GraphId graph_id) {
  std::lock_guard<std::mutex> lock(member_mutex_);
  return compiler_stages_[graph_id];
}

void GraphManager::RemoveCompilerStages(GraphId graph_id) {
  std::lock_guard<std::mutex> lock(member_mutex_);
  compiler_stages_.erase(graph_id);
}
}  // namespace ge
