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

#include "framework/memory/memory_assigner.h"
#include <memory>
#include "framework/common/debug/ge_log.h"
#include "graph/build/memory/graph_mem_assigner.h"

namespace ge {
Status MemoryAssigner::AssignMemory(bool is_loop_graph, map<uint64_t, size_t> &mem_offset, size_t &zero_copy_mem_size) {
  GraphMemoryAssigner graph_mem_assigner(compute_graph_);

  if (graph_mem_assigner.AssignMemory() != ge::SUCCESS) {
    GELOGE(ge::FAILED, "[Assign][Memory] failed, graph:%s", compute_graph_->GetName().c_str());
    return ge::FAILED;
  }

  // Reassign memory for special nodes
  Status ret = graph_mem_assigner.ReAssignMemory(is_loop_graph, mem_offset);
  if (ret != ge::SUCCESS) {
    GELOGE(ge::FAILED, "[ReAssign][Memory] failed, graph:%s", compute_graph_->GetName().c_str());
    return ret;
  }

  // Assign memory (block and offset) for zero copy nodes
  if (graph_mem_assigner.AssignZeroCopyMemory(mem_offset, zero_copy_mem_size) != ge::SUCCESS) {
    GELOGE(ge::FAILED, "[Assign][ZeroCopyMemory] failed, graph:%s", compute_graph_->GetName().c_str());
    return ge::FAILED;
  }

  if (graph_mem_assigner.AssignMemory2HasRefAttrNode() != ge::SUCCESS) {
    GELOGE(ge::FAILED, "[Assign][Memory] to node which has ref attr failed! graph:%s",
           compute_graph_->GetName().c_str());
    return ge::FAILED;
  }

  // Assign memory for reference
  if (graph_mem_assigner.AssignReferenceMemory() != ge::SUCCESS) {
    GELOGE(ge::FAILED, "[Assign][ReferenceMemory] failed! graph:%s", compute_graph_->GetName().c_str());
    return ge::FAILED;
  }

  // Must do variable attr assign after all the memory assigned
  if (graph_mem_assigner.AssignVarAttr2Nodes() != SUCCESS) {
    GELOGE(FAILED, "[Variable][Memory] assigner failed, graph:%s", compute_graph_->GetName().c_str());
    return FAILED;
  }
  if (graph_mem_assigner.SetInputOffset() != ge::SUCCESS) {
    GELOGE(ge::FAILED, "[Set][InputOffset] Fail! graph:%s", compute_graph_->GetName().c_str());
    return ge::FAILED;
  }

  if (graph_mem_assigner.CheckOffset() != SUCCESS) {
    GELOGE(FAILED, "[Check][Offset] Fail! graph:%s", compute_graph_->GetName().c_str());
    return FAILED;
  }

  graph_mem_assigner.MarkDistanceAttr();
  return SUCCESS;
}
}  // namespace ge
