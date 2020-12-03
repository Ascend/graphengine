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

#include "ge_runtime/task/event_wait_task.h"
#include "ge_runtime/task/task_factory.h"

namespace ge {
namespace model_runner {
EventWaitTask::EventWaitTask(const ModelContext &model_context, const std::shared_ptr<EventWaitTaskInfo> &task_info)
    : TaskRepeater<EventWaitTaskInfo>(model_context, task_info),
      task_info_(task_info),
      stream_(nullptr),
      event_(nullptr) {
  if (task_info_ == nullptr) {
    GELOGW("task_info_ is null!");
    return;
  }
  auto stream_list = model_context.stream_list();
  auto event_list = model_context.event_list();
  uint32_t stream_id = task_info->stream_id();
  uint32_t event_id = task_info->event_id();
  if (stream_id >= stream_list.size() || event_id >= event_list.size()) {
    GELOGW("stream_list size:%zu, stream_id:%u, event_list size:%zu, event_id:%u", stream_list.size(), stream_id,
           event_list.size(), event_id);
    return;
  }
  stream_ = stream_list[stream_id];
  event_ = event_list[event_id];
}

EventWaitTask::~EventWaitTask() {}

bool EventWaitTask::Distribute() {
  GELOGI("EventWaitTask Distribute start, stream: %p, event: %p, stream_id: %u, event_id: %u.", stream_, event_,
         task_info_->stream_id(), task_info_->event_id());

  rtError_t rt_ret = rtStreamWaitEvent(stream_, event_);
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(RT_FAILED, "Call rt api rtStreamWaitEvent failed, ret: 0x%X", rt_ret);
    return false;
  }

  rt_ret = rtEventReset(event_, stream_);
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(RT_FAILED, "Call rt api rtEventReset failed, ret: 0x%X", rt_ret);
    return false;
  }
  GELOGI("Distribute end.");
  return true;
}

REGISTER_TASK(TaskInfoType::EVENT_WAIT, EventWaitTask, EventWaitTaskInfo);
}  // namespace model_runner
}  // namespace ge
