/**
 * Copyright 2019 Huawei Technologies Co., Ltd
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

/*!
 * \file axis_util.h
 * \brief get the axis value
 */
#ifndef COMMON_UTILS_TRANSFER_AXIS_UTIL_H_
#define COMMON_UTILS_TRANSFER_AXIS_UTIL_H_

#include <memory.h>
#include <functional>
#include <vector>

#include "external/graph/ge_error_codes.h"
#include "external/graph/types.h"
#include "framework/common/debug/ge_log.h"

namespace common {
namespace transformer {

const int32_t DIM_DEFAULT_SIZE = 4;
const uint32_t NCHW_DIMENSION_NUM = 4;

const int32_t AXIS_NCHW_DIM_N = 0;
const int32_t AXIS_NCHW_DIM_C = 1;
const int32_t AXIS_NCHW_DIM_H = 2;
const int32_t AXIS_NCHW_DIM_W = 3;

const int32_t AXIS_NHWC_DIM_N = 0;
const int32_t AXIS_NHWC_DIM_H = 1;
const int32_t AXIS_NHWC_DIM_W = 2;
const int32_t AXIS_NHWC_DIM_C = 3;

const int32_t AXIS_NC1HWC0_DIM_N = 0;
const int32_t AXIS_NC1HWC0_DIM_C1 = 1;
const int32_t AXIS_NC1HWC0_DIM_C0 = 4;
const int32_t AXIS_NC1HWC0_DIM_H = 2;
const int32_t AXIS_NC1HWC0_DIM_W = 3;

const int32_t AXIS_HWCN_DIM_H = 0;
const int32_t AXIS_HWCN_DIM_W = 1;
const int32_t AXIS_HWCN_DIM_C = 2;
const int32_t AXIS_HWCN_DIM_N = 3;

const int32_t AXIS_C1HWNCoC0_DIM_C1 = 0;
const int32_t AXIS_C1HWNCoC0_DIM_H = 1;
const int32_t AXIS_C1HWNCoC0_DIM_W = 2;
const int32_t AXIS_C1HWNCoC0_DIM_N = 3;
const int32_t AXIS_C1HWNCoC0_DIM_Co = 4;
const int32_t AXIS_C1HWNCoC0_DIM_C0 = 5;

const int32_t NDHWC_DIM_N = 0;
const int32_t NDHWC_DIM_D = 1;
const int32_t NDHWC_DIM_H = 2;
const int32_t NDHWC_DIM_W = 3;
const int32_t NDHWC_DIM_C = 4;

const int32_t NCDHW_DIM_N = 0;
const int32_t NCDHW_DIM_C = 1;
const int32_t NCDHW_DIM_D = 2;
const int32_t NCDHW_DIM_H = 3;
const int32_t NCDHW_DIM_W = 4;

const int32_t DHWCN_DIM_D = 0;
const int32_t DHWCN_DIM_H = 1;
const int32_t DHWCN_DIM_W = 2;
const int32_t DHWCN_DIM_C = 3;
const int32_t DHWCN_DIM_N = 4;

const int32_t DHWNC_DIM_D = 0;
const int32_t DHWNC_DIM_H = 1;
const int32_t DHWNC_DIM_W = 2;
const int32_t DHWNC_DIM_N = 3;
const int32_t DHWNC_DIM_C = 4;


#define CHECK_NOTNULL(val)                                       \
  do {                                                           \
    if ((val) == nullptr) {                                      \
      GELOGE(GRAPH_FAILED, "[ERROR]Parameter[%s] must not be null.", #val); \
      return false;                                              \
    }                                                            \
  } while (0)

#define CHECK(cond, log_func, return_expr) \
  do {                                     \
    if (cond) {                            \
      log_func;                            \
      return_expr;                         \
    }                                      \
  } while (0)

enum AxisValueType {
  AXIS_N = 0,
  AXIS_C = 1,
  AXIS_H = 2,
  AXIS_W = 3,
  AXIS_C1 = 4,
  AXIS_C0 = 5,
  AXIS_Co = 6,
  AXIS_D = 7,
  AXIS_BOTTOM = 8
};

int64_t DivisionCeiling(int64_t dividend, int64_t divisor);

/* Axis value is arranged as {N,C,H,W,C1,C0,...} */
/* The first parameter is old shape's dimension,
 * second is c0 and third is axis value. */
using GetAxisValueInfoByFormat =
    std::function<bool(const std::vector<int64_t>&, const uint32_t&, std::vector<int64_t>&, std::vector<int64_t>&)>;

using GetAxisValueInfoByFormatPtr = std::shared_ptr<GetAxisValueInfoByFormat>;

class AxisUtil {
 public:
  AxisUtil();
  ~AxisUtil(){};
  bool GetAxisValueByOriginFormat(const ge::Format& format, const std::vector<int64_t>& dimVec, const uint32_t& c0,
                                  std::vector<int64_t>& axisValue, std::vector<int64_t>& ndValue);
  bool HasAxisValueFunc(const ge::Format& format);

 private:
  static bool CheckParams(const std::vector<int64_t>& originalDimVec, const uint32_t& c0,
                          std::vector<int64_t>& axisValue, std::vector<int64_t>& ndValue);

  static bool GetAxisValueByNCHW(const std::vector<int64_t>& originalDimVec, const uint32_t& c0,
                                 std::vector<int64_t>& axisValue, std::vector<int64_t>& ndValue);

  static bool GetAxisValueByNHWC(const std::vector<int64_t>& originalDimVec, const uint32_t& c0,
                                 std::vector<int64_t>& axisValue, std::vector<int64_t>& ndValue);

  static bool GetAxisValueByNC1HWC0(const std::vector<int64_t>& originalDimVec, const uint32_t& c0,
                                    std::vector<int64_t>& axisValue, std::vector<int64_t>& ndValue);

  static bool GetAxisValueByFz(const std::vector<int64_t>& originalDimVec, const uint32_t& c0,
                               std::vector<int64_t>& axisValue, std::vector<int64_t>& ndValue);

  static bool GetAxisValueByHWCN(const std::vector<int64_t>& originalDimVec, const uint32_t& c0,
                                 std::vector<int64_t>& axisValue, std::vector<int64_t>& ndValue);

  static bool GetAxisValueByND(const std::vector<int64_t>& originalDimVec, const uint32_t& c0,
                               std::vector<int64_t>& axisValue, std::vector<int64_t>& ndValue);

  static bool GetAxisValueByC1HWNCoC0(const std::vector<int64_t>& originalDimVec, const uint32_t& c0,
                                      std::vector<int64_t>& axisValue, std::vector<int64_t>& ndValue);

  static bool GetAxisValueByNDHWC(const std::vector<int64_t>& original_dim_vec, const uint32_t& c0,
                                  std::vector<int64_t>& axis_value, std::vector<int64_t>& nd_value);

  static bool GetAxisValueByNCDHW(const std::vector<int64_t>& original_dim_vec, const uint32_t& c0,
                                  std::vector<int64_t>& axis_value, std::vector<int64_t>& nd_value);

  static bool GetAxisValueByDHWCN(const std::vector<int64_t>& original_dim_vec, const uint32_t& c0,
                                  std::vector<int64_t>& axis_value, std::vector<int64_t>& nd_value);

  static bool GetAxisValueByDHWNC(const std::vector<int64_t>& original_dim_vec, const uint32_t& c0,
                                  std::vector<int64_t>& axis_value, std::vector<int64_t>& nd_value);
  /* map of GetAxisValueInfoByFormat, get axis value by different original
   * formats. */
  std::map<ge::Format, GetAxisValueInfoByFormatPtr> getAxisValueFuncMap;
};
} // namespace transformer
} // namespace common

#endif // COMMON_UTILS_TRANSFER_AXIS_UTIL_H_
