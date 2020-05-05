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

 #ifndef GE_OP_POWER_H
 #define GE_OP_POWER_H

 #include "../graph/operator_reg.h"

 namespace ge {

/**
*@brief Computes the output as (shift + scale * x) ^ power.

*@par Inputs:
* x: A Tensor of type float16 or float32.

*@par Attributes:
*@li power: Optional. Defaults to 1.0.
*@li scale: Optional. Defaults to 1.0.
*@li shift: Optional. Defaults to 0.0.

*@par Outputs:
* y: A Tensor. Has the same type and shape as "x".
*/

 REG_OP(Power)
     .INPUT(x, TensorType({DT_FLOAT16, DT_FLOAT}))
     .OUTPUT(y, TensorType({DT_FLOAT16, DT_FLOAT}))
     .ATTR(power, Float, 1.0)
     .ATTR(scale, Float, 1.0)
     .ATTR(shift, Float, 0.0)
     .OP_END_FACTORY_REG(Power);

 } // namespace ge

 #endif // GE_OP_POWER_H
