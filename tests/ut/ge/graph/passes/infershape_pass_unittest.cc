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

#define protected public
#define private public
#include "graph/passes/infershape_pass.h"

#include "graph/compute_graph.h"
#include "graph/node.h"
#include "graph/operator.h"
#include "graph/operator_factory.h"
#include "graph/operator_reg.h"
#include "graph_builder_utils.h"

using namespace std;
using namespace testing;
namespace ge {
class UtestGraphInfershapePass : public testing::Test {
 protected:
  void SetUp() {}
  void TearDown() {}
};

TEST_F(UtestGraphInfershapePass, infershape_pass_failed) {
  GeTensorDesc ge_tensor_desc(GeShape({-2, 2, 3, 4}), ge::FORMAT_NCHW, DT_FLOAT16);
  string type = "AddN";
  auto addn_op_desc = std::make_shared<OpDesc>("AddN", type);
  addn_op_desc->AddInputDesc(ge_tensor_desc);
  addn_op_desc->AddOutputDesc(ge_tensor_desc);
  auto graph = std::make_shared<ComputeGraph>("test");
  auto addn_node = std::make_shared<Node>(addn_op_desc, graph);
  addn_node->Init();

  InferShapePass infershape_pass;
  EXPECT_EQ(infershape_pass.Run(addn_node), GE_GRAPH_INFERSHAPE_FAILED);
}

TEST_F(UtestGraphInfershapePass, delete_need_infer_again) {
  auto graph = std::make_shared<ComputeGraph>("test");

  auto no_op_desc = std::make_shared<OpDesc>("No", "NoOp");
  auto no_op_node = graph->AddNode(no_op_desc);
  AttrUtils::SetBool(no_op_desc, "_need_infer_again", false);

  InferShapePass infershape_pass;
  infershape_pass.options_[kOptimizeAfterSubGraph] = "yes";
  EXPECT_EQ(infershape_pass.Run(no_op_node), SUCCESS);
}

}  // namespace ge
