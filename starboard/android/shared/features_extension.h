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

#ifndef STARBOARD_ANDROID_SHARED_FEATURES_EXTENSION_H_
#define STARBOARD_ANDROID_SHARED_FEATURES_EXTENSION_H_

namespace starboard::android::shared {

// Starboard extension function to receive the Starboard
// FeaturesAPI.
const void* GetFeaturesApi();

}  // namespace starboard::android::shared

#endif  // STARBOARD_ANDROID_SHARED_FEATURES_EXTENSION_H_
