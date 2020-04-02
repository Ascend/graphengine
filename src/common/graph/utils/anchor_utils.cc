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

#include "utils/anchor_utils.h"
#include <algorithm>
#include "debug/ge_util.h"
#include "framework/common/debug/ge_log.h"

namespace ge {
GE_FUNC_DEV_VISIBILITY GE_FUNC_HOST_VISIBILITY Format AnchorUtils::GetFormat(const DataAnchorPtr &data_anchor) {
  if (data_anchor == nullptr) {
    GELOGE(GRAPH_FAILED, "The input data anchor is invalid.");
    return FORMAT_RESERVED;
  }
  return data_anchor->format_;
}

GE_FUNC_DEV_VISIBILITY GE_FUNC_HOST_VISIBILITY graphStatus AnchorUtils::SetFormat(const DataAnchorPtr &data_anchor,
                                                                                  Format data_format) {
  if ((data_anchor == nullptr) || (data_format == FORMAT_RESERVED)) {
    GELOGE(GRAPH_FAILED, "The input data anchor or input data format is invalid .");
    return GRAPH_FAILED;
  }
  data_anchor->format_ = data_format;
  return GRAPH_SUCCESS;
}

// Get anchor status
GE_FUNC_DEV_VISIBILITY GE_FUNC_HOST_VISIBILITY AnchorStatus AnchorUtils::GetStatus(const DataAnchorPtr &data_anchor) {
  if (data_anchor == nullptr) {
    GELOGE(GRAPH_FAILED, "The input data anchor is invalid.");
    return ANCHOR_RESERVED;
  }
  return data_anchor->status_;
}

// Set anchor status
GE_FUNC_DEV_VISIBILITY GE_FUNC_HOST_VISIBILITY graphStatus AnchorUtils::SetStatus(const DataAnchorPtr &data_anchor,
                                                                                  AnchorStatus anchor_status) {
  if ((data_anchor == nullptr) || (anchor_status == ANCHOR_RESERVED)) {
    GELOGE(GRAPH_FAILED, "The input data anchor or input data format is invalid .");
    return GRAPH_FAILED;
  }
  data_anchor->status_ = anchor_status;
  return GRAPH_SUCCESS;
}

bool AnchorUtils::HasControlEdge(const AnchorPtr &anchor) {
  auto control_anchor = Anchor::DynamicAnchorCast<ControlAnchor>(anchor);
  if (control_anchor != nullptr) {
    return (control_anchor->GetPeerAnchors().size() != 0);
  }

  auto data_anchor = Anchor::DynamicAnchorCast<DataAnchor>(anchor);
  if (data_anchor) {
    for (const auto &peer : data_anchor->GetPeerAnchors()) {
      auto peer_cast = Anchor::DynamicAnchorCast<ControlAnchor>(peer);
      if (peer_cast) {
        return true;
      }
    }
    return false;
  }
  GELOGE(GRAPH_FAILED, "the anchor is neither control anchor nor data anchor");
  return false;
}

bool AnchorUtils::IsControlEdge(const AnchorPtr &src, const AnchorPtr &dst) {
  GE_CHK_BOOL_EXEC(src != nullptr, return false, "src is null.");
  GE_CHK_BOOL_RET_STATUS_NOLOG(src->IsLinkedWith(dst), false);
  auto src_control_anchor = Anchor::DynamicAnchorCast<ControlAnchor>(src);
  auto dst_control_anchor = Anchor::DynamicAnchorCast<ControlAnchor>(dst);
  return (src_control_anchor || dst_control_anchor);
}

GE_FUNC_DEV_VISIBILITY GE_FUNC_HOST_VISIBILITY int AnchorUtils::GetIdx(const AnchorPtr &anchor) {
  // Check if it can add edge between DataAnchor
  auto data_anchor = Anchor::DynamicAnchorCast<DataAnchor>(anchor);
  if (data_anchor != nullptr) {
    return data_anchor->GetIdx();
  }
  // Check if it can add edge between ControlAnchor
  auto control_anchor = Anchor::DynamicAnchorCast<ControlAnchor>(anchor);
  if (control_anchor != nullptr) {
    return control_anchor->GetIdx();
  }
  return -1;
}
}  // namespace ge
