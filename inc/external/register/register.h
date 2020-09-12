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

#ifndef INC_EXTERNAL_REGISTER_REGISTER_H_
#define INC_EXTERNAL_REGISTER_REGISTER_H_

#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

#include "graph/operator.h"
#include "register/register_error_codes.h"
#include "register/register_fmk_types.h"
#include "register/register_types.h"

using std::make_shared;
using std::map;
using std::pair;
using std::string;
using std::to_string;
using std::unique_ptr;
using std::vector;

/*lint -e148*/
namespace ge {
class Operator;
class TensorDesc;
class Tensor;
class TBEPluginManager;
}  // namespace ge

namespace google {
namespace protobuf {
class Message;
}
}  // namespace google

namespace domi {
const int64_t kMaxNameLength = 1048576;  // 1M

enum DynamicType { kInvalid = 0, kInput = 1, kOutput = 2 };
struct DynamicInputOutputInfo {
  DynamicType type;  // input/output
  const char *port_name;
  int64_t port_name_len;
  const char *attr_name;
  int64_t attr_name_len;
  DynamicInputOutputInfo()
      : type(kInvalid), port_name(nullptr), port_name_len(0), attr_name(nullptr), attr_name_len(0) {}
  DynamicInputOutputInfo(DynamicType type, const char *port_name, int64_t port_name_len, const char *attr_name,
                         int64_t attr_name_len)
      : type(type),
        port_name(port_name),
        port_name_len(port_name_len),
        attr_name(attr_name),
        attr_name_len(attr_name_len) {}
};
Status AutoMappingByOpFn(const ge::Operator &op_src, ge::Operator &op);
Status AutoMappingByOpFnDynamic(const ge::Operator &op_src, ge::Operator &op,
                                const vector<DynamicInputOutputInfo> &dynamic_name_attr_value);
Status AutoMappingFn(const google::protobuf::Message *op_src, ge::Operator &op);
Status AutoMappingFnDynamic(const google::protobuf::Message *op_src, ge::Operator &op,
                            std::map<std::string, std::pair<std::string, std::string>> dynamic_name_attr_value,
                            int in_pos = -1, int out_pos = -1);
Status AutoMappingSubgraphIndex(const ge::Graph &graph, const std::function<int(int data_index)> &input,
                                const std::function<int(int netoutput_index)> &output);
Status AutoMappingSubgraphIndex(const ge::Graph &graph,
                                const std::function<Status(int data_index, int &parent_input_index)> &input,
                                const std::function<Status(int netoutput_index, int &parent_output_index)> &output);
using google::protobuf::Message;
class OpRegistrationDataImpl;

using ParseParamFunc = std::function<domi::Status(const google::protobuf::Message *, ge::Operator &)>;
using ParseParamByOpFunc = std::function<domi::Status(const ge::Operator &, ge::Operator &)>;
using FusionParseParamFunc =
  std::function<domi::Status(const std::vector<const google::protobuf::Message *>, ge::Operator &)>;
using FusionParseParamByOpFunc = std::function<domi::Status(const std::vector<ge::Operator> &, ge::Operator &)>;
using ParseSubgraphFunc = std::function<Status(const std::string &subgraph_name, const ge::Graph &graph)>;

class FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY OpRegistrationData {
 public:
  OpRegistrationData(const std::string &om_optype);

  ~OpRegistrationData();

  OpRegistrationData &FrameworkType(const domi::FrameworkType &fmk_type);

  OpRegistrationData &OriginOpType(const std::initializer_list<std::string> &ori_optype_list);

  OpRegistrationData &OriginOpType(const std::string &ori_optype);

  OpRegistrationData &ParseParamsFn(const ParseParamFunc &parseParamFn);

  OpRegistrationData &ParseParamsByOperatorFn(const ParseParamByOpFunc &parse_param_by_op_fn);

  OpRegistrationData &FusionParseParamsFn(const FusionParseParamFunc &fusionParseParamFn);

  OpRegistrationData &FusionParseParamsFn(const FusionParseParamByOpFunc &fusion_parse_param_fn);

  OpRegistrationData &ParseSubgraphPostFn(const ParseSubgraphFunc &subgraph_post_fn);

  OpRegistrationData &ImplyType(const domi::ImplyType &imply_type);

  OpRegistrationData &DelInputWithCond(int inputIdx, const std::string &attrName, bool attrValue);

  OpRegistrationData &DelInputWithOriginalType(int input_idx, const std::string &ori_type);

  OpRegistrationData &InputReorderVector(const vector<int> &input_order);

  domi::ImplyType GetImplyType() const;
  std::string GetOmOptype() const;
  std::set<std::string> GetOriginOpTypeSet() const;
  domi::FrameworkType GetFrameworkType() const;
  ParseParamFunc GetParseParamFn() const;
  ParseParamByOpFunc GetParseParamByOperatorFn() const;
  FusionParseParamFunc GetFusionParseParamFn() const;
  FusionParseParamByOpFunc GetFusionParseParamByOpFn() const;
  ParseSubgraphFunc GetParseSubgraphPostFn() const;

 private:
  std::shared_ptr<OpRegistrationDataImpl> impl_;
  friend class OpRegistry;
  friend class OpRegistrationTbe;
  friend class ge::TBEPluginManager;
};

class FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY OpReceiver {
 public:
  OpReceiver(OpRegistrationData &reg_data);
  ~OpReceiver() {}
};

#define REGISTER_CUSTOM_OP(name) REGISTER_CUSTOM_OP_UNIQ_HELPER(__COUNTER__, name)
#define REGISTER_CUSTOM_OP_UNIQ_HELPER(ctr, name) REGISTER_CUSTOM_OP_UNIQ(ctr, name)
#define REGISTER_CUSTOM_OP_UNIQ(ctr, name) \
  static OpReceiver register_op##ctr __attribute__((unused)) = OpRegistrationData(name)
}  // namespace domi

namespace ge {
using OpRegistrationData = domi::OpRegistrationData;
using OpReceiver = domi::OpReceiver;
}  // namespace ge
/*lint +e148*/
#endif  // INC_EXTERNAL_REGISTER_REGISTER_H_
