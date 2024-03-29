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

#ifndef GE_SINGLE_OP_STREAM_RESOURCE_H_
#define GE_SINGLE_OP_STREAM_RESOURCE_H_

#include <string>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "common/ge_inner_error_codes.h"
#include "runtime/stream.h"
#include "single_op/single_op.h"

namespace ge {
class StreamResource {
 public:
  explicit StreamResource(uintptr_t resource_id);
  ~StreamResource();

  StreamResource(const StreamResource &) = delete;
  StreamResource(StreamResource &&) = delete;
  StreamResource &operator=(const StreamResource &) = delete;
  StreamResource &operator=(StreamResource &&) = delete;
  rtStream_t GetStream() const;
  void SetStream(rtStream_t stream);

  SingleOp *GetOperator(const void *key);
  DynamicSingleOp *GetDynamicOperator(const void *key);

  Status BuildOperator(const std::string &model_name, const ModelData &model_data, SingleOp **single_op);
  Status BuildDynamicOperator(const std::string &model_name, const ModelData &model_data, DynamicSingleOp **single_op);

  uint8_t *MallocMemory(const std::string &purpose, size_t size, bool holding_lock = true);
  uint8_t *MallocWeight(const std::string &purpose, size_t size);
  const uint8_t *GetMemoryBase() const;

 private:
  uint8_t *DoMallocMemory(const std::string &purpose,
                          size_t size,
                          size_t &max_allocated,
                          std::vector<uint8_t *> &allocated);

  uintptr_t resource_id_;
  size_t max_memory_size_ = 0;
  std::vector<uint8_t *> memory_list_;
  std::vector<uint8_t *> weight_list_;
  std::unordered_map<const void *, std::unique_ptr<SingleOp>> op_map_;
  std::unordered_map<const void *, std::unique_ptr<DynamicSingleOp>> dynamic_op_map_;
  rtStream_t stream_ = nullptr;
  std::mutex mu_;
  std::mutex stream_mu_;
};
}  // namespace ge

#endif  // GE_SINGLE_OP_STREAM_RESOURCE_H_
