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

#ifndef STARBOARD_SHARED_STARBOARD_FILE_ATOMIC_REPLACE_WRITE_FILE_H_
#define STARBOARD_SHARED_STARBOARD_FILE_ATOMIC_REPLACE_WRITE_FILE_H_

#include "starboard/configuration.h"
#include "starboard/types.h"

namespace starboard::shared::starboard {

bool SbFileAtomicReplaceWriteFile(const char* path,
                                  const char* data,
                                  int64_t data_size);

}  // namespace starboard::shared::starboard

#endif  // STARBOARD_SHARED_STARBOARD_FILE_ATOMIC_REPLACE_WRITE_FILE_H_
