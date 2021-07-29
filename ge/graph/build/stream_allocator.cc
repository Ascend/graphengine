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

#include "graph/build/stream_allocator.h"
#include <algorithm>
#include <memory>
#include "common/ge/ge_util.h"
#include "framework/common/debug/ge_log.h"
#include "framework/common/fmk_error_codes.h"
#include "framework/common/types.h"
#include "graph/build/logical_stream_allocator.h"
#include "common/omg_util.h"
#include "graph/debug/ge_attr_define.h"
#include "graph/ge_context.h"
#include "graph/utils/graph_utils.h"
#include "init/gelib.h"
#include "framework/common/string_util.h"
#include "common/util/error_manager/error_manager.h"

using std::map;
using std::set;
using std::string;
using std::vector;

namespace {
const int64_t kTaskNumPerNormalNode = 3;
const int64_t kTaskNumPerHcclNode = 245;
const char *const kTrueStr = "true";
const char *const kFalseStr = "false";
const size_t kEventMultiplexingItemCount = 3;
const size_t kKeyWordIndex = 0;
const size_t kNodeNameIndex = 1;
const size_t kEventIdIndex = 2;
const char *const kSend = "SendTo";
const char *const kRecv = "RecvFrom";
const char kDelim = ';';

inline bool HasContinuousStreamLabel(const ge::OpDescPtr &op_desc, std::string &continuous_stream_label) {
  if (ge::AttrUtils::GetStr(op_desc, ge::ATTR_NAME_CONTINUOUS_STREAM_LABEL, continuous_stream_label)) {
    GELOGD("node[%s] get continuous_stream_label %s", op_desc->GetName().c_str(), continuous_stream_label.c_str());
    return true;
  }
  return false;
}

bool IsHcclOp(const string &op_type) {
  const set<string> hccl_op_types({ge::HCOMBROADCAST, ge::HCOMALLGATHER,
                                   ge::HCOMALLREDUCE, ge::HCOMREDUCESCATTER, ge::HCOMREDUCE});
  return hccl_op_types.find(op_type) != hccl_op_types.end();
}

ge::Status ParseNodeEventMultiplexing(const ge::NodePtr &node,
    const std::vector<std::string> &raw_event_multiplexing,
    std::unordered_map<ge::NodePtr, std::vector<std::pair<std::string, uint32_t>>> &node_to_send,
    std::unordered_map<ge::NodePtr, std::vector<std::pair<std::string, uint32_t>>> &node_to_recv) {
  GE_CHECK_NOTNULL(node);
  for (const auto &str : raw_event_multiplexing) {
    std::vector<std::string> ele = ge::StringUtils::Split(str, kDelim);
    if (ele.size() != kEventMultiplexingItemCount) {
      GELOGE(ge::PARAM_INVALID, "[Check][RawMultiplexing]Size error, node:%s, require size:%zu, actually:%zu.",
             node->GetName().c_str(), kEventMultiplexingItemCount, ele.size());
      REPORT_INNER_ERROR("E19999", "Raw event multiplexing is invalid, node:%s, require size:%zu, actually:%zu.",
                         node->GetName().c_str(), kEventMultiplexingItemCount, ele.size());
      return ge::PARAM_INVALID;
    }
    int value;
    try {
      value = std::stoi(ele[kEventIdIndex]);
    } catch (std::invalid_argument &) {
      GELOGE(ge::PARAM_INVALID, "[Throw][Exception]Event id is invalid, node:%s, raw:%s.",
             node->GetName().c_str(), ele[kEventIdIndex].c_str());
      REPORT_INNER_ERROR("E19999", "Event id is invalid, node:%s, raw:%s.",
                         node->GetName().c_str(), ele[kEventIdIndex].c_str());
      return ge::PARAM_INVALID;
    } catch (std::out_of_range &) {
      GELOGE(ge::PARAM_INVALID, "[Throw][Exception]Event id is out of range, node:%s, raw:%s.",
             node->GetName().c_str(), ele[kEventIdIndex].c_str());
      REPORT_INNER_ERROR("E19999", "Event id is out of range, node:%s, raw:%s.",
                         node->GetName().c_str(), ele[kEventIdIndex].c_str());
      return ge::PARAM_INVALID;
    }
    if (value < 0) {
      GELOGE(ge::PARAM_INVALID, "[Check][EventId]Event id is out of range, node:%s, raw:%s, value:%d.",
             node->GetName().c_str(), ele[kEventIdIndex].c_str(), value);
      REPORT_INNER_ERROR("E19999", "Event id is out of range, node:%s, raw:%s, value:%d.",
                         node->GetName().c_str(), ele[kEventIdIndex].c_str(), value);
      return ge::PARAM_INVALID;
    }
    if (ele[kKeyWordIndex] == kSend) {
      node_to_send[node].emplace_back(std::make_pair(ele[kNodeNameIndex], static_cast<uint32_t>(value)));
    } else if (ele[kKeyWordIndex] == kRecv) {
      node_to_recv[node].emplace_back(std::make_pair(ele[kNodeNameIndex], static_cast<uint32_t>(value)));
    } else {
      GELOGE(ge::PARAM_INVALID, "[Check][KeyWord]Key word is not supported, node:%s, key:%s.",
             node->GetName().c_str(), ele[kEventIdIndex].c_str());
      REPORT_INNER_ERROR("E19999", "Key word is not supported, node:%s, key:%s.",
                         node->GetName().c_str(), ele[kEventIdIndex].c_str());
      return ge::PARAM_INVALID;
    }
  }
  return ge::SUCCESS;
}

ge::Status ParseAllNodeEventMultiplexing(const ge::ComputeGraphPtr &graph,
    std::unordered_map<std::string, ge::NodePtr> &name_to_node_map,
    std::unordered_map<ge::NodePtr, std::vector<std::pair<std::string, uint32_t>>> &node_to_send,
    std::unordered_map<ge::NodePtr, std::vector<std::pair<std::string, uint32_t>>> &node_to_recv) {
  for (const auto &node : graph->GetNodes(graph->GetGraphUnknownFlag())) {
    ge::OpDescPtr op_desc = node->GetOpDesc();
    GE_CHECK_NOTNULL(op_desc);
    name_to_node_map.insert({node->GetName(), node});
    std::vector<std::string> raw_event_multiplexing;
    if (!(op_desc->HasAttr(ge::ATTR_NAME_EVENT_MULTIPLEXING))) {
      continue;
    }
    bool get_attr = ge::AttrUtils::GetListStr(op_desc, ge::ATTR_NAME_EVENT_MULTIPLEXING, raw_event_multiplexing);
    if (!get_attr) {
      GELOGE(ge::PARAM_INVALID, "[Get][Attr]Node:%s.", node->GetName().c_str());
      REPORT_INNER_ERROR("E19999", "Failed to get raw event multiplexing, node:%s.", node->GetName().c_str());
      return ge::PARAM_INVALID;
    }
    auto parse_ret = ParseNodeEventMultiplexing(node, raw_event_multiplexing, node_to_send, node_to_recv);
    if (parse_ret != ge::SUCCESS) {
      GELOGE(parse_ret, "[Parse][Eventmultiplexing]Node:%s.", node->GetName().c_str());
      REPORT_INNER_ERROR("E19999", "Failed to parse node event multiplexing, node:%s.", node->GetName().c_str());
      return parse_ret;
    }
  }
  return ge::SUCCESS;
}

std::vector<uint32_t> GetIntersection(std::vector<uint32_t> &a, std::vector<uint32_t> &b) {
  std::unordered_set<uint32_t> ele_of_a(a.begin(), a.end());
  std::vector<uint32_t> res;
  for (auto &ele : b) {
    if (ele_of_a.count(ele) > 0) {
      res.emplace_back(ele);
    }
  }
  return res;
}
}  // namespace

