// Copyright 2019 The Cobalt Authors. All Rights Reserved.
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

#include <pthread.h>

#include <string>

#include "build/build_config.h"
#include "starboard/common/command_line.h"
#include "starboard/common/log.h"
#include "starboard/common/string.h"
#include "starboard/crashpad_wrapper/annotations.h"
#include "starboard/crashpad_wrapper/wrapper.h"
#include "starboard/elf_loader/elf_loader.h"
#include "starboard/elf_loader/elf_loader_constants.h"
#include "starboard/elf_loader/evergreen_info.h"
#include "starboard/elf_loader/sabi_string.h"
#include "starboard/event.h"

starboard::elf_loader::ElfLoader g_elf_loader;

void (*g_sb_event_func)(const SbEvent*) = NULL;

void LoadLibraryAndInitialize(const std::string& library_path,
                              const std::string& content_path) {
  if (library_path.empty()) {
    SB_LOG(ERROR) << "Library must be specified with --"
                  << starboard::elf_loader::kEvergreenLibrary
                  << "=path/to/library/relative/to/loader/content.";
    return;
  }
  if (content_path.empty()) {
    SB_LOG(ERROR) << "Content must be specified with --"
                  << starboard::elf_loader::kEvergreenContent
                  << "=path/to/content/relative/to/loader/content.";
    return;
  }
  if (!g_elf_loader.Load(library_path, content_path, true)) {
    SB_NOTREACHED() << "Failed to load library at '"
                    << g_elf_loader.GetLibraryPath() << "'.";
    return;
  }

  SB_LOG(INFO) << "Successfully loaded '" << g_elf_loader.GetLibraryPath()
               << "'.";

  EvergreenInfo evergreen_info;
  GetEvergreenInfo(&evergreen_info);
  if (!third_party::crashpad::wrapper::AddEvergreenInfoToCrashpad(
          evergreen_info)) {
    SB_LOG(ERROR) << "Could not send Cobalt library information into Crashpad.";
  } else {
    SB_LOG(INFO) << "Loaded Cobalt library information into Crashpad.";
  }

// TODO: Cobalt b/428765497 - Make sure the shared library has
// GetEvergreenSabiString.
#if BUILDFLAG(ENABLE_COBALT_HERMETIC_HACKS)
  auto get_evergreen_sabi_string_func = reinterpret_cast<const char* (*)()>(
      g_elf_loader.LookupSymbol("GetEvergreenSabiString"));

  if (!CheckSabi(get_evergreen_sabi_string_func)) {
    SB_LOG(ERROR) << "CheckSabi failed";
    return;
  }
#endif

  g_sb_event_func = reinterpret_cast<void (*)(const SbEvent*)>(
      g_elf_loader.LookupSymbol("SbEventHandle"));

  auto get_user_agent_func = reinterpret_cast<const char* (*)()>(
      g_elf_loader.LookupSymbol("GetCobaltUserAgentString"));
  if (!get_user_agent_func) {
    SB_LOG(ERROR) << "Failed to get user agent string";
  } else {
    std::vector<char> buffer(USER_AGENT_STRING_MAX_SIZE);
    starboard::strlcpy(buffer.data(), get_user_agent_func(),
                       USER_AGENT_STRING_MAX_SIZE);
    if (third_party::crashpad::wrapper::InsertCrashpadAnnotation(
            third_party::crashpad::wrapper::kCrashpadUserAgentStringKey,
            buffer.data())) {
      SB_DLOG(INFO) << "Added user agent string to Crashpad.";
    } else {
      SB_DLOG(INFO) << "Failed to add user agent string to Crashpad.";
    }
  }

  if (!g_sb_event_func) {
    SB_LOG(ERROR) << "Failed to find SbEventHandle.";
    return;
  }

  SB_LOG(INFO) << "Found SbEventHandle at address: "
               << reinterpret_cast<void*>(g_sb_event_func);
}

void SbEventHandle(const SbEvent* event) {
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

  SB_CHECK(pthread_mutex_lock(&mutex) == 0);

  if (!g_sb_event_func) {
    const SbEventStartData* data = static_cast<SbEventStartData*>(event->data);
    const starboard::CommandLine command_line(
        data->argument_count, const_cast<const char**>(data->argument_values));
    LoadLibraryAndInitialize(
        command_line.GetSwitchValue(starboard::elf_loader::kEvergreenLibrary),
        command_line.GetSwitchValue(starboard::elf_loader::kEvergreenContent));
    SB_CHECK(g_sb_event_func);
  }

  g_sb_event_func(event);

  SB_CHECK(pthread_mutex_unlock(&mutex) == 0);
}

int main(int argc, char** argv) {
  return SbRunStarboardMain(argc, argv, SbEventHandle);
}
