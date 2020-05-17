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

#ifndef GE_COMMON_FORMATS_UTILS_FORMATS_TRANS_UTILS_H_
#define GE_COMMON_FORMATS_UTILS_FORMATS_TRANS_UTILS_H_

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "external/graph/types.h"
#include "graph/ge_tensor.h"

namespace ge {
namespace formats {
int64_t GetCubeSizeByDataType(DataType data_type);

/**
 * Convert a vector to a string using ','
 * @tparam T
 * @param vec
 * @return
 */
template <typename T>
std::string JoinToString(const std::vector<T> &vec) {
  std::stringstream ss;
  bool first = true;
  for (auto &ele : vec) {
    if (first) {
      first = false;
    } else {
      ss << ",";
    }
    ss << ele;
  }
  return ss.str();
}

std::string ShapeToString(const GeShape &shape);

std::string ShapeToString(const std::vector<int64_t> &shape);

int64_t GetItemNumByShape(const std::vector<int64_t> &shape);

bool CheckShapeValid(const std::vector<int64_t> &shape, const int64_t expect_dims);

bool IsShapeValid(const std::vector<int64_t> &shape);

bool IsShapeEqual(const GeShape &src, const GeShape &dst);

template <typename T>
T Ceil(T n1, T n2) {
  return (n2 != 0) ? (n1 - 1) / n2 + 1 : 0;
}

}  // namespace formats
}  // namespace ge
#endif  // GE_COMMON_FORMATS_UTILS_FORMATS_TRANS_UTILS_H_
