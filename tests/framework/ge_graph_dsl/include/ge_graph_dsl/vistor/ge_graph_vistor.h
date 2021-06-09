/**
 * Copyright 2021 Huawei Technologies Co., Ltd
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

#ifndef HD31125D4_0EB8_494C_B83D_3B8B923A914D
#define HD31125D4_0EB8_494C_B83D_3B8B923A914D

#include "easy_graph/graph/graph_visitor.h"
#include "graph/compute_graph.h"
#include "external/graph/graph.h"
#include "ge_graph_dsl/ge.h"

GE_NS_BEGIN

struct GeGraphVisitor : ::EG_NS::GraphVisitor {
  GeGraphVisitor();
  void reset(const ComputeGraphPtr &graph);
  Graph BuildGeGraph() const;
  ComputeGraphPtr BuildComputeGraph() const;

 private:
  ::EG_NS::Status Visit(const ::EG_NS::Graph &) override;
  ::EG_NS::Status Visit(const ::EG_NS::Node &) override;
  ::EG_NS::Status Visit(const ::EG_NS::Edge &) override;

 private:
  ComputeGraphPtr build_graph_;
};

GE_NS_END

#endif /* TESTS_ST_EASY_GRAPH_GELAYOUT_GRAPH_GE_VISTOR_H_ */
