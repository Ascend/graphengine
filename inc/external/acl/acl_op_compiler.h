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

#ifndef INC_EXTERNAL_ACL_ACL_OP_COMPILER_H_
#define INC_EXTERNAL_ACL_ACL_OP_COMPILER_H_

#include "acl_base.h"
#include "acl_op.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum aclCompileType { ACL_COMPILE_SYS, ACL_COMPILE_UNREGISTERED } aclopCompileType;

typedef enum {
  ACL_PRECISION_MODE,
  ACL_AICORE_NUM,
  ACL_AUTO_TUNE_MODE,
  ACL_OP_SELECT_IMPL_MODE,
  ACL_OPTYPELIST_FOR_IMPLMODE,
  ACL_OP_DEBUG_LEVEL,
  ACL_DEBUG_DIR,
  ACL_OP_COMPILER_CACHE_MODE,
  ACL_OP_COMPILER_CACHE_DIR,
  ACL_OP_PERFORMANCE_MODE
} aclCompileOpt;

typedef enum aclCompileFlag { ACL_OP_COMPILE_DEFAULT, ACL_OP_COMPILE_FUZZ } aclOpCompileFlag;

/**
 * @ingroup AscendCL
 * @brief compile op
 *
 * @param opType [IN]           op type
 * @param numInputs [IN]        number of inputs
 * @param inputDesc [IN]        pointer to array of input tensor descriptions
 * @param numOutputs [IN]       number of outputs
 * @param outputDesc [IN]       pointer to array of output tensor descriptions
 * @param attr [IN]           pointer to instance of aclopAttr.
 *                              may pass nullptr if the op has no attribute
 * @param engineType [IN]       engine type
 * @param compileFlag [IN]      compile flag
 * @param opPath [IN]           path of op
 *
 * @retval ACL_SUCCESS The function is successfully executed.
 * @retval OtherValues Failure
 */
ACL_FUNC_VISIBILITY aclError aclopCompile(const char *opType, int numInputs, const aclTensorDesc *const inputDesc[],
                                          int numOutputs, const aclTensorDesc *const outputDesc[],
                                          const aclopAttr *attr, aclopEngineType engineType,
                                          aclopCompileType compileFlag, const char *opPath);

/**
 * @ingroup AscendCL
 * @brief compile and execute op
 *
 * @param opType [IN]           op type
 * @param numInputs [IN]        number of inputs
 * @param inputDesc [IN]        pointer to array of input tensor descriptions
 * @param inputs [IN]           pointer to array of input buffers
 * @param numOutputs [IN]       number of outputs
 * @param outputDesc [IN]       pointer to array of output tensor descriptions
 * @param outputs [IN]          pointer to array of outputs buffers
 * @param attr [IN]             pointer to instance of aclopAttr.
 *                              may pass nullptr if the op has no attribute
 * @param engineType [IN]       engine type
 * @param compileFlag [IN]      compile flag
 * @param opPath [IN]           path of op
 * @param stream [IN]           stream handle
 *
 * @retval ACL_SUCCESS The function is successfully executed.
 * @retval OtherValues Failure
 */
ACL_FUNC_VISIBILITY aclError aclopCompileAndExecute(
    const char *opType, int numInputs, const aclTensorDesc *const inputDesc[], const aclDataBuffer *const inputs[],
    int numOutputs, const aclTensorDesc *const outputDesc[], aclDataBuffer *const outputs[], const aclopAttr *attr,
    aclopEngineType engineType, aclopCompileType compileFlag, const char *opPath, aclrtStream stream);

/**
 * @ingroup AscendCL
 * @brief set compile option
 *
 * @param aclCompileOpt [IN]      compile option
 * @param value [IN]              pointer for the option value
 *
 * @retval ACL_SUCCESS The function is successfully executed.
 * @retval OtherValues Failure
 */
ACL_FUNC_VISIBILITY aclError aclSetCompileopt(aclCompileOpt opt, const char *value);

/**
 * @ingroup AscendCL
 * @brief set compile flag
 *
 * @param flag [IN]    compile flag, ACL_OP_COMPILE_DEFAULT means compile with default mode
 *                     ACL_OP_COMPILE_FUZZ means compile with fuzz mode
 *
 * @retval ACL_SUCCESS The function is successfully executed.
 * @retval OtherValues Failure
 */
ACL_FUNC_VISIBILITY aclError aclopSetCompileFlag(aclOpCompileFlag flag);

#ifdef __cplusplus
}
#endif

#endif  // INC_EXTERNAL_ACL_ACL_OP_COMPILER_H_
