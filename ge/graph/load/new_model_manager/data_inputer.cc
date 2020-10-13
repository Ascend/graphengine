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

#include "graph/load/new_model_manager/data_inputer.h"

#include <securec.h>

#include "common/debug/log.h"
#include "common/scope_guard.h"
#include "common/types.h"

namespace ge {
domi::Status InputDataWrapper::Init(const InputData &input, const OutputData &output) {
  GE_CHK_BOOL_RET_STATUS(!is_init, domi::INTERNAL_ERROR, "InputDataWrapper is re-initialized");

  input_ = input;
  output_ = output;
  is_init = true;
  return domi::SUCCESS;
}
}  // namespace ge
