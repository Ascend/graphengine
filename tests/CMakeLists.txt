# Copyright 2019-2020 Huawei Technologies Co., Ltd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ============================================================================

project(tests CXX C)
find_package(Threads)
add_subdirectory(depends/cce)
add_subdirectory(depends/slog)
add_subdirectory(depends/mmpa)
add_subdirectory(depends/runtime)
add_subdirectory(depends/omg)
add_subdirectory(depends/hccl)
add_subdirectory(depends/profiler)
add_subdirectory(depends/error_manager)

if (ENABLE_GE_COV OR ENABLE_GE_UT)
    add_subdirectory(ut)
endif()

if (ENABLE_GE_ST)
    add_subdirectory(st)
endif()
