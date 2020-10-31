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

#ifndef INC_COMMON_OPSKERNEL_OPS_KERNEL_INFO_TYPES_H_
#define INC_COMMON_OPSKERNEL_OPS_KERNEL_INFO_TYPES_H_

#include <stdint.h>
#include <string>
#include <vector>
#include "graph/buffer.h"
#include "runtime/rt_model.h"

using std::string;

namespace ge {
/*lint -e148*/
struct RunContext {
  rtModel_t model;
  rtStream_t stream;
  uint64_t sessionId;
  uint64_t dataMemSize;
  uint8_t *dataMemBase;
  uint64_t weightMemSize;
  uint8_t *weightMemBase;
  ge::Buffer weightsBuffer;
  std::vector<rtStream_t> graphStreamList;  // all streams of graph, order by ge stream id(0,1,...)
  std::vector<rtEvent_t> graphEventList;    // all events of graph, order by ge event id(0,1,...)
  std::vector<rtLabel_t> graphLabelList;    // all labels of graph, order by ge label id(0,1,...)
};

/*lint +e148*/

struct Task {
  uint32_t id;
  uint16_t type;
  void *stream;
  void *event;
};

struct OpInfo {
  string engine;  // which engin
  /*lint -e148*/
  string opKernelLib;  // which opsKernelStore
  int computeCost;     // compute cost
  bool flagPartial;    // whether to support is related to shape
  bool flagAsync;      // Whether to support asynchronous
  bool isAtomic;       // whether to support atomic addr clean
  string opFileName;   // op file name
  string opFuncName;   // op function name
};
}  // namespace ge

#endif  // INC_COMMON_OPSKERNEL_OPS_KERNEL_INFO_TYPES_H_
