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
#include "graph/load/model_manager/data_dumper.h"
#include "graph/load/model_manager/davinci_model.h"
#undef private
#undef protected

using namespace std;

namespace ge {
class UtestDataDumper : public testing::Test {
 protected:
  void SetUp() {}

  void TearDown() {}
};

std::vector<void *> stub_get_output_addrs(const RuntimeParam &model_param, ConstOpDescPtr op_desc) {
  std::vector<void *> res;
  res.emplace_back(reinterpret_cast<void *>(23333));
  return res;
}
/*
TEST_F(UtestDataDumper, LoadDumpInfo_no_output_addrs_fail) {
  RuntimeParam rts_param;
  DataDumper data_dumper(rts_param);
  data_dumper.SetModelName("test");
  data_dumper.SetModelId(2333);
  std::shared_ptr<OpDesc> op_desc_1(new OpDesc());
  op_desc_1->AddOutputDesc("test", GeTensorDesc());
  data_dumper.SaveDumpTask(0, 0, op_desc_1, 0);
  string dump_mode = "output";
  data_dumper.dump_properties_.SetDumpMode(dump_mode);
  Status ret = data_dumper.LoadDumpInfo();
  EXPECT_EQ(ret, SUCCESS);
}
*/

TEST_F(UtestDataDumper, UnloadDumpInfo_success) {
  RuntimeParam rts_param;
  DataDumper data_dumper(rts_param);
  data_dumper.SetModelName("test");
  data_dumper.SetModelId(2333);

  Status ret = data_dumper.UnloadDumpInfo();
  EXPECT_EQ(ret, SUCCESS);
}
}  // namespace ge
