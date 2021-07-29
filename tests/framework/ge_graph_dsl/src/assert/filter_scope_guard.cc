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

#include "ge_graph_dsl/assert/filter_scope_guard.h"
#include "graph/utils/dumper/ge_graph_dumper.h"
#include "ge_dump_filter.h"

GE_NS_BEGIN

namespace {
GeDumpFilter &GetDumpFilter() { return dynamic_cast<GeDumpFilter &>(GraphDumperRegistry::GetDumper()); }
}  // namespace

FilterScopeGuard::FilterScopeGuard(const std::vector<std::string> &filter) { GetDumpFilter().Update(filter); }

FilterScopeGuard::~FilterScopeGuard() { GetDumpFilter().Reset(); }

GE_NS_END