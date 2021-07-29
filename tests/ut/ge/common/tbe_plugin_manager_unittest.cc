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

#include <gtest/gtest.h>

#define protected public
#define private public
#include "common/ge/tbe_plugin_manager.h"
#undef private
#undef protected

namespace ge {
class UtestTBEPluginManager: public testing::Test {
 protected:
  void SetUp() {}
  void TearDown() {}
};

TEST_F(UtestTBEPluginManager, CheckFindParserSo) {
  string path = "";
  vector<string> file_list = {};
  string caffe_parser_path = "";
  TBEPluginManager::Instance().FindParserSo(path, file_list, caffe_parser_path);
  path = "/lib64";
  TBEPluginManager::Instance().FindParserSo(path, file_list, caffe_parser_path);
}
}  // namespace ge
