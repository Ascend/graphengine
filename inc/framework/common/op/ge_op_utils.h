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

#ifndef INC_FRAMEWORK_COMMON_OP_GE_OP_UTILS_H_
#define INC_FRAMEWORK_COMMON_OP_GE_OP_UTILS_H_

#include <memory>
#include <vector>

#include "common/op/attr_value_util.h"
#include "register/register_types.h"
#include "register/register_error_codes.h"
#include "common/util.h"
#include "graph/attr_value.h"
#include "graph/ge_tensor.h"
#include "graph/node.h"
#include "graph/op_desc.h"
#include "proto/insert_op.pb.h"

namespace ge {
using domi::Status;

// Add Sub Mul
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t ADD_INPUT_NUM;
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t SUB_INPUT_NUM;
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t MUL_INPUT_NUM;

// Permute
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const int32_t PERMUTE_ORDER_NUM;

// Ssd PriroBox
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const double SSD_PRIORBOX_ASPECT_RATIO_VALUE;

FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t STRIDEDSLICE_INPUT_NUM;

// Switch
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t SWITCH_INPUT_NUM;
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t SWITCH_OUTPUT_NUM;
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t SWITCH_FALSE_OUTPUT;
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t SWITCH_TRUE_OUTPUT;
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t SWITCH_DATA_INPUT;
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t SWITCH_PRED_INPUT;

// FunctionOp
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t IF_COND_INPUT;
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t FOR_START_INPUT;
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t FOR_LIMIT_INPUT;
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t FOR_DELTA_INPUT;
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const uint32_t FOR_DATA_INPUT;

FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY extern const int NORMAL_TENSOR_SIZE;

class OpUtils {
 public:
  ///
  /// @ingroup domi_ome
  /// @brief Check whether check_value is in [min_enum_value, max_enum_value]
  /// @return true Within
  /// @return false out of range
  //
  static inline bool CheckEnumValid(int32_t check_value, int32_t min_enum_value, int32_t max_enum_value) {
    return check_value < min_enum_value ? false : (check_value >= max_enum_value ? false : true);
  }

  ///
  /// @ingroup domi_omg
  /// @brief Determine whether to manually calculate the tensor size based on the values of format and dim
  /// @param [in] format, Format information of the tensor
  /// @param [in] real_dim_cnt, Tensor dim
  /// @return true Manually calculate the size based on dim and datatype
  /// @return false skip
  ///
  static bool IsComputDimsSize(const int32_t format, const uint32_t real_dim_cnt);

  ///
  /// @brief Extract AIPP parameters from AttrDefMap and splice them
  /// @param [in] aipp_attr attr of operator
  /// @param [out] aipp_params aipp parameters
  /// @return enum of tagCCAippInputFormat
  ///
  static Status ConvertAippParams(const GeAttrValue::NamedAttrs &aipp_attr, domi::AippOpParams *aipp_params);
  static Status TransferDim(const std::vector<int64_t> &dim, std::vector<int64_t> &dim_vector);
  template <typename T>
  static void SliceData(const std::vector<char *> &input, int64_t chunk_size, std::vector<char *> &output,
                        int64_t begin, int64_t out_dim, int64_t stride);
  template <typename T>
  static Status SetDataByDataType(size_t out_size, const std::vector<char *> &chunk_input,
                                  const std::vector<char *> &chunk_output, GeTensor *output);
  template <typename T>
  static Status SetOutputSliceDataByDataType(void *data, int64_t data_size, const std::vector<int64_t> &input_dims,
                                             const std::vector<int64_t> &begin, const std::vector<int64_t> &output_dims,
                                             ge::GeTensor *output, const std::vector<int64_t> &stride);
  static Status SetOutputSliceData(void *data, int64_t data_size, int32_t data_type, std::vector<int64_t> &input_dims,
                                   std::vector<int64_t> &begin, std::vector<int64_t> &output_dims, ge::GeTensor *output,
                                   std::vector<int64_t> &stride);

  ///
  /// @ingroup domi_omg
  /// @brief Convert the convolutional weight data from [h, w, c, k] to [k, c, h, w]
  /// @param [in] input Weight data in HWCK format
  /// @param [in] H value of H dimension
  /// @param [in] W value of W dimension
  /// @param [in] C value of C dimension
  /// @param [in] K value of K dimension
  /// @param [out] output Data pointer after conversion. The format is KCHW.
  ///
  static void TransDataHWCK2KCHW(const void *input, int64_t H, int64_t W, int64_t C, int64_t K, void **output);
  ///
  /// @ingroup domi_omg
  /// @brief Converts the convolutional weight data from [k, c, h, w] to [h, w, c, k].
  /// @param [in] input Weight data in HWCK format
  /// @param [in] K value of K dimension
  /// @param [in] C value of C dimension
  /// @param [in] H value of H dimension
  /// @param [in] W value of W dimension
  /// @param [out] output Data pointer after conversion. The format is HWCK
  ///
  static void TransDataKCHW2HWCK(const void *input, int64_t K, int64_t C, int64_t H, int64_t W, void *output);
  
  static vector<ConstGeTensorPtr> GetWeights(const ge::Node &node);
  static vector<ConstGeTensorPtr> GetWeights(ge::ConstNodePtr node);
  static vector<GeTensorPtr> MutableWeights(const ge::Node &node);
  static vector<GeTensorPtr> MutableWeights(const ge::NodePtr node);
  static Status SetWeights(ge::Node &node, const vector<ge::GeTensorPtr> &weights);
  static Status SetWeights(ge::NodePtr node, const vector<ge::GeTensorPtr> &weights);
  static Status GetShapeDataFromConstTensor(const ConstGeTensorPtr &tensor, DataType type, std::vector<int64_t> &dims);

 private:
  static uint32_t GetRealDimCnt(const GeTensorDesc &tensor_desc);
};
}  // namespace ge
#endif  // INC_FRAMEWORK_COMMON_OP_GE_OP_UTILS_H_
