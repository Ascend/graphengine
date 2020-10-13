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

/*!
 * \file spectral_ops.h
 * \brief
 */
#ifndef OPS_BUILT_IN_OP_PROTO_INC_SPECTRAL_OPS_H_
#define OPS_BUILT_IN_OP_PROTO_INC_SPECTRAL_OPS_H_

#include "graph/operator.h"
#include "graph/operator_reg.h"

namespace ge {

/**
*@brief Real-valued fast Fourier transform . \n

*@par Inputs:
*@li input: A float32 tensor.
*@li fft_length: An int32 tensor of shape [1]. The FFT length . \n

*@par Outputs:
*@li y: A complex64 tensor of the same rank as `input`. The inner-most
dimension of `input` is replaced with the `fft_length / 2 + 1` unique
frequency components of its 1D Fourier transform . \n

*@par Third-party framework compatibility
* Compatible with TensorFlow RFFT operator.
*/
REG_OP(RFFT)
    .INPUT(input, TensorType({DT_FLOAT}))
    .INPUT(fft_length, TensorType({DT_INT32}))
    .OUTPUT(y, TensorType({DT_COMPLEX64}))
    .OP_END_FACTORY_REG(RFFT)

}  // namespace ge

#endif  // OPS_BUILT_IN_OP_PROTO_INC_SPECTRAL_OPS_H_