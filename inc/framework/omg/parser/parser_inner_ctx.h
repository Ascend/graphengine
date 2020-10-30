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

#ifndef INC_FRAMEWORK_OMG_PARSER_PARSER_INNER_CONTEXT_H_
#define INC_FRAMEWORK_OMG_PARSER_PARSER_INNER_CONTEXT_H_

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "external/register/register_fmk_types.h"
#include "external/register/register_types.h"
#include "framework/omg/omg_inner_types.h"

namespace ge {
struct ParserContext {
  std::unordered_map<std::string, std::vector<int64_t>> input_dims;
  domi::domiTensorFormat_t format = domi::DOMI_TENSOR_ND;
  RunMode run_mode = ONLY_PRE_CHECK;
  std::string custom_proto_path; // save caffe custom proto path, used by caffe parse
  std::string caffe_proto_path; // save caffe proto path, used by caffe parse
  std::string enable_scope_fusion_passes;  // name of the pass that needs to take effect
};

ParserContext &GetParserContext();
}  // namespace ge

#endif  // INC_FRAMEWORK_OMG_PARSER_PARSER_INNER_CONTEXT_H_
