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

#define private public
#define protected public
#include "graph/partition/dynamic_shape_partition.h"
#include "compute_graph.h"
#include "graph/compute_graph_impl.h"
#include "inc/framework/common/types.h"
#include "utils/graph_utils.h"
#include "graph/debug/ge_attr_define.h"

namespace ge {
namespace {
GeTensorDescPtr CreateTensorDesc(std::initializer_list<int64_t> shape, Format format = FORMAT_NCHW,
                                 DataType data_type = DT_FLOAT) {
  GeShape ge_shape{vector<int64_t>(shape)};
  GeTensorDescPtr tensor_desc = std::make_shared<GeTensorDesc>();
  tensor_desc->SetShape(ge_shape);
  tensor_desc->SetFormat(format);
  tensor_desc->SetDataType(data_type);
  return tensor_desc;
}

class NodeBuilder {
  public:
    NodeBuilder(const std::string &name, const std::string &type) { op_desc_ = std::make_shared<OpDesc>(name, type); }

    NodeBuilder &AddInputDesc(std::initializer_list<int64_t> shape = {1, 1, 224, 224}, Format format = FORMAT_NCHW,
                              DataType data_type = DT_FLOAT) {
      op_desc_->AddInputDesc(CreateTensorDesc(shape, format, data_type)->Clone());
      return *this;
    }

    NodeBuilder &AddOutputDesc(std::initializer_list<int64_t> shape = {1, 1, 224, 224}, Format format = FORMAT_NCHW,
                               DataType data_type = DT_FLOAT) {
      op_desc_->AddOutputDesc(CreateTensorDesc(shape, format, data_type)->Clone());
      return *this;
    }

    NodeBuilder &AddOutputDesc(GeTensorDescPtr tensor_desc) {
      op_desc_->AddOutputDesc(tensor_desc->Clone());
      return *this;
    }

    NodePtr Build(const ComputeGraphPtr &graph) {
      NodePtr node = graph->AddNode(op_desc_);
      return node;
    }

  private:
    OpDescPtr op_desc_;
};
}  // namespace

class UtestDynamicShapePartition : public testing::Test {
  protected:
    void SetUp() {}

    void TearDown() {}
};

TEST_F(UtestDynamicShapePartition, single_op_scene_success) {
  ComputeGraphPtr graph = std::make_shared<ComputeGraph>("default");

  NodePtr node1 =
      NodeBuilder("node1", CONSTANTOP).AddInputDesc({1, 1, 224, 224}).AddOutputDesc({1, 1, 224, 224}).Build(graph);
  NodePtr add_n_node =
      NodeBuilder("add_n_node", ADDN).AddInputDesc({1, 1, 224, 224}).AddOutputDesc({1, 1, 224, 224}).Build(graph);
  NodePtr node2 =
      NodeBuilder("node2", RELU).AddInputDesc({1, 1, 224, 224}).AddOutputDesc({1, 1, 224, 224}).Build(graph);
  GraphUtils::AddEdge(node1->GetOutDataAnchor(0), add_n_node->GetInDataAnchor(0));
  GraphUtils::AddEdge(add_n_node->GetOutDataAnchor(0), node2->GetInDataAnchor(0));

  (void)AttrUtils::SetBool(add_n_node->GetOpDesc(), ATTR_SINGLE_OP_SCENE, true);

  DynamicShapePartitioner partitioner(graph);
  EXPECT_EQ(partitioner.Partition(), SUCCESS);
}

TEST_F(UtestDynamicShapePartition, merge_control_flow_group) {
  ComputeGraphPtr graph = std::make_shared<ComputeGraph>("default");
  AttrUtils::SetStr(*graph, ATTR_NAME_SESSION_GRAPH_ID, "session_graph_id");

  NodePtr data1 = NodeBuilder("data1", DATA).AddInputDesc({1}).AddOutputDesc({1}).Build(graph);
  NodePtr data2 = NodeBuilder("data2", DATA).AddInputDesc({1}).AddOutputDesc({1}).Build(graph);
  NodePtr merge = NodeBuilder("node2", MERGE).AddInputDesc({1}).AddInputDesc({1})
                    .AddOutputDesc({1}).AddOutputDesc({}).Build(graph);

  GraphUtils::AddEdge(data1->GetOutDataAnchor(0), merge->GetInDataAnchor(0));
  GraphUtils::AddEdge(data2->GetOutDataAnchor(0), merge->GetInDataAnchor(1));

  (void)AttrUtils::SetBool(data1->GetOpDesc(), ATTR_NAME_FORCE_UNKNOWN_SHAPE, true);
  (void)AttrUtils::SetInt(data1->GetOpDesc(), ATTR_NAME_CONTROL_FLOW_GROUP, 3);
  (void)AttrUtils::SetBool(data2->GetOpDesc(), ATTR_NAME_FORCE_UNKNOWN_SHAPE, true);
  (void)AttrUtils::SetInt(data2->GetOpDesc(), ATTR_NAME_CONTROL_FLOW_GROUP, 3);
  (void)AttrUtils::SetBool(merge->GetOpDesc(), ATTR_NAME_FORCE_UNKNOWN_SHAPE, true);
  (void)AttrUtils::SetInt(merge->GetOpDesc(), ATTR_NAME_CONTROL_FLOW_GROUP, 3);

  EXPECT_EQ(graph->impl_->sub_graph_.size(), 0);
  DynamicShapePartitioner partitioner(graph);
  EXPECT_EQ(partitioner.Partition(), SUCCESS);
  EXPECT_EQ(graph->impl_->sub_graph_.size(), 1);
}
} // namespace ge