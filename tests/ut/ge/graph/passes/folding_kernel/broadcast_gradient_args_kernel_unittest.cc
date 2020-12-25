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

#include <assert.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

#define protected public
#define private public
#include "host_kernels/broadcast_gradient_args_kernel.h"

#include "common/debug/log.h"
#include "common/debug/memory_dumper.h"
#include "common/op/attr_value_util.h"
#include "common/types.h"
#include "folding_kernel_unittest_utils.h"
#include "framework/common/ge_inner_error_codes.h"
#include "ge/ge_api.h"
#include "graph/debug/ge_attr_define.h"
#include "graph/operator.h"
#include "graph/passes/dimension_compute_pass.h"
#include "graph/utils/graph_utils.h"
#include "graph/utils/op_desc_utils.h"
#include "graph/utils/tensor_utils.h"
#include "inc/kernel_factory.h"
#undef protected
#undef private

using namespace testing;
using namespace ge;

class UtestBroadcastGradientArgsKernel : public testing::Test {
 protected:
  void SetUp() { init(); }

  void TearDown() { destory(); }

 private:
  void init() {
    pass_ = new ConstantFoldingPass();
    graph_ = std::make_shared<ge::ComputeGraph>("default");
    op_desc_ptr_ = std::make_shared<OpDesc>("broadcast_gradient_args", BROADCASTGRADIENTARGS);
    node_ = std::make_shared<Node>(op_desc_ptr_, graph_);
    kernel_ = KernelFactory::Instance().Create(BROADCASTGRADIENTARGS);
  }
  void destory() {
    delete pass_;
    pass_ = NULL;
  }

 protected:
  ConstantFoldingPass *pass_;

  ge::ComputeGraphPtr graph_;
  OpDescPtr op_desc_ptr_;
  NodePtr node_;
  shared_ptr<Kernel> kernel_;
};

TEST_F(UtestBroadcastGradientArgsKernel, SizeCheckFail) {
  vector<int64_t> dims_vec_0 = {8, 2};
  vector<int64_t> data_vec_0 = {2, 1, 4, 1, 2};
  GeTensorDesc tensor_desc_0(GeShape(dims_vec_0), FORMAT_NCHW, DT_FLOAT);
  ConstGeTensorPtr tensor_0 =
      std::make_shared<GeTensor>(tensor_desc_0, (uint8_t *)data_vec_0.data(), data_vec_0.size() * sizeof(int64_t));
  op_desc_ptr_->AddInputDesc(tensor_desc_0);

  GeTensorDesc tensor_desc_out(GeShape(), FORMAT_NCHW, DT_INT64);
  op_desc_ptr_->AddOutputDesc(tensor_desc_out);

  vector<ConstGeTensorPtr> input = {tensor_0};

  std::vector<GeTensorPtr> outputs;
  Status status = kernel_->Compute(op_desc_ptr_, input, outputs);
  EXPECT_EQ(NOT_CHANGED, status);
}
TEST_F(UtestBroadcastGradientArgsKernel, CheckOutputNormal) {
  string op_type = BROADCASTGRADIENTARGS;
  vector<vector<int64_t>> input_shape_dims({
      {4},
      {3},
  });
  vector<vector<int64_t>> input_data({
      {2, 1, 5, 3},
      {4, 5, 1},
  });

  vector<vector<int64_t>> output_shape_dims({
      {1},
      {2},
  });
  vector<vector<int64_t>> output_data({{1}, {0, 3}});

  bool result = ge::test::ConstFoldingKernelCheckShapeAndOutput(op_type, input_shape_dims, input_data, DT_INT64,
                                                                output_shape_dims, output_data, DT_INT64);
  EXPECT_EQ(result, true);
}
TEST_F(UtestBroadcastGradientArgsKernel, CheckOutputNormalInt32) {
  string op_type = BROADCASTGRADIENTARGS;
  vector<vector<int64_t>> input_shape_dims({
      {4},
      {3},
  });
  vector<vector<int32_t>> input_data({
      {2, 1, 5, 3},
      {4, 5, 1},
  });

  vector<vector<int64_t>> output_shape_dims({
      {1},
      {2},
  });
  vector<vector<int32_t>> output_data({{1}, {0, 3}});

  bool result = ge::test::ConstFoldingKernelCheckShapeAndOutput(op_type, input_shape_dims, input_data, DT_INT32,
                                                                output_shape_dims, output_data, DT_INT32);
  EXPECT_EQ(result, true);
}

