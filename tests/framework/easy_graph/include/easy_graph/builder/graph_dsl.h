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

#ifndef H46D07001_D54E_497C_B1BA_878A47164DA5
#define H46D07001_D54E_497C_B1BA_878A47164DA5

#include "easy_graph/builder/graph_builder.h"
#include "easy_graph/builder/chain_builder.h"
#include "easy_graph/builder/box_builder.h"
#include "easy_graph/infra/macro_traits.h"

EG_NS_BEGIN

////////////////////////////////////////////////////////////////
namespace detail {
template <typename GRAPH_BUILDER>
Graph BuildGraph(const char *name, GRAPH_BUILDER builderInDSL) {
  GraphBuilder builder(name);
  builderInDSL(builder);
  return std::move(*builder);
}

struct GraphDefiner {
  GraphDefiner(const char *defaultName, const char *specifiedName = nullptr) {
    name = specifiedName ? specifiedName : defaultName;
  }

  template <typename USER_BUILDER>
  auto operator|(USER_BUILDER &&userBuilder) {
    GraphBuilder graphBuilder{name};
    std::forward<USER_BUILDER>(userBuilder)(graphBuilder);
    return *graphBuilder;
  }

 private:
  const char *name;
};

}  // namespace detail

#define DEF_GRAPH(G, ...) ::EG_NS::Graph G = ::EG_NS::detail::GraphDefiner(#G, ##__VA_ARGS__) | [&](auto &&BUILDER)
#define DATA_CHAIN(...) ::EG_NS::ChainBuilder(BUILDER, ::EG_NS::EdgeType::DATA)->__VA_ARGS__
#define CTRL_CHAIN(...) ::EG_NS::ChainBuilder(BUILDER, ::EG_NS::EdgeType::CTRL)->__VA_ARGS__
#define CHAIN(...) DATA_CHAIN(__VA_ARGS__)

EG_NS_END

#endif
