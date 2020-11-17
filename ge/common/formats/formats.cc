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

#include "common/formats/formats.h"

#include <securec.h>
#include <cmath>
#include <cstring>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "common/formats/utils/formats_trans_utils.h"
#include "framework/common/debug/ge_log.h"
#include "framework/common/debug/log.h"
#include "framework/common/ge_inner_error_codes.h"
#include "graph/utils/type_utils.h"

namespace ge {
namespace formats {
GE_FUNC_DEV_VISIBILITY GE_FUNC_HOST_VISIBILITY Status TransFormat(const TransArgs &args, TransResult &result) {
  auto transfer = BuildFormatTransfer(args);
  if (transfer == nullptr) {
    std::string error = "Failed to trans data from format " +
        FmtToStr(TypeUtils::FormatToSerialString(args.src_format)) + " to " +
        FmtToStr(TypeUtils::FormatToSerialString(args.dst_format));
    GE_ERRORLOG_AND_ERRORMSG(UNSUPPORTED, error.c_str());
    return UNSUPPORTED;
  }

  auto src_shape_size = GetItemNumByShape(args.src_shape);
  if (args.data == nullptr && src_shape_size != 0) {
    GELOGE(PARAM_INVALID, "Invalid input null data");
    return PARAM_INVALID;
  }

  return transfer->TransFormat(args, result);
}

GE_FUNC_DEV_VISIBILITY GE_FUNC_HOST_VISIBILITY Status TransShape(Format src_format,
                                                                 const std::vector<int64_t> &src_shape,
                                                                 DataType data_type,
                                                                 Format dst_format,
                                                                 std::vector<int64_t> &dst_shape) {
  formats::TransArgs args;
  args.src_format = src_format;
  args.dst_format = dst_format;
  auto transfer = BuildFormatTransfer(args);
  if (transfer == nullptr) {
    std::string error = "Failed to trans data from format " +
        FmtToStr(TypeUtils::FormatToSerialString(args.src_format)) + " to " +
        FmtToStr(TypeUtils::FormatToSerialString(args.dst_format));
    GE_ERRORLOG_AND_ERRORMSG(UNSUPPORTED, error.c_str());
    return UNSUPPORTED;
  }

  return transfer->TransShape(src_format, src_shape, data_type, dst_format, dst_shape);
}

GE_FUNC_DEV_VISIBILITY GE_FUNC_HOST_VISIBILITY Status TransDataType(const CastArgs &args, TransResult &result) {
  auto transfer = BuildDataTypeTransfer(args);
  if (transfer == nullptr) {
    std::string error = "Failed to trans data from datatype " +
        FmtToStr(TypeUtils::DataTypeToSerialString(args.src_data_type)) + " to " +
        FmtToStr(TypeUtils::DataTypeToSerialString(args.dst_data_type));
    GE_ERRORLOG_AND_ERRORMSG(UNSUPPORTED, error.c_str());
    return UNSUPPORTED;
  }

  if (args.data == nullptr && args.src_data_size != 0) {
    GELOGE(PARAM_INVALID, "Invalid input null data");
    return PARAM_INVALID;
  }

  return transfer->TransDataType(args, result);
}

GE_FUNC_DEV_VISIBILITY GE_FUNC_HOST_VISIBILITY bool IsTransFormatSupport(const TransArgs &args) {
  return FormatTransferExists(args);
}

GE_FUNC_DEV_VISIBILITY GE_FUNC_HOST_VISIBILITY bool IsTransDataTypeSupport(const CastArgs &args) {
  return DataTypeTransferExists(args);
}

GE_FUNC_DEV_VISIBILITY GE_FUNC_HOST_VISIBILITY bool IsTransShapeSrcCorrect(const TransArgs &args, std::vector<int64_t> &expect_shape) {
  if (!args.src_shape.empty() && args.src_shape != expect_shape) {
    std::string error = "Failed to trans format from" +
        FmtToStr(TypeUtils::FormatToSerialString(args.src_format)) + " to " +
        FmtToStr(TypeUtils::FormatToSerialString(args.dst_format)) + ", invalid relationship between src shape " +
        FmtToStr(ShapeToString(args.src_shape)) + " and dst " +
        FmtToStr(ShapeToString(args.dst_shape));
    GE_ERRORLOG_AND_ERRORMSG(PARAM_INVALID, error.c_str());
    return false;
  }
  return true;
}

GE_FUNC_DEV_VISIBILITY GE_FUNC_HOST_VISIBILITY bool IsTransShapeDstCorrect(const TransArgs &args, std::vector<int64_t> &expect_shape) {
  if (!args.dst_shape.empty() && args.dst_shape != expect_shape) {
    std::string error = "Failed to trans format from " +
        FmtToStr(TypeUtils::FormatToSerialString(args.src_format)) + " to " +
        FmtToStr(TypeUtils::FormatToSerialString(args.dst_format)) + ", the dst shape" +
        FmtToStr(ShapeToString(args.dst_shape)) + " is invalid, expect" +
        FmtToStr(ShapeToString(expect_shape));
    GE_ERRORLOG_AND_ERRORMSG(PARAM_INVALID, error.c_str());
    return false;
  }
  return true;
}
}  // namespace formats
}  // namespace ge
