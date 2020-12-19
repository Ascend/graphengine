/**
 * Copyright 2020 Huawei Technologies Co., Ltd

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 * http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#include "register/scope/scope_pass_registry_impl.h"
#include <algorithm>
#include <map>
#include <vector>
#include <utility>
#include "graph/debug/ge_log.h"
#include "external/register/scope/scope_fusion_pass_register.h"

using ge::MEMALLOC_FAILED;

namespace ge {
struct CreatePassFnPack {
  bool is_enable;
  ScopeFusionPassRegistry::CreateFn create_fn;
};

void ScopeFusionPassRegistry::ScopeFusionPassRegistryImpl::RegisterScopeFusionPass(
    const std::string &pass_name, ScopeFusionPassRegistry::CreateFn create_fn, bool is_general) {
  std::lock_guard<std::mutex> lock(mu_);
  auto iter = std::find(pass_names_.begin(), pass_names_.end(), pass_name);
  if (iter != pass_names_.end()) {
    GELOGW("The scope fusion pass has been registered and will not overwrite the previous one, pass name = %s.",
           pass_name.c_str());
    return;
  }

  CreatePassFnPack create_fn_pack;
  create_fn_pack.is_enable = is_general;
  create_fn_pack.create_fn = create_fn;
  create_fn_packs_[pass_name] = create_fn_pack;
  pass_names_.push_back(pass_name);
  GELOGI("Register scope fusion pass, pass name = %s, is_enable = %d.", pass_name.c_str(), is_general);
}

ScopeFusionPassRegistry::CreateFn ScopeFusionPassRegistry::ScopeFusionPassRegistryImpl::GetCreateFn(
    const std::string &pass_name) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = create_fn_packs_.find(pass_name);
  if (it == create_fn_packs_.end()) {
    GELOGW("Scope fusion pass is not registered. pass name = %s.", pass_name.c_str());
    return nullptr;
  }

  CreatePassFnPack &create_fn_pack = it->second;
  if (create_fn_pack.is_enable) {
    return create_fn_pack.create_fn;
  } else {
    GELOGW("The scope fusion pass is disabled, pass name = %s", pass_name.c_str());
    return nullptr;
  }
}

std::vector<std::string> ScopeFusionPassRegistry::ScopeFusionPassRegistryImpl::GetAllRegisteredPasses() {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<std::string> all_passes;
  for (size_t i = 0; i < pass_names_.size(); ++i) {
    if (create_fn_packs_[pass_names_[i]].is_enable) {
      all_passes.push_back(pass_names_[i]);
    }
  }

  return all_passes;
}

bool ScopeFusionPassRegistry::ScopeFusionPassRegistryImpl::SetPassEnableFlag(
    const std::string pass_name, const bool flag) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = create_fn_packs_.find(pass_name);
  if (it == create_fn_packs_.end()) {
    GELOGW("Scope fusion pass is not registered. pass name = %s.", pass_name.c_str());
    return false;
  }

  CreatePassFnPack &create_fn_pack = it->second;
  create_fn_pack.is_enable = flag;
  GELOGI("enable flag of scope fusion pass:%s is set with %s.", pass_name.c_str(), flag ? "true" : "false");

  return true;
}

std::unique_ptr<ScopeBasePass> ScopeFusionPassRegistry::ScopeFusionPassRegistryImpl::CreateScopeFusionPass(
    const std::string &pass_name) {
  auto create_fn = GetCreateFn(pass_name);
  if (create_fn == nullptr) {
    GELOGD("Create scope fusion pass failed, pass name = %s.", pass_name.c_str());
    return nullptr;
  }
  GELOGI("Create scope fusion pass, pass name = %s.", pass_name.c_str());
  return std::unique_ptr<ScopeBasePass>(create_fn());
}

ScopeFusionPassRegistry::ScopeFusionPassRegistry() {
  impl_ = std::unique_ptr<ScopeFusionPassRegistryImpl>(new (std::nothrow) ScopeFusionPassRegistryImpl);
}

ScopeFusionPassRegistry::~ScopeFusionPassRegistry() {}

void ScopeFusionPassRegistry::RegisterScopeFusionPass(const std::string &pass_name, CreateFn create_fn,
                                                      bool is_general) {
  if (impl_ == nullptr) {
    GELOGE(MEMALLOC_FAILED, "Failed to register %s, ScopeFusionPassRegistry is not properly initialized.",
           pass_name.c_str());
    return;
  }
  impl_->RegisterScopeFusionPass(pass_name, create_fn, is_general);
}

void ScopeFusionPassRegistry::RegisterScopeFusionPass(const char *pass_name, CreateFn create_fn,
                                                      bool is_general) {
  if (impl_ == nullptr) {
    GELOGE(MEMALLOC_FAILED, "Failed to register %s, ScopeFusionPassRegistry is not properly initialized.",
           pass_name);
    return;
  }
  std::string str_pass_name;
  if (pass_name != nullptr) {
    str_pass_name = pass_name;
  }
  impl_->RegisterScopeFusionPass(str_pass_name, create_fn, is_general);
}

ScopeFusionPassRegistrar::ScopeFusionPassRegistrar(const char *pass_name, ScopeBasePass *(*create_fn)(),
                                                   bool is_general) {
  if (pass_name == nullptr) {
    GELOGE(PARAM_INVALID, "Failed to register scope fusion pass, pass name is null.");
    return;
  }

  ScopeFusionPassRegistry::GetInstance().RegisterScopeFusionPass(pass_name, create_fn, is_general);
}
}  // namespace ge