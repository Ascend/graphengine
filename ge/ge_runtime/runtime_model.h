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

#ifndef GE_GE_RUNTIME_RUNTIME_MODEL_H_
#define GE_GE_RUNTIME_RUNTIME_MODEL_H_
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "framework/ge_runtime/davinci_model.h"
#include "framework/common/ge_types.h"
#include "runtime/base.h"
#include "runtime/rt_model.h"

namespace ge {
namespace model_runner {
using RuntimeInfo = std::tuple<uint32_t, uint32_t, void *>;
class Task;
class RuntimeModel {
 public:
  RuntimeModel() = default;
  ~RuntimeModel();

  bool Load(uint32_t device_id, uint64_t session_id, std::shared_ptr<DavinciModel> &davinci_model);
  bool DistributeTask();
  bool LoadComplete();
  const std::vector<uint32_t> &GetTaskIdList() const;
  const std::vector<uint32_t> &GetStreamIdList() const;
  const std::map<std::string, std::shared_ptr<RuntimeInfo>> &GetRuntimeInfoMap() const { return runtime_info_map_; }
  rtModel_t GetModelHandle() const { return rt_model_handle_; }
  bool Run();
  bool CopyInputData(const InputData &input_data);
  bool GetInputOutputDescInfo(bool zero_copy, std::vector<InputOutputDescInfo> *input_desc,
                              std::vector<InputOutputDescInfo> *output_desc, std::vector<uint32_t> *input_format,
                              std::vector<uint32_t> *output_format);

 private:
  bool InitResource(std::shared_ptr<DavinciModel> &davinci_model);
  void GenerateTask(uint32_t device_id, uint64_t session_id, std::shared_ptr<DavinciModel> &davinci_model);
  bool LoadTask();
  bool InitStream(std::shared_ptr<DavinciModel> &davinci_model);
  bool InitEvent(uint32_t event_num);
  bool InitLabel(std::shared_ptr<DavinciModel> &davinci_model);
  bool InitDataInfo(std::shared_ptr<DavinciModel> &davinci_model);
  bool InitOutputInfo(std::shared_ptr<DavinciModel> &davinci_model);
  bool InitConstantInfo(std::shared_ptr<DavinciModel> &davinci_model);
  void RtModelUnbindStream() noexcept;
  void RtStreamDestory() noexcept;
  void RtModelDestory() noexcept;
  void RtLabelDestory() noexcept;
  void RtEventDestory() noexcept;
  bool CopyInputDataToModel(const std::vector<DataBuffer> &data, const std::shared_ptr<OpInfo> &data_info);
  bool CopyHostData(const std::vector<DataBuffer> &data, const std::shared_ptr<OpInfo> &data_info) const;
  bool CopyTransData(const std::vector<DataBuffer> &data, const std::shared_ptr<OpInfo> &data_info);
  bool GetInputDescInfo(std::vector<InputOutputDescInfo> *input_desc, std::vector<uint32_t> *formats);
  bool GetOutputDescInfo(std::vector<InputOutputDescInfo> *output_desc, std::vector<uint32_t> *formats);
  void CreateOutput(uint32_t index, const OpInfo &op_info, InputOutputDescInfo *output, uint32_t *format);

  rtModel_t rt_model_handle_{};
  rtStream_t rt_model_stream_{};

  std::vector<rtStream_t> stream_list_{};
  std::vector<rtLabel_t> label_list_{};
  std::vector<rtEvent_t> event_list_{};

  std::vector<std::shared_ptr<Task>> task_list_{};
  std::vector<std::shared_ptr<OpInfo>> data_info_list_{};
  std::vector<std::shared_ptr<OpInfo>> output_info_list_{};
  std::vector<std::shared_ptr<OpInfo>> constant_info_list_{};

  std::vector<uint32_t> task_id_list_{};
  std::vector<uint32_t> stream_id_list_{};
  std::map<std::string, std::shared_ptr<RuntimeInfo>> runtime_info_map_;
};

}  // namespace model_runner
}  // namespace ge

#endif  // GE_GE_RUNTIME_RUNTIME_MODEL_H_
