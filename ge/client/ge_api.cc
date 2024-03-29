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

#include "ge/ge_api.h"
#include <iostream>
#include <malloc.h>
#include "common/debug/log.h"
#include "framework/common/debug/ge_log.h"
#include "common/ge/datatype_util.h"
#include "proto/ge_api.pb.h"
#include "graph/model_serialize.h"
#include "graph/detail/model_serialize_imp.h"
#include "graph/utils/tensor_adapter.h"
#include "init/gelib.h"
#include "session/session_manager.h"
#include "graph/opsproto_manager.h"
#include "graph/utils/type_utils.h"
#include "graph/manager/util/rt_context_util.h"
#include "graph/common/ge_call_wrapper.h"
#include "register/op_registry.h"
#include "common/ge/tbe_plugin_manager.h"
#include "toolchain/plog.h"

using domi::OpRegistry;
using std::map;
using std::string;
using std::vector;

namespace {
const int32_t kMaxStrLen = 128;
}  // namespace

static bool g_ge_initialized = false;
static std::mutex g_ge_release_mutex;  // GEFinalize and ~Session use

namespace ge {
void GetOpsProtoPath(std::string &opsproto_path) {
  GELOGI("Enter get ops proto path schedule");
  const char *path_env = std::getenv("ASCEND_OPP_PATH");
  if (path_env != nullptr) {
    std::string path = path_env;
    opsproto_path = (path + "/op_proto/custom/" + ":") + (path + "/op_proto/built-in/");
    GELOGI("Get opsproto so path from env: %s", path.c_str());
    return;
  }
  std::string path_base = PluginManager::GetPath();
  GELOGI("path_base is %s", path_base.c_str());
  path_base = path_base.substr(0, path_base.rfind('/'));
  path_base = path_base.substr(0, path_base.rfind('/') + 1);
  opsproto_path = (path_base + "ops/op_proto/custom/" + ":") + (path_base + "ops/op_proto/built-in/");
}

Status CheckOptionsValid(const std::map<string, string> &options) {
  // check job_id is valid
  auto job_id_iter = options.find(OPTION_EXEC_JOB_ID);
  if (job_id_iter != options.end()) {
    if (job_id_iter->second.length() > kMaxStrLen) {
      GELOGE(PARAM_INVALID, "CheckOptionsValid job_id failed, string len > %d", kMaxStrLen);
      return FAILED;
    }
  }

  return SUCCESS;
}

// Initialize GE, prepare for execution, call GELib::Initialize
Status GEInitializeImpl(const std::map<string, string> &options) {
  GELOGT(TRACE_INIT, "GEInitialize start");
  // 0.check init status
  if (g_ge_initialized) {
    GELOGW("GEInitialize is called more than once");
    return SUCCESS;
  }
  // Load OpsProto lib plugin
  std::string opsproto_path;
  GetOpsProtoPath(opsproto_path);
  OpsProtoManager *manager = OpsProtoManager::Instance();
  std::map<string, string> option_tmp;
  option_tmp.emplace(std::pair<string, string>(string("ge.opsProtoLibPath"), opsproto_path));
  GE_TIMESTAMP_START(GEInitialize);
  bool is_proto_init = manager->Initialize(option_tmp);
  GE_TIMESTAMP_END(GEInitialize, "GEInitialize::ManagerInitialize");
  if (!is_proto_init) {
    GELOGE(GE_CLI_INIT_FAILED, "geInitialize failed, ops proto path is invalid.");
    return FAILED;
  }

  // check options is valid
  GE_TIMESTAMP_START(CheckOptionsValid);
  if (CheckOptionsValid(options) != SUCCESS) {
    return FAILED;
  }
  GE_TIMESTAMP_END(CheckOptionsValid, "GEInitialize::CheckOptionsValid");

  GE_TIMESTAMP_START(InitPreparation);
  TBEPluginManager::Instance().InitPreparation(options);
  GE_TIMESTAMP_END(InitPreparation, "GEInitialize::InitPreparation");
  // call Initialize
  GELOGT(TRACE_RUNNING, "Initializing environment");
  GE_TIMESTAMP_START(GELibInitialize);
  Status ret = ge::GELib::Initialize(options);
  GE_TIMESTAMP_END(GELibInitialize, "GEInitialize::GELibInitialize");
  if (ret != SUCCESS) {
    GELOGE(GE_CLI_INIT_FAILED, "geInitialize failed, error code = %u", ret);
    return FAILED;
  }

  // 7.check return status, return
  if (!g_ge_initialized) {
    // Initialize success, first time calling initialize
    g_ge_initialized = true;
  }

  GELOGT(TRACE_STOP, "GEInitialize finished");
  return ret;
}

// Initialize GE, prepare for execution, call GELib::Initialize
Status GEInitialize(const std::map<string, string> &options) {
  if (DlogReportInitialize() != SUCCESS) {
    GELOGW("Dlog report device log initialize failed.");
  }
  return GEInitializeImpl(options);
}

Status GEInitialize(const std::map<AscendString, AscendString> &options) {
  std::map<std::string, std::string> str_options;
  for (auto &option : options) {
    if (option.first.GetString() == nullptr || option.second.GetString() == nullptr) {
      GELOGE(FAILED, "GEInitialize options is nullptr.");
      return FAILED;
    }
    std::string key = option.first.GetString();
    std::string val = option.second.GetString();
    str_options[key] = val;
  }
  if (DlogReportInitialize() != SUCCESS) {
    GELOGW("Dlog report device log initialize failed.");
  }
  return GEInitializeImpl(str_options);
}


// GE finalize, releasing all resources
Status GEFinalize() {
  GELOGT(TRACE_INIT, "GEFinalize start");
  // check init status
  if (!g_ge_initialized) {
    GELOGW("GEFinalize is called before GEInitialize");
    return SUCCESS;
  }

  std::lock_guard<std::mutex> lock(g_ge_release_mutex);
  // call Finalize
  Status ret = SUCCESS;
  Status middle_ret;
  GELOGT(TRACE_RUNNING, "Finalizing environment");
  std::shared_ptr<GELib> instancePtr = ge::GELib::GetInstance();
  if (instancePtr == nullptr || !instancePtr->InitFlag()) {
    GELOGW("GEFinalize Failed: GE not initialized.");
    ret = GE_CLI_GE_NOT_INITIALIZED;
  }
  if (ret != GE_CLI_GE_NOT_INITIALIZED) {
    middle_ret = instancePtr->Finalize();
    GELOGI("GEFinalize finalize gelib ret=%u", middle_ret);
    if (middle_ret != SUCCESS) {
      ret = middle_ret;
    }
  }
  middle_ret = TBEPluginManager::Instance().Finalize();
  if (middle_ret != SUCCESS) {
    ret = middle_ret;
  }

  if (g_ge_initialized && ret == SUCCESS) {
    // Unified destruct rt_context
    RtContextUtil::GetInstance().DestroyAllRtContexts();
    g_ge_initialized = false;
  }

  // to avoid memory fragment, use malloc_trim to back free stack to system
  malloc_trim(0);

  if (DlogReportFinalize() != SUCCESS) {
    GELOGW("Dlog report device log finalize failed.");
  }

  GELOGT(TRACE_STOP, "GEFinalize finished");
  return ret;
}

// Initialize session，which calls innerSession
Session::Session(const std::map<string, string> &options) {
  GELOGT(TRACE_INIT, "Session Constructor start");
  // check init status
  sessionId_ = 0;
  if (!g_ge_initialized) {
    GELOGE(GE_CLI_GE_NOT_INITIALIZED, "GE is not initialized.");
    return;
  }
  // call Initialize
  std::shared_ptr<GELib> instance_ptr = ge::GELib::GetInstance();
  if (instance_ptr == nullptr || !instance_ptr->InitFlag()) {
    GELOGE(GE_CLI_GE_NOT_INITIALIZED, "Session Constructor failed");
    return;
  }

  GELOGT(TRACE_RUNNING, "Creating session");
  uint64_t session_id = 0;
  Status ret = instance_ptr->SessionManagerObj().CreateSession(options, session_id);
  GELOGT(TRACE_RUNNING, "Session id is %lu", session_id);

  // check return status, return, update session id if success
  if (ret == SUCCESS) {
    sessionId_ = session_id;
  } else {
    GELOGE(ret, "Session constructor failed, session Id not initialized");
    return;
  }
  GELOGT(TRACE_STOP, "Session Constructor finished");
}

Session::Session(const std::map<AscendString, AscendString> &options) {
  GELOGT(TRACE_INIT, "Session Constructor start");
  // check init status
  sessionId_ = 0;
  if (!g_ge_initialized) {
    GELOGE(GE_CLI_GE_NOT_INITIALIZED, "GE is not initialized.");
    return;
  }
  // call Initialize
  std::shared_ptr<GELib> instance_ptr = ge::GELib::GetInstance();
  if (instance_ptr == nullptr || !instance_ptr->InitFlag()) {
    GELOGE(GE_CLI_GE_NOT_INITIALIZED, "Session Constructor failed");
    return;
  }

  GELOGT(TRACE_RUNNING, "Creating session");
  std::map<std::string, std::string> str_options;
  for (auto &option : options) {
    if (option.first.GetString() == nullptr || option.second.GetString() == nullptr) {
      GELOGE(FAILED, "Session options is nullptr.");
      return;
    }
    std::string key = option.first.GetString();
    std::string val = option.second.GetString();
    str_options[key] = val;
  }
  uint64_t session_id = 0;
  Status ret = instance_ptr->SessionManagerObj().CreateSession(str_options, session_id);
  GELOGT(TRACE_RUNNING, "Session id is %lu", session_id);

  // check return status, return, update session id if success
  if (ret == SUCCESS) {
    sessionId_ = session_id;
  } else {
    GELOGE(ret, "Session constructor failed, session Id not initialized");
    return;
  }
  GELOGT(TRACE_STOP, "Session Constructor finished");
}

// session destructor
Session::~Session() {
  GELOGT(TRACE_INIT, "Session Destructor start");
  // 0.check init status
  if (!g_ge_initialized) {
    GELOGW("GE is not yet initialized or is finalized.");
    return;
  }

  Status ret = FAILED;
  std::lock_guard<std::mutex> lock(g_ge_release_mutex);
  try {
    uint64_t session_id = sessionId_;
    // call DestroySession
    std::shared_ptr<GELib> instance_ptr = ge::GELib::GetInstance();
    if (instance_ptr == nullptr || !instance_ptr->InitFlag()) {
      GELOGW("GE is not yet initialized or is finalized.");
      return;
    }
    GELOGT(TRACE_RUNNING, "Session id is %lu", session_id);

    GELOGT(TRACE_RUNNING, "Destroying session");

    ret = instance_ptr->SessionManagerObj().DestroySession(session_id);
  } catch (google::protobuf::FatalException &e) {
    GELOGE(GE_CLI_SESS_DESTROY_FAILED, "SessionDestructor throws FatalException");
  }

  // check return status, return, update session id if success
  if (ret != SUCCESS) {
    GELOGE(ret, "Session Destructor failed");
  }

  GELOGT(TRACE_STOP, "Session Destructor finished");
}

Status Session::AddGraph(uint32_t graph_id, const Graph &graph) {
  std::map<std::string, std::string> options;
  return AddGraph(graph_id, graph, options);
}

Status Session::AddGraph(uint32_t graph_id, const Graph &graph, const std::map<std::string, std::string> &options) {
  GELOGT(TRACE_INIT, "Start to add graph in Session. graph_id: %u, session_id: %lu.", graph_id, sessionId_);
  std::shared_ptr<GELib> instance_ptr = ge::GELib::GetInstance();
  if (instance_ptr == nullptr || !instance_ptr->InitFlag()) {
    GELOGE(GE_CLI_GE_NOT_INITIALIZED, "AddGraph failed in Session.");
    return FAILED;
  }
  GELOGD("Adding graph to session");
  Status ret = instance_ptr->SessionManagerObj().AddGraph(sessionId_, graph_id, graph, options);
  if (ret != SUCCESS) {
    GELOGE(ret, "AddGraph failed in Session.");
    return FAILED;
  }
  GELOGD("AddGraph finished in Session.");
  return ret;
}

Status Session::AddGraph(uint32_t graph_id, const Graph &graph,
                         const std::map<AscendString, AscendString> &options) {
  GELOGT(TRACE_INIT, "Start to add graph in Session. graph_id: %u, session_id: %lu.", graph_id, sessionId_);
  std::shared_ptr<GELib> instance_ptr = ge::GELib::GetInstance();
  if (instance_ptr == nullptr || !instance_ptr->InitFlag()) {
    GELOGE(GE_CLI_GE_NOT_INITIALIZED, "AddGraph failed in Session.");
    return FAILED;
  }
  GELOGD("Adding graph to session");
  std::map<std::string, std::string> str_options;
  for (auto &option : options) {
    if (option.first.GetString() == nullptr || option.second.GetString() == nullptr) {
      GELOGE(FAILED, "AddGraph options is nullptr.");
      return FAILED;
    }
    std::string key = option.first.GetString();
    std::string val = option.second.GetString();
    str_options[key] = val;
  }
  Status ret = instance_ptr->SessionManagerObj().AddGraph(sessionId_, graph_id, graph, str_options);
  if (ret != SUCCESS) {
    GELOGE(ret, "AddGraph failed in Session.");
    return FAILED;
  }
  GELOGD("AddGraph finished in Session.");
  return ret;
}

Status Session::AddGraphWithCopy(uint32_t graph_id, const Graph &graph) {
  std::map<AscendString, AscendString> options;
  return AddGraphWithCopy(graph_id, graph, options);
}

Status Session::AddGraphWithCopy(uint32_t graph_id, const Graph &graph,
                                 const std::map<AscendString, AscendString> &options) {
  GELOGT(TRACE_INIT, "Start to add graph in Session. graph_id: %u, session_id: %lu.", graph_id, sessionId_);
  std::shared_ptr<GELib> instance_ptr = ge::GELib::GetInstance();
  if (instance_ptr == nullptr || !instance_ptr->InitFlag()) {
    GELOGE(GE_CLI_GE_NOT_INITIALIZED, "AddGraph failed in Session.");
    return FAILED;
  }
  std::map<std::string, std::string> str_options;
  for (auto it = options.begin(); it != options.end(); ++it) {
    str_options.insert({it->first.GetString(), it->second.GetString()});
  }
  GELOGD("Adding graph to session");
  Status ret = instance_ptr->SessionManagerObj().AddGraphWithCopy(sessionId_, graph_id, graph, str_options);
  if (ret != SUCCESS) {
    GELOGE(ret, "AddGraph failed in Session.");
    return FAILED;
  }
  GELOGD("AddGraph finished in Session.");
  return ret;
}

Status Session::RemoveGraph(uint32_t graph_id) {
  GELOGT(TRACE_INIT, "Session RemoveGraph start");

  // call RemoveGraph
  std::shared_ptr<GELib> instance_ptr = ge::GELib::GetInstance();
  if (!instance_ptr || !instance_ptr->InitFlag()) {
    GELOGE(GE_CLI_GE_NOT_INITIALIZED, "Session RemoveGraph failed");
    return FAILED;
  }

  GELOGT(TRACE_RUNNING, "Removing Graph from session");
  Status ret = instance_ptr->SessionManagerObj().RemoveGraph(sessionId_, graph_id);
  // check return status, return
  if (ret != SUCCESS) {
    GELOGE(ret, "session RemoveGraph failed");
    return FAILED;
  }
  GELOGT(TRACE_STOP, "Session RemoveGraph finished");
  return ret;
}

void PrintOutputResult(std::vector<Tensor> &outputs) {
  if (outputs.empty() || outputs[0].GetData() == nullptr) {
    GELOGW("outputs is empty or data is nullptr.");
    return;
  }

  size_t out_buf_size = outputs[0].GetSize();
  TensorDesc desc(outputs[0].GetTensorDesc());
  DataType data_type = desc.GetDataType();
  auto iter = CONST_OPDATA_TYPE_SIZE_MAP.find(data_type);
  if (iter == CONST_OPDATA_TYPE_SIZE_MAP.end()) {
    GELOGI("DataType %s has not defined size", TypeUtils::DataTypeToSerialString(data_type).c_str());
    return;
  }
  size_t length = CONST_OPDATA_TYPE_SIZE_MAP[data_type];
  for (size_t i = 0; i < 10 && i < (out_buf_size / length); ++i) {  // take first 10 at most
    switch (data_type) {
      case DT_BOOL:
      case DT_INT8:
      case DT_UINT8:
        GELOGI("output data[%zu]=%d", i, *(reinterpret_cast<int8_t *>(outputs[0].GetData()) + i));
        break;
      case DT_INT16:
      case DT_UINT16:
        GELOGI("output data[%zu]=%d", i, *(reinterpret_cast<int16_t *>(outputs[0].GetData()) + i));
        break;
      case DT_INT32:
      case DT_UINT32:
        GELOGI("output data[%zu]=%d", i, *(reinterpret_cast<int32_t *>(outputs[0].GetData()) + i));
        break;
      case DT_INT64:
      case DT_UINT64:
        GELOGI("output data[%zu]=%ld", i, *(reinterpret_cast<int64_t *>(outputs[0].GetData()) + i));
        break;
      case DT_FLOAT:
        GELOGI("output data[%zu]=%f", i, *(reinterpret_cast<float *>(outputs[0].GetData()) + i));
        break;
      case DT_DOUBLE:
        GELOGI("output data[%zu]=%lf", i, *(reinterpret_cast<double *>(outputs[0].GetData()) + i));
        break;
      default:
        GELOGI("Output datatype %s is not supported.", TypeUtils::DataTypeToSerialString(data_type).c_str());
        return;
    }
  }
}

Status Session::RunGraph(uint32_t graph_id, const std::vector<Tensor> &inputs, std::vector<Tensor> &outputs) {
  GELOGT(TRACE_INIT, "Session RunGraph start");

  std::vector<Tensor> graph_inputs = inputs;
  // call RunGraph
  std::shared_ptr<GELib> instance_ptr = ge::GELib::GetInstance();
  if (instance_ptr == nullptr || !instance_ptr->InitFlag()) {
    GELOGE(GE_CLI_GE_NOT_INITIALIZED, "Session RunGraph failed");
    return FAILED;
  }
  GELOGT(TRACE_RUNNING, "Running Graph");
  Status ret = instance_ptr->SessionManagerObj().RunGraph(sessionId_, graph_id, graph_inputs, outputs);
  // check return status
  if (ret != SUCCESS) {
    GELOGE(ret, "Session RunGraph failed");
    return FAILED;
  }

  // print output
  if (outputs.size() > 0) {
    PrintOutputResult(outputs);
  }

  // return
  GELOGT(TRACE_STOP, "Session RunGraph finished");
  return ret;
}

Status Session::RegisterCallBackFunc(const std::string &key, const pCallBackFunc &callback) {
  return ge::GELib::GetInstance()->SessionManagerObj().RegisterCallBackFunc(sessionId_, key, callback);
}

Status Session::RegisterCallBackFunc(const char *key, const session::pCallBackFunc &callback) {
  std::string str_key;
  if (key != nullptr) {
    str_key = key;
  }
  return ge::GELib::GetInstance()->SessionManagerObj().RegisterCallBackFunc(sessionId_, str_key, callback);
}

Status Session::BuildGraph(uint32_t graph_id, const std::vector<InputTensorInfo> &inputs) {
  std::shared_ptr<GELib> instance_ptr = ge::GELib::GetInstance();
  if (instance_ptr == nullptr || !instance_ptr->InitFlag()) {
    GELOGE(GE_CLI_GE_NOT_INITIALIZED, "SessionConstructor failed");
    return FAILED;
  }
  GELOGT(TRACE_RUNNING, "Building Graph");
  Status ret = instance_ptr->SessionManagerObj().BuildGraph(sessionId_, graph_id, inputs);
  if (ret != SUCCESS) {
    GELOGE(ret, "Session BuildGraph failed");
    return FAILED;
  }
  return SUCCESS;
}

Status Session::RunGraphAsync(uint32_t graph_id, const std::vector<InputTensorInfo> &inputs,
                              RunAsyncCallback callback) {
  std::shared_ptr<GELib> instance_ptr = ge::GELib::GetInstance();
  if (instance_ptr == nullptr || !instance_ptr->InitFlag()) {
    GELOGE(GE_CLI_GE_NOT_INITIALIZED, "SessionConstructor failed");
    return FAILED;
  }
  GELOGT(TRACE_RUNNING, "Run Graph Asynchronously");
  GELOGW(
      "The callback function will not be checked. Please ensure that the implementation of the function is trusted.");

  Status ret = ge::GELib::GetInstance()->SessionManagerObj().RunGraphAsync(sessionId_, graph_id, inputs, callback);
  if (ret != SUCCESS) {
    GELOGE(ret, "SessionManager RunGraphAsync failed");
    return FAILED;
  }
  return SUCCESS;
}

Status Session::GetVariables(const std::vector<std::string> &var_names, std::vector<Tensor> &var_values) {
  auto instance_ptr = ge::GELib::GetInstance();
  if (instance_ptr == nullptr || !instance_ptr->InitFlag()) {
    GELOGE(GE_CLI_GE_NOT_INITIALIZED, "SessionConstructor failed");
    return FAILED;
  }
  GELOGT(TRACE_RUNNING, "Get Variables");
  Status ret = ge::GELib::GetInstance()->SessionManagerObj().GetVariables(sessionId_, var_names, var_values);
  if (ret != SUCCESS) {
    GELOGE(ret, "SessionManager RunGraphAsync failed");
    return FAILED;
  }
  return SUCCESS;
}

Status Session::GetVariables(const std::vector<AscendString> &var_names, std::vector<Tensor> &var_values) {
  auto instance_ptr = ge::GELib::GetInstance();
  if (instance_ptr == nullptr || !instance_ptr->InitFlag()) {
    GELOGE(GE_CLI_GE_NOT_INITIALIZED, "SessionConstructor failed");
    return FAILED;
  }
  GELOGT(TRACE_RUNNING, "Get Variables");
  std::vector<ge::string> str_var_names;
  for (auto &var_name : var_names) {
    if (var_name.GetString() == nullptr) {
      GELOGE(FAILED, "GetVariables name is nullptr.");
      return FAILED;
    }
    str_var_names.emplace_back(var_name.GetString());
  }
  Status ret = ge::GELib::GetInstance()->SessionManagerObj().GetVariables(sessionId_, str_var_names, var_values);
  if (ret != SUCCESS) {
    GELOGE(ret, "SessionManager RunGraphAsync failed");
    return FAILED;
  }
  return SUCCESS;
}

bool Session::IsGraphNeedRebuild(uint32_t graph_id) {
  return ge::GELib::GetInstance()->SessionManagerObj().IsGraphNeedRebuild(sessionId_, graph_id);
}
}  // namespace ge
