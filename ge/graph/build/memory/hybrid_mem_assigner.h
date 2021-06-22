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

#ifndef GE_GRAPH_BUILD_MEMORY_HYBRID_MEM_ASSIGNER_H_
#define GE_GRAPH_BUILD_MEMORY_HYBRID_MEM_ASSIGNER_H_

#include <memory>
#include "graph/build/memory/mem_assigner.h"
#include "graph/build/memory/block_mem_assigner.h"
#include "graph/compute_graph.h"

#include "framework/common/types.h"
#include "framework/common/util.h"

namespace ge {
using BlockMemAssignerPtr = std::shared_ptr<BlockMemAssigner>;

class BlockMemAssigner;

class HybridMemAssigner : public MemAssigner {
 public:
  explicit HybridMemAssigner(ge::ComputeGraphPtr compute_graph);

  HybridMemAssigner(const HybridMemAssigner &) = delete;

  HybridMemAssigner &operator=(const HybridMemAssigner &) = delete;

  ~HybridMemAssigner() override = default;

  Status Assign() override;

  const std::map<uint64_t, size_t> &GetMemOffsets() const { return mem_offsets_; }

  BlockMemAssignerPtr GetPriorityAssinger() const { return priority_assigner_; }

 private:
  Status AssignMemory(std::unique_ptr<BlockMemAssigner> &block_assigner, size_t &mem_size);

  std::map<uint64_t, size_t> mem_offsets_;

  ge::ComputeGraphPtr compute_graph_;

  BlockMemAssignerPtr priority_assigner_;

  std::map<std::string, std::string> anchor_to_symbol_;
  std::map<std::string, std::list<NodeIndexIO>> symbol_to_anchors_;
};
}  // namespace ge
#endif  // GE_GRAPH_BUILD_MEMORY_HYBRID_MEM_ASSIGNER_H_
