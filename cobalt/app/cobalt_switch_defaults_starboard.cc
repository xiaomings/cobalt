// Copyright 2025 The Cobalt Authors. All Rights Reserved.
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

#include "cobalt/app/cobalt_switch_defaults_starboard.h"

#include "base/base_switches.h"
#include "base/files/file_path.h"
#include "cobalt/browser/switches.h"
#include "cobalt/shell/common/shell_switches.h"
#include "content/public/common/content_switches.h"
#include "gpu/command_buffer/service/gpu_switches.h"
#include "gpu/config/gpu_switches.h"
#include "media/base/media_switches.h"
#include "sandbox/policy/switches.h"
#include "third_party/blink/public/common/switches.h"
#include "ui/gl/gl_switches.h"

#if BUILDFLAG(IS_OZONE)
#include "ui/ozone/public/ozone_switches.h"
#endif

namespace {

// ==========
// IMPORTANT:
//
// These command line switches defaults do not affect non-POSIX platforms. They
// only affect platforms such as Linux and AOSP.
// If you are making changes to these values, please check that other
// platforms (such as AndroidTV) are getting corresponding updates.

// List of toggleable default switches.
static constexpr auto kCobaltToggleSwitches = std::to_array<const char*>({
  // Enable Blink to work in overlay video mode
  switches::kForceVideoOverlays,
      // Disable multiprocess mode.
      switches::kSingleProcess,
      // Hide content shell toolbar.
      switches::kContentShellHideToolbar,
      // Accelerated GL is blanket disabled for Linux. Ignore the GPU blocklist
      // to enable it.
      switches::kIgnoreGpuBlocklist,
#if BUILDFLAG(IS_ANDROID)
      // This flag is added specifically for m114 and should be removed after
      // rebasing to m120+
      switches::kUserLevelMemoryPressureSignalParams,
#endif  // BUILDFLAG(IS_ANDROID)
      // Disable Zygote (a process fork utility); in turn needs sandbox
      // disabled.
      switches::kNoZygote, sandbox::policy::switches::kNoSandbox,
      // Rasterize Tiles directly to GPU memory (ZeroCopyRasterBufferProvider).
      blink::switches::kEnableZeroCopy,
      // Enable low-end device mode. This comes with a load of memory and CPU
      // saving goodies but can degrade the experience considerably. One of the
      // known regressions is 4444 textures, which are then disabled explicitly.
      switches::kEnableLowEndDeviceMode,
      blink::switches::kDisableRGBA4444Textures,
      // For Starboard the signal handlers are already setup. Disable the
      // Chromium registrations to avoid overriding the Starboard ones.
      switches::kDisableInProcessStackTraces,
      // Cobalt doesn't use Chrome's accelerated video decoding/encoding.
      switches::kDisableAcceleratedVideoDecode,
      switches::kDisableAcceleratedVideoEncode,
});

// Map of switches with parameters and their defaults.
const base::CommandLine::SwitchMap GetCobaltParamSwitchDefaults() {
  const base::CommandLine::SwitchMap cobalt_param_switch_defaults({
    // Disable Vulkan.
    {switches::kDisableFeatures, "Vulkan"},
        // The Renderer Compositor (a.k.a. "cc" see //docs/how_cc_works.md) has
        // two important parts re. memory consumption, one is the image decode
        // cache whose size is specified by the LimitImageDecodeCacheSize flag
        // and the tile manager cache of rasterized content (i.e. content that
        // has been rastered already or pre-rastered and is kept for later fast
        // (re)use) that can be overwritten with the kForceGpuMemAvailableMb
        // switch.
        // TODO(mcasas): Ideally configure depending on policy.
        {switches::kForceGpuMemAvailableMb, "32"},
        {switches::kEnableFeatures, "LimitImageDecodeCacheSize:mb/24"},
    // Force some ozone settings.
#if BUILDFLAG(IS_OZONE)
        {switches::kUseGL, "angle"}, {switches::kUseANGLE, "gles-egl"},
#endif
        // Use passthrough command decoder.
        {switches::kUseCmdDecoder, "passthrough"},
        // Set the default size for the content shell/starboard window.
        {switches::kContentShellHostWindowSize, "1920x1080"},
        // Enable remote Devtools access.
        {switches::kRemoteDebuggingPort, "9222"},
        {switches::kRemoteAllowOrigins, "http://localhost:9222"},
        // kEnableLowEndDeviceMode sets MSAA to 4 (and not 8, the default). But
        // we set it explicitly just in case.
        {blink::switches::kGpuRasterizationMSAASampleCount, "4"},
  });
  return cobalt_param_switch_defaults;
}

constexpr base::CommandLine::StringPieceType kDefaultSwitchPrefix = "--";
constexpr base::CommandLine::CharType kSwitchValueSeparator[] =
    FILE_PATH_LITERAL("=");

bool IsSwitch(const base::CommandLine::StringType& string) {
  base::CommandLine::StringType prefix(kDefaultSwitchPrefix);
  if (string.substr(0, prefix.length()) == prefix) {
    return true;
  }
  return false;
}

}  // namespace