namespace ge {
StreamAllocator::StreamAllocator(ComputeGraphPtr whole_graph, const Graph2SubGraphInfoList &subgraphs)
    : whole_graph_(std::move(whole_graph)), subgraphs_(subgraphs) {
  string single_stream_str;
  (void)GetContext().GetOption(ENABLE_SINGLE_STREAM, single_stream_str);

  const set<string> stream_options = {"", kTrueStr, kFalseStr};
  if (stream_options.find(single_stream_str) == stream_options.end()) {
    GELOGW("The value %s of the %s option is invalid, it should be true or false.", single_stream_str.c_str(),
           ENABLE_SINGLE_STREAM);
  }

  enable_single_stream_ = (single_stream_str == kTrueStr) ? true : false;
  GELOGD("Enable single stream: %s.", enable_single_stream_ ? kTrueStr : kFalseStr);
}

Status StreamAllocator::AssignLogicalStreams(const std::map<std::string, int> &max_parallel_num, bool hcom_parallel) {
  GE_CHECK_NOTNULL(whole_graph_);
  GE_DUMP(whole_graph_, "BeforeAssignedLogicalStreams");

  auto gelib = GELib::GetInstance();
  if (gelib == nullptr) {
    REPORT_INNER_ERROR("E19999", "Check GELib instance nullptr, graph:%s", whole_graph_->GetName().c_str());
    GELOGE(FAILED, "[Get][Instance] of GELib failed. graph:%s", whole_graph_->GetName().c_str());
    return FAILED;
  }

  const map<string, SchedulerConf> &scheduler_confs = gelib->DNNEngineManagerObj().GetSchedulers();
  LogicalStreamAllocator logical_allocator(scheduler_confs, max_parallel_num);
  logical_allocator.EnableSingleStream(enable_single_stream_);
  logical_allocator.EnableHcomParallel(hcom_parallel);

  Status status = logical_allocator.Assign(whole_graph_, subgraphs_, stream_num_);
  if (status != SUCCESS) {
    GELOGE(status, "[Assign][LogicalStreams] failed. graph:%s", whole_graph_->GetName().c_str());
    return status;
  }
  GE_DUMP(whole_graph_, "AfterAssignedLogicalStreams");
  return SUCCESS;
}

// After allocating the logical stream in the graph, refresh the stream in the
// graph and insert the synchronization node.
Status StreamAllocator::RefreshRealStream(int64_t &stream_num, int64_t &event_num) {
  GE_CHECK_NOTNULL(whole_graph_);
  GE_DUMP(whole_graph_, "BeforeRefreshRealStream");

  Status status = AssignSingleStream();
  if (status != SUCCESS) {
    GELOGE(status, "[Assign][SingleStream] failed! graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  status = SetActiveStreamsByLabel();
  if (status != SUCCESS) {
    GELOGE(status, "[Set][ActiveStreams] By Label failed! graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  status = SetActiveStreamsForSubgraphs();
  if (status != SUCCESS) {
    GELOGE(status, "[Set][ActiveStreams] For Subgraphs failed. graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  status = InsertSyncEvents();
  if (status != SUCCESS) {
    GELOGE(status, "[Insert][SyncEventId] failed! graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  status = OptimizeSyncEvents();
  if (status != SUCCESS) {
    GELOGE(status, "[Optimize][SyncEventId] failed! graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  vector<set<int64_t>> split_streams(stream_num_);
  status = SplitStreams(split_streams);
  if (status != SUCCESS) {
    GELOGE(status, "[Split][Streams] failed! graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  status = UpdateActiveStreams(split_streams);
  if (status != SUCCESS) {
    GELOGE(status, "[Update][ActiveStreams] failed! graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  status = RefreshContinuousEvents();
  if (status != SUCCESS) {
    GELOGE(status, "[Refresh][ContinuousEvents] failed! graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  status = RefreshEventsWithReuse();
  if (status != SUCCESS) {
    GELOGE(status, "[Refresh][Events] With Reuse failed! graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  status = InsertSyncEventNodes();
  if (status != SUCCESS) {
    GELOGE(status, "[Insert][SyncEventNode] failed! graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  DumpEvents();
  GE_DUMP(whole_graph_, "AfterRefreshRealStream");

  for (const NodePtr &node : whole_graph_->GetNodes(whole_graph_->GetGraphUnknownFlag())) {
    GE_CHECK_NOTNULL(node->GetOpDesc());
    auto stream_id = node->GetOpDesc()->GetStreamId();
    if (stream_id == kInvalidStream) {
      node->GetOpDesc()->SetStreamId(0);
    }
  }

  if (stream_num_ == 0) {
    GELOGI("None of nodes need to assign stream, stream num is 0, it will cause error, so change it to 1");
    stream_num_ = 1;
  }
  GELOGD("stream num: %ld, event num: %u.", stream_num_, event_num_);

  stream_num = stream_num_;
  event_num = static_cast<int64_t>(event_num_);

  return SUCCESS;
}

Status StreamAllocator::AssignSingleStream() {
  if (!enable_single_stream_) {
    return SUCCESS;
  }

  if (stream_num_ > 1) {
    REPORT_INNER_ERROR("E19999", "The number of ts streams is %ld, only one is supported",
                       stream_num_);
    GELOGE(FAILED, "[Check][Param] The number of ts streams is %ld, only one is supported.", stream_num_);
    return FAILED;
  }

  int64_t task_count = 0;
  for (const NodePtr &node : whole_graph_->GetNodes(whole_graph_->GetGraphUnknownFlag())) {
    string op_type = node->GetType();
    if (IsHcclOp(op_type)) {
      task_count += kTaskNumPerHcclNode;
    } else {
      task_count += kTaskNumPerNormalNode;
    }
  }

  uint32_t max_normal_stream_count = 0;
  uint32_t max_normal_task_count = 0;
  Status status = GetMaxStreamAndTask(false, max_normal_stream_count, max_normal_task_count);
  if (status != SUCCESS) {
    GELOGE(status, "[Get][MaxCount] of normal stream and task failed. graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  if (task_count > static_cast<int64_t>(max_normal_task_count)) {
    uint32_t max_huge_stream_count = 0;
    uint32_t max_huge_task_count = 0;
    Status status = GetMaxStreamAndTask(true, max_huge_stream_count, max_huge_task_count);
    if (status == SUCCESS) {
      int64_t huge_stream = 0;
      GELOGI("Use huge stream %ld.", huge_stream);
      huge_streams_.emplace_back(huge_stream);
    } else {
      GELOGW(
          "The estimated task count %ld is greater than the max count of normal stream,"
          " but the huge stream is not supported.",
          task_count);
    }
  }

  return SUCCESS;
}

Status StreamAllocator::SetActiveStreamsByLabel() {
  for (const auto &node : whole_graph_->GetNodes(whole_graph_->GetGraphUnknownFlag())) {
    OpDescPtr op_desc = node->GetOpDesc();
    GE_CHECK_NOTNULL(op_desc);
    string stream_label;
    if (AttrUtils::GetStr(op_desc, ATTR_NAME_STREAM_LABEL, stream_label) && !stream_label.empty()) {
      int64_t stream_id = op_desc->GetStreamId();
      if (stream_id != kInvalidStream) {
        labeled_streams_[stream_label].emplace(stream_id);
      }
    }
  }

  for (const auto &node : whole_graph_->GetNodes(whole_graph_->GetGraphUnknownFlag())) {
    GE_CHECK_NOTNULL(node->GetOpDesc());
    vector<string> activated_label_list;
    if (!AttrUtils::GetListStr(node->GetOpDesc(), ATTR_NAME_ACTIVE_LABEL_LIST, activated_label_list) ||
        activated_label_list.empty()) {
      continue;
    }

    vector<uint32_t> activated_stream_list;
    for (string &activated_label : activated_label_list) {
      specific_activated_labels_[activated_label].emplace(node);
      for (int64_t activated_stream : labeled_streams_[activated_label]) {
        activated_stream_list.push_back(static_cast<uint32_t>(activated_stream));
        specific_activated_streams_.emplace(activated_stream);
        specific_activated_streams_nodes_map_[activated_stream].emplace(node);
        GELOGI("Node %s active stream %ld by %s.", node->GetName().c_str(), activated_stream, activated_label.c_str());
      }
    }
    GE_CHK_BOOL_EXEC(AttrUtils::SetListInt(node->GetOpDesc(), ATTR_NAME_ACTIVE_STREAM_LIST, activated_stream_list),
                     REPORT_INNER_ERROR("E19999", "Set Attr:%s for op:%s(%s) failed",
                                        ATTR_NAME_ACTIVE_STREAM_LIST.c_str(),
                                        node->GetName().c_str(), node->GetType().c_str());
                     GELOGE(FAILED, "[Set][Attr] %s for op:%s(%s) failed",
                            ATTR_NAME_ACTIVE_STREAM_LIST.c_str(), node->GetName().c_str(), node->GetType().c_str());
                     return FAILED);
  }

  return SUCCESS;
}

Status StreamAllocator::SetActiveStreamsForSubgraphs() {
  for (auto &subgraph : whole_graph_->GetAllSubgraphs()) {
    GE_CHECK_NOTNULL(subgraph);
    NodePtr first_active_node = nullptr;

    // Get all streams in subgraph.
    set<int64_t> subgraph_streams;
    for (auto &node : subgraph->GetDirectNode()) {
      OpDescPtr op_desc = node->GetOpDesc();
      GE_CHECK_NOTNULL(op_desc);
      // Skip streams with label
      string stream_label;
      if (AttrUtils::GetStr(op_desc, ATTR_NAME_STREAM_LABEL, stream_label) && !stream_label.empty()) {
        continue;
      }
      int64_t stream_id = op_desc->GetStreamId();
      if (stream_id != kInvalidStream) {
        subgraph_streams.emplace(stream_id);
        GELOGI("Add stream %ld to active_stream_list of node %s of graph %s", stream_id, node->GetName().c_str(),
               subgraph->GetName().c_str());
      }
      bool is_first_active = false;
      if (AttrUtils::GetBool(op_desc, ATTR_NAME_SUBGRAPH_FIRST_ACTIVE, is_first_active) && is_first_active) {
        first_active_node = node;
      }
    }

    if (first_active_node == nullptr) {
      continue;
    }

    subgraph_first_active_node_map_[subgraph] = first_active_node;

    // Set active streams for StreamActive.
    subgraph_streams.erase(first_active_node->GetOpDesc()->GetStreamId());

    vector<uint32_t> active_streams;
    for (int64_t active_stream : subgraph_streams) {
      active_streams.emplace_back(static_cast<uint32_t>(active_stream));
      specific_activated_streams_.emplace(active_stream);
    }

    if (!AttrUtils::SetListInt(first_active_node->GetOpDesc(), ATTR_NAME_ACTIVE_STREAM_LIST, active_streams)) {
      REPORT_INNER_ERROR("E19999", "Set Attr:%s for op:%s(%s) failed",
                         ATTR_NAME_ACTIVE_STREAM_LIST.c_str(),
                         first_active_node->GetName().c_str(), first_active_node->GetType().c_str());
      GELOGE(FAILED, "[Set][Attr] active streams for node %s failed.", first_active_node->GetName().c_str());
      return FAILED;
    }
  }
  return SUCCESS;
}

// Insert the send/recv event id to the graph
Status StreamAllocator::InsertSyncEvents() {
  auto ffts_filter = [](const Node &node, const char *, const ComputeGraphPtr &) {
    return !node.GetOpDesc()->HasAttr(ATTR_NAME_FFTS_SUB_GRAPH);
  };

  for (const auto &cur_node : whole_graph_->GetNodes(whole_graph_->GetGraphUnknownFlag(), nullptr, ffts_filter)) {
    // Take the adjacent points, then judge whether need to insert the event
    for (const OutDataAnchorPtr &anchor : cur_node->GetAllOutDataAnchors()) {
      for (const InDataAnchorPtr &peer_in_anchor : anchor->GetPeerInDataAnchors()) {
        NodePtr next_node = peer_in_anchor->GetOwnerNode();
        Status status = InsertOneEventInTwoNodes(cur_node, next_node);
        if (status != SUCCESS) {
          GELOGE(status, "[Insert][OneEvent] In Two Nodes failed! cur node:%s", cur_node->GetName().c_str());
          return status;
        }
      }
    }

    /// If the two nodes of the control side belong to two streams,
    /// you also need to add the send/recv event.
    if (cur_node->GetOutControlAnchor() != nullptr) {
      for (const AnchorPtr &peer_in_anchor : cur_node->GetOutControlAnchor()->GetPeerAnchors()) {
        NodePtr next_node = peer_in_anchor->GetOwnerNode();
        Status status = InsertOneEventInTwoNodes(cur_node, next_node);
        if (status != SUCCESS) {
          GELOGE(status, "[Insert][OneEvent] In Two Nodes failed! cur node:%s", cur_node->GetName().c_str());
          return status;
        }
      }
    }
  }

  Status status = InsertEventsForSubgraph();
  if (status != SUCCESS) {
    GELOGE(status, "[Insert][Events] Between Sub And Parent GraphNodes failed! graph:%s",
           whole_graph_->GetName().c_str());
    return status;
  }

  return SUCCESS;
}

// Insert one send/recv event in two nodes
Status StreamAllocator::InsertOneEventInTwoNodes(const NodePtr &cur_node, const NodePtr &next_node) {
  GE_CHECK_NOTNULL(cur_node->GetOpDesc());
  GE_CHECK_NOTNULL(next_node->GetOpDesc());

  // No need to insert events after node that do not assign streams.
  int64_t cur_stream_id = cur_node->GetOpDesc()->GetStreamId();
  if (cur_stream_id == kInvalidStream) {
    GELOGD("No need to insert event after node %s.", cur_node->GetName().c_str());
    return SUCCESS;
  }

  // No need to insert events between nodes in the same stream.
  int64_t next_stream_id = next_node->GetOpDesc()->GetStreamId();
  if (cur_stream_id == next_stream_id) {
    return SUCCESS;
  }

  if (((cur_node->GetType() == ENTER) || (cur_node->GetType() == REFENTER)) && (next_node->GetType() != STREAMACTIVE)) {
    GELOGD("No need to insert event between %s and %s.", cur_node->GetName().c_str(), next_node->GetName().c_str());
    return SUCCESS;
  }

  if (next_stream_id == kInvalidStream) {
    REPORT_INNER_ERROR("E19999", "Stream id of next_node %s(%s) should not be %ld",
                       next_node->GetName().c_str(), next_node->GetType().c_str(), kInvalidStream);
    GELOGE(FAILED, "[Check][Param] Stream id of next_node %s should not be %ld",
           next_node->GetName().c_str(), kInvalidStream);
    return FAILED;
  }

  // No event needs to be inserted between the active node and the activated stream.
  string next_node_label;
  if (AttrUtils::GetStr(next_node->GetOpDesc(), ATTR_NAME_STREAM_LABEL, next_node_label) && !next_node_label.empty()) {
    auto iter = specific_activated_labels_.find(next_node_label);
    if (iter != specific_activated_labels_.end()) {
      for (const auto &active_node : iter->second) {
        OpDescPtr active_op = active_node->GetOpDesc();
        GE_CHECK_NOTNULL(active_op);
        if ((cur_stream_id == active_op->GetStreamId()) && (cur_node->GetOpDesc()->GetId() <= active_op->GetId())) {
          GELOGI("No need to insert event between node %s and %s.", cur_node->GetName().c_str(),
                 next_node->GetName().c_str());
          return SUCCESS;
        }
      }
    }
  }

  // Add send and receive events.
  AddSendEventId(cur_node, event_num_);
  AddRecvEventId(next_node, event_num_);
  GELOGD("Insert event %u between node %s(stream %ld) and %s(stream %ld)", event_num_, cur_node->GetName().c_str(),
         cur_stream_id, next_node->GetName().c_str(), next_stream_id);

  ++event_num_;

  return SUCCESS;
}

Status StreamAllocator::InsertEventsForSubgraph() {
  for (const auto &subgraph : whole_graph_->GetAllSubgraphs()) {
    GE_CHECK_NOTNULL(subgraph);
    const auto parent_node = subgraph->GetParentNode();
    if (parent_node != nullptr && parent_node->GetOpDesc()->HasAttr(ATTR_NAME_FFTS_SUB_GRAPH)) {
      GELOGD("Skip ffts subgraph, parent node is %s.", parent_node->GetName().c_str());
      continue;
    }
    for (const auto &node : subgraph->GetDirectNode()) {
      auto op_desc = node->GetOpDesc();
      GE_CHECK_NOTNULL(op_desc);
      bool is_subgraph_end_node = false;
      if (!AttrUtils::GetBool(op_desc, ATTR_NAME_SUBGRAPH_END_NODE, is_subgraph_end_node) || !is_subgraph_end_node) {
        continue;
      }
      const auto parent_node = subgraph->GetParentNode();
      GE_CHECK_NOTNULL(parent_node);

      // Insert events between subgraph end node and parent node's out nodes
      for (const auto &next_node : parent_node->GetOutAllNodes()) {
        Status status = InsertOneEventInTwoNodes(node, next_node);
        if (status != SUCCESS) {
          GELOGE(status, "[Insert][OneEvent] In Two Nodes failed! node:%s", node->GetName().c_str());
          return status;
        }
      }

      break;
    }
  }

  return SUCCESS;
}

// Optimize the event in the graph, delete the redundant sync event according to the stream information
Status StreamAllocator::OptimizeSyncEvents() {
  map<int64_t, vector<NodePtr>> stream_nodes;

  for (const auto &node : whole_graph_->GetNodes(whole_graph_->GetGraphUnknownFlag())) {
    GE_CHECK_NOTNULL(node->GetOpDesc());
    int64_t stream_id = node->GetOpDesc()->GetStreamId();
    stream_nodes[stream_id].emplace_back(node);
  }

  Status status = OptimizeBySendEvents(stream_nodes);
  if (status != SUCCESS) {
    GELOGE(status, "[Optimize][StreamNodes] By Send Events failed! graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  status = OptimizeByRecvEvents(stream_nodes);
  if (status != SUCCESS) {
    GELOGE(status, "[Optimize][StreamNodes] By Recv Events failed! graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  status = OptimizeByStreamActivate();
  if (status != SUCCESS) {
    GELOGE(status, "[Call][OptimizeByStreamActivate] failed! graph:%s", whole_graph_->GetName().c_str());
    return status;
  }
  for (auto pair : node_to_send_events_) {
    if (pair.first->GetType() == STREAMSWITCH) {
      for (auto event_id : pair.second) {
        GELOGI("Curren switch node is %s, remove send event_id %d.", pair.first->GetName().c_str(), event_id);
        RmvSendEventId(pair.first, event_id);
        auto recv_node = GetNodeFromRecvEventId(event_id);
        GELOGI("Curren recv_node is %s, remove recv event_id %d.", recv_node->GetName().c_str(), event_id);
        RmvRecvEventId(recv_node, event_id);
      }
    }
  }
  return SUCCESS;
}

/// Optimization scenario: one stream has multiple send events in one node,
/// and multiple nodes for recv events on another stream
/// Example:
/// Stream0            Stream1
///   N1 - - - event - > N1
///     \                |
///      \               v
///        - - event - > N2
Status StreamAllocator::OptimizeBySendEvents(const map<int64_t, vector<NodePtr>> &stream_nodes) {
  for (const auto &one_pair : stream_nodes) {
    // The nodes on a stream in order
    const vector<NodePtr> &nodes = one_pair.second;

    map<NodePtr, uint32_t> send_node_to_event_id;

    for (const auto &recv_node_ptr : nodes) {
      GE_CHECK_NOTNULL(recv_node_ptr);
      // Get all recv events of the current node, then traverse the event
      vector<uint32_t> recv_events;
      GetRecvEventIdList(recv_node_ptr, recv_events);

      for (const auto &event_id : recv_events) {
        NodePtr send_node_ptr = GetNodeFromSendEventId(event_id);
        GE_CHECK_NOTNULL(send_node_ptr);

        /// If the record to the stream is found in the map,
        /// and the recv node is the node, then remove sync event
        if (send_node_to_event_id.find(send_node_ptr) != send_node_to_event_id.end()) {
          RmvSendEventId(send_node_ptr, event_id);
          RmvRecvEventId(recv_node_ptr, event_id);
          GELOGI("Remove event %u between node %s and node %s", event_id, send_node_ptr->GetName().c_str(),
                 recv_node_ptr->GetName().c_str());
        } else {
          send_node_to_event_id[send_node_ptr] = event_id;
        }
      }
    }
  }

  return SUCCESS;
}

/// Scenario: multiple send nodes on a stream sent to a single recv node on the destination stream
/// Example:
/// Stream0            Stream1
///   N1 - -
///   |    |
///   |    - - event - - -
///   |                  |
///   V                  V
///   N2 - - - event - > N2
Status StreamAllocator::OptimizeByRecvEvents(const map<int64_t, vector<NodePtr>> &stream_nodes) {
  for (const auto &one_pair : stream_nodes) {
    // The nodes on a stream in order
    const vector<NodePtr> &nodes = one_pair.second;

    map<NodePtr, uint32_t> recv_node_to_event_id;

    for (const auto &send_node_ptr : nodes) {
      GE_CHECK_NOTNULL(send_node_ptr);
      //  Get all send events of the current node, then traverse the event
      vector<uint32_t> send_id_list;
      GetSendEventIdList(send_node_ptr, send_id_list);

      for (const auto &event_id : send_id_list) {
        NodePtr recv_node_ptr = GetNodeFromRecvEventId(event_id);
        GE_CHECK_NOTNULL(recv_node_ptr);

        /// If the record to the stream is found in the map,
        /// and the send node is the node, then remove sync event
        auto it = recv_node_to_event_id.find(recv_node_ptr);
        if (it != recv_node_to_event_id.end()) {
          uint32_t pre_event_id = it->second;
          NodePtr pre_send_node_ptr = GetNodeFromSendEventId(pre_event_id);
          GE_CHECK_NOTNULL(pre_send_node_ptr);

          RmvSendEventId(pre_send_node_ptr, pre_event_id);
          RmvRecvEventId(recv_node_ptr, pre_event_id);
          GELOGI("Remove event %u between node %s and node %s.", event_id, pre_send_node_ptr->GetName().c_str(),
                 recv_node_ptr->GetName().c_str());
        }
        recv_node_to_event_id[recv_node_ptr] = event_id;
      }
    }
  }

  return SUCCESS;
}

Status StreamAllocator::OptimizeByStreamActivate() {
  auto node_to_send_events_temp = node_to_send_events_;
  for (const auto &node_event_id_pair : node_to_send_events_temp) {
    const NodePtr &send_node_ptr = node_event_id_pair.first;
    for (const auto &event_id : node_event_id_pair.second) {
      NodePtr recv_node_ptr = GetNodeFromRecvEventId(event_id);
      GE_CHECK_NOTNULL(recv_node_ptr);
      if (IsRecvNodeActivatedBySendNode(send_node_ptr, recv_node_ptr)) {
        RmvSendEventId(send_node_ptr, event_id);
        RmvRecvEventId(recv_node_ptr, event_id);
        GELOGI("Remove event %u between node %s and node %s.", event_id, send_node_ptr->GetName().c_str(),
               recv_node_ptr->GetName().c_str());
      }
    }
  }
  return SUCCESS;
}

// In situation : stream(normal) -> stream(streamActivate)->
// -> stream(streamSwitch) -> stream(streamActivate) -> stream(stream true or false)
// No need to insert an event between node in stream(normal) and node in stream(stream true or false)
bool StreamAllocator::IsRecvNodeActivatedBySendNode(const NodePtr &send_node_ptr, const NodePtr &recv_node_ptr) const {
  GE_CHECK_NOTNULL_EXEC(send_node_ptr->GetOpDesc(),
                        REPORT_INNER_ERROR("E19999", "Check param send_node_ptr nullptr");
                        GELOGE(FAILED, "[Check][Param] op desc is nullptr");
                        return false);
  GE_CHECK_NOTNULL_EXEC(recv_node_ptr->GetOpDesc(),
                        REPORT_INNER_ERROR("E19999", "Check param recv_node_ptr nullptr");
                        GELOGE(FAILED, "[Check][Param] op desc is nullptr");
                        return false);
  auto cur_stream_id = send_node_ptr->GetOpDesc()->GetStreamId();
  if (AttrUtils::HasAttr(recv_node_ptr->GetOpDesc(), ATTR_NAME_STREAM_LABEL)) {
    // find streamActivate node
    auto iter = specific_activated_streams_nodes_map_.find(recv_node_ptr->GetOpDesc()->GetStreamId());
    set<NodePtr> activate_stream_nodes;
    if (iter != specific_activated_streams_nodes_map_.end()) {
      activate_stream_nodes = iter->second;
    }
    set<NodePtr> visited_nodes{recv_node_ptr};
    while (!activate_stream_nodes.empty()) {
      set<NodePtr> activate_stream_nodes_temp;
      for (const auto &activate_stream_node : activate_stream_nodes) {
        GE_IF_BOOL_EXEC(activate_stream_node->GetOpDesc() == nullptr, continue);
        if (visited_nodes.find(activate_stream_node) != visited_nodes.end() ||
            AttrUtils::HasAttr(activate_stream_node->GetOpDesc(), ATTR_NAME_IS_LOOP_ACTIVE)) {
          return false;
        }

        ///
        /// stream_0  -->  stream_2  -->  stream_3  -->  stream_4
        ///                   /\             |
        ///                   |             \/
        ///                   |           stream_1  -->  stream_5  -->  stream_6  -->  stream_7
        ///                   |                             /\             |              |
        ///                   |                             |             \/              |
        ///                   |                             |---------- stream_8          |
        ///                   |                                                           |
        ///                   |-----------------------------------------------------------|
        ///
        ///  Exit1(S7) Exit2(S7)  Exit3(S7)
        ///     \       /           |
        ///     AddN(S1)     NextIteration(S7)
        ///       |                 |
        ///     NextIteration(S1)  /
        ///          |            /
        ///          |           /
        ///        StreamActive(S7)
        ///
        /// Event between Exit1/Exit2 and AddN should not be optimized
        ///
        if (IsActiveAfterNextIteration(activate_stream_node)) {
          continue;
        }

        visited_nodes.insert(activate_stream_node);
        // nodes in stream link to streamActivate no need to add event before activated node
        for (const auto &pre_activate_stream_node : activate_stream_node->GetInNodes()) {
          GE_IF_BOOL_EXEC(pre_activate_stream_node->GetOpDesc() == nullptr, continue);
          if (pre_activate_stream_node->GetOpDesc()->GetStreamId() == cur_stream_id &&
              pre_activate_stream_node->GetOpDesc()->GetId() >= send_node_ptr->GetOpDesc()->GetId()) {
            return true;
          }
          auto in_nodes_of_pre = pre_activate_stream_node->GetInNodes();
          if (std::find(in_nodes_of_pre.begin(), in_nodes_of_pre.end(), send_node_ptr) != in_nodes_of_pre.end()) {
            return true;
          }
        }
        auto iterator = specific_activated_streams_nodes_map_.find(activate_stream_node->GetOpDesc()->GetStreamId());
        if (iterator != specific_activated_streams_nodes_map_.end()) {
          auto active_nodes = iterator->second;
          for (const auto &active_node : active_nodes) {
            activate_stream_nodes_temp.emplace(active_node);
          }
        }
      }
      activate_stream_nodes = activate_stream_nodes_temp;
    }
  }
  return false;
}

bool StreamAllocator::IsActiveAfterNextIteration(const NodePtr &active_node_ptr) const {
  if ((active_node_ptr == nullptr) || active_node_ptr->GetInControlNodes().empty()) {
    return false;
  }
  for (const auto &in_node : active_node_ptr->GetInControlNodes()) {
    if ((in_node->GetType() != NEXTITERATION) && (in_node->GetType() != REFNEXTITERATION)) {
      return false;
    }
  }
  return true;
}

// Split the stream according to the maximum number of nodes in the stream.
Status StreamAllocator::SplitStreams(vector<set<int64_t>> &split_streams) {
  if (enable_single_stream_ || stream_num_ == 0) {
    GELOGI("The single stream option is enabled or the number of streams is 0, no need to split streams.");
    return SUCCESS;
  }

  // stream_node_num_vec records the number of all nodes on each stream
  // added_stream_num_vec records the number of streams that each stream needs to increase
  // new_stream_id_vec records the new physical stream id for each stream
  vector<int64_t> stream_node_num_vec(stream_num_);
  vector<int64_t> added_stream_num_vec(stream_num_);
  vector<int64_t> new_stream_id_vec(stream_num_);
  map<string, int64_t> stream_continuous_2_node_num_map;
  map<string, vector<NodePtr>> stream_continuous_2_nodes_map;
  map<int64_t, vector<NodePtr>> stream_2_nodes_map;
  vector<NodePtr> pre_node_vec(stream_num_);

  int64_t last_stream_id = stream_num_ - 1;
  for (auto i = 0; i <= last_stream_id; i++) {
    stream_node_num_vec[i] = 0;
    added_stream_num_vec[i] = 0;
    new_stream_id_vec[i] = i;
    pre_node_vec[i] = nullptr;
  }

  uint32_t max_stream_count = 0;
  uint32_t max_task_count = 0;
  GE_CHK_STATUS_RET(GetMaxStreamAndTask(false, max_stream_count, max_task_count),
                    "[Get][MaxCount] of stream and task failed.");

  for (const auto &cur_node : whole_graph_->GetNodes(whole_graph_->GetGraphUnknownFlag())) {
    GE_CHECK_NOTNULL(cur_node);
    auto op_desc = cur_node->GetOpDesc();
    GE_CHECK_NOTNULL(op_desc);
    int64_t stream_id = op_desc->GetStreamId();
    if (stream_id == kInvalidStream) {
      continue;
    }
    if (stream_id > last_stream_id) {
      REPORT_INNER_ERROR("E19999", "streamid(%ld) > last_stream_id(%ld), check invalid",
                         stream_id, last_stream_id);
      GELOGE(FAILED, "[Check][Param] SplitStreams:streamid(%ld) > last_stream_id(%ld)", stream_id, last_stream_id);
      return FAILED;
    }
    bool is_stream_first_node = (stream_node_num_vec[stream_id] == 0);
    AddNodeNum(cur_node, stream_node_num_vec[stream_id]);
    stream_2_nodes_map[stream_id].push_back(cur_node);
    // The maximum number of tasks per stream.
    int64_t max_node_num_one_stream = GetMaxNodeNumPerStream(cur_node, max_task_count);
    std::string continuous_stream_label;
    if (HasContinuousStreamLabel(op_desc, continuous_stream_label)) {
      stream_continuous_2_node_num_map[continuous_stream_label]++;
      // return error
      if (stream_continuous_2_node_num_map[continuous_stream_label] > max_node_num_one_stream) {
        REPORT_INNER_ERROR("E19999", "Check node[%s] stream_id[%ld] continuous stream label[%s] unsatisfied",
                           op_desc->GetName().c_str(), stream_id, continuous_stream_label.c_str());
        GELOGE(FAILED, "[Check][Param] SplitStreams:node[%s] stream_id[%ld] continuous stream label[%s] unsatisfied ",
               op_desc->GetName().c_str(), stream_id, continuous_stream_label.c_str());
        return FAILED;
      }
      stream_continuous_2_nodes_map[continuous_stream_label].push_back(cur_node);
    }
    // Split the stream if it exceeds the maximum number of nodes in the stream.
    if (NeedSpiltNewStream(stream_node_num_vec[stream_id], max_node_num_one_stream, op_desc, is_stream_first_node)) {
      last_stream_id++;
      GELOGI(
          "stream_node_num_vec[%ld]= %ld > max_node_num_one_stream : %ld, "
          "It's time to split the stream, split newly-added stream id is %ld",
          stream_id, stream_node_num_vec[stream_id], max_node_num_one_stream, last_stream_id);
      NodePtr pre_node = pre_node_vec[stream_id];
      stream_node_num_vec[stream_id] = 0;
      AddNodeNum(cur_node, stream_node_num_vec[stream_id]);
      // try spilt a new stream and move same continuous stream label nodes from this stream
      bool not_use_cur = false;
      NodePtr not_cur = nullptr;
      std::string cur_continuous_stream_label;
      if (HasContinuousStreamLabel(op_desc, cur_continuous_stream_label)) {
        // get stored nodes
        auto nodes = stream_continuous_2_nodes_map[cur_continuous_stream_label];
        GE_RETURN_WITH_LOG_IF_FALSE(!nodes.empty(),
                                    "[Check][Param] split stream with continuous stream label %s failed",
                                    cur_continuous_stream_label.c_str());
        for (const auto &node : nodes) {
          auto stored_op_desc = node->GetOpDesc();
          GE_CHECK_NOTNULL(stored_op_desc);
          stored_op_desc->SetStreamId(last_stream_id);
          AddNodeNum(node, stream_node_num_vec[stream_id]);
        }
        not_use_cur = true;
        not_cur = nodes.front();
        GE_CHECK_NOTNULL(not_cur);
        GELOGI("split from first node %s with continuous stream label %s", not_cur->GetName().c_str(),
               cur_continuous_stream_label.c_str());
        auto iter = std::find(stream_2_nodes_map[stream_id].begin(), stream_2_nodes_map[stream_id].end(), not_cur);
        GE_RETURN_WITH_LOG_IF_FALSE(
            (iter != stream_2_nodes_map[stream_id].end()) && (iter != stream_2_nodes_map[stream_id].begin()),
            "[Check][Param] split stream with continuous stream label %s failed", cur_continuous_stream_label.c_str());
        iter--;
        pre_node = *iter;
      }

      added_stream_num_vec[stream_id]++;
      new_stream_id_vec[stream_id] = last_stream_id;
      split_streams[stream_id].emplace(last_stream_id);
      split_ori_stream_map_[last_stream_id] = stream_id;
      node_split_stream_map_[cur_node] = last_stream_id;

      // Add the send/recv event to the first and last nodes of the split stream.
      if (pre_node != nullptr) {
        GE_CHK_STATUS_RET(AddEventId(pre_node, not_cur, cur_node, not_use_cur),
                          "[Add][EventId] failed, pre node:%s, not cur node:%s, cur node:%s.",
                          pre_node->GetName().c_str(), not_cur->GetName().c_str(), cur_node->GetName().c_str());
      }
    }

    /// If the split stream num is greater than 1, the node behind the same
    /// stream must reset the new stream id.
    if (added_stream_num_vec[stream_id] >= 1) {
      op_desc->SetStreamId(new_stream_id_vec[stream_id]);
    }

    pre_node_vec[stream_id] = cur_node;
  }

  if (last_stream_id >= 0) {
    stream_num_ = last_stream_id + 1;
  }
  return SUCCESS;
}

bool StreamAllocator::NeedSpiltNewStream(int64_t stream_node_num, int64_t max_node_num_one_stream,
                                         const OpDescPtr &op_desc, bool is_stream_first_node) const {
  if (is_stream_first_node) {
    GELOGD("First node of stream does not need to split new stream");
    return false;
  }
  const set<string> label_op_types({LABELSET, LABELGOTO, LABELGOTOEX, LABELSWITCH, LABELSWITCHBYINDEX});
  bool is_first_active_node = false;
  (void)AttrUtils::GetBool(op_desc, ATTR_NAME_SUBGRAPH_FIRST_ACTIVE, is_first_active_node);
  return (stream_node_num > max_node_num_one_stream && op_desc->GetSubgraphInstanceNames().empty() &&
          !is_first_active_node && label_op_types.count(op_desc->GetType()) == 0);
}

Status StreamAllocator::UpdateActiveStreams(const vector<set<int64_t>> &split_streams) {
  UpdateLabelStreams(split_streams);

  for (auto &node : whole_graph_->GetNodes(whole_graph_->GetGraphUnknownFlag())) {
    if ((node->GetType() == STREAMSWITCH) || (node->GetType() == STREAMSWITCHN)) {
      if (UpdateActiveStreamsForSwitchNode(node) != SUCCESS) {
        GELOGE(FAILED, "[Update][ActiveStreams] for switch node: %s failed.", node->GetName().c_str());
        return FAILED;
      }
    } else {
      if (UpdateActiveStreamsForActiveNode(split_streams, node) != SUCCESS) {
        GELOGE(FAILED, "[Update][ActiveStreams] for active node: %s failed.", node->GetName().c_str());
        return FAILED;
      }
    }
  }

  Status status = UpdateActiveStreamsForSubgraphs();
  if (status != SUCCESS) {
    GELOGE(status, "[Update][ActiveStreams] for subgraphs failed! graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  status = SetActiveStreamsForLoop();
  if (status != SUCCESS) {
    GELOGE(status, "[Set][ActiveStreams] For Loop failed! graph:%s", whole_graph_->GetName().c_str());
    return status;
  }

  return SUCCESS;
}

void StreamAllocator::UpdateLabelStreams(const vector<set<int64_t>> &split_streams) {
  for (size_t i = 0; i < split_streams.size(); i++) {
    auto &streams = split_streams[i];
    if (streams.empty()) {
      continue;
    }
    if (specific_activated_streams_.count(static_cast<int64_t>(i)) > 0) {
      specific_activated_streams_.insert(streams.begin(), streams.end());
    }
    for (auto &labeled_stream : labeled_streams_) {
      if (labeled_stream.second.count(static_cast<int64_t>(i)) > 0) {
        labeled_stream.second.insert(streams.begin(), streams.end());
        break;
      }
    }
  }
}

Status StreamAllocator::UpdateActiveStreamsForSwitchNode(NodePtr &switch_node) {
  vector<NodePtr> active_nodes;
  if (InsertActiveNodesAfterSwitch(switch_node, active_nodes) != SUCCESS) {
    GELOGE(FAILED, "[Insert][ActiveNodes] after node %s failed.", switch_node->GetName().c_str());
    return FAILED;
  }
  if (active_nodes.empty()) {
    return SUCCESS;
  }
  vector<int64_t> stream_ids;
  for (auto &active_node : active_nodes) {
    GE_CHECK_NOTNULL(active_node->GetOpDesc());
    active_node->GetOpDesc()->SetStreamId(stream_num_);
    stream_ids.emplace_back(stream_num_);
    specific_activated_streams_.emplace(stream_num_);
    stream_num_++;
  }
  auto op_desc = switch_node->GetOpDesc();
  GE_CHECK_NOTNULL(op_desc);

  if (!AttrUtils::SetListInt(op_desc, ATTR_NAME_ACTIVE_STREAM_LIST, stream_ids)) {
    REPORT_INNER_ERROR("E19999", "Set Attr:%s fail for op:%s(%s)", ATTR_NAME_ACTIVE_STREAM_LIST.c_str(),
                       op_desc->GetName().c_str(), op_desc->GetType().c_str());
    GELOGE(FAILED, "[Set][Attr] %s fail for op:%s(%s)", ATTR_NAME_ACTIVE_STREAM_LIST.c_str(),
           op_desc->GetName().c_str(), op_desc->GetType().c_str());
    return FAILED;
  }

  return SUCCESS;
}

Status StreamAllocator::InsertActiveNodesAfterSwitch(NodePtr &switch_node, vector<NodePtr> &active_nodes) {
  GE_CHECK_NOTNULL(switch_node);
  OpDescPtr switch_desc = switch_node->GetOpDesc();
  GE_CHECK_NOTNULL(switch_desc);
  vector<string> ori_active_label_list;
  if (!AttrUtils::GetListStr(switch_desc, ATTR_NAME_ACTIVE_LABEL_LIST, ori_active_label_list) ||
      ori_active_label_list.empty()) {
    REPORT_INNER_ERROR("E19999", "Get Attr:%s fail from op:%s(%s)", ATTR_NAME_ACTIVE_LABEL_LIST.c_str(),
                       switch_node->GetName().c_str(), switch_node->GetType().c_str());
    GELOGE(INTERNAL_ERROR, "[Get][Attr] active label list of switch %s failed.", switch_node->GetName().c_str());
    return INTERNAL_ERROR;
  }

  vector<string> active_label_list;
  vector<NodePtr> added_active_nodes;
  if (AddActiveNodes(switch_node, ori_active_label_list, active_label_list, added_active_nodes) != SUCCESS) {
    GELOGE(FAILED, "[Add][ActiveNodes] after node %s failed.", switch_node->GetName().c_str());
    return FAILED;
  }

  if (SetActiveLabelList(switch_node, active_label_list) != SUCCESS) {
    GELOGE(FAILED, "[Set][ActiveLabelList] failed, node:%s", switch_node->GetName().c_str());
    return FAILED;
  }

  if (added_active_nodes.empty()) {
    return SUCCESS;
  }

  for (auto &active_node : added_active_nodes) {
    GE_CHECK_NOTNULL(switch_node->GetOutControlAnchor());
    if (switch_node->GetOutControlAnchor()->LinkTo(active_node->GetInControlAnchor()) != GRAPH_SUCCESS) {
      REPORT_CALL_ERROR("E19999", "Link from %s to %s failed",
                        switch_node->GetName().c_str(), active_node->GetName().c_str());
      GELOGE(FAILED, "[Link][Nodes] from %s to %s failed.",
             switch_node->GetName().c_str(), active_node->GetName().c_str());
      return FAILED;
    }
    active_nodes.emplace_back(active_node);
  }
  return SUCCESS;
}

Status StreamAllocator::UpdateActiveStreamsForActiveNode(const vector<set<int64_t>> &split_streams, NodePtr &node) {
  GE_CHECK_NOTNULL(node->GetOpDesc());
  vector<uint32_t> active_streams;
  if (AttrUtils::GetListInt(node->GetOpDesc(), ATTR_NAME_ACTIVE_STREAM_LIST, active_streams)) {
    vector<uint32_t> new_active_streams = active_streams;
    for (uint32_t logical_stream : active_streams) {
      if (static_cast<size_t>(logical_stream) >= split_streams.size()) {
        REPORT_INNER_ERROR("E19999", "Check logical stream:%u is out of range:%zu",
                           logical_stream, split_streams.size());
        GELOGE(FAILED, "[Check][Param] logical stream:%u is out of range(0, %zu).",
               logical_stream, split_streams.size());
        return FAILED;
      }
      const set<int64_t> &new_split_streams = split_streams[logical_stream];
      for (int64_t split_stream : new_split_streams) {
        for (const auto &node_stream : node_split_stream_map_) {
          if (split_stream == node_stream.second) {
            if (node_stream.first->GetOwnerComputeGraph() == node->GetOwnerComputeGraph()) {
              new_active_streams.emplace_back(static_cast<uint32_t>(split_stream));
              GELOGI("Add stream %ld to active_stream_list of node %s of graph %s", split_stream,
                     node->GetName().c_str(), node->GetOwnerComputeGraph()->GetName().c_str());
            }
            break;
          }
        }
      }
    }
    if (!AttrUtils::SetListInt(node->GetOpDesc(), ATTR_NAME_ACTIVE_STREAM_LIST, new_active_streams)) {
      REPORT_INNER_ERROR("E19999", "Set Attr:%s fail for op:%s(%s)", ATTR_NAME_ACTIVE_STREAM_LIST.c_str(),
                         node->GetName().c_str(), node->GetType().c_str());
      GELOGE(FAILED, "[Set][Attr] active streams for node %s failed.", node->GetName().c_str());
      return FAILED;
    }
  }
  return SUCCESS;
}

Status StreamAllocator::UpdateActiveStreamsForSubgraphs() {
  // Update active stream list for active nodes
  for (auto &node_stream_pair : node_split_stream_map_) {
    auto node = node_stream_pair.first;
    auto subgraph = node->GetOwnerComputeGraph();
    if (subgraph->GetParentNode() == nullptr) {
      continue;
    }
    // Skip streams with label
    GE_CHECK_NOTNULL(node->GetOpDesc());
    string stream_label;
    if (AttrUtils::GetStr(node->GetOpDesc(), ATTR_NAME_STREAM_LABEL, stream_label) && !stream_label.empty()) {
      continue;
    }
    auto it = subgraph_first_active_node_map_.find(subgraph);
    if (it == subgraph_first_active_node_map_.end()) {
      continue;
    }
    const auto &active_node = it->second;
    GE_CHECK_NOTNULL(active_node);
    auto active_op = active_node->GetOpDesc();
    GE_CHECK_NOTNULL(active_op);
    vector<uint32_t> active_streams;
    (void)AttrUtils::GetListInt(active_op, ATTR_NAME_ACTIVE_STREAM_LIST, active_streams);
    set<uint32_t> new_active_streams(active_streams.begin(), active_streams.end());
    // specific_activated_streams_ has already contained new split activated stream
    int64_t new_split_stream = node_stream_pair.second;
    if (IsActivated(new_split_stream)) {
      continue;
    }
    specific_activated_streams_.emplace(new_split_stream);
    new_active_streams.emplace(static_cast<uint32_t>(new_split_stream));
    active_streams.assign(new_active_streams.begin(), new_active_streams.end());
    if (!AttrUtils::SetListInt(active_op, ATTR_NAME_ACTIVE_STREAM_LIST, active_streams)) {
      REPORT_INNER_ERROR("E19999", "Set Attr:%s fail for op:%s(%s)", ATTR_NAME_ACTIVE_STREAM_LIST.c_str(),
                         active_op->GetName().c_str(), active_op->GetType().c_str());
      GELOGE(FAILED, "[Set][Attr] active streams for node %s failed.", active_node->GetName().c_str());
      return FAILED;
    }
  }

  return SUCCESS;
}

bool StreamAllocator::IsActivated(int64_t stream_id) const {
  const auto &iter = split_ori_stream_map_.find(stream_id);
  if (iter == split_ori_stream_map_.end()) {
    REPORT_INNER_ERROR("E19999", "Find original stream_id failed, split_stream_id=%ld", stream_id);
    GELOGE(INTERNAL_ERROR, "[CheckActivated][Check] Find original stream_id failed, split_stream_id=%ld", stream_id);
    return false;
  }
  int64_t ori_stream_id = iter->second;
  for (const auto &node : whole_graph_->GetNodes(whole_graph_->GetGraphUnknownFlag())) {
    auto op_desc = node->GetOpDesc();
    vector<uint32_t> active_streams;
    if (op_desc == nullptr || !AttrUtils::GetListInt(op_desc, ATTR_NAME_ACTIVE_STREAM_LIST, active_streams)) {
      continue;
    }
    if (std::find(active_streams.begin(), active_streams.end(), stream_id) != active_streams.end() ||
        std::find(active_streams.begin(), active_streams.end(), ori_stream_id) != active_streams.end()) {
      return true;
    }
  }
  return false;
}

// Iteraotor loop :
// StreamSwitch  ->  StreamActive
// FpBp loop:
// StreamSwitch  ->  AssignAdd  ->  StreamActive
NodePtr FindSwitchNodeBeforeLoopActiveNode(const NodePtr &active_node) {
  for (auto pre_node : active_node->GetInControlNodes()) {
    if (pre_node->GetType() == STREAMSWITCH) {
      return pre_node;
    }
    for (auto pre_pre_node : pre_node->GetInControlNodes()) {
      if (pre_pre_node->GetType() == STREAMSWITCH) {
        return pre_pre_node;
      }
    }
  }
  return nullptr;
}

Status StreamAllocator::SetActiveStreamsForLoop() {
  vector<uint32_t> loop_active_streams;
  for (int64_t stream_id = 0; stream_id < stream_num_; stream_id++) {
    if (specific_activated_streams_.count(stream_id) == 0) {
      loop_active_streams.emplace_back(static_cast<uint32_t>(stream_id));
    }
  }
  map<int64_t, NodePtr> stream_id_to_last_node;
  set<int64_t> streams_skip_iterator_event;
  for (const auto &node : whole_graph_->GetNodes(whole_graph_->GetGraphUnknownFlag())) {
    int64_t stream_id = node->GetOpDesc()->GetStreamId();
    if (find(loop_active_streams.begin(), loop_active_streams.end(), stream_id) != loop_active_streams.end()) {
      stream_id_to_last_node[stream_id] = node;
      // last node in stream which has streamswitch or IF may be not execute, it will cause block if add event on them
      if (node->GetOpDesc()->GetType() == STREAMSWITCH) {
        streams_skip_iterator_event.insert(stream_id);
      }
    }
  }
  // Set the stream that needs to be activated
  for (const auto &node : whole_graph_->GetNodes(whole_graph_->GetGraphUnknownFlag())) {
    GE_CHECK_NOTNULL(node->GetOpDesc());
    bool is_loop_active = false;
    (void)AttrUtils::GetBool(node->GetOpDesc(), ATTR_NAME_IS_LOOP_ACTIVE, is_loop_active);
    if (is_loop_active) {
      vector<string> activated_label_list;

      NodePtr pre_switch_node = FindSwitchNodeBeforeLoopActiveNode(node);
      if (pre_switch_node == nullptr) {
        REPORT_INNER_ERROR("E19999", "Find switch node before loop active node %s fail",
                           node->GetName().c_str());
        GELOGE(FAILED, "[Find][SwitchNode] before loop active node %s failed", node->GetName().c_str());
        return FAILED;
      }

      if (!AttrUtils::GetListStr(node->GetOpDesc(), ATTR_NAME_ACTIVE_LABEL_LIST, activated_label_list) ||
          activated_label_list.empty()) {
        GE_CHK_BOOL_EXEC(AttrUtils::SetListInt(node->GetOpDesc(), ATTR_NAME_ACTIVE_STREAM_LIST, loop_active_streams),
                         REPORT_INNER_ERROR("E19999", "Set Attr:%s fail for op:%s(%s)",
                                            ATTR_NAME_ACTIVE_STREAM_LIST.c_str(),
                                            node->GetName().c_str(), node->GetType().c_str());
                         GELOGE(FAILED, "[Set][Attr] %s fail for op:%s(%s)", ATTR_NAME_ACTIVE_STREAM_LIST.c_str(),
                                node->GetName().c_str(), node->GetType().c_str());
                         return FAILED);
        for (const auto &stream_id : loop_active_streams) {
          GELOGI("Active stream %u for node: %s.", stream_id, node->GetName().c_str());
        }

        // In switch group optimze case, some data input branch may exec slowly.
        // when condition input branch judge false and some switch has no false branch,
        // In this condition, data branch has no synchronize point,
        // it may cause some stream actived by iterator next step when this stream still alive.
        // If above situation happen, active message will lose, cause process block in next iteration.
        // In order to avoid this abnormal happen,
        // add event between each last node and iterator switch node
        GELOGI("there are %zu next iterator target streams has streamswitch node.", streams_skip_iterator_event.size());
        for (auto iter : stream_id_to_last_node) {
          if (streams_skip_iterator_event.find(iter.first) != streams_skip_iterator_event.end()) {
            GELOGI("Skip stream %ld which has streamswitch node when adding event to next iterator active node",
                   iter.first);
            continue;
          }
          if (iter.second->GetOwnerComputeGraph()->GetParentGraph() != nullptr) {
            GELOGI("Skip stream %ld which is last node in subgraph when adding event to next iterator active node",
                   iter.first);
            continue;
          }
          AddSendEventId(iter.second, event_num_);
          AddRecvEventId(pre_switch_node, event_num_);
          event_num_++;
        }

        break;
      }
    }
  }

  return CheckStreamActived();
}

Status StreamAllocator::CheckStreamActived() const {
  for (const auto &node : whole_graph_->GetNodes(whole_graph_->GetGraphUnknownFlag())) {
    GE_CHECK_NOTNULL(node->GetOpDesc());
    vector<uint32_t> active_streams;
    if (AttrUtils::GetListInt(node->GetOpDesc(), ATTR_NAME_ACTIVE_STREAM_LIST, active_streams)) {
      uint32_t stream_id = static_cast<uint32_t>(node->GetOpDesc()->GetStreamId());
      auto iter = find(active_streams.begin(), active_streams.end(), stream_id);
      if (iter != active_streams.end()) {
        REPORT_INNER_ERROR("E19999", "Node:%s(%s) cannot active its own stream %u, check invalid ",
                           node->GetName().c_str(), node->GetType().c_str(), stream_id);
        GELOGE(FAILED, "[Check][Param] Node %s cannot active its own stream %u.", node->GetName().c_str(), stream_id);
        return FAILED;
      }
    }
  }

  return SUCCESS;
}

Status StreamAllocator::ReuseEvent(bool send_to,
    const std::unordered_map<std::string, ge::NodePtr> &name_to_node_map,
    const std::unordered_map<ge::NodePtr, std::vector<std::pair<std::string, uint32_t>>> &node_to_event_id) {
  for (const auto &node_event_id : node_to_event_id) {
    ge::NodePtr curr_node = node_event_id.first;
    NodePtr send_node = send_to ? curr_node : nullptr;
    NodePtr recv_node = send_to ? nullptr : curr_node;
    for (const auto &event_pair : node_event_id.second) {
      auto peer_node_iter = name_to_node_map.find(event_pair.first);
      if (peer_node_iter == name_to_node_map.end()) {
        GELOGE(PARAM_INVALID, "[Get][Node]Name:%s.", event_pair.first.c_str());
        REPORT_INNER_ERROR("E19999", "Failed to find node, name:%s.", event_pair.first.c_str());
        return PARAM_INVALID;
      }
      recv_node = send_to ? peer_node_iter->second : recv_node;
      send_node = send_to ? send_node : peer_node_iter->second;
      GE_CHECK_NOTNULL(send_node);
      GE_CHECK_NOTNULL(recv_node);
      auto event_id = GetIntersection(node_to_send_events_[send_node], node_to_recv_events_[recv_node]);
      uint32_t new_event = event_pair.second + event_num_;
      if (event_id.empty()) {
        GELOGI("[Check][Optimized]Send:%s, recv:%s.", send_node->GetName().c_str(), recv_node->GetName().c_str());
        continue;
      } else if (event_id.size() != 1) {
        GELOGW("[Check][Event]More than one event are found between %s and %s, event num:%zu.",
               send_node->GetName().c_str(), recv_node->GetName().c_str(), event_id.size());
      }
      uint32_t old_event = event_id[0];
      auto reuse_event_id = [] (vector<uint32_t> &event_list, uint32_t old_event, uint32_t new_event) -> void {
        event_list.erase(std::remove(event_list.begin(), event_list.end(), old_event), event_list.end());
        event_list.push_back(new_event);
        return;
      };
      reuse_event_id(node_to_send_events_[send_node], old_event, new_event);
      reuse_event_id(node_to_recv_events_[recv_node], old_event, new_event);
      GELOGI("[Reuse][Event]Replace event successfully, send node:%s, recv node:%s, old id:%u, new id:%u.",
             send_node->GetName().c_str(), recv_node->GetName().c_str(), old_event, new_event);
    }
  }
  return ge::SUCCESS;
}

// Refresh events to reuse events
Status StreamAllocator::RefreshEventsWithReuse() {
  GELOGI("[Refresh][Events]Refresh events with reuse, stream num:%ld, original event num:%u.", stream_num_, event_num_);
  if (event_num_ <= kEventReuseThreshold) {
    GELOGI("[Check][ReuseThreshold]Event used num is %u, less than %u, skip reuse.",
           event_num_, kEventReuseThreshold);
    return SUCCESS;
  }
  std::unordered_map<std::string, NodePtr> name_to_node_map;
  std::unordered_map<NodePtr, std::vector<std::pair<std::string, uint32_t>>> node_to_send;
  std::unordered_map<NodePtr, std::vector<std::pair<std::string, uint32_t>>> node_to_recv;
  Status ret = ParseAllNodeEventMultiplexing(whole_graph_, name_to_node_map, node_to_send, node_to_recv);
  if (ret != SUCCESS) {
    GELOGE(ret, "[Parse][AllNodeEventMultiplexing]Graph:%s.", whole_graph_->GetName().c_str());
    REPORT_INNER_ERROR("E19999", "Failed to parse all node event multiplexing, graph:%s.",
                       whole_graph_->GetName().c_str());
    return ret;
  }
  if (node_to_send.empty() && node_to_recv.empty()) {
    return SUCCESS;
  }

  ret = ReuseEvent(true, name_to_node_map, node_to_send);
  if (ret != SUCCESS) {
    GELOGE(ret, "[Reuse][Event]Phase:Send, graph:%s.", whole_graph_->GetName().c_str());
    REPORT_INNER_ERROR("E19999", "Failed to reuse event, phase:Send, graph:%s.", whole_graph_->GetName().c_str());
    return ret;
  }

  ret = ReuseEvent(false, name_to_node_map, node_to_recv);
  if (ret != SUCCESS) {
    GELOGE(ret, "[Reuse][Event]Phase:Recv, graph:%s.", whole_graph_->GetName().c_str());
    REPORT_INNER_ERROR("E19999", "Failed to reuse event, phase:Recv, graph:%s.", whole_graph_->GetName().c_str());
    return ret;
  }

  Status status = RefreshContinuousEvents();
  if (status != SUCCESS) {
    GELOGE(status, "[Refresh][ContinuousEvents]Graph:%s.", whole_graph_->GetName().c_str());
    REPORT_INNER_ERROR("E19999", "Failed to refresh continuous events, graph:%s.", whole_graph_->GetName().c_str());
    return status;
  }
  GELOGI("[Refresh][Events]RefreshEventsWithReuse successfully, event num:%u.", event_num_);
  return SUCCESS;
}

// Refresh events to continuous events
Status StreamAllocator::RefreshContinuousEvents() {
  // Establish a mapping relationship from old to new event id
  map<uint32_t, uint32_t> old_to_new_events;
  uint32_t new_event_id = 0;
  for (const auto &one_pair : node_to_send_events_) {
    for (const auto &event_id : one_pair.second) {
      if (old_to_new_events.find(event_id) == old_to_new_events.end()) {
        old_to_new_events[event_id] = new_event_id;
        new_event_id++;
      }
    }
  }

  // Refresh send event id
  for (auto &one_pair : node_to_send_events_) {
    vector<uint32_t> &send_events = one_pair.second;
    for (size_t i = 0; i < send_events.size(); i++) {
      auto find_it = old_to_new_events.find(send_events[i]);
      if (find_it == old_to_new_events.end()) {
        REPORT_INNER_ERROR("E19999", "Check invalid send event %u", send_events[i]);
        GELOGE(FAILED, "[Check][Param] RefreshContinuousEvents: invalid send event %u", send_events[i]);
        return FAILED;
      }
      send_events[i] = find_it->second;
    }
  }

  // Refresh recv event id
  for (auto &one_pair : node_to_recv_events_) {
    vector<uint32_t> &recv_events = one_pair.second;
    for (size_t i = 0; i < recv_events.size(); i++) {
      auto find_it = old_to_new_events.find(recv_events[i]);
      if (find_it == old_to_new_events.end()) {
        REPORT_INNER_ERROR("E19999", "Check invalid recv event %u", recv_events[i]);
        GELOGE(FAILED, "[Check][Param] RefreshContinuousEvents: invalid recv event %u", recv_events[i]);
        return FAILED;
      }
      recv_events[i] = find_it->second;
    }
  }

  event_num_ = static_cast<uint32_t>(old_to_new_events.size());

  return SUCCESS;
}

// Insert the real send/recv node in the graph
Status StreamAllocator::InsertSyncEventNodes() {
  unordered_map<string, uint32_t> sync_event_name;
  for (const auto &node : whole_graph_->GetNodes(whole_graph_->GetGraphUnknownFlag())) {
    // Add the node corresponding to the recv event
    vector<uint32_t> recv_event_id_list;
    GetRecvEventIdList(node, recv_event_id_list);
    GE_CHECK_NOTNULL(node->GetOpDesc());
    GE_CHECK_NOTNULL(node->GetInControlAnchor());
    GE_CHECK_NOTNULL(node->GetOutControlAnchor());
    for (auto &event_id : recv_event_id_list) {
      string recv_node_name = whole_graph_->GetName() + "_Recv_" + to_string(event_id);
      auto iter = sync_event_name.find(recv_node_name);
      if (iter == sync_event_name.end()) {
          sync_event_name[recv_node_name] = 1;
      } else {
          recv_node_name = recv_node_name + "_Reuse_" + to_string(iter->second);
          ++(iter->second);
      }
      OpDescPtr op_desc_ptr = MakeShared<OpDesc>(recv_node_name, RECV);
      GE_CHECK_NOTNULL(op_desc_ptr);

      int64_t temp_stream_id = node->GetOpDesc()->GetStreamId();
      op_desc_ptr->SetStreamId(temp_stream_id);
      GE_CHK_BOOL_EXEC(AttrUtils::SetInt(op_desc_ptr, RECV_ATTR_EVENT_ID, event_id),
                       REPORT_INNER_ERROR("E19999", "Set Attr:%s for op:%s(%s) failed, event_id:%u,",
                                          RECV_ATTR_EVENT_ID.c_str(),
                                          node->GetName().c_str(), node->GetType().c_str(), event_id);
                       GELOGE(FAILED, "[Set][Attr] %s for op:%s(%s) failed, event_id:%u,",
                              RECV_ATTR_EVENT_ID.c_str(), node->GetName().c_str(), node->GetType().c_str(), event_id);
                       return FAILED);
      (void)AttrUtils::SetListStr(op_desc_ptr, ATTR_NAME_DATA_DUMP_ORIGIN_OP_NAMES,
                                  std::move(std::vector<std::string>()));
      NodePtr recv_node = node->GetOwnerComputeGraph()->AddNode(op_desc_ptr);
      GE_CHECK_NOTNULL(recv_node);
      GE_CHECK_NOTNULL(recv_node->GetOutControlAnchor());
      Status status = GraphUtils::AddEdge(recv_node->GetOutControlAnchor(), node->GetInControlAnchor());
      if (status != SUCCESS) {
        REPORT_INNER_ERROR("E19999", "Add edge from node %s to node %s failed",
                           recv_node->GetName().c_str(), node->GetName().c_str());
        GELOGE(status, "[Add][Edge] for node %s and node %s failed.", recv_node->GetName().c_str(),
               node->GetName().c_str());
        return status;
      }

      GELOGI("Insert recv event %u before node: %s.", event_id, node->GetName().c_str());
    }

    // Add the node corresponding to the send event
    vector<uint32_t> send_event_id_list;
    GetSendEventIdList(node, send_event_id_list);

    for (auto &event_id : send_event_id_list) {
      string send_node_name = whole_graph_->GetName() + "_Send_" + to_string(event_id);
      auto iter = sync_event_name.find(send_node_name);
      if (iter == sync_event_name.end()) {
          sync_event_name[send_node_name] = 1;
      } else {
        send_node_name = send_node_name + "_Reuse_" + to_string(iter->second);
          ++(iter->second);
      }
      OpDescPtr op_desc_ptr = MakeShared<OpDesc>(send_node_name, SEND);
      GE_CHECK_NOTNULL(op_desc_ptr);

      int64_t temp_stream_id = node->GetOpDesc()->GetStreamId();
      op_desc_ptr->SetStreamId(temp_stream_id);
      GE_CHK_BOOL_EXEC(AttrUtils::SetInt(op_desc_ptr, SEND_ATTR_EVENT_ID, event_id), GELOGE(FAILED, "SetInt failed.");
                       return FAILED);
      (void)AttrUtils::SetListStr(op_desc_ptr, ATTR_NAME_DATA_DUMP_ORIGIN_OP_NAMES,
                                  std::move(std::vector<std::string>()));
      NodePtr send_node = node->GetOwnerComputeGraph()->AddNode(op_desc_ptr);
      GE_CHECK_NOTNULL(send_node);
      GE_CHECK_NOTNULL(send_node->GetInControlAnchor());
      Status status = GraphUtils::AddEdge(node->GetOutControlAnchor(), send_node->GetInControlAnchor());
      if (status != SUCCESS) {
        REPORT_INNER_ERROR("E19999", "Add edge from node %s to node %s failed",
                           node->GetName().c_str(), send_node->GetName().c_str());
        GELOGE(status, "[Add][Edge] for node %s and node %s failed.", node->GetName().c_str(),
               send_node->GetName().c_str());
        return status;
      }

      GELOGI("Insert send event %u after node: %s.", event_id, node->GetName().c_str());
    }
  }

  Status status = whole_graph_->InsertGraphEvents();
  if (status != SUCCESS) {
    REPORT_CALL_ERROR("E19999", "Insert Graph Events fail, graph:%s,",
                      whole_graph_->GetName().c_str());
    GELOGE(status, "[Insert][GraphEvents] Graph ReorderEventNodes failed, graph:%s,",
           whole_graph_->GetName().c_str());
    return status;
  }

  return SUCCESS;
}

void StreamAllocator::DumpEvents() {
  map<int64_t, vector<NodePtr>> after_refresh_stream_nodes;
  for (const auto &node : whole_graph_->GetNodes(whole_graph_->GetGraphUnknownFlag())) {
    GE_IF_BOOL_EXEC(node->GetOpDesc() == nullptr, continue);
    int64_t stream_id = node->GetOpDesc()->GetStreamId();
    after_refresh_stream_nodes[stream_id].emplace_back(node);
  }

  for (const auto &one_pair : after_refresh_stream_nodes) {
    int64_t stream_id = one_pair.first;
    GELOGD("After RefreshRealStream: stream %ld.", stream_id);

    for (const auto &node : one_pair.second) {
      if (node == nullptr || node->GetOpDesc() == nullptr) {
        continue;
      }
      string send_event_str;
      for (const auto &send_event_id : node_to_send_events_[node]) {
        send_event_str += " " + to_string(send_event_id);
      }
      if (!send_event_str.empty()) {
        GELOGI("node: %s, id: %ld, stream id :%ld, send events: %s.", node->GetName().c_str(),
               node->GetOpDesc()->GetId(), node->GetOpDesc()->GetStreamId(), send_event_str.c_str());
      }

      string recv_event_str;
      for (const auto &recv_event_id : node_to_recv_events_[node]) {
        recv_event_str += " " + to_string(recv_event_id);
      }
      if (!recv_event_str.empty()) {
        GELOGI("node: %s, id: %ld, stream id :%ld, recv events: %s.", node->GetName().c_str(),
               node->GetOpDesc()->GetId(), node->GetOpDesc()->GetStreamId(), recv_event_str.c_str());
      }
    }
  }
}

Status StreamAllocator::GetMaxStreamAndTask(bool huge_stream, uint32_t &max_stream_count, uint32_t &max_task_count) {
  uint32_t stream_type = RT_NORMAL_STREAM;
  if (huge_stream) {
    stream_type = RT_HUGE_STREAM;
  }
  rtError_t ret = rtGetMaxStreamAndTask(stream_type, &max_stream_count, &max_task_count);
  if (ret != RT_ERROR_NONE) {
    REPORT_CALL_ERROR("E19999", "call rtGetMaxStreamAndTask fail, ret:%d, stream_type:%u,",
                      static_cast<int>(ret), stream_type);
    GELOGE(FAILED, "[Call][RtGetMaxStreamAndTask] Get max stream and task count by rts failed, "
           "ret:%d, stream_type:%u,", static_cast<int>(ret), stream_type);
    return FAILED;
  }
  GELOGD("Allowed max stream count: %u, max task count per stream: %u.", max_stream_count, max_task_count);

  return SUCCESS;
}

int64_t StreamAllocator::GetMaxNodeNumPerStream(const NodePtr &node, uint32_t max_task_count) {
  int64_t max_node_num_one_stream = static_cast<int64_t>(max_task_count);
  string op_type = node->GetType();
  if (IsHcclOp(op_type)) {
    max_node_num_one_stream /= kTaskNumPerHcclNode;
  } else {
    max_node_num_one_stream /= kTaskNumPerNormalNode;
  }
  if (max_node_num_one_stream == 0) {
    max_node_num_one_stream = 1;
  }

  return max_node_num_one_stream;
}

void StreamAllocator::AddNodeNum(const NodePtr &node, int64_t &node_num) {
  node_num++;
  vector<uint32_t> events;
  GetSendEventIdList(node, events);
  node_num += static_cast<int64_t>(events.size());
  GetRecvEventIdList(node, events);
  node_num += static_cast<int64_t>(events.size());
}

// Insert send event id on a node
void StreamAllocator::AddSendEventId(const NodePtr &node, uint32_t event_id) {
  node_to_send_events_[node].emplace_back(event_id);
}

// Insert recv event id on a node
void StreamAllocator::AddRecvEventId(const NodePtr &node, uint32_t event_id) {
  node_to_recv_events_[node].emplace_back(event_id);
}

// Remove send event id from a node
void StreamAllocator::RmvSendEventId(const NodePtr &node, uint32_t event_id) {
  auto find_it = node_to_send_events_.find(node);
  if (find_it == node_to_send_events_.end()) {
    return;
  }

  vector<uint32_t> &send_events = find_it->second;
  for (auto it = send_events.begin(); it != send_events.end(); ++it) {
    if (*it == event_id) {
      send_events.erase(it);
      return;
    }
  }
}

// Remove recv event id from a node
void StreamAllocator::RmvRecvEventId(const NodePtr &node, uint32_t event_id) {
  auto find_it = node_to_recv_events_.find(node);
  if (find_it == node_to_recv_events_.end()) {
    return;
  }

  vector<uint32_t> &recv_events = find_it->second;
  for (auto it = recv_events.begin(); it != recv_events.end(); ++it) {
    if (*it == event_id) {
      recv_events.erase(it);
      return;
    }
  }
}

// Get send event id list from a node
void StreamAllocator::GetSendEventIdList(const NodePtr &node, vector<uint32_t> &send_list) const {
  send_list.clear();
  auto find_it = node_to_send_events_.find(node);
  if (find_it != node_to_send_events_.end()) {
    send_list = find_it->second;
  }
}

// Get recv event id list from a node
void StreamAllocator::GetRecvEventIdList(const NodePtr &node, vector<uint32_t> &recv_list) const {
  recv_list.clear();
  auto find_it = node_to_recv_events_.find(node);
  if (find_it != node_to_recv_events_.end()) {
    recv_list = find_it->second;
  }
}

// Get a specific send node according to the recv event
NodePtr StreamAllocator::GetNodeFromSendEventId(uint32_t send_event_id) const {
  for (const auto &one_pair : node_to_send_events_) {
    const vector<uint32_t> &events = one_pair.second;
    for (const auto &event_id : events) {
      if (event_id == send_event_id) {
        return one_pair.first;
      }
    }
  }

  return nullptr;
}

// Get a specific recv node according to the recv event
NodePtr StreamAllocator::GetNodeFromRecvEventId(uint32_t recv_event_id) const {
  for (const auto &one_pair : node_to_recv_events_) {
    const vector<uint32_t> &events = one_pair.second;
    for (const auto &event_id : events) {
      if (event_id == recv_event_id) {
        return one_pair.first;
      }
    }
  }

  return nullptr;
}

Status StreamAllocator::AddEventId(const NodePtr &pre_node, const NodePtr &not_cur, const NodePtr &cur_node,
                                   bool not_use_cur) {
  GELOGI("Add send event %u for node %s", event_num_, pre_node->GetName().c_str());
  AddSendEventId(pre_node, event_num_);
  if (not_use_cur) {
    GE_CHECK_NOTNULL(not_cur);
    GELOGI("Add recv event %u for node %s", event_num_, not_cur->GetName().c_str());
    AddRecvEventId(not_cur, event_num_);
  } else {
    GELOGI("Add recv event %u for node %s", event_num_, cur_node->GetName().c_str());
    AddRecvEventId(cur_node, event_num_);
  }
  ++event_num_;
  return SUCCESS;
}

Status StreamAllocator::AddActiveNodes(NodePtr &switch_node, const vector<string> &ori_active_label_list,
                                       vector<string> &active_label_list, vector<NodePtr> &added_active_nodes) {
  size_t label_num = ori_active_label_list.size();
  for (size_t i = 0; i < label_num; i++) {
    const string &active_label = ori_active_label_list[i];
    if (labeled_streams_.find(active_label) == labeled_streams_.end()) {
      REPORT_INNER_ERROR("E19999", "can not find stream label:%s", active_label.c_str());
      GELOGE(FAILED, "[Check][Param] can not find stream label %s", active_label.c_str());
      return FAILED;
    }
    if (labeled_streams_[active_label].size() <= 1) {
      active_label_list.emplace_back(active_label);
      continue;
    }

    string name = switch_node->GetName() + "_" + STREAMACTIVE + "_" + std::to_string(i);
    GELOGI("Create StreamActive op %s after node %s.", name.c_str(), switch_node->GetName().c_str());
    OpDescPtr active_op_desc = MakeShared<OpDesc>(name, STREAMACTIVE);
    GE_CHECK_NOTNULL(active_op_desc);
    NodePtr active_node = whole_graph_->AddNode(active_op_desc);
    GE_CHECK_NOTNULL(active_node);

    for (NodePtr &node : switch_node->GetOutControlNodes()) {
      OpDescPtr op_desc = node->GetOpDesc();
      GE_CHECK_NOTNULL(op_desc);
      string stream_label;
      // If GetStr failed, stream_label is empty.
      (void)AttrUtils::GetStr(op_desc, ATTR_NAME_STREAM_LABEL, stream_label);
      if (stream_label != active_label) {
        continue;
      }
      GE_CHECK_NOTNULL(switch_node->GetOutControlAnchor());
      if (switch_node->GetOutControlAnchor()->Unlink(node->GetInControlAnchor()) != GRAPH_SUCCESS) {
        REPORT_CALL_ERROR("E19999", "Unlink %s to %s failed",
                          switch_node->GetName().c_str(), node->GetName().c_str());
        GELOGE(FAILED, "[Unlink][Nodes] %s to %s failed.", switch_node->GetName().c_str(), node->GetName().c_str());
        return FAILED;
      }
      GE_CHECK_NOTNULL(active_node->GetOutControlAnchor());
      if (active_node->GetOutControlAnchor()->LinkTo(node->GetInControlAnchor()) != GRAPH_SUCCESS) {
        REPORT_CALL_ERROR("E19999", "Link %s to %s failed",
                          active_node->GetName().c_str(), node->GetName().c_str());
        GELOGE(FAILED, "[Link][Nodes] %s to %s failed.", active_node->GetName().c_str(), node->GetName().c_str());
        return FAILED;
      }
    }

    if (SetSwitchBranchNodeLabel(active_node, name) != SUCCESS) {
      GELOGE(FAILED, "[Set][SwitchBranchNodeLabel] failed, node:%s.", active_node->GetName().c_str());
      return FAILED;
    }
    if (SetStreamLabel(active_node, name) != SUCCESS) {
      GELOGE(FAILED, "[Set][StreamLabel] failed, node:%s.", active_node->GetName().c_str());
      return FAILED;
    }
    if (SetActiveLabelList(active_node, {active_label}) != SUCCESS) {
      GELOGE(FAILED, "[Set][ActiveLabelList] failed, node:%s.", active_node->GetName().c_str());
      return FAILED;
    }
    if (SetActiveStreamList(active_node, active_label) != SUCCESS) {
      GELOGE(FAILED, "[Set][ActiveStreamList] failed, node:%s.", active_node->GetName().c_str());
      return FAILED;
    }

    added_active_nodes.emplace_back(active_node);
    active_label_list.emplace_back(name);
  }
  return SUCCESS;
}

Status StreamAllocator::SetActiveStreamList(NodePtr &active_node, const string &active_label) {
  if (labeled_streams_.find(active_label) == labeled_streams_.end()) {
    REPORT_INNER_ERROR("E19999", "Can not find stream label:%s", active_label.c_str());
    GELOGE(FAILED, "[Check][Param] Can not find stream label %s.", active_label.c_str());
    return FAILED;
  }
  set<int64_t> &streams = labeled_streams_[active_label];
  vector<int64_t> active_streams(streams.begin(), streams.end());
  if (!AttrUtils::SetListInt(active_node->GetOpDesc(), ATTR_NAME_ACTIVE_STREAM_LIST, active_streams)) {
    REPORT_INNER_ERROR("E19999", "Set Attr:%s fail for op:%s(%s)", ATTR_NAME_ACTIVE_STREAM_LIST.c_str(),
                       active_node->GetName().c_str(), active_node->GetType().c_str());
    GELOGE(FAILED, "[Set][Attr] %s failed for op:%s(%s).", ATTR_NAME_ACTIVE_STREAM_LIST.c_str(),
           active_node->GetName().c_str(), active_node->GetType().c_str());
    return FAILED;
  }
  return SUCCESS;
}
}  // namespace ge
