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
#include <vector>

#include "graph/load/model_manager/model_utils.h"
#include "graph/utils/graph_utils.h"
#include "runtime/rt.h"

#define protected public
#define private public
#include "single_op/single_op_model.h"
#include "single_op/task/tbe_task_builder.h"
#include "single_op/task/op_task.h"
#include "single_op/task/tbe_task_builder.h"
#include "external/register/op_tiling_registry.h"
#undef private
#undef protected

using namespace std;
using namespace testing;
using namespace ge;
using namespace optiling;

class UtestSingleOpTask : public testing::Test {
 protected:
  void SetUp() {}

  void TearDown() {}
};

TEST_F(UtestSingleOpTask, test_build_kernel_task) {
  string model_data_str = "123456789";
  SingleOpModel model("model", model_data_str.c_str(), model_data_str.size());
  model.input_offset_list_.push_back(0);
  model.input_sizes_.push_back(16);

  model.output_offset_list_.push_back(0);
  model.output_sizes_.push_back(16);

  auto graph = make_shared<ComputeGraph>("graph");
  auto op_desc = make_shared<OpDesc>("Add", "Add");
  std::vector<char> kernelBin;
  TBEKernelPtr tbe_kernel = std::make_shared<ge::OpKernelBin>("name/Add", std::move(kernelBin));
  op_desc->SetExtAttr(ge::OP_EXTATTR_NAME_TBE_KERNEL, tbe_kernel);
  std::string kernel_name("kernel/Add");
  AttrUtils::SetStr(op_desc, op_desc->GetName() + "_kernelname", kernel_name);

  vector<int64_t> shape{16, 16};
  GeShape ge_shape(shape);
  GeTensorDesc desc(ge_shape);
  op_desc->AddInputDesc(desc);
  op_desc->AddOutputDesc(desc);
  auto node = graph->AddNode(op_desc);

  std::mutex stream_mu_;
  rtStream_t stream_ = nullptr;
  StreamResource stream_resource(0);
  SingleOp single_op(&stream_resource, &stream_mu_, stream_);

  domi::TaskDef task_def;
  task_def.set_type(RT_MODEL_TASK_ALL_KERNEL);
  domi::KernelDefWithHandle *kernel_with_handle = task_def.mutable_kernel_with_handle();
  kernel_with_handle->set_original_kernel_key("");
  kernel_with_handle->set_node_info("");
  kernel_with_handle->set_block_dim(32);
  kernel_with_handle->set_args_size(64);
  string args(64, '1');
  kernel_with_handle->set_args(args.data(), 64);
  domi::KernelContext *context = kernel_with_handle->mutable_context();
  context->set_op_index(1);
  context->set_kernel_type(2);    // ccKernelType::TE
  uint16_t args_offset[9] = {0};
  context->set_args_offset(args_offset, 9 * sizeof(uint16_t));
  model.op_list_[1] = node;

  TbeOpTask task_tmp;
  TbeOpTask *task = &task_tmp;
  ASSERT_EQ(model.BuildKernelTask(task_def, &task), SUCCESS);
  ge::DataBuffer data_buffer;
  vector<GeTensorDesc> input_desc;
  vector<DataBuffer> input_buffers = { data_buffer };
  vector<GeTensorDesc> output_desc;
  vector<DataBuffer> output_buffers = { data_buffer };
  task->node_ = node;
  OpTilingFunc op_tiling_func = [](const TeOpParas &, const OpCompileInfo &, OpRunInfo &) -> bool {return true;};
  OpTilingRegistryInterf("Add", op_tiling_func);
  ge::AttrUtils::SetStr(op_desc, "compile_info_key", "op_compile_info_key");
  ge::AttrUtils::SetStr(op_desc, "compile_info_json", "op_compile_info_json");
  char c = '0';
  char* buffer = &c;
  task->tiling_buffer_ = buffer;
  task->max_tiling_size_ = 64;
  task->tiling_data_ = "tiling_data";
  task->arg_size_ = 64;
  task->args_.reset(new (std::nothrow) uint8_t[sizeof(void *) * 3]);

  ASSERT_EQ(task->LaunchKernel(input_desc, input_buffers, output_desc, output_buffers, stream_), SUCCESS);
  char *handle = "00";
  task->SetHandle(handle);
  ASSERT_EQ(task->LaunchKernel(input_desc, input_buffers, output_desc, output_buffers, stream_), SUCCESS);
}

TEST_F(UtestSingleOpTask, test_update_ioaddr) {
  auto graph = make_shared<ComputeGraph>("graph");
  auto op_desc = make_shared<OpDesc>("Add", "Add");

  GeTensorDesc desc;
  op_desc->AddInputDesc(desc);
  op_desc->AddInputDesc(desc);
  op_desc->AddOutputDesc(desc);
  vector<bool> is_input_const = { true, false };
  op_desc->SetIsInputConst(is_input_const);
  auto node = graph->AddNode(op_desc);

  TbeOpTask task;
  task.op_desc_ = op_desc;
  task.node_ = node;
  ASSERT_EQ(task.SetArgIndex(), SUCCESS);
  task.arg_size_ = sizeof(void *) * 4;
  task.args_.reset(new (std::nothrow) uint8_t[task.arg_size_]);
  task.arg_index_ = {0};
  task.input_num_ = 2;
  task.output_num_ = 1;

  vector<void *> args;
  vector<DataBuffer> inputs;
  vector<DataBuffer> outputs;
  ASSERT_EQ(task.UpdateIoAddr(inputs, outputs), ACL_ERROR_GE_PARAM_INVALID);

  ge::DataBuffer data_buffer;
  inputs = { data_buffer };
  outputs = { data_buffer };
  ASSERT_EQ(task.UpdateIoAddr(inputs, outputs), SUCCESS);

  task.tiling_buffer_ = (void *)0x0001;
  task.workspaces_ = { (void *)0x0002 };
  ASSERT_EQ(task.UpdateTilingArgs(nullptr), SUCCESS);
  task.tiling_buffer_ = nullptr;
}

