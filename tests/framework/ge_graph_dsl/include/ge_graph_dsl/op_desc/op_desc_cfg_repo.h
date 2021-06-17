/**
 * Copyright 2021 Huawei Technologies Co., Ltd
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

#ifndef H600DEDD4_D5B9_4803_AF48_262B2C4FBA94d
#define H600DEDD4_D5B9_4803_AF48_262B2C4FBA94c

#include "easy_graph/infra/singleton.h"
#include "ge_graph_dsl/ge.h"
#include "ge_graph_dsl/op_desc/op_type.h"

GE_NS_BEGIN

struct OpDescCfg;

SINGLETON(OpDescCfgRepo) {
  const OpDescCfg *FindBy(const OpType &);
};

GE_NS_END

#endif /* H600DEDD4_D5B9_4803_AF48_262B2C4FBA94 */
