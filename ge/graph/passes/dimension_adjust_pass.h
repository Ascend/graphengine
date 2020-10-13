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

#ifndef GE_GRAPH_PASSES_DIMENSION_ADJUST_PASS_H_
#define GE_GRAPH_PASSES_DIMENSION_ADJUST_PASS_H_

#include "common/debug/log.h"
#include "framework/common/debug/ge_log.h"
#include "common/ge_inner_error_codes.h"
#include "common/types.h"
#include "graph/common/omg_util.h"
#include "graph/passes/base_pass.h"
#include "graph/utils/attr_utils.h"
#include "graph/utils/graph_utils.h"
#include "graph/utils/op_desc_utils.h"
#include "inc/kernel.h"
#include "inc/kernel_factory.h"
#include "graph/passes/pass_utils.h"

namespace ge {
class DimensionAdjustPass : public BaseNodePass {
 public:
  Status Run(ge::NodePtr &node) override;
};
}  // namespace ge

#endif  // GE_GRAPH_PASSES_DIMENSION_ADJUST_PASS_H_
