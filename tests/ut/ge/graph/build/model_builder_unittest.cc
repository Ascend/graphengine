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

#include <gtest/gtest.h>
#include <memory>

#include "common/local_context.h"
#include "graph/anchor.h"
#include "graph/attr_value.h"
#include "graph/debug/ge_attr_define.h"
#include "graph/utils/graph_utils.h"
#include "graph/utils/node_utils.h"
#include "graph/utils/op_desc_utils.h"
#include "graph/utils/tensor_utils.h"
#include "omg/omg_inner_types.h"
#include "../passes/graph_builder_utils.h"

#define protected public
#define private public
#include "graph/build/model_builder.h"
#include "memory/memory_assigner.h"
#undef protected
#undef private

using namespace std;
using namespace testing;
using namespace ge;
using domi::GetContext;

class UtestModelBuilderTest : public testing::Test {
 public:
  ge::OpDescPtr CreateOpWithWsSize(const string &name, int64_t wsByte, const string &type = "some") {
    ge::OpDescPtr op_def = make_shared<ge::OpDesc>(name, type);
    auto desc_temp_ptr = make_shared<ge::GeTensorDesc>();
    auto desc_temp = *desc_temp_ptr;

    TensorUtils::SetSize(desc_temp, 1024);
    op_def->AddInputDesc(desc_temp);
    op_def->AddOutputDesc(desc_temp);

    std::vector<int64_t> workspace_bytes;
    workspace_bytes.push_back(wsByte);
    op_def->SetWorkspaceBytes(workspace_bytes);
    return op_def;
  }
  ge::OpDescPtr CreateRefOpWithWsSize(const string &name, int64_t wsByte, const string &type = "some") {
    ge::OpDescPtr op_def = make_shared<ge::OpDesc>(name, type);
    auto desc_temp_ptr = make_shared<ge::GeTensorDesc>();
    auto desc_temp = *desc_temp_ptr;

    TensorUtils::SetSize(desc_temp, 1024);
    op_def->AddInputDesc(desc_temp);

    auto desc_output_ptr = make_shared<ge::GeTensorDesc>();
    auto desc_output = *desc_output_ptr;
    TensorUtils::SetSize(desc_output, 6500);
    ge::TensorUtils::SetReuseInput(desc_output, true);
    ge::TensorUtils::SetReuseInputIndex(desc_output, 0);
    op_def->AddOutputDesc(desc_output);

    std::vector<int64_t> workspace_bytes;
    workspace_bytes.push_back(wsByte);
    op_def->SetWorkspaceBytes(workspace_bytes);
    return op_def;
  }
  void MakeGraph(ge::ComputeGraphPtr &graph) {
    ge::OpDescPtr op_def_a = CreateOpWithWsSize("A", 6000);
    op_def_a->SetStreamId(0);
    ge::OpDescPtr op_def_b = CreateOpWithWsSize("B", 120000);
    op_def_b->SetStreamId(0);
    ge::OpDescPtr op_def_c = CreateOpWithWsSize("C", 16000);
    op_def_c->SetStreamId(1);
    ge::OpDescPtr op_def_d = CreateOpWithWsSize("D", 24000);
    op_def_d->SetStreamId(2);
    ge::OpDescPtr op_def_e = CreateOpWithWsSize("E", 24000);
    op_def_e->SetStreamId(3);
    ge::OpDescPtr op_def_f = CreateOpWithWsSize("F", 30000);
    op_def_f->SetStreamId(2);
    ge::OpDescPtr op_def_g = CreateOpWithWsSize("G", 32000);
    op_def_g->SetStreamId(3);
    ge::OpDescPtr op_def_h = CreateOpWithWsSize("H", 48000);
    op_def_h->SetStreamId(2);
    ge::OpDescPtr op_def_i = CreateOpWithWsSize("I", 60000);
    op_def_i->SetStreamId(2);
    ge::OpDescPtr op_def_j = CreateOpWithWsSize("J", 256000, NETOUTPUT);
    op_def_j->SetStreamId(3);

    // add node
    ge::NodePtr node_a = graph->AddNode(op_def_a);
    ge::NodePtr node_b = graph->AddNode(op_def_b);
    ge::NodePtr node_c = graph->AddNode(op_def_c);
    ge::NodePtr node_d = graph->AddNode(op_def_d);
    ge::NodePtr node_e = graph->AddNode(op_def_e);
    ge::NodePtr node_f = graph->AddNode(op_def_f);
    ge::NodePtr node_g = graph->AddNode(op_def_g);
    ge::NodePtr node_h = graph->AddNode(op_def_h);
    ge::NodePtr node_i = graph->AddNode(op_def_i);
    ge::NodePtr node_j = graph->AddNode(op_def_j);

    // add edge
    ge::GraphUtils::AddEdge(node_a->GetOutDataAnchor(0), node_b->GetInDataAnchor(0));
    ge::GraphUtils::AddEdge(node_a->GetOutDataAnchor(0), node_c->GetInDataAnchor(0));
    ge::GraphUtils::AddEdge(node_b->GetOutDataAnchor(0), node_d->GetInDataAnchor(0));
    ge::GraphUtils::AddEdge(node_b->GetOutDataAnchor(0), node_e->GetInDataAnchor(0));
    ge::GraphUtils::AddEdge(node_c->GetOutDataAnchor(0), node_g->GetInDataAnchor(0));
    ge::GraphUtils::AddEdge(node_d->GetOutDataAnchor(0), node_f->GetInDataAnchor(0));
    ge::GraphUtils::AddEdge(node_e->GetOutDataAnchor(0), node_g->GetInDataAnchor(1));
    ge::GraphUtils::AddEdge(node_f->GetOutDataAnchor(0), node_h->GetInDataAnchor(0));
    ge::GraphUtils::AddEdge(node_g->GetOutDataAnchor(0), node_j->GetInDataAnchor(0));
    ge::GraphUtils::AddEdge(node_h->GetOutDataAnchor(0), node_i->GetInDataAnchor(0));
    ge::GraphUtils::AddEdge(node_i->GetOutDataAnchor(0), node_j->GetInDataAnchor(1));

    GetContext().out_nodes_map["H"] = {0};
    GetContext().out_nodes_map["I"] = {0};
    GetContext().out_nodes_map["J"] = {0};
    graph->TopologicalSorting();
  }

void MakeSessionScopeReuseGraph(ge::ComputeGraphPtr graph) {
    ge::OpDescPtr op_def_a = CreateOpWithWsSize("A", 512);
    ge::OpDescPtr op_def_b = CreateOpWithWsSize("B", 0);
    ge::OpDescPtr op_def_c = CreateOpWithWsSize("C", 512);
    ge::OpDescPtr op_def_d = CreateOpWithWsSize("D", 512);
    ge::OpDescPtr op_def_e = CreateOpWithWsSize("E", 1024);
    ge::OpDescPtr op_def_f = CreateOpWithWsSize("F", 512);
    ge::OpDescPtr op_def_g = CreateOpWithWsSize("G", 0);

    std::vector<int64_t> workspace_bytes;
    workspace_bytes.push_back(1024);
    workspace_bytes.push_back(512);
    op_def_c->SetWorkspaceBytes(workspace_bytes);
    vector<int32_t> workspace_no_reuse_scope = { 0 , 1 };
    (void)ge::AttrUtils::SetListInt(op_def_c, ATTR_NAME_WORKSPACE_MEMORY_NO_REUSE_SCOPE, workspace_no_reuse_scope);

    vector<int32_t> workspace_no_reuse_scope_e = { 1 };
    (void)ge::AttrUtils::SetListInt(op_def_e, ATTR_NAME_WORKSPACE_MEMORY_NO_REUSE_SCOPE, workspace_no_reuse_scope_e);

    ge::NodePtr node_a = graph->AddNode(op_def_a);
    ge::NodePtr node_b = graph->AddNode(op_def_b);
    ge::NodePtr node_c = graph->AddNode(op_def_c);
    ge::NodePtr node_d = graph->AddNode(op_def_d);
    ge::NodePtr node_e = graph->AddNode(op_def_e);
    ge::NodePtr node_f = graph->AddNode(op_def_f);
    ge::NodePtr node_g = graph->AddNode(op_def_g);

    ge::GraphUtils::AddEdge(node_a->GetOutDataAnchor(0), node_b->GetInDataAnchor(0));
    ge::GraphUtils::AddEdge(node_b->GetOutDataAnchor(0), node_c->GetInDataAnchor(0));
    ge::GraphUtils::AddEdge(node_c->GetOutDataAnchor(0), node_d->GetInDataAnchor(0));
    ge::GraphUtils::AddEdge(node_d->GetOutDataAnchor(0), node_e->GetInDataAnchor(0));
    ge::GraphUtils::AddEdge(node_e->GetOutDataAnchor(0), node_f->GetInDataAnchor(0));
    ge::GraphUtils::AddEdge(node_f->GetOutDataAnchor(0), node_g->GetInDataAnchor(0));
    graph->TopologicalSorting();
  }

