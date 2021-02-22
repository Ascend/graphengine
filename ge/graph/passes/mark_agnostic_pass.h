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
#ifndef GE_MARK_AGNOSTIC_PASS_H_
#define GE_MARK_AGNOSTIC_PASS_H_

#include "inc/graph_pass.h"

namespace ge {
class MarkAgnosticPass : public GraphPass {
 public:
  Status Run(ComputeGraphPtr graph) override;

 private:
  bool IsWhileLoop(const NodePtr& node, NodePtr& enter, NodePtr& next);
  Status HandWhileLoop(const NodePtr& node);
  Status SetContinuousAttr(const NodePtr& node, const std::vector<uint32_t>& index);
};
}

#endif  //GE_MARK_AGNOSTIC_PASS_H_