namespace cobalt {

CommandLinePreprocessor::CommandLinePreprocessor(int argc,
                                                 const char* const* argv)
    : cmd_line_(argc, argv) {
  // Toggle-switch defaults are just turned on by default.
  for (const auto& cobalt_switch : kCobaltToggleSwitches) {
    cmd_line_.AppendSwitch(cobalt_switch);
  }

  const auto cobalt_param_switch_defaults = GetCobaltParamSwitchDefaults();

  // Handle special-case switches with parameters, such as:
  // * Duplicate switches with arguments.
  // * Inconsistent settings across related switches.

  // Merge all disabled feature lists together.
  if (cmd_line_.HasSwitch(::switches::kDisableFeatures)) {
    std::string disabled_features(
        cmd_line_.GetSwitchValueASCII(::switches::kDisableFeatures));
    auto old_value =
        cobalt_param_switch_defaults.find(::switches::kDisableFeatures);
    if (old_value != cobalt_param_switch_defaults.end()) {
      disabled_features += std::string(",");
      disabled_features += std::string(old_value->second);
      cmd_line_.AppendSwitchNative(::switches::kDisableFeatures,
                                   disabled_features);
    }
  }

  // Override kContentShellHostWindowSize if the user sets kWindowSize.
  if (cmd_line_.HasSwitch(switches::kWindowSize)) {
    std::string window_size =
        cmd_line_.GetSwitchValueASCII(switches::kWindowSize);
    std::replace(window_size.begin(), window_size.end(), ',', 'x');
    cmd_line_.AppendSwitchASCII(::switches::kContentShellHostWindowSize,
                                window_size);
  }

  // Any remaining parameter switches are set to their defaults.
  for (const auto& iter : cobalt_param_switch_defaults) {
    const auto& switch_key = iter.first;
    const auto& switch_val = iter.second;
    if (!cmd_line_.HasSwitch(iter.first)) {
      cmd_line_.AppendSwitchNative(switch_key, switch_val);
    }
  }

  // Fix any remaining conflicts with the initial URL.
  const auto initial_url = switches::GetInitialURL(cmd_line_);

  // Collect all non-switch arguments.
  base::CommandLine::StringVector nonswitch_args;
  for (const auto& arg : cmd_line_.argv()) {
    if (!IsSwitch(arg)) {
      nonswitch_args.push_back(arg);
    }
  }

  if (nonswitch_args.size() == 1) {
    startup_url_ = initial_url;
  } else {
    const auto first_arg = nonswitch_args.at(1);
    if (std::string(first_arg) != initial_url) {
      LOG(INFO) << "First argument differs from initial URL: \"" << first_arg
                << "\" vs. \"" << initial_url << "\"";
      // Always prefer the first argument.
      // The warning indicates that `--url` will be ignored.
      LOG(WARNING) << "Overriding initial URL with first argument";
      cmd_line_.AppendSwitchNative(cobalt::switches::kInitialURL, first_arg);
      startup_url_ = std::string(first_arg);
    }
  }
}

const base::CommandLine::StringVector CommandLinePreprocessor::argv() const {
  // Reconstruct as well formatted string: PROGRAM [SWITCHES...] [ARGS...]
  base::CommandLine::StringVector out_argv;
  out_argv.push_back(cmd_line_.GetProgram().value());

  for (const auto& switch_arg : cmd_line_.GetSwitches()) {
    auto key = std::string(switch_arg.first);
    auto val = std::string(switch_arg.second);
    std::string switch_str = std::string(kDefaultSwitchPrefix) + key;
    if (val.length() != 0) {
      switch_str += std::string(kSwitchValueSeparator) + val;
    }
    out_argv.push_back(switch_str);
  }

  // Only forward the desired startup URL argument to Cobalt.
  // Note: this is also duplicated in the --url argument.
  out_argv.push_back(startup_url_);

  return out_argv;
}

}  // namespace cobalt