TEST_F(UtestBroadcastGradientArgsKernel, CheckOutputInputsSame) {
  string op_type = BROADCASTGRADIENTARGS;
  vector<vector<int64_t>> input_shape_dims({
      {4},
      {4},
  });
  vector<vector<int64_t>> input_data({
      {2, 1, 5, 3},
      {2, 1, 5, 3},
  });

  vector<vector<int64_t>> output_shape_dims({
      {1},
      {1},
  });
  vector<vector<int64_t>> output_data({{1}, {1}});

  bool result = ge::test::ConstFoldingKernelCheckShapeAndOutput(op_type, input_shape_dims, input_data, DT_INT64,
                                                                output_shape_dims, output_data, DT_INT64);
  EXPECT_EQ(result, true);
}

TEST_F(UtestBroadcastGradientArgsKernel, CheckOutputInputsSameEmptyOut) {
  string op_type = BROADCASTGRADIENTARGS;
  vector<vector<int64_t>> input_shape_dims({
      {4},
      {4},
  });
  vector<vector<int64_t>> input_data({
      {2, 3, 5, 3},
      {2, 3, 5, 3},
  });

  vector<vector<int64_t>> output_shape_dims({
      {0},
      {0},
  });
  vector<vector<int64_t>> output_data({{}, {}});

  bool result = ge::test::ConstFoldingKernelCheckShapeAndOutput(op_type, input_shape_dims, input_data, DT_INT64,
                                                                output_shape_dims, output_data, DT_INT64);
  EXPECT_EQ(result, true);
}

TEST_F(UtestBroadcastGradientArgsKernel, CheckOutputInputsOneScalar) {
  string op_type = BROADCASTGRADIENTARGS;
  vector<vector<int64_t>> input_shape_dims({
      {4},
      {1},
  });
  vector<vector<int64_t>> input_data({
      {2, 1, 5, 3},
      {1},
  });

  vector<vector<int64_t>> output_shape_dims({
      {1},
      {4},
  });
  vector<vector<int64_t>> output_data({{1}, {0, 1, 2, 3}});

  bool result = ge::test::ConstFoldingKernelCheckShapeAndOutput(op_type, input_shape_dims, input_data, DT_INT64,
                                                                output_shape_dims, output_data, DT_INT64);
  EXPECT_EQ(result, true);
}
TEST_F(UtestBroadcastGradientArgsKernel, CheckOutputInputsBothScalar) {
  string op_type = BROADCASTGRADIENTARGS;
  vector<vector<int64_t>> input_shape_dims({
      {1},
      {1},
  });
  vector<vector<int64_t>> input_data({
      {3},
      {1},
  });

  vector<vector<int64_t>> output_shape_dims({
      {0},
      {1},
  });
  vector<vector<int64_t>> output_data({{}, {0}});

  bool result = ge::test::ConstFoldingKernelCheckShapeAndOutput(op_type, input_shape_dims, input_data, DT_INT64,
                                                                output_shape_dims, output_data, DT_INT64);
  EXPECT_EQ(result, true);
}

