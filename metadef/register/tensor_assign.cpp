/**
 * Copyright 2020 Huawei Technologies Co., Ltd

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 * http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#include <map>
#include <memory>
#include "securec.h"
#include "framework/common/debug/ge_log.h"
#include "graph/debug/ge_log.h"
#include "graph/debug/ge_util.h"
#include "graph/utils/op_desc_utils.h"
#include "graph/utils/type_utils.h"
#include "graph/utils/attr_utils.h"
#include "register/register_error_codes.h"
#include "register/tensor_assign.h"

using GeTensorDesc = ge::GeTensorDesc;
using GeShape = ge::GeShape;

namespace domi {
namespace {
const uint32_t kExtraBytesForString = sizeof(int64_t) + 1;
const char *const kOriginElementNumAttrName = "origin_element_num";
const std::map<uint32_t, ge::DataType> data_type_map = {
    {domi::tensorflow::DataType::DT_FLOAT, ge::DataType::DT_FLOAT},
    {domi::tensorflow::DataType::DT_HALF, ge::DataType::DT_FLOAT16},
    {domi::tensorflow::DataType::DT_INT8, ge::DataType::DT_INT8},
    {domi::tensorflow::DataType::DT_INT16, ge::DataType::DT_INT16},
    {domi::tensorflow::DataType::DT_UINT16, ge::DataType::DT_UINT16},
    {domi::tensorflow::DataType::DT_UINT8, ge::DataType::DT_UINT8},
    {domi::tensorflow::DataType::DT_INT32, ge::DataType::DT_INT32},
    {domi::tensorflow::DataType::DT_INT64, ge::DataType::DT_INT64},
    {domi::tensorflow::DataType::DT_UINT32, ge::DataType::DT_UINT32},
    {domi::tensorflow::DataType::DT_UINT64, ge::DataType::DT_UINT64},
    {domi::tensorflow::DataType::DT_BOOL, ge::DataType::DT_BOOL},
    {domi::tensorflow::DataType::DT_DOUBLE, ge::DataType::DT_DOUBLE},
    {domi::tensorflow::DataType::DT_COMPLEX64, ge::DataType::DT_COMPLEX64},
    {domi::tensorflow::DataType::DT_QINT8, ge::DataType::DT_INT8},
    {domi::tensorflow::DataType::DT_QUINT8, ge::DataType::DT_UINT8},
    {domi::tensorflow::DataType::DT_QINT32, ge::DataType::DT_INT32},
    {domi::tensorflow::DataType::DT_QINT16, ge::DataType::DT_INT16},
    {domi::tensorflow::DataType::DT_QUINT16, ge::DataType::DT_UINT16},
    {domi::tensorflow::DataType::DT_COMPLEX128, ge::DataType::DT_COMPLEX128},
    {domi::tensorflow::DataType::DT_RESOURCE, ge::DataType::DT_RESOURCE},
    {domi::tensorflow::DataType::DT_BFLOAT16, ge::DataType::DT_FLOAT16},
    {domi::tensorflow::DataType::DT_STRING, ge::DataType::DT_STRING},
    {domi::tensorflow::DataType::DT_FLOAT_REF, ge::DataType::DT_FLOAT},
    {domi::tensorflow::DataType::DT_DOUBLE_REF, ge::DataType::DT_DOUBLE},
    {domi::tensorflow::DataType::DT_INT32_REF, ge::DataType::DT_INT32},
    {domi::tensorflow::DataType::DT_INT8_REF, ge::DataType::DT_INT8},
    {domi::tensorflow::DataType::DT_UINT8_REF, ge::DataType::DT_UINT8},
    {domi::tensorflow::DataType::DT_INT16_REF, ge::DataType::DT_INT16},
    {domi::tensorflow::DataType::DT_UINT16_REF, ge::DataType::DT_UINT16},
    {domi::tensorflow::DataType::DT_COMPLEX64_REF, ge::DataType::DT_COMPLEX64},
    {domi::tensorflow::DataType::DT_QINT8_REF, ge::DataType::DT_INT8},
    {domi::tensorflow::DataType::DT_QUINT8_REF, ge::DataType::DT_UINT8},
    {domi::tensorflow::DataType::DT_QINT32_REF, ge::DataType::DT_INT32},
    {domi::tensorflow::DataType::DT_QINT16_REF, ge::DataType::DT_INT16},
    {domi::tensorflow::DataType::DT_QUINT16_REF, ge::DataType::DT_UINT16},
    {domi::tensorflow::DataType::DT_COMPLEX128_REF, ge::DataType::DT_COMPLEX128},
    {domi::tensorflow::DataType::DT_RESOURCE_REF, ge::DataType::DT_RESOURCE},
    {domi::tensorflow::DataType::DT_BFLOAT16_REF, ge::DataType::DT_FLOAT16},
    {domi::tensorflow::DataType::DT_UINT32_REF, ge::DataType::DT_UINT32},
    {domi::tensorflow::DataType::DT_UINT64_REF, ge::DataType::DT_UINT64},
    {domi::tensorflow::DataType::DT_INT64_REF, ge::DataType::DT_INT64},
    {domi::tensorflow::DataType::DT_BOOL_REF, ge::DataType::DT_BOOL},
    {domi::tensorflow::DataType::DT_HALF_REF, ge::DataType::DT_FLOAT16},
    {domi::tensorflow::DataType::DT_STRING_REF, ge::DataType::DT_STRING},
};
}  // namespace

ge::DataType TensorAssign::ConvertTensorflowDataType(uint32_t tf_data_type) {
  auto search = data_type_map.find(tf_data_type);
  if (search != data_type_map.end()) {
    return search->second;
  } else {
    return ge::DataType::DT_UNDEFINED;
  }
}

bool TensorAssign::CheckBoolVal(tensorflow::DataType data_type) {
  return ((data_type == tensorflow::DT_BOOL) || (data_type == tensorflow::DT_BOOL_REF));
}

bool TensorAssign::CheckHalfVal(tensorflow::DataType data_type) {
  return ((data_type == tensorflow::DT_HALF) || (data_type == tensorflow::DT_BFLOAT16) ||
          (data_type == tensorflow::DT_HALF_REF) || (data_type == tensorflow::DT_BFLOAT16_REF));
}

bool TensorAssign::CheckFloatVal(tensorflow::DataType data_type) {
  return ((data_type == tensorflow::DT_FLOAT) || (data_type == tensorflow::DT_FLOAT_REF));
}

bool TensorAssign::CheckDoubleVal(tensorflow::DataType data_type) {
  return ((data_type == tensorflow::DT_DOUBLE) || (data_type == tensorflow::DT_DOUBLE_REF));
}

bool TensorAssign::CheckComplex64Val(tensorflow::DataType data_type) {
  return ((data_type == tensorflow::DT_COMPLEX64) || (data_type == tensorflow::DT_COMPLEX64_REF));
}

bool TensorAssign::CheckComplex128Val(tensorflow::DataType data_type) {
  return ((data_type == tensorflow::DT_COMPLEX128) || (data_type == tensorflow::DT_COMPLEX128_REF));
}

bool TensorAssign::CheckStringVal(tensorflow::DataType data_type) {
  return ((data_type == tensorflow::DT_STRING) || (data_type == tensorflow::DT_STRING_REF));
}

bool TensorAssign::CheckByte(tensorflow::DataType data_type) {
  return ((data_type == tensorflow::DT_UINT8) || (data_type == tensorflow::DT_INT8) ||
          (data_type == tensorflow::DT_QINT8) || (data_type == tensorflow::DT_QUINT8) ||
          (data_type == tensorflow::DT_UINT8_REF) || (data_type == tensorflow::DT_INT8_REF) ||
          (data_type == tensorflow::DT_QINT8_REF) || (data_type == tensorflow::DT_QUINT8_REF));
}

bool TensorAssign::CheckDoubleByte(tensorflow::DataType data_type) {
  return ((data_type == tensorflow::DT_INT16) || (data_type == tensorflow::DT_UINT16) ||
          (data_type == tensorflow::DT_QINT16) || (data_type == tensorflow::DT_QUINT16) ||
          (data_type == tensorflow::DT_INT16_REF) || (data_type == tensorflow::DT_UINT16_REF) ||
          (data_type == tensorflow::DT_QINT16_REF) || (data_type == tensorflow::DT_QUINT16_REF));
}

bool TensorAssign::CheckSignedFourByte(tensorflow::DataType data_type) {
  return ((data_type == tensorflow::DT_INT32) || (data_type == tensorflow::DT_QINT32) ||
          (data_type == tensorflow::DT_INT32_REF) || (data_type == tensorflow::DT_QINT32_REF));
}

bool TensorAssign::CheckUnsignedFourByte(tensorflow::DataType data_type) {
  return ((data_type == tensorflow::DT_UINT32) || (data_type == tensorflow::DT_UINT32_REF));
}

bool TensorAssign::CheckSignedEightByte(tensorflow::DataType data_type) {
  return ((data_type == tensorflow::DT_INT64) || (data_type == tensorflow::DT_INT64_REF));
}

bool TensorAssign::CheckUnsignedEightByte(tensorflow::DataType data_type) {
  return ((data_type == tensorflow::DT_UINT64) || (data_type == tensorflow::DT_UINT64_REF));
}

Status TensorAssign::GetDoubleByteVal(int32_t val_size, const google::protobuf::RepeatedField<int32> &val_vector,
                                      int count, GeTensorPtr &weight) {
  GE_CHECK_NOTNULL(weight);
  bool zerosLike = (count != val_size && val_size == 1);
  uint16_t *addr = new (std::nothrow) uint16_t[count]();
  GE_CHECK_NOTNULL(addr);
  int minCount = (count > val_size) ? val_size : count;
  if (!zerosLike) {
    for (int32_t i = 0; i < minCount; i++) {
      *(addr + i) = static_cast<uint16_t>(val_vector.Get(i));
    }
    for (int32_t i = minCount; i < count; i++) {
      *(addr + i) = static_cast<uint16_t>(val_vector.Get(minCount - 1));
    }
  } else {
    for (int32_t i = 0; i < count; i++) {
      *(addr + i) = static_cast<uint16_t>(val_vector.Get(0));
    }
  }
  weight->SetData(reinterpret_cast<uint8_t *>(addr), count * sizeof(uint16_t));
  GE_DELETE_NEW_ARRAY(addr);
  return SUCCESS;
}

Status TensorAssign::GetByteVal(int32_t val_size, const google::protobuf::RepeatedField<int32> &val_vector, int count,
                                GeTensorPtr &weight) {
  GE_CHECK_NOTNULL(weight);
  bool zerosLike = (count != val_size && val_size == 1);
  uint8_t *addr = new (std::nothrow) uint8_t[count]();
  GE_CHECK_NOTNULL(addr);
  int minCount = (count > val_size) ? val_size : count;
  if (!zerosLike) {
    for (int32_t i = 0; i < minCount; i++) {
      *(addr + i) = static_cast<uint8_t>(val_vector.Get(i));
    }
    for (int32_t i = minCount; i < count; i++) {
      *(addr + i) = static_cast<uint8_t>(val_vector.Get(minCount - 1));
    }
  } else {
    for (int32_t i = 0; i < count; i++) {
      *(addr + i) = static_cast<uint8_t>(val_vector.Get(0));
    }
  }
  weight->SetData(addr, count * sizeof(uint8_t));
  GE_DELETE_NEW_ARRAY(addr);
  return SUCCESS;
}

Status TensorAssign::GetStringVal(int32_t val_size, const google::protobuf::RepeatedPtrField<std::string> &val_vector,
                                  int count, GeTensorPtr &weight) {
  GE_CHECK_NOTNULL(weight);
  bool flag = (count != val_size && val_size == 1);
  int min_count = (count > val_size) ? val_size : count;
  size_t total_size = 0;
  if (!flag) {
    for (int32_t i = 0; i < min_count; i++) {
      // extra 8 bytes store pointer of string
      // extra 1 byte store '\0'
      total_size += (val_vector[i].size() + kExtraBytesForString);
    }
    total_size += (count - min_count) * kExtraBytesForString;
    std::unique_ptr<char[]> addr(new (std::nothrow) char[total_size]());
    GE_CHECK_NOTNULL(addr);
    uint64_t *p = reinterpret_cast<uint64_t *>(addr.get());
    // front some bytes store pointer of each string
    char *raw_data = addr.get() + count * sizeof(uint64_t);
    for (int32_t i = 0; i < count; ++i) {
      p[i] = reinterpret_cast<uintptr_t>(raw_data);
      if (i < val_size) {
        const string &str = val_vector.Get(i);
        CHECK_FALSE_EXEC(memcpy_s(raw_data, str.size() + 1, str.c_str(), str.size() + 1) == EOK,
                         GELOGW("call memcpy_s fail!"));
        raw_data += (str.size() + 1);
      } else {
        raw_data += 1;
      }
    }
    weight->SetData(reinterpret_cast<const uint8_t *>(addr.get()), total_size);
  } else {
    const string &str = val_vector.Get(0);
    // extra 8 bytes store pointer of string
    // extra 1 byte store '\0'
    total_size = (str.size() + kExtraBytesForString) * count;
    std::unique_ptr<char[]> addr(new (std::nothrow) char[total_size]());
    GE_CHECK_NOTNULL(addr);
    uint64_t *p = reinterpret_cast<uint64_t *>(addr.get());
    // front some bytes store pointer of each string
    char *raw_data = addr.get() + count * sizeof(uint64_t);
    for (int32_t i = 0; i < count; ++i) {
      p[i] = reinterpret_cast<uintptr_t>(raw_data);
      CHECK_FALSE_EXEC(memcpy_s(raw_data, str.size() + 1, str.c_str(), str.size() + 1) == EOK,
                       GELOGW("call memcpy_s fail!"));
      raw_data += (str.size() + 1);
    }
    weight->SetData(reinterpret_cast<const uint8_t *>(addr.get()), total_size);
  }
  return SUCCESS;
}

void TensorAssign::SetGeTensorWeightData(const TensorProto &tensor, int32_t val_size, int count, GeTensorPtr &weight) {
  tensorflow::DataType data_type = tensor.dtype();
  if (CheckFloatVal(data_type)) {
    (void)GetVal(val_size, tensor.float_val(), count, weight);
  } else if (CheckComplex64Val(data_type)) {
    (void)GetVal(val_size, tensor.scomplex_val(), count, weight);
  } else if (CheckSignedFourByte(data_type)) {
    (void)GetVal(val_size, tensor.int_val(), count, weight);
  } else if (CheckUnsignedFourByte(data_type)) {
    (void)GetVal(val_size, tensor.uint32_val(), count, weight);
  } else if (CheckSignedEightByte(data_type)) {
    (void)GetVal(val_size, tensor.int64_val(), count, weight);
  } else if (CheckUnsignedEightByte(data_type)) {
    (void)GetVal(val_size, tensor.uint64_val(), count, weight);
  } else if (CheckBoolVal(data_type)) {
    (void)GetVal(val_size, tensor.bool_val(), count, weight);
  } else if (CheckStringVal(data_type)) {
    (void)GetStringVal(val_size, tensor.string_val(), count, weight);
  } else if (CheckHalfVal(data_type)) {
    (void)GetDoubleByteVal(val_size, tensor.half_val(), count, weight);
  } else if (CheckDoubleByte(data_type)) {
    (void)GetDoubleByteVal(val_size, tensor.int_val(), count, weight);
  } else if (CheckByte(data_type)) {
    (void)GetByteVal(val_size, tensor.int_val(), count, weight);
  } else if (CheckDoubleVal(data_type)) {
    (void)GetVal(val_size, tensor.double_val(), count, weight);
  } else if (CheckComplex128Val(data_type)) {
    (void)GetVal(val_size, tensor.dcomplex_val(), count, weight);
  } else {
    GELOGI("data_type:%s.", DataType_Name(data_type).c_str());
  }
}

void TensorAssign::SetWeightData(tensorflow::DataType data_type, int count, const std::string &tensor_content,
                                 GeTensorPtr &weight) {
  if (weight == nullptr) {
    GE_LOGE("weight is nullptr.");
    return;
  }
  GELOGD("Set data from tensor_content, count = %d ,data_type = %s.", count, DataType_Name(data_type).c_str());
  if (CheckByte(data_type)) {
    weight->SetData(reinterpret_cast<const uint8_t *>(tensor_content.data()), count * sizeof(uint8_t));
  } else if (CheckBoolVal(data_type)) {
    weight->SetData(reinterpret_cast<const uint8_t *>(tensor_content.data()), count * sizeof(bool));
  } else if (CheckHalfVal(data_type) || CheckDoubleByte(data_type)) {
    weight->SetData(reinterpret_cast<const uint8_t *>(tensor_content.data()), count * sizeof(uint16_t));
  } else if (CheckSignedFourByte(data_type) || CheckUnsignedFourByte(data_type)) {
    weight->SetData(reinterpret_cast<const uint8_t *>(tensor_content.data()), count * sizeof(uint32_t));
  } else if (CheckSignedEightByte(data_type) || CheckUnsignedEightByte(data_type)) {
    weight->SetData(reinterpret_cast<const uint8_t *>(tensor_content.data()), count * sizeof(uint64_t));
  } else if (CheckDoubleVal(data_type) || CheckComplex128Val(data_type)) {
    weight->SetData(reinterpret_cast<const uint8_t *>(tensor_content.data()), count * sizeof(double));
  } else {
    weight->SetData(reinterpret_cast<const uint8_t *>(tensor_content.data()), count * sizeof(float));
  }
}

Status TensorAssign::SetGeTensor(const TensorProto &tensor, GeTensorPtr &weight) {
  GE_CHECK_NOTNULL(weight);
  std::map<tensorflow::DataType, int32_t> datatype_val_size_map = {
      {tensorflow::DT_FLOAT, tensor.float_val().size()},
      {tensorflow::DT_INT32, tensor.int_val().size()},
      {tensorflow::DT_INT64, tensor.int64_val().size()},
      {tensorflow::DT_BOOL, tensor.bool_val().size()},
      {tensorflow::DT_HALF, tensor.half_val().size()},
      {tensorflow::DT_INT8, tensor.int_val().size()},
      {tensorflow::DT_UINT8, tensor.int_val().size()},
      {tensorflow::DT_INT16, tensor.int_val().size()},
      {tensorflow::DT_UINT16, tensor.int_val().size()},
      {tensorflow::DT_DOUBLE, tensor.double_val().size()},
      {tensorflow::DT_STRING, tensor.string_val().size()},
      {tensorflow::DT_QINT8, tensor.int_val().size()},
      {tensorflow::DT_QINT16, tensor.int_val().size()},
      {tensorflow::DT_QINT32, tensor.int_val().size()},
      {tensorflow::DT_QUINT8, tensor.int_val().size()},
      {tensorflow::DT_QUINT16, tensor.int_val().size()},
      {tensorflow::DT_COMPLEX64, tensor.scomplex_val().size()},
      {tensorflow::DT_COMPLEX128, tensor.dcomplex_val().size()},
      {tensorflow::DT_BFLOAT16, tensor.half_val().size()},
      {tensorflow::DT_UINT32, tensor.uint32_val().size()},
      {tensorflow::DT_UINT64, tensor.uint64_val().size()},
      {tensorflow::DT_RESOURCE, tensor.resource_handle_val().size()},
      {tensorflow::DT_VARIANT, tensor.variant_val().size()},
      {tensorflow::DT_FLOAT_REF, tensor.float_val().size()},
      {tensorflow::DT_INT32_REF, tensor.int_val().size()},
      {tensorflow::DT_INT64_REF, tensor.int64_val().size()},
      {tensorflow::DT_BOOL_REF, tensor.bool_val().size()},
      {tensorflow::DT_HALF_REF, tensor.half_val().size()},
      {tensorflow::DT_INT8_REF, tensor.int_val().size()},
      {tensorflow::DT_UINT8_REF, tensor.int_val().size()},
      {tensorflow::DT_INT16_REF, tensor.int_val().size()},
      {tensorflow::DT_UINT16_REF, tensor.int_val().size()},
      {tensorflow::DT_DOUBLE_REF, tensor.double_val().size()},
      {tensorflow::DT_STRING_REF, tensor.string_val().size()},
      {tensorflow::DT_QINT8_REF, tensor.int_val().size()},
      {tensorflow::DT_QINT16_REF, tensor.int_val().size()},
      {tensorflow::DT_QINT32_REF, tensor.int_val().size()},
      {tensorflow::DT_QUINT8_REF, tensor.int_val().size()},
      {tensorflow::DT_QUINT16_REF, tensor.int_val().size()},
      {tensorflow::DT_COMPLEX64_REF, tensor.scomplex_val().size()},
      {tensorflow::DT_COMPLEX128_REF, tensor.dcomplex_val().size()},
      {tensorflow::DT_BFLOAT16_REF, tensor.half_val().size()},
      {tensorflow::DT_UINT32_REF, tensor.uint32_val().size()},
      {tensorflow::DT_UINT64_REF, tensor.uint64_val().size()},
      {tensorflow::DT_RESOURCE_REF, tensor.resource_handle_val().size()},
      {tensorflow::DT_VARIANT_REF, tensor.variant_val().size()},
  };
  tensorflow::DataType data_type = tensor.dtype();
  int32_t datatype_val_size = 0;

  auto iter = datatype_val_size_map.find(data_type);
  if (iter != datatype_val_size_map.end()) {
    datatype_val_size = iter->second;
  } else {
    GE_CHECK_GE(data_type, 0);
    GE_LOGE("datatype:%s not support.", DataType_Name(data_type).c_str());
    return FAILED;
  }

  std::vector<int64_t> shape_vec;
  // There is tensor shape, get the dimension
  int count = 1;
  GE_IF_BOOL_EXEC(
      tensor.has_tensor_shape(), const tensorflow::TensorShapeProto &tensor_shape = tensor.tensor_shape();
      for (int i = 0; i < tensor_shape.dim_size(); i++) {
        const tensorflow::TensorShapeProto_Dim &shape_dim = tensor_shape.dim(i);
        shape_vec.push_back(shape_dim.size());
        int64_t dim = shape_vec[i];
        // tensorflow support weights shape [0],have no weights
        GE_CHK_BOOL_TRUE_EXEC_WITH_LOG(dim < 0, return FAILED, "Dim size invalid");
        GE_CHK_BOOL_TRUE_EXEC_WITH_LOG((count != 0 && dim >= INT64_MAX / count), return FAILED,
                                       "Dim size exceeds INT64_MAX");
        count *= dim;
      });
  GeShape shape(shape_vec);
  GeTensorDesc tmp_desc = weight->GetTensorDesc();
  tmp_desc.SetShape(shape);

  // Fixed input ND
  tmp_desc.SetFormat(ge::Format::FORMAT_ND);
  tmp_desc.SetOriginFormat(ge::Format::FORMAT_ND);

  weight->SetTensorDesc(tmp_desc);

  if (datatype_val_size > 0) {
    SetGeTensorWeightData(tensor, datatype_val_size, count, weight);
    int64_t origin_element_num = static_cast<int64_t>(datatype_val_size);
    GE_CHK_BOOL_EXEC(ge::AttrUtils::SetInt(weight->MutableTensorDesc(), kOriginElementNumAttrName, origin_element_num),
                     return FAILED, "Set origin element num failed.");
  } else if (!tensor.tensor_content().empty()) {
    const auto &tensor_content = tensor.tensor_content();
    SetWeightData(data_type, count, tensor_content, weight);
  } else {
    if (count == 0) {
      GELOGI("Empty tensor, has no data.");
      return SUCCESS;
    }
    GE_LOGE("value Attr tensor should have val() or tensor_content");
    return FAILED;
  }

  return SUCCESS;
}

Status TensorAssign::SetGeTensorDataType(int64_t data_type, GeTensorPtr &weight) {
  GE_CHECK_NOTNULL(weight);
  GeTensorDesc tmp_desc = weight->GetTensorDesc();
  tmp_desc.SetDataType(ge::DataType(data_type));
  weight->SetTensorDesc(tmp_desc);
  return SUCCESS;
}
}  // namespace domi
