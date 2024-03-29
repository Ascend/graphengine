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
#include "attr_options.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include "graph/debug/ge_attr_define.h"
#include "framework/common/util.h"
#include "common/util/error_manager/error_manager.h"

namespace ge {
graphStatus WeightCompressFunc(ComputeGraphPtr &graph, const string &cfg_path) {
  GE_CHECK_NOTNULL(graph);
  if (cfg_path.empty()) {
    return GRAPH_SUCCESS;
  }
  std::string real_path = RealPath(cfg_path.c_str());
  if (real_path.empty()) {
    GELOGE(GRAPH_PARAM_INVALID, "Can not get real path for %s.", cfg_path.c_str());
    return GRAPH_PARAM_INVALID;
  }
  std::ifstream ifs(real_path);
  if (!ifs.is_open()) {
    GELOGE(GRAPH_FAILED, "Open file %s failed", cfg_path.c_str());
    return GRAPH_FAILED;
  }

  std::string compress_nodes;
  ifs >> compress_nodes;
  ifs.close();
  GELOGI("Compress weight of nodes: %s", compress_nodes.c_str());

  vector<string> compress_node_vec = StringUtils::Split(compress_nodes, ';');
  for (size_t i = 0; i < compress_node_vec.size(); ++i) {
    for (auto &node_ptr : graph->GetDirectNode()) {
      GE_CHECK_NOTNULL(node_ptr);
      auto op_desc = node_ptr->GetOpDesc();
      GE_CHECK_NOTNULL(op_desc);

      if ((op_desc->GetName() == compress_node_vec[i]) || IsOriginalOpFind(op_desc, compress_node_vec[i])) {
        if (!ge::AttrUtils::SetBool(op_desc, ge::ATTR_NAME_COMPRESS_WEIGHT, true)) {
          GELOGE(GRAPH_FAILED, "node %s SetBool failed.", compress_node_vec[i].c_str());
          return GRAPH_FAILED;
        }
      }
    }
  }
  return GRAPH_SUCCESS;
}
}  // namespace ge
