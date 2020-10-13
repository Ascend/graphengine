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

#ifndef GE_GRAPH_LOAD_NEW_MODEL_MANAGER_DAVINCI_MODEL_PARSER_H_
#define GE_GRAPH_LOAD_NEW_MODEL_MANAGER_DAVINCI_MODEL_PARSER_H_

#include <securec.h>
#include <memory>

#include "common/debug/log.h"
#include "common/ge_types.h"
#include "common/model_parser/base.h"
#include "common/types.h"
#include "common/util.h"

namespace ge {
class DavinciModelParser : public ModelParserBase {
 public:
  ///
  /// @ingroup hiai
  /// @brief constructor
  ///
  DavinciModelParser();

  ///
  /// @ingroup hiai
  /// @brief destructor
  ///
  ~DavinciModelParser();
};
}  // namespace ge

#endif  // GE_GRAPH_LOAD_NEW_MODEL_MANAGER_DAVINCI_MODEL_PARSER_H_
