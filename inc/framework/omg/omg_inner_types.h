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

#ifndef INC_FRAMEWORK_OMG_OMG_INNER_TYPES_H_
#define INC_FRAMEWORK_OMG_OMG_INNER_TYPES_H_

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "framework/common/fmk_error_codes.h"
#include "framework/common/types.h"
#include "register/register_fmk_types.h"

using domi::DOMI_TENSOR_ND;
using domi::DOMI_TENSOR_RESERVED;
using domi::domiTensorFormat_t;
using domi::FRAMEWORK_RESERVED;
using domi::FrameworkType;
using std::map;
using std::string;
using std::unordered_map;
using std::vector;

namespace ge {
/**
 * @ingroup domi_omg
 * @brief run model
 */
enum RunMode {
  GEN_OM_MODEL = 0,    // generate offline model file
  MODEL_TO_JSON = 1,   // convert to JSON file
  ONLY_PRE_CHECK = 3,  // only for pre-check
  PBTXT_TO_JSON = 5    // pbtxt to json
};

///
/// @ingroup domi_omg
/// @brief high-precision mode
///
enum HighPrecisionMode {
  // the FP16 high-precision function is disabled in common mode
  HIGH_PRECISION_DEFAULT = 0,

  // high-precision mode, enabling FP16 high-precision mode (Convolution/FullConnect/AvgPooling are involved)
  HIGH_PRECISION_FP16 = 1
};

///
/// @ingroup domi_omg
/// @brief description buffer data
///
struct OMGBufferData {
  void *data;
  uint32_t length;
};

struct OmgContext {
  OmgContext() { format = DOMI_TENSOR_ND; }
  domiTensorFormat_t format;

  // format of the input specified by the command line
  std::unordered_map<std::string, domiTensorFormat_t> input_nodes_format_map;
  std::vector<domiTensorFormat_t> output_formats;

  // user-designate input dims
  std::vector<std::pair<std::string, std::vector<int64_t>>> user_input_dims;
  // global input dims
  std::unordered_map<std::string, std::vector<int64_t>> input_dims;

  // resolve the mapping between operators with the same name and corresponding network. format e.g.
  // Detectionoutput:SsdDetectiontOutput
  std::map<std::string, std::string> op_conf_map;
  // save the output node of the network. key = operator name, value = index, index indicates the output index of the
  // operator
  std::map<std::string, std::vector<int32_t>> out_nodes_map;
  // user-designate out nodes (this is used for determing the orders)
  std::vector<std::pair<std::string, int32_t>> user_out_nodes;
  // save the output node of the network, value = topName,
  // topName indicates the output name of the operator.
  std::vector<std::string> user_out_nodes_top_vec;
  // net out nodes (where user_out_nodes or leaf nodes)
  std::vector<std::string> net_out_nodes;
  // net out nodes top names(only caffe has top)
  std::vector<std::string> out_top_names;
  // path for the aicpu custom operator so_file
  std::vector<std::string> aicpu_op_run_paths;
  // preferential format used by the entire network
  domiTensorFormat_t net_format = DOMI_TENSOR_RESERVED;
  domi::FrameworkType type = domi::FRAMEWORK_RESERVED;
  RunMode run_mode = ONLY_PRE_CHECK;
  bool train_flag = false;

  std::string output_type;

  // Whether to use dynamic batch size or dynamic image size
  bool is_dynamic_input = false;
  std::string dynamic_batch_size;
  std::string dynamic_image_size;
  std::string dynamic_dims;
};
}  // namespace ge

namespace domi {
/**
 * @ingroup domi_omg
 * @brief get OMG context
 * @return OmgContext context
 */
ge::OmgContext &GetContext();

struct TEBinInfo {
  // It is obsolete. It will be automatically obtained from the binfilename field of the JSON file later.
  // To be compatible with use cases written by previous users, fields are not deleted.(2018.11.21)
  std::string bin_file_path;
  std::string json_file_path;
  std::string ddk_version;
};
}  // namespace domi

#endif  // INC_FRAMEWORK_OMG_OMG_INNER_TYPES_H_
