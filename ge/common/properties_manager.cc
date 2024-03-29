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

#include "common/properties_manager.h"

#include <climits>
#include <cstdio>
#include <fstream>

#include "common/ge/ge_util.h"
#include "common/util.h"
#include "framework/common/debug/ge_log.h"
#include "framework/common/debug/log.h"
#include "framework/common/ge_types.h"
#include "framework/common/types.h"
#include "graph/debug/ge_attr_define.h"
#include "graph/ge_context.h"
#include "graph/utils/attr_utils.h"

namespace ge {
PropertiesManager::PropertiesManager() : is_inited_(false), delimiter("=") {}
PropertiesManager::~PropertiesManager() {}

// singleton
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY PropertiesManager &PropertiesManager::Instance() {
  static PropertiesManager instance;
  return instance;
}

// Initialize property configuration
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY bool PropertiesManager::Init(const std::string &file_path) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (is_inited_) {
    GELOGW("Already inited, will be initialized again");
    properties_map_.clear();
    is_inited_ = false;
    return is_inited_;
  }

  if (!LoadFileContent(file_path)) {
    return false;
  }

  is_inited_ = true;
  return is_inited_;
}

// Load file contents
bool PropertiesManager::LoadFileContent(const std::string &file_path) {
  // Normalize the path
  string resolved_file_path = RealPath(file_path.c_str());
  if (resolved_file_path.empty()) {
    DOMI_LOGE("Invalid input file path [%s], make sure that the file path is correct.", file_path.c_str());
    return false;
  }
  std::ifstream fs(resolved_file_path, std::ifstream::in);

  if (!fs.is_open()) {
    GELOGE(PARAM_INVALID, "Open %s failed.", file_path.c_str());
    return false;
  }

  std::string line;

  while (getline(fs, line)) {  // line not with \n
    if (!ParseLine(line)) {
      GELOGE(PARAM_INVALID, "Parse line failed. content is [%s].", line.c_str());
      fs.close();
      return false;
    }
  }

  fs.close();  // close the file

  GELOGI("LoadFileContent success.");
  return true;
}

// Parsing the command line
bool PropertiesManager::ParseLine(const std::string &line) {
  std::string temp = Trim(line);
  // Comment or newline returns true directly
  if (temp.find_first_of('#') == 0 || *(temp.c_str()) == '\n') {
    return true;
  }

  if (!temp.empty()) {
    std::string::size_type pos = temp.find_first_of(delimiter);
    if (pos == std::string::npos) {
      GELOGE(PARAM_INVALID, "Incorrect line [%s], it must include [%s].Perhaps you use illegal chinese symbol",
             line.c_str(), delimiter.c_str());
      return false;
    }

    std::string map_key = Trim(temp.substr(0, pos));
    std::string value = Trim(temp.substr(pos + 1));
    if (map_key.empty() || value.empty()) {
      GELOGE(PARAM_INVALID, "Map_key or value empty. %s", line.c_str());
      return false;
    }

    properties_map_[map_key] = value;
  }

  return true;
}

// Remove the space and tab before and after the string
std::string PropertiesManager::Trim(const std::string &str) {
  if (str.empty()) {
    return str;
  }

  std::string::size_type start = str.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return str;
  }

  std::string::size_type end = str.find_last_not_of(" \t\r\n") + 1;
  return str.substr(start, end);
}

// Get property value, if not found, return ""
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY std::string PropertiesManager::GetPropertyValue(
    const std::string &map_key) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto iter = properties_map_.find(map_key);
  if (properties_map_.end() != iter) {
    return iter->second;
  }

  return "";
}

// Set property value
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY void PropertiesManager::SetPropertyValue(const std::string &map_key,
                                                                                          const std::string &value) {
  std::lock_guard<std::mutex> lock(mutex_);
  properties_map_[map_key] = value;
}

// return properties_map_
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY std::map<std::string, std::string>
PropertiesManager::GetPropertyMap() {
  std::lock_guard<std::mutex> lock(mutex_);
  return properties_map_;
}

// Set separator
FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY void PropertiesManager::SetPropertyDelimiter(const std::string &de) {
  std::lock_guard<std::mutex> lock(mutex_);
  delimiter = de;
}

FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY DumpProperties &PropertiesManager::GetDumpProperties(
    uint64_t session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  // If session_id is not found in dump_properties_map_, operator[] will insert one.
  return dump_properties_map_[session_id];
}

FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY void PropertiesManager::AddDumpProperties(
    uint64_t session_id, const DumpProperties &dump_properties) {
  std::lock_guard<std::mutex> lock(mutex_);
  dump_properties_map_.emplace(session_id, dump_properties);
}

FMK_FUNC_HOST_VISIBILITY FMK_FUNC_DEV_VISIBILITY void PropertiesManager::RemoveDumpProperties(uint64_t session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto iter = dump_properties_map_.find(session_id);
  if (iter != dump_properties_map_.end()) {
    dump_properties_map_.erase(iter);
  }
}
}  // namespace ge
