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
#include "graph/build/memory/binary_block_mem_assigner.h"
#include <algorithm>
#include "framework/common/debug/ge_log.h"
#include "graph/utils/type_utils.h"

namespace {
const uint32_t kRangeCeilInterval = 2;
const uint32_t kLogBase = 2;
const int64_t kLargeBlockSize = 8388608;   // 8 * 1024 * 1024
const int64_t kLargeBlockRangeSize = 2;
}  // namespace

namespace ge {
using std::vector;

void BinaryBlockMemAssigner::PlanRanges(size_t range_number_limit, vector<vector<int64_t>> &ranges) {
  /// range delete and merge
  /// result after delete and merge is: [[6,12],[16,24,24],[30,32,48],[60,256]]
  bool changed = false;
  vector<int64_t> temp;
  do {
    changed = false;
    for (auto iter = ranges.begin(); iter != ranges.end();) {
      if (!temp.empty()) {
        iter->insert(iter->end(), temp.begin(), temp.end());
        temp.clear();
      }
      if (iter->empty()) {
        iter = ranges.erase(iter);
        changed = true;
      } else if ((iter->size() < range_number_limit) && (ranges.end() - iter > 1) &&
                 !(iter->at(0) >= kLargeBlockSize && iter->size() >= kLargeBlockRangeSize)) {
        temp.insert(temp.end(), iter->begin(), iter->end());
        iter = ranges.erase(iter);
        changed = true;
      } else {
        ++iter;
      }
    }
  } while (changed);
}

///
/// @ingroup domi_omg
/// @brief memory size fixed for reuse. this function determines memory types and sizes
/// @param [out] range_ceils return memory sizes
/// @return Status result
/// @author
///
Status BinaryBlockMemAssigner::GetMemoryRanges(vector<int64_t> &range_ceils) {
  vector<int64_t> all_memory_size;
  GetOutAndWorkSpaceMem(all_memory_size);
  if (all_memory_size.empty()) {
    GELOGW("Vector all_memory_size is empty!");
    return SUCCESS;
  }
  if ((all_memory_size.front() <= 0) || (log(kLogBase) == 0)) {
    GELOGE(FAILED, "[Check][MemRangeStep]first mem_range_step:%ld less than 0,invalid,"
          "maybe has dynamic shape in graph", all_memory_size.front());
    REPORT_INNER_ERROR("E19999", "first mem_range_step:%ld less than 0,invalid,"
          "maybe has dynamic shape in graph", all_memory_size.front());
    return FAILED;
  }
  // Memory size is 512 aligned, so it is not necessary to take less than 512
  int64_t min_memory_size = (all_memory_size.back() > MEM_ALIGN_SIZE) ? MEM_ALIGN_SIZE : all_memory_size.front();
  auto range_number = static_cast<size_t>(
    ceil(log(all_memory_size.back() / static_cast<double>(min_memory_size)) / log(kLogBase)));
  range_number = (range_number == 0) ? 1 : range_number;
  GELOGD("Range number: %zu", range_number);

  vector<vector<int64_t>> ranges(range_number);
  GE_CHK_BOOL_EXEC((range_number != 0),
    REPORT_INNER_ERROR("E19999", "inner data[range_number] is 0, judge invalid");
    return PARAM_INVALID,
    "[Check][RangeNumber]inner data is 0, judge invalid.");
  size_t range_number_limit = all_memory_size.size() / range_number;
  int64_t range_ceil = min_memory_size;
  for (size_t i = 1; i <= range_number; i++) {
    GE_IF_BOOL_EXEC(TypeUtils::CheckUint64MulOverflow(static_cast<uint64_t>(range_ceil), kRangeCeilInterval),
                    GELOGE(FAILED, "[Check][MemRangeCeil]Multiply result is out of range,"
                      "range_ceil:%ld, interval:%u", range_ceil, kRangeCeilInterval);
                    REPORT_INNER_ERROR("E19999", "process mem_range_ceil,multiply result out of range,"
                      "range_ceil:%ld, interval:%u", range_ceil, kRangeCeilInterval);
                    return FAILED);
    range_ceil *= kRangeCeilInterval;  // The block size of each interval is doubled every time.
    for (auto iter = all_memory_size.begin(); iter != all_memory_size.end();) {
      if (*iter <= range_ceil) {
        ranges[i - 1].push_back(*iter);
        iter = all_memory_size.erase(iter);
      } else {
        break;
      }
    }
  }

  GELOGD("Origin ranges:");
  for (auto &v : ranges) {
    GELOGD("__%s", ToString(v).c_str());
  }

  PlanRanges(range_number_limit, ranges);
  GELOGD("Origin ranges:");
  for (auto &v : ranges) {
    GELOGD("__%s", ToString(v).c_str());
  }

  for (auto &range : ranges) {
    std::sort(range.begin(), range.end());
    if (!range.empty()) {
      range_ceils.push_back(range.back());
    }
  }
  GELOGI("Range ceils: %s", ToString(range_ceils).c_str());

  return SUCCESS;
}
}  // namespace ge
