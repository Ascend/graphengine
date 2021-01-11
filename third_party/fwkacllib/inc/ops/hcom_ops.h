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
 * \file hcom_ops.h
 * \brief huawei collective communication library ops.
 */
#ifndef OPS_BUILT_IN_OP_PROTO_INC_HCOM_OPS_H_
#define OPS_BUILT_IN_OP_PROTO_INC_HCOM_OPS_H_

#include "graph/operator_reg.h"

namespace ge {
/**
 * @brief Outputs a tensor gathering all input tensors.
 * @par Inputs:
 * x: A tensor. Must be one of the following types: int8, int16, int32, float16,
  float32.
 * @par Attributes:
 * @li rank_size: A required integer identifying the number of ranks
  participating in the op.
 * @li group: A required string identifying the group name of ranks
  participating in the op.
 * @par Outputs:
 * y: A Tensor. Has the same type as "x".
 * @attention Constraints:
  "group" is limited to 128 characters. Use "hccl_world_group"
  as the name of a world group.
 */
REG_OP(HcomAllGather)
    .INPUT(x, TensorType({DT_FLOAT, DT_INT32, DT_INT8, DT_INT16, DT_FLOAT16, DT_INT64, DT_UINT64}))
    .OUTPUT(y, TensorType({DT_FLOAT, DT_INT32, DT_INT8, DT_INT16, DT_FLOAT16, DT_INT64, DT_UINT64}))
    .REQUIRED_ATTR(rank_size, Int)
    .REQUIRED_ATTR(group, String)
    .OP_END_FACTORY_REG(HcomAllGather)

/**
 * @brief Outputs a tensor containing the reduction across all input tensors
  passed to op.
 * @par Inputs:
 * x: A tensor. Must be one of the following types: int8, int16, int32, float16,
  float32.
 * @par Attributes:
 * @li reduction: A required string identifying the reduction operation to
  perform.The supported operation are: "sum", "max", "min", "prod".
 * @li group: A required string identifying the group name of ranks
  participating in the op.
 * @li fusion: An optional integer identifying the fusion flag of the op.
  0: no fusion; 1 (default): fusion; 2: fusion the ops by fusion id.
 * @li fusion_id: An optional integer identifying the fusion id of the op.
 * The HcomAllReduce ops with the same fusion id will be fused.
 * @par Outputs:
 * y: A Tensor. Has the same type as "x".
 * @attention Constraints:
 *"group" is limited to 128 characters. Use "hccl_world_group"
  as the name of a world group.
 */
REG_OP(HcomAllReduce)
    .INPUT(x, TensorType({DT_FLOAT, DT_INT32, DT_INT8, DT_INT16, DT_FLOAT16}))
    .OUTPUT(y, TensorType({DT_FLOAT, DT_INT32, DT_INT8, DT_INT16, DT_FLOAT16}))
    .REQUIRED_ATTR(reduction, String)
    .REQUIRED_ATTR(group, String)
    .ATTR(fusion, Int, 1)
    .ATTR(fusion_id, Int, -1)
    .OP_END_FACTORY_REG(HcomAllReduce)

/**
 * @brief Broadcasts the input tensor in root rank to all ranks.
 * @par Inputs:
 * x: A list of dynamic input tensor. Must be one of the following types:
  int8, int16, int32, float16, float32. It's a dynamic input.
 * @par Attributes:
 * @li root_rank: A required integer identifying the root rank in the op
  input of this rank will be broadcast to other ranks.
 * @li fusion: A required integer identifying if the op need to fusion,the 
  default value is none fusion
  * @li fusion_id: A required integer identifying the fusion id if para fusion
  is set.
 * @li group: A required string identifying the group name of ranks
  participating in the op.
 * @par Outputs:
 * y: A list of dynamic output tensor. Has the same type and length as "x".
 * It's a dynamic output.
 * @attention Constraints:
  "group" is limited to 128 characters. Use "hccl_world_group"
  as the name of a world group.
 */
REG_OP(HcomBroadcast)
    .DYNAMIC_INPUT(x, TensorType({DT_FLOAT, DT_INT32, DT_INT8, DT_INT16, DT_FLOAT16, DT_INT64, DT_UINT64}))
    .DYNAMIC_OUTPUT(y, TensorType({DT_FLOAT, DT_INT32, DT_INT8, DT_INT16, DT_FLOAT16, DT_INT64, DT_UINT64}))
    .REQUIRED_ATTR(root_rank, Int)
    .REQUIRED_ATTR(group, String)
    .ATTR(fusion, Int, 0)
    .ATTR(fusion_id, Int, -1)
    .OP_END_FACTORY_REG(HcomBroadcast)

/**
 * @brief preforms reduction from others rank to rootrank
 * @par Inputs:
* @li root_rank: A required integer identifying the root rank in the op
  the reduction result will be on this root rank
 * x: A tensor. Must be one of the following types: int8, int16, int32, float16,
  float32.
 * @par Attributes:
 * @li reduction: A required string identifying the reduction operation to
  perform.The supported operation are: "sum", "max", "min", "prod".
 * @li group: A required string identifying the group name of ranks
  participating in the op.
 * @li fusion: An optional integer identifying the fusion flag of the op.
  0: no fusion; 1 (default): fusion; 2: fusion the ops by fusion id.
 * @li fusion_id: An optional integer identifying the fusion id of the op.
 * The HcomReduce ops with the same fusion id will be fused.
 * @par Outputs:
 * y: A Tensor. Has the same type as "x".
 * @attention Constraints:
 *"group" is limited to 128 characters. Use "hccl_world_group"
  as the name of a world group.
 */
REG_OP(HcomReduce)
    .INPUT(x, TensorType({DT_FLOAT, DT_INT32, DT_INT8, DT_INT16, DT_FLOAT16}))
    .OUTPUT(y, TensorType({DT_FLOAT, DT_INT32, DT_INT8, DT_INT16, DT_FLOAT16}))
    .REQUIRED_ATTR(root_rank, Int)
    .REQUIRED_ATTR(reduction, String)
    .REQUIRED_ATTR(group, String)
    .ATTR(fusion, Int, 0)
    .ATTR(fusion_id, Int, -1)
    .OP_END_FACTORY_REG(HcomReduce)
/**
 * @brief Performs reduction across all input tensors, scattering in equal
  blocks among ranks, each rank getting a chunk of data based on its rank
  index.
 * @par Inputs:
 * x: A tensor. Must be one of the following types: int8, int16, int32, float16,
  float32.
 * @par Attributes:
 * @li reduction: A required string identifying the reduction operation to
  perform. The supported operation are: "sum", "max", "min", "prod".
 * @li group: A required string identifying the group name of ranks
  participating in the op.
 * @li rank_size: A required integer identifying the number of ranks
  participating in the op.
 * @par Outputs:
 * y: A Tensor. Has the same type as "x".
 * @attention Constraints:
  "group" is limited to 128 characters. Use "hccl_world_group"
  as the name of a world group.
 */
REG_OP(HcomReduceScatter)
    .INPUT(x, TensorType({DT_FLOAT, DT_INT32, DT_INT8, DT_INT16, DT_FLOAT16}))
    .OUTPUT(y, TensorType({DT_FLOAT, DT_INT32, DT_INT8, DT_INT16, DT_FLOAT16}))
    .REQUIRED_ATTR(reduction, String)
    .REQUIRED_ATTR(group, String)
    .REQUIRED_ATTR(rank_size, Int)
    .OP_END_FACTORY_REG(HcomReduceScatter)

/**
 * @brief Sends the input tensor to destination rank.
 * @par Inputs:
 * x: A tensor. Must be one of the following types: int8, int16, int32, float16,
  float32.
 * @par Attributes:
 * @li sr_tag: A required integer identifying the send/recv message tag. The
   message will be received by the HcomReceive op with the same "sr_tag".
 * @li dest_rank: A required integer identifying the destination rank.
 * @li group: A string identifying the group name of ranks participating in
  the op.
 * @par Outputs:
 * None.
 * @attention Constraints:
  @li "group" is limited to 128 characters. Use
  "hccl_world_group" as the name of a world group.
 * @li Operators HcomSend and HcomReceive have the same "sr_tag".
 * @see HcomReceive
*/
REG_OP(HcomSend)
    .INPUT(x, TensorType({DT_FLOAT, DT_INT32, DT_INT8, DT_INT16, DT_FLOAT16, DT_INT64, DT_UINT64}))
    .REQUIRED_ATTR(group, String)
    .REQUIRED_ATTR(sr_tag, Int)
    .REQUIRED_ATTR(dest_rank, Int)
    .OP_END_FACTORY_REG(HcomSend)

/**
 * @brief Receives the tensor from source rank.
 * @par Inputs:
 * None.
 * @par Attributes:
 * @li sr_tag: A required integer identifying the send/recv message tag. The
  message will be send by the HcomSend op with the same "sr_tag".
 * @li src_rank: A required integer identifying the source rank.
 * @li group: A required string identifying the group name of ranks
 * participating in the op.
 * @li shape: A required list identifying the shape of the tensor to be
  received.
 * @li dtype: A required integer identifying the type of the tensor to be
  received. The supported types are: int8, int16, int32, float16, float32.
 * @par Outputs:
 * y: A tensor with type identified in "dtype".
 * @attention Constraints:
  @li "group" is limited to 128 characters. Use
  "hccl_world_group" as the name of a world group.
 * @li Operators HcomSend and HcomReceive have the same "sr_tag".
 * @li "shape" should be same as the input tensor of HcomSend.
 * @li "dtype" should be same as the input tensor of HcomSend.
 * @see HcomSend
*/
REG_OP(HcomReceive)
    .OUTPUT(y, TensorType({DT_FLOAT, DT_INT32, DT_INT8, DT_INT16, DT_FLOAT16, DT_INT64, DT_UINT64}))
    .REQUIRED_ATTR(group, String)
    .REQUIRED_ATTR(sr_tag, Int)
    .REQUIRED_ATTR(src_rank, Int)
    .REQUIRED_ATTR(shape, ListInt)
    .REQUIRED_ATTR(dtype, Type)
    .OP_END_FACTORY_REG(HcomReceive)

/**
 * @brief Performs Remote Read of input tensors
 * @par Inputs:
 * remote: A tensor. describing the remote memory address to read: u64 remoteId, u64 addrRemote, u64 length
 * @par Outputs:
 * local: A Tensor. whose value is length / size_of(Type)
 */
REG_OP(HcomRemoteRead)
    .INPUT(remote, TensorType({DT_INT64, DT_UINT64}))
    .OUTPUT(local, TensorType::ALL())
    .REQUIRED_ATTR(dtype, Type)
    .OP_END_FACTORY_REG(HcomRemoteRead)

REG_OP(HcomRemoteRefRead)
    .INPUT(remote, TensorType({DT_UINT64}))
    .INPUT(cache_var, TensorType({DT_UINT64}))
    .INPUT(local_offset, TensorType({DT_UINT64}))
    .OUTPUT(cache_var, TensorType({DT_UINT64})) 
    .REQUIRED_ATTR(dtype, Type)
    .OP_END_FACTORY_REG(HcomRemoteRefRead)

/**
 * @brief Performs Remote Write of input tensors
 * @par Inputs:
 * remote: A tensor. describing the remote memory address to write: u64 remoteId, u64 addrRemote, u64 length
 * @par Inputs:
 * local: A Tensor. whose value is length / size_of(Type)
 */
REG_OP(HcomRemoteWrite)
    .INPUT(remote, TensorType({DT_INT64, DT_UINT64}))
    .INPUT(local, TensorType::ALL())
    .OP_END_FACTORY_REG(HcomRemoteWrite)

REG_OP(HcomRemoteScatterWrite)
    .INPUT(remote, TensorType({DT_INT64, DT_UINT64}))
    .INPUT(local, TensorType::ALL())
    .OPTIONAL_INPUT(local_offset, TensorType({DT_UINT64}))
    .OP_END_FACTORY_REG(HcomRemoteScatterWrite)

} // namespace ge
#endif  // OPS_BUILT_IN_OP_PROTO_INC_HCOM_OPS_H_
