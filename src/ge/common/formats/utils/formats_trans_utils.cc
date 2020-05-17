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

#include "common/formats/utils/formats_trans_utils.h"

#include <cstdint>

#include "common/formats/utils/formats_definitions.h"
#include "framework/common/debug/ge_log.h"
#include "framework/common/ge_inner_error_codes.h"
#include "graph/utils/type_utils.h"

namespace ge {
namespace formats {
int64_t GetCubeSizeByDataType(DataType data_type) {
  // Current cube does not support 4 bytes and longer data
  auto size = GetSizeByDataType(data_type);
  if (size <= 0) {
    GELOGE(PARAM_INVALID, "Failed to get cube size, the data type %s is invalid",
           TypeUtils::DataTypeToSerialString(data_type).c_str());
    return -1;
  } else if (size == 1) {
    return kCubeSize * 2;  // 32 bytes cube size
  } else {
    return kCubeSize;
  }
}

GE_FUNC_DEV_VISIBILITY GE_FUNC_HOST_VISIBILITY std::string ShapeToString(const GeShape &shape) {
  return ShapeToString(shape.GetDims());
}

GE_FUNC_DEV_VISIBILITY GE_FUNC_HOST_VISIBILITY std::string ShapeToString(const std::vector<int64_t> &shape) {
  return JoinToString(shape);
}

int64_t GetItemNumByShape(const std::vector<int64_t> &shape) {
  int64_t num = 1;
  for (auto dim : shape) {
    num *= dim;
  }
  return num;
}

bool CheckShapeValid(const std::vector<int64_t> &shape, const int64_t expect_dims) {
  if (expect_dims <= 0 || shape.size() != static_cast<size_t>(expect_dims)) {
    GELOGE(PARAM_INVALID, "Invalid shape, dims num %zu, expect %ld", shape.size(), expect_dims);
    return false;
  }
  return IsShapeValid(shape);
}

bool IsShapeValid(const std::vector<int64_t> &shape) {
  if (shape.empty()) {
    return false;
  }
  int64_t num = 1;
  for (auto dim : shape) {
    if (dim < 1) {
      GELOGE(PARAM_INVALID, "Invalid zero dim in the shape %s", ShapeToString(shape).c_str());
      return false;
    }
    if (kShapeItemNumMAX / dim < num) {
      GELOGE(PARAM_INVALID, "Shape overflow, the total count should be less than %ld!", kShapeItemNumMAX);
      return false;
    }
    num *= dim;
  }
  return true;
}

bool IsShapeEqual(const GeShape &src, const GeShape &dst) {
  if (src.GetDims().size() != dst.GetDims().size()) {
    return false;
  }

  for (size_t i = 0; i < src.GetDims().size(); ++i) {
    if (src.GetDim(i) != dst.GetDim(i)) {
      return false;
    }
  }
  return true;
}
}  // namespace formats
}  // namespace ge
