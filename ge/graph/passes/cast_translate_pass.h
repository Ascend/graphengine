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

#ifndef GE_GRAPH_PASSES_CAST_TRANSLATE_PASS_H_
#define GE_GRAPH_PASSES_CAST_TRANSLATE_PASS_H_

#include "graph/passes/base_pass.h"

namespace ge {
class CastTranslatePass : public BaseNodePass {
 public:
  Status Run(NodePtr &node) override;

 private:
  bool CheckInAndOutDataAnchor(NodePtr &node) const;
  bool IsCastNode(NodePtr &node) const;
  bool IsTranslateNode(NodePtr &node) const;
  bool IsSameCastOrTranslate(NodePtr &node, NodePtr &base_node) const;
  bool IsNodeNeedOptimize(NodePtr &node) const;
  bool CheckDstNode(NodePtr &out_node, bool &is_src_cast) const;
  bool IsNextNodeNeedOptimize(NodePtr &node, bool &is_src_cast) const;
  bool IsOpSupportedOptimize(NodePtr &cast_node, NodePtr &trans_node, bool &is_src_cast);
  bool CheckOpSupportOptimize(NodePtr &node, bool &is_src_cast);
  Status FuseDstNTranslates(NodePtr &node);
  bool TranslateCheckAccuracySupported(const OpDescPtr &op_desc);
};
}  // namespace ge
#endif  // GE_GRAPH_PASSES_CAST_TRANSLATE_PASS_H_
