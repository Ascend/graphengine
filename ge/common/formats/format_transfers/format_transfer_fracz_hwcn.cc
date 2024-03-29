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

#include "common/formats/format_transfers/format_transfer_fracz_hwcn.h"

#include <securec.h>
#include <memory>

#include "common/formats/utils/formats_definitions.h"
#include "common/formats/utils/formats_trans_utils.h"
#include "framework/common/debug/ge_log.h"
#include "framework/common/debug/log.h"
#include "graph/utils/type_utils.h"

namespace ge {
namespace formats {
namespace {
bool CheckDataTypeSupported(const DataType &data_type) { return GetSizeByDataType(data_type) > 0; }

Status CheckArgsForFracZToHwcn(const TransArgs &args) {
  auto src_shape = args.src_shape;
  auto dst_shape = args.dst_shape;
  if (args.src_format != FORMAT_FRACTAL_Z || args.dst_format != FORMAT_HWCN) {
    std::string error = "Dose not support trans format from " +
        FmtToStr(TypeUtils::FormatToSerialString(args.src_format)) + " to " +
        FmtToStr(TypeUtils::FormatToSerialString(args.dst_format));
    GE_ERRORLOG_AND_ERRORMSG(UNSUPPORTED, error.c_str());
    return UNSUPPORTED;
  }
  if (!CheckDataTypeSupported(args.src_data_type)) {
    GELOGE(UNSUPPORTED, "Failed to trans shape from FORMAT_FRACTAL_Z to HWCN, invalid data type %s",
           TypeUtils::DataTypeToSerialString(args.src_data_type).c_str());
    return UNSUPPORTED;
  }
  if (!CheckShapeValid(src_shape, kFracZDimsNum)) {
    GELOGE(PARAM_INVALID, "Failed to check src shape %s", ShapeToString(src_shape).c_str());
    return PARAM_INVALID;
  }
  if (!CheckShapeValid(dst_shape, kHwcnDimsNum)) {
    GELOGE(PARAM_INVALID, "Failed to check dst shape %s", ShapeToString(dst_shape).c_str());
    return PARAM_INVALID;
  }
  int64_t c0 = GetCubeSizeByDataType(args.src_data_type);
  if (c0 < 0) {
    return PARAM_INVALID;
  }
  int64_t c1 = Ceil(dst_shape.at(kHwcnC), c0);
  int64_t n0 = Ceil(dst_shape.at(kHwcnN), static_cast<int64_t>(kNiSize));
  if (src_shape.at(kFracZHWC1) != dst_shape.at(kHwcnH) * dst_shape.at(kHwcnW) * c1 || src_shape.at(kFracZC0) != c0 ||
      src_shape.at(kFracZNi) != kNiSize || src_shape.at(kFracZN0) != n0) {
    std::string error = "Failed to check relationship between src shape" +
        FmtToStr(ShapeToString(src_shape)) + " and dst shape" +
        FmtToStr(ShapeToString(dst_shape));
    GE_ERRORLOG_AND_ERRORMSG(UNSUPPORTED, error.c_str());
    return PARAM_INVALID;
  } 

  return SUCCESS;
}

Status GetDstDataAfterTrans(const TransArgs &args, TransResult &result, const int size, const int64_t total_size) {
  std::shared_ptr<uint8_t> dst(new (std::nothrow) uint8_t[total_size], std::default_delete<uint8_t[]>());
  if (dst == nullptr) {
    GELOGE(OUT_OF_MEMORY, "Failed to trans format from %s to %s, can not alloc the memory for dst buf %ld, shape %s",
           TypeUtils::FormatToSerialString(args.src_format).c_str(),
           TypeUtils::FormatToSerialString(args.dst_format).c_str(), total_size, ShapeToString(args.dst_shape).c_str());
    return OUT_OF_MEMORY;
  }

  auto n0 = args.src_shape.at(kFracZN0);
  auto ni = args.src_shape.at(kFracZNi);
  auto c0 = args.src_shape.at(kFracZC0);
  auto h = args.dst_shape.at(kHwcnH);
  auto w = args.dst_shape.at(kHwcnW);
  auto c = args.dst_shape.at(kHwcnC);
  auto n = args.dst_shape.at(kHwcnN);
  int64_t nc = ni * n0;
  int64_t ncc0 = nc * c0;
  int64_t wncc0 = w * ncc0;
  int64_t hwncc0 = h * wncc0;
  int64_t cn = c * n;
  int64_t wcn = w * cn;

  for (int64_t h_idx = 0; h_idx < h; h_idx++) {
    int64_t h_head_addr = h_idx * wcn;
    for (int64_t w_idx = 0; w_idx < w; w_idx++) {
      int64_t w_head_addr = h_head_addr + w_idx * cn;
      for (int64_t c_idx = 0; c_idx < c; c_idx++) {
        int64_t c_head_addr = w_head_addr + c_idx * n;
        for (int64_t n_idx = 0; n_idx < n; n_idx++) {
          int64_t dst_idx = c_head_addr + n_idx;
          int64_t c1_idx = c_idx / c0;
          int64_t c0_idx = c_idx % c0;
          int64_t nc_idx = n_idx;
          int64_t src_idx = c1_idx * hwncc0 + h_idx * wncc0 + w_idx * ncc0 + nc_idx * c0 + c0_idx;
          auto src_offset = src_idx * size;
          auto dst_offset = dst_idx * size;
          auto protected_size = total_size - dst_offset < static_cast<int64_t>(SECUREC_MEM_MAX_LEN) ?
                                total_size - dst_offset : static_cast<int64_t>(SECUREC_MEM_MAX_LEN);
          auto ret = memcpy_s(dst.get() + dst_offset, static_cast<size_t>(protected_size), args.data + src_offset,
                              static_cast<size_t>(size));
          if (ret != EOK) {
            GELOGE(INTERNAL_ERROR,
                   "Failed to copy data from FracZ offset %ld to HWCN[%ld, %ld, %ld, %ld] "
                   "offset %ld, err-code %d",
                   src_offset, h_idx, w_idx, c_idx, n_idx, dst_offset, ret);
            return INTERNAL_ERROR;
          }
        }
      }
    }
  }
  result.data = dst;
  result.length = static_cast<size_t>(total_size);
  return SUCCESS;
}
}  // namespace

Status FormatTransferFracZHwcn::TransFormat(const TransArgs &args, TransResult &result) {
  if (CheckArgsForFracZToHwcn(args) != SUCCESS) {
    return PARAM_INVALID;
  }
  int size = GetSizeByDataType(args.src_data_type);
  auto total_size = GetItemNumByShape(args.dst_shape) * size;
  if (total_size <= 0) {
    int64_t src_size = GetItemNumByShape(args.src_shape);
    if (total_size == 0 && src_size == 0) {
      result.length = static_cast<size_t>(total_size);
      return SUCCESS;
    }

    GELOGE(INTERNAL_ERROR, "Get %ld total size from dst shape %s, src shape %s", total_size,
        ShapeToString(args.dst_shape).c_str(), ShapeToString(args.src_shape).c_str());
    return PARAM_INVALID;
  }
  GELOGD("Begin to trans format from FracZ to HWCN, src shape %s, data type %s, dst shape %s, memory size %ld",
         ShapeToString(args.src_shape).c_str(), TypeUtils::DataTypeToSerialString(args.src_data_type).c_str(),
         ShapeToString(args.dst_shape).c_str(), total_size);
  if (GetDstDataAfterTrans(args, result, size, total_size) != SUCCESS) {
    GELOGE(INTERNAL_ERROR, "Failed to get data after trans, src shape %s, data type %s, dst shape %s, memory size %ld",
           ShapeToString(args.src_shape).c_str(), TypeUtils::DataTypeToSerialString(args.src_data_type).c_str(),
           ShapeToString(args.dst_shape).c_str(), total_size);
    return INTERNAL_ERROR;
  }
  return SUCCESS;
}

Status FormatTransferFracZHwcn::TransShape(Format src_format, const std::vector<int64_t> &src_shape, DataType data_type,
                                           Format dst_format, std::vector<int64_t> &dst_shape) {
  GELOGD("The shape derivation from FracZ to HWCN is not unique. Trans shape in this direction is not supported");
  return ACL_ERROR_GE_TRANSSHAPE_FORMAT_INVALID;
}

REGISTER_FORMAT_TRANSFER(FormatTransferFracZHwcn, FORMAT_FRACTAL_Z, FORMAT_HWCN)
}  // namespace formats
}  // namespace ge