TEST_F(UtestBroadcastGradientArgsKernel, GetShapeDataFromConstTensorFail) {
  OpDescPtr op_desc_ptr = std::make_shared<OpDesc>("BroadcastGradientArgs", BROADCASTGRADIENTARGS);
  vector<bool> is_input_const_vec = {
      true,
      true,
  };
  op_desc_ptr->SetIsInputConst(is_input_const_vec);
  AttrUtils::SetInt(op_desc_ptr, ATTR_NAME_T, (int64_t)DT_INT32);

  vector<int64_t> dims_vec_0 = {};
  vector<int64_t> data_vec_0 = {};
  GeTensorDesc tensor_desc_0(GeShape(dims_vec_0), FORMAT_NCHW, DT_INT32);
  ConstGeTensorPtr tensor_0 =
      std::make_shared<GeTensor>(tensor_desc_0, (uint8_t *)data_vec_0.data(), data_vec_0.size() * sizeof(int64_t));
  op_desc_ptr->AddInputDesc(tensor_desc_0);

  vector<int64_t> dims_vec_1 = {5};
  vector<int64_t> data_vec_1 = {0, 1, 4, 1, 2};
  GeTensorDesc tensor_desc_1(GeShape(dims_vec_1), FORMAT_NCHW, DT_INT32);
  ConstGeTensorPtr tensor_1 =
      std::make_shared<GeTensor>(tensor_desc_1, (uint8_t *)data_vec_1.data(), data_vec_1.size() * sizeof(int64_t));
  op_desc_ptr->AddInputDesc(tensor_desc_1);
  op_desc_ptr->AddOutputDesc(GeTensorDesc());
  op_desc_ptr->AddOutputDesc(GeTensorDesc());

  vector<ConstGeTensorPtr> input = {tensor_0, tensor_1};
  vector<GeTensorPtr> outputs;

  shared_ptr<ge::Kernel> kernel = ge::KernelFactory::Instance().Create(BROADCASTGRADIENTARGS);
  Status status = kernel->Compute(op_desc_ptr, input, outputs);

  EXPECT_EQ(ge::PARAM_INVALID, status);
}

TEST_F(UtestBroadcastGradientArgsKernel, GenerateBcastInfoFail) {
  OpDescPtr op_desc_ptr = std::make_shared<OpDesc>("BroadcastGradientArgs", BROADCASTGRADIENTARGS);
  vector<bool> is_input_const_vec = {
      true,
      true,
  };
  op_desc_ptr->SetIsInputConst(is_input_const_vec);
  AttrUtils::SetInt(op_desc_ptr, ATTR_NAME_T, (int64_t)DT_INT32);

  vector<int64_t> dims_vec_0 = {5};
  vector<int64_t> data_vec_0 = {-1, 0, 4, 1, 2};
  GeTensorDesc tensor_desc_0(GeShape(dims_vec_0), FORMAT_NCHW, DT_INT32);
  ConstGeTensorPtr tensor_0 =
      std::make_shared<GeTensor>(tensor_desc_0, (uint8_t *)data_vec_0.data(), data_vec_0.size() * sizeof(int64_t));
  op_desc_ptr->AddInputDesc(tensor_desc_0);

  vector<int64_t> dims_vec_1 = {5};
  vector<int64_t> data_vec_1 = {1, 1, 4, 1, 2};
  GeTensorDesc tensor_desc_1(GeShape(dims_vec_1), FORMAT_NCHW, DT_INT32);
  ConstGeTensorPtr tensor_1 =
      std::make_shared<GeTensor>(tensor_desc_1, (uint8_t *)data_vec_1.data(), data_vec_1.size() * sizeof(int64_t));
  op_desc_ptr->AddInputDesc(tensor_desc_1);
  op_desc_ptr->AddOutputDesc(GeTensorDesc());
  op_desc_ptr->AddOutputDesc(GeTensorDesc());

  vector<ConstGeTensorPtr> input = {tensor_0, tensor_1};
  vector<GeTensorPtr> outputs;

  shared_ptr<ge::Kernel> kernel = ge::KernelFactory::Instance().Create(BROADCASTGRADIENTARGS);
  Status status = kernel->Compute(op_desc_ptr, input, outputs);

  EXPECT_EQ(ge::PARAM_INVALID, status);
}
