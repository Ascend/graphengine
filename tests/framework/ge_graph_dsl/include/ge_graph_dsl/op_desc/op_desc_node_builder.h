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

#ifndef H53F1A984_1D06_4458_9595_0A6DC60EA9CE
#define H53F1A984_1D06_4458_9595_0A6DC60EA9CE

#include "easy_graph/graph/node.h"
#include "ge_graph_dsl/ge.h"
#include "ge_graph_dsl/op_desc/op_desc_ptr_box.h"
#include "ge_graph_dsl/op_desc/op_desc_cfg_box.h"
#include "graph/op_desc.h"

using ::EG_NS::Node;
using ::EG_NS::NodeId;

GE_NS_BEGIN

inline const ::EG_NS::NodeId OpDescNodeBuild(const ::EG_NS::NodeId &id) {
  return id;
}

template<typename... GRAPHS, SUBGRAPH_CONCEPT(GRAPHS, ::EG_NS::Graph)>
inline ::EG_NS::Node OpDescNodeBuild(const ::EG_NS::NodeId &id, const GRAPHS &... graphs) {
  return ::EG_NS::Node(id, graphs...);
}

template<typename... GRAPHS, SUBGRAPH_CONCEPT(GRAPHS, ::EG_NS::Graph)>
inline ::EG_NS::Node OpDescNodeBuild(const OpDescPtr &op, const GRAPHS &... graphs) {
  return ::EG_NS::Node(op->GetName(), BOX_OF(::GE_NS::OpDescPtrBox, op), graphs...);
}

template<typename... GRAPHS, SUBGRAPH_CONCEPT(GRAPHS, ::EG_NS::Graph)>
inline ::EG_NS::Node OpDescNodeBuild(const ::EG_NS::NodeId &id, const OpType &opType, const GRAPHS &... graphs) {
  return ::EG_NS::Node(id, BOX_OF(OpDescCfgBox, opType), graphs...);
}

template<typename... GRAPHS, SUBGRAPH_CONCEPT(GRAPHS, ::EG_NS::Graph)>
inline ::EG_NS::Node OpDescNodeBuild(const ::EG_NS::NodeId &id, const OpDescCfgBox &opBox, const GRAPHS &... graphs) {
  return ::EG_NS::Node(id, BOX_OF(OpDescCfgBox, opBox), graphs...);
}

GE_NS_END

#endif /* H53F1A984_1D06_4458_9595_0A6DC60EA9CE */