 protected:
  void SetUp() {
    SetLocalOmgContext(domi::GetContext());
  }

  void TearDown() { GetContext().out_nodes_map.clear(); }
};

// when check GetMemoryRanges return fail, Assign return fail
TEST_F(UtestModelBuilderTest, SetInputIsConst) {
  Graph2SubGraphInfoList subgraphs;
  std::map<std::string, int> stream_max_parallel_num;
  ge::ComputeGraphPtr graph = make_shared<ge::ComputeGraph>("");
  MakeGraph(graph);
  graph->TopologicalSorting();
  ge::ModelBuilder builder(0, graph, subgraphs, stream_max_parallel_num, false);
  EXPECT_EQ(builder.PreBuildModel(), SUCCESS);
}

TEST_F(UtestModelBuilderTest, test_save_atomic_bin) {
  Graph2SubGraphInfoList subgraphs;
  std::map<std::string, int> stream_max_parallel_num;
  ge::ComputeGraphPtr graph = make_shared<ge::ComputeGraph>("");
  ge::ModelBuilder builder(0, graph, subgraphs, stream_max_parallel_num, false);

  auto atomic_op_desc = make_shared<OpDesc>("Atomic", "Atomic");
  auto kernel_buffer = static_cast<GeAttrValue::BYTES>(Buffer(10));
  AttrUtils::SetStr(atomic_op_desc, ATTR_NAME_TBE_KERNEL_NAME, "Atomic");
  AttrUtils::SetBytes(atomic_op_desc, ATTR_NAME_TBE_KERNEL_BUFFER, kernel_buffer);

  ge::NodePtr atomic_node = graph->AddNode(atomic_op_desc);
  auto op_desc = make_shared<OpDesc>("Sum", "Sum");
  op_desc->SetExtAttr("atomic_clean_node_ptr", atomic_node);
  EXPECT_EQ(builder.SaveAtomicTBEKernel(op_desc), SUCCESS);
}

