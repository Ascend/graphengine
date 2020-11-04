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

#include "graph/load/new_model_manager/task_info/label_switch_by_index_task_info.h"

#include "graph/debug/ge_attr_define.h"
#include "graph/load/new_model_manager/davinci_model.h"

namespace ge {
constexpr uint8_t kLabelSwitchIndexNum = 1;

LabelSwitchByIndexTaskInfo::~LabelSwitchByIndexTaskInfo() {
  if (args_ != nullptr) {
    rtError_t ret = rtFree(args_);
    if (ret != RT_ERROR_NONE) {
      GELOGE(RT_FAILED, "Call rt api failed, ret: 0x%X", ret);
    }
  }
  args_ = nullptr;
  index_value_ = nullptr;
}

Status LabelSwitchByIndexTaskInfo::Init(const domi::TaskDef &task_def, DavinciModel *davinci_model) {
  GELOGI("LabelSwitchByIndexTaskInfo Init Start.");
  GE_CHECK_NOTNULL(davinci_model);

  const vector<rtLabel_t> &label_list = davinci_model->GetLabelList();
  Status ret = SetStream(task_def.stream_id(), davinci_model->GetStreamList());
  if (ret != SUCCESS) {
    return FAILED;
  }

  // Get LabelSwitch task def
  const domi::LabelSwitchByIndexDef &label_switch = task_def.label_switch_by_index();
  OpDescPtr op_desc = davinci_model->GetOpByIndex(label_switch.op_index());
  if (op_desc == nullptr) {
    GELOGE(INTERNAL_ERROR, "Task op index:%u out of range!", label_switch.op_index());
    return INTERNAL_ERROR;
  }

  branch_max_ = label_switch.label_max();

  auto input_data_addr = ModelUtils::GetInputDataAddrs(davinci_model->GetRuntimeParam(), op_desc);
  if (input_data_addr.size() != kLabelSwitchIndexNum) {
    GELOGE(INTERNAL_ERROR, "LabelSwitchByIndexTaskInfo: %s invalid addr size: %zu, num: %u!",
           op_desc->GetName().c_str(), input_data_addr.size(), kLabelSwitchIndexNum);
    return INTERNAL_ERROR;
  }

  if (davinci_model->IsKnownNode()) {
    index_value_ = davinci_model->GetCurrentFixedAddr(fixed_addr_offset_);
  } else {
    index_value_ = input_data_addr[0];
  }

  davinci_model->DisableZeroCopy(index_value_);

  std::vector<uint32_t> label_idx_list;
  if (!AttrUtils::GetListInt(op_desc, ATTR_NAME_LABEL_SWITCH_LIST, label_idx_list)) {
    GELOGE(INTERNAL_ERROR, "LabelSwitchByIndexTaskInfo: %s Get attr %s failed.", op_desc->GetName().c_str(),
           ATTR_NAME_LABEL_SWITCH_LIST.c_str());
    return INTERNAL_ERROR;
  }

  if (label_idx_list.empty() || label_idx_list.size() != branch_max_) {
    GELOGE(INTERNAL_ERROR, "LabelSwitchByIndexTaskInfo: %s label index size: %zu, task branch max: %u.",
           op_desc->GetName().c_str(), label_idx_list.size(), branch_max_);
    return INTERNAL_ERROR;
  }

  label_list_.resize(branch_max_, nullptr);
  for (size_t idx = 0; idx < label_idx_list.size(); ++idx) {
    uint32_t label_id = label_idx_list[idx];
    if (label_id >= label_list.size()) {
      GELOGE(INTERNAL_ERROR, "LabelSwitchByIndexTaskInfo: %s index: %zu, label index: %u, model label size: %zu.",
             op_desc->GetName().c_str(), idx, label_id, label_list.size());
      return INTERNAL_ERROR;
    }
    GE_CHECK_NOTNULL(label_list[label_id]);

    label_list_[idx] = label_list[label_id];
  }

  rtMemType_t memory_type = op_desc->HasAttr(ATTR_NAME_MEMORY_TYPE_RANGE) ? RT_MEMORY_TS_4G : RT_MEMORY_HBM;
  GELOGI("memory_type: %u", memory_type);
  args_size_ = branch_max_ * sizeof(rtLabelDevInfo);
  rtError_t rt_ret = rtMalloc(&args_, args_size_, memory_type);
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(RT_FAILED, "Call rt api failed, ret: 0x%X", rt_ret);
    return RT_ERROR_TO_GE_STATUS(rt_ret);
  }

  rt_ret = rtLabelListCpy(label_list_.data(), label_list_.size(), args_, args_size_);
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(RT_FAILED, "Call rt api failed, ret: 0x%X", rt_ret);
    return RT_ERROR_TO_GE_STATUS(rt_ret);
  }

  GELOGI("LabelSwitchByIndexTaskInfo Init success, branch max: %u.", branch_max_);
  return SUCCESS;
}

Status LabelSwitchByIndexTaskInfo::Distribute() {
  GELOGI("LabelSwitchByIndexTaskInfo Distribute Start, branch max: %u", branch_max_);
  GE_CHECK_NOTNULL(args_);
  GE_CHECK_NOTNULL(index_value_);
  if (branch_max_ == 0 || args_size_ == 0) {
    GELOGE(PARAM_INVALID, "branch max: %u, args size: %u invalid.", branch_max_, args_size_);
    return PARAM_INVALID;
  }

  rtError_t rt_ret = rtLabelSwitchByIndex(index_value_, branch_max_, args_, stream_);
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(RT_FAILED, "Call rt api failed, ret: 0x%X", rt_ret);
    return RT_FAILED;
  }

  GELOGI("LabelSwitchByIndexTaskInfo Distribute Success.");
  return SUCCESS;
}

Status LabelSwitchByIndexTaskInfo::CalculateArgs(const domi::TaskDef &task_def, DavinciModel *davinci_model) {
  GE_CHECK_NOTNULL(davinci_model);
  auto label_switch = task_def.label_switch_by_index();
  uint32_t op_index = label_switch.op_index();
  GELOGI("Begin to calculate args, op_index is: %u", op_index);
  auto op_desc = davinci_model->GetOpByIndex(op_index);
  GE_CHECK_NOTNULL(op_desc);
  GELOGI("Calc opType[%s] args size. Node name is [%s]", op_desc->GetType().c_str(), op_desc->GetName().c_str());
  if (op_desc->GetInputsSize() != kLabelSwitchIndexNum) {
    GELOGE(FAILED, "Label switch op only have one data input. Now input size is %zu", op_desc->GetInputsSize());
    return FAILED;
  }
  string input_tensor_name = op_desc->GetName();
  fixed_addr_offset_ = davinci_model->GetFixedAddrsSize(input_tensor_name);
  auto tensor_desc = op_desc->GetInputDesc(0);
  int64_t tensor_size = 0;
  GE_CHK_STATUS(TensorUtils::GetSize(tensor_desc, tensor_size));
  davinci_model->SetTotalFixedAddrsSize(input_tensor_name, tensor_size);
  GELOGI("Calculate stream switchn task args , tensor_size %ld, fixed_addr_offset %ld", tensor_size,
         fixed_addr_offset_);
  return SUCCESS;
}

REGISTER_TASK_INFO(RT_MODEL_TASK_STREAM_LABEL_SWITCH_BY_INDEX, LabelSwitchByIndexTaskInfo);
}  // namespace ge
