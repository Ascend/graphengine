/**
 * Copyright 2020 Huawei Technologies Co., Ltd
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

#include "graph/passes/infershape_pass.h"
#include "common/util/error_manager/error_manager.h"
#include "framework/common/debug/ge_log.h"
#include "framework/common/ge_inner_error_codes.h"
#include "analyzer/analyzer.h"
#include "framework/common/util.h"
#include "graph/shape_refiner.h"
#include "graph/utils/graph_utils.h"
#include "utils/tensor_utils.h"
#include "utils/type_utils.h"

namespace ge {

void SerialShapeRange(const GeTensorDescPtr &desc, std::string &desc_str) {
  desc_str += "[";
  std::vector<std::pair<int64_t, int64_t>> shape_range;
  (void)desc->GetShapeRange(shape_range);
  for (const auto &pair : shape_range) {
    desc_str += "{";
    desc_str += std::to_string(pair.first) + "," + std::to_string(pair.second);
    desc_str += "},";
  }
  desc_str += "]";
  shape_range.clear();
  (void)desc->GetOriginShapeRange(shape_range);
  for (const auto &pair : shape_range) {
    desc_str += ",{";
    desc_str += std::to_string(pair.first) + "," + std::to_string(pair.second);
    desc_str += "},";
  }
}

std::string GetInTensorInfoWithString(const ge::NodePtr &node) {
  ge::OpDescPtr op_desc = node->GetOpDesc();
  std::stringstream ss;
  ss << "{";
  int32_t in_idx = 0;
  for (const auto &input_desc : op_desc->GetAllInputsDescPtr()) {
    if (input_desc == nullptr) {
      in_idx++;
      continue;
    }
    if (in_idx > 0) {
      ss << "    ";
    }
    ss << "input_" << in_idx << " " << "tensor: [";
    ss << "(shape:[" << input_desc->MutableShape().ToString() << "]),";
    ss << "(format:" << TypeUtils::FormatToSerialString(input_desc->GetFormat()) << "),";
    ss << "(dtype:" << TypeUtils::DataTypeToSerialString(input_desc->GetDataType()) << "),";
    ss << "(origin_shape:" << input_desc->GetOriginShape().ToString() << "),";
    ss << "(origin_format:" << TypeUtils::FormatToSerialString(input_desc->GetOriginFormat()) << "),";
    ss << "(origin_dtype:" << TypeUtils::DataTypeToSerialString(input_desc->GetOriginDataType()) << "),";
    string range_str;
    SerialShapeRange(input_desc, range_str);
    ss << "(shape_range:" << range_str << ")]";
    in_idx++;
  }
  return ss.str();
}

Status InferShapePass::Run(NodePtr &node) {
  // kOptimizeAfterSubGraph exist means after subgraph
  auto ret = ShapeRefiner::InferShapeAndType(node, !OptionExists(kOptimizeAfterSubGraph));
  if (ret != GRAPH_SUCCESS) {
    // select INFERSHAPE failed info
    auto graph = node->GetOwnerComputeGraph();
    GE_CHECK_NOTNULL(graph);
    auto root_graph = ge::GraphUtils::FindRootGraph(graph);
    GE_CHECK_NOTNULL(root_graph);
    analyzer::DataInfo analyze_info{root_graph->GetSessionID(), root_graph->GetGraphID(),
                                  analyzer::INFER_SHAPE, node, "InferShapeFailed!"};
    (void)Analyzer::GetInstance()->DoAnalyze(analyze_info);
    (void)Analyzer::GetInstance()->SaveAnalyzerDataToFile(root_graph->GetSessionID(),
                                                          root_graph->GetGraphID());

    REPORT_CALL_ERROR("E19999", "Call InferShapeAndType for node:%s(%s) failed, input_tensor:%s",
                      node->GetName().c_str(), node->GetType().c_str(), GetInTensorInfoWithString(node).c_str());
    GELOGE(GE_GRAPH_INFERSHAPE_FAILED, "infershape failed. node: %s", node->GetName().c_str());
    return GE_GRAPH_INFERSHAPE_FAILED;
  }
  bool need_repass = false;
  auto has_attr = AttrUtils::GetBool(node->GetOpDesc(), "_need_infer_again", need_repass);
  if (has_attr) {
    if (!OptionExists(kOptimizeAfterSubGraph)) {
      return SUCCESS;
    }
    if (need_repass) {
      AddImmediateRePassNode(node);
      GELOGD("Node %s need repass immediately.", node->GetName().c_str());
    } else {
      // clear attr on while
      node->GetOpDesc()->DelAttr("_need_infer_again");
    }
  }
  return SUCCESS;
}
}  // namespace ge
