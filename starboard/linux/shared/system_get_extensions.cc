// Copyright 2020 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "starboard/system.h"

#include "starboard/common/string.h"
#if SB_IS(EVERGREEN_COMPATIBLE)
#include "starboard/elf_loader/evergreen_config.h"
#endif
#include "starboard/extension/configuration.h"
#include "starboard/extension/crash_handler.h"
#include "starboard/extension/enhanced_audio.h"
#include "starboard/extension/free_space.h"
#include "starboard/extension/ifa.h"
#if SB_IS(EVERGREEN_COMPATIBLE)
#include "starboard/extension/loader_app_metrics.h"
#endif
#include "starboard/extension/memory_mapped_file.h"
#include "starboard/extension/platform_service.h"
#include "starboard/linux/shared/configuration.h"
#include "starboard/linux/shared/ifa.h"
#include "starboard/linux/shared/platform_service.h"
#include "starboard/linux/shared/time_zone.h"
#include "starboard/shared/enhanced_audio/enhanced_audio.h"
#include "starboard/shared/posix/free_space.h"
#include "starboard/shared/posix/memory_mapped_file.h"
#include "starboard/shared/starboard/crash_handler.h"
#if SB_IS(EVERGREEN_COMPATIBLE)
#include "starboard/shared/starboard/loader_app_metrics.h"
#endif

const void* SbSystemGetExtension(const char* name) {
#if SB_IS(EVERGREEN_COMPATIBLE)
  const starboard::elf_loader::EvergreenConfig* evergreen_config =
      starboard::elf_loader::EvergreenConfig::GetInstance();
  if (evergreen_config != NULL &&
      evergreen_config->custom_get_extension_ != NULL) {
    const void* ext = evergreen_config->custom_get_extension_(name);
    if (ext != NULL) {
      return ext;
    }
  }
#endif
  if (strcmp(name, kCobaltExtensionPlatformServiceName) == 0) {
    return starboard::shared::GetPlatformServiceApi();
  }
  if (strcmp(name, kCobaltExtensionConfigurationName) == 0) {
    return starboard::shared::GetConfigurationApi();
  }
  if (strcmp(name, kCobaltExtensionCrashHandlerName) == 0) {
    return starboard::common::GetCrashHandlerApi();
  }
  if (strcmp(name, kCobaltExtensionMemoryMappedFileName) == 0) {
    return starboard::shared::posix::GetMemoryMappedFileApi();
  }
  if (strcmp(name, kCobaltExtensionFreeSpaceName) == 0) {
    return starboard::shared::posix::GetFreeSpaceApi();
  }
#if SB_IS(EVERGREEN_COMPATIBLE)
  if (strcmp(name, kStarboardExtensionLoaderAppMetricsName) == 0) {
    return starboard::shared::starboard::GetLoaderAppMetricsApi();
  }
#endif
  return NULL;
}