TEST_F(UtestModelBuilderTest, build_model_for_get_task) {
  Graph2SubGraphInfoList subgraphs;
  std::map<std::string, int> stream_max_parallel_num;
  ge::ComputeGraphPtr graph = make_shared<ge::ComputeGraph>("");
  MakeSessionScopeReuseGraph(graph);
  std::map<std::string, std::string> option;
  ge::ModelBuilder builder(0, graph, subgraphs, stream_max_parallel_num, false);

  MemoryAssigner mem_assigner(graph);
  EXPECT_EQ(mem_assigner.AssignMemory(false, builder.mem_type_to_mem_offset_, builder.zero_copy_mem_size_), SUCCESS);

  ge::Model model;
  EXPECT_EQ(builder.BuildModelDef(model), SUCCESS);
  int64_t session_scope_mem_offset = 0;
  ge::AttrUtils::GetInt(&model, ATTR_MODEL_SESSION_SCOPE_MEMORY_SIZE, session_scope_mem_offset);
  EXPECT_EQ(session_scope_mem_offset, 1536);
}

TEST_F(UtestModelBuilderTest, test_model_save) {
  Graph2SubGraphInfoList subgraphs;
  std::map<std::string, int> stream_max_parallel_num;
  ge::ComputeGraphPtr graph = make_shared<ge::ComputeGraph>("");
  ge::ModelBuilder builder(0, graph, subgraphs, stream_max_parallel_num, false);

  auto op_desc = make_shared<OpDesc>("Conv2d", "Conv2d");
  auto kernel_buffer = static_cast<GeAttrValue::BYTES>(Buffer(10));
  AttrUtils::SetStr(op_desc, ATTR_NAME_TBE_KERNEL_NAME, "Conv2d");
  AttrUtils::SetBytes(op_desc, ATTR_NAME_TBE_KERNEL_BUFFER, kernel_buffer);

  ge::NodePtr node = graph->AddNode(op_desc);
  ge::Model ge_model;
  ge::GeModel ge_gemodel;
  builder.SaveDataToModel(ge_model, ge_gemodel);
  auto tbe_kernel = op_desc->TryGetExtAttr(OP_EXTATTR_NAME_TBE_KERNEL, TBEKernelPtr());
  EXPECT_NE(tbe_kernel, nullptr);
}
