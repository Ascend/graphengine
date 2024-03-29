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

#include "graph/manager/graph_mem_allocator.h"

#include <string>
#include "graph/manager/graph_caching_allocator.h"
#include "graph/manager/rdma_pool_allocator.h"
#include "graph/manager/host_mem_allocator.h"
namespace ge {
void MemoryAllocator::Initialize(uint32_t device_id) {
  GELOGI("MemoryAllocator::Initialize");

  // when redo Initialize free memory
  for (auto &it : memory_base_map_) {
    if (FreeMemory(it.second.memory_addr_, device_id) != ge::SUCCESS) {
      GELOGW("Initialize: FreeMemory failed");
    }
  }
  memory_base_map_.clear();
}

void MemoryAllocator::Finalize(uint32_t device_id) {
  GELOGI("MemoryAllocator::Finalize");

  // free memory
  for (auto &it : memory_base_map_) {
    if (FreeMemory(it.second.memory_addr_, device_id) != ge::SUCCESS) {
      GELOGW("Finalize: FreeMemory failed");
    }
  }
  memory_base_map_.clear();
}

uint8_t *MemoryAllocator::MallocMemory(const string &purpose, size_t memory_size, uint32_t device_id) const {
  uint8_t *memory_addr = nullptr;

  if (rtMalloc(reinterpret_cast<void **>(&memory_addr), memory_size, memory_type_) != RT_ERROR_NONE) {
    GELOGE(ge::INTERNAL_ERROR,
           "MemoryAllocator::MallocMemory device_id = %u,"
           " size= %lu",
           device_id, memory_size);

    return nullptr;
  }

  GELOGI("MemoryAllocator::MallocMemory device_id = %u, size= %lu", device_id, memory_size);
  GE_PRINT_DYNAMIC_MEMORY(rtMalloc, purpose.c_str(), memory_size)
  return memory_addr;
}

Status MemoryAllocator::FreeMemory(uint8_t *memory_addr, uint32_t device_id) const {
  GELOGI("MemoryAllocator::FreeMemory device_id = %u", device_id);
  auto rtRet = rtFree(memory_addr);
  if (rtRet != RT_ERROR_NONE) {
    GELOGE(rtRet, "MemoryAllocator::MallocMemory device_id = %u", device_id);
    return RT_ERROR_TO_GE_STATUS(rtRet);
  }
  memory_addr = nullptr;
  return ge::SUCCESS;
}

uint8_t *MemoryAllocator::MallocMemory(const string &purpose, const string &memory_key, size_t memory_size,
                                       uint32_t device_id) {
  auto it = memory_base_map_.find(memory_key);
  if (it != memory_base_map_.end()) {
    it->second.memory_used_num_++;
    return it->second.memory_addr_;
  }

  uint8_t *memory_addr = MallocMemory(purpose, memory_size, device_id);

  if (memory_addr == nullptr) {
    GELOGE(ge::INTERNAL_ERROR,
           "MemoryAllocator::MallocMemory failed,"
           " memory_key[%s], size = %lu.",
           memory_key.c_str(), memory_size);
    return nullptr;
  }

  MemoryInfo memory_info(memory_addr, memory_size);
  memory_info.memory_used_num_++;
  memory_base_map_[memory_key] = memory_info;
  mem_malloced_ = true;
  return memory_addr;
}

Status MemoryAllocator::FreeMemory(const string &memory_key, uint32_t device_id) {
  auto it = memory_base_map_.find(memory_key);
  if (it == memory_base_map_.end()) {
    if (mem_malloced_) {
      GELOGW(
          "MemoryAllocator::FreeMemory failed,"
          " memory_key[%s] was not exist, device_id = %u.",
          memory_key.c_str(), device_id);
    }
    return ge::INTERNAL_ERROR;
  }

  if (it->second.memory_used_num_ > 1) {
    GELOGW("MemoryAllocator::FreeMemory memory_key[%s] should not be released, reference count %d", memory_key.c_str(),
           it->second.memory_used_num_);
    // reference count greater than 1 represnt that static memory is used by
    // someone else, reference count decrement
    it->second.memory_used_num_--;
    return ge::SUCCESS;
  }

  if (FreeMemory(it->second.memory_addr_, device_id) != ge::SUCCESS) {
    GELOGE(ge::INTERNAL_ERROR,
           "MemoryAllocator::FreeMemory rtFree failed,"
           " memory_key[%s]",
           memory_key.c_str());
    return ge::INTERNAL_ERROR;
  }

  GELOGI("MemoryAllocator::FreeMemory device_id = %u", device_id);

  memory_base_map_.erase(it);
  return ge::SUCCESS;
}

uint8_t *MemoryAllocator::GetMemoryAddr(const string &memory_key, uint32_t device_id) {
  auto it = memory_base_map_.find(memory_key);
  if (it == memory_base_map_.end()) {
    GELOGW(
        "MemoryAllocator::GetMemoryAddr failed,"
        " memory_key[%s] was not exist, device_id = %u.",
        memory_key.c_str(), device_id);
    return nullptr;
  }

  return it->second.memory_addr_;
}

MemManager::MemManager() {}

MemManager::~MemManager() { Finalize(); }

MemManager &MemManager::Instance() {
  static MemManager mem_manager;
  return mem_manager;
}

MemoryAllocator *MemManager::Instance(rtMemType_t memory_type) { return Instance().GetMemoryAllocator(memory_type); }

Status MemManager::Initialize(const std::vector<rtMemType_t> &memory_type) {
  std::lock_guard<std::recursive_mutex> lock(allocator_mutex_);
  MemoryAllocator *memory_allocator = nullptr;
  for (unsigned int index : memory_type) {
    auto it = memory_allocator_map_.find(index);
    if (it == memory_allocator_map_.end()) {
      memory_allocator = new (std::nothrow) MemoryAllocator(index);

      if (memory_allocator != nullptr) {
        memory_allocator_map_[index] = memory_allocator;
        GELOGI("Create MemoryAllocator memory type[%u] success.", index);
      } else {
        GELOGE(ACL_ERROR_GE_MEMORY_ALLOCATION, "Alloc MemoryAllocator failed.");
      }
    } else {
      memory_allocator = it->second;
    }

    if (memory_allocator == nullptr) {
      GELOGE(ACL_ERROR_GE_MEMORY_ALLOCATION, "Create MemoryAllocator failed.");
      return ACL_ERROR_GE_MEMORY_ALLOCATION;
    } else {
      memory_allocator->Initialize(0);
    }
  }

  auto ret = InitAllocator(memory_type, caching_allocator_map_);
  if (ret != SUCCESS) {
    GELOGE(ret, "Create CachingAllocator failed.");
    return ret;
  }

  ret = InitAllocator(memory_type, rdma_allocator_map_);
  if (ret != SUCCESS) {
    GELOGE(ret, "Create RdmaAllocator failed.");
    return ret;
  }

  ret = InitAllocator(memory_type, host_allocator_map_);
  if (ret != SUCCESS) {
    GELOGE(ret, "Create HostMemAllocator failed.");
    return ret;
  }
  return SUCCESS;
}

template <typename T>
void FinalizeAllocatorMap(std::map<rtMemType_t, T *> &allocate_map) {
  for (auto &allocator : allocate_map) {
    if (allocator.second != nullptr) {
      allocator.second->Finalize();
      delete allocator.second;
      allocator.second = nullptr;
    }
  }
  allocate_map.clear();
}

void MemManager::Finalize() noexcept {
  GELOGI("Finalize.");
  std::lock_guard<std::recursive_mutex> lock(allocator_mutex_);
  // caching and rdma allocator use memory allocator, so finalize them first
  FinalizeAllocatorMap(caching_allocator_map_);
  FinalizeAllocatorMap(rdma_allocator_map_);
  FinalizeAllocatorMap(host_allocator_map_);
  FinalizeAllocatorMap(memory_allocator_map_);
}

MemoryAllocator *MemManager::GetMemoryAllocator(rtMemType_t memory_type) {
  std::lock_guard<std::recursive_mutex> lock(allocator_mutex_);
  MemoryAllocator *memory_allocator = nullptr;
  auto it = memory_allocator_map_.find(memory_type);
  if (it != memory_allocator_map_.end()) {
    memory_allocator = it->second;
  }

  // Usually impossible
  if (memory_allocator == nullptr) {
    GELOGE(ACL_ERROR_GE_INTERNAL_ERROR, "GetMemoryAllocator failed, memory type is %u.", memory_type);
    static MemoryAllocator default_memory_allocator(RT_MEMORY_RESERVED);
    return &default_memory_allocator;
  }

  return memory_allocator;
}

CachingAllocator &MemManager::CachingInstance(rtMemType_t memory_type) {
  return Instance().GetAllocator(memory_type, caching_allocator_map_);
}

RdmaPoolAllocator &MemManager::RdmaPoolInstance(rtMemType_t memory_type) {
  return Instance().GetAllocator(memory_type, rdma_allocator_map_);
}
HostMemAllocator &MemManager::HostMemInstance(rtMemType_t memory_type) {
  return Instance().GetAllocator(memory_type, host_allocator_map_);
}
}  // namespace ge
