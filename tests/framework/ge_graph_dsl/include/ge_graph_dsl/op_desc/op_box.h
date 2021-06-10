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

#ifndef H6DCF6C61_7C9B_4048_BB5D_E748142FF7F8
#define H6DCF6C61_7C9B_4048_BB5D_E748142FF7F8

#include "ge_graph_dsl/ge.h"
#include "easy_graph/graph/node_id.h"
#include "easy_graph/graph/box.h"
#include "external/graph/gnode.h"

GE_NS_BEGIN

struct OpBox : ::EG_NS::Box {
  ABSTRACT(OpDescPtr Build(const ::EG_NS::NodeId &) const);
};

GE_NS_END

#endif /* H6DCF6C61_7C9B_4048_BB5D_E748142FF7F8 */
