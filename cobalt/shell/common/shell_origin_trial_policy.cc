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

#include "cobalt/shell/common/shell_origin_trial_policy.h"

#include "base/cxx17_backports.h"
#include "base/feature_list.h"
#include "content/public/common/content_features.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"

namespace content {

namespace {

// This is the public key which the content shell will use to enable origin
// trial features. Trial tokens for use in web tests can be created with the
// tool in /tools/origin_trials/generate_token.py, using the private key
// contained in /tools/origin_trials/eftest.key.
static const blink::OriginTrialPublicKey kOriginTrialPublicKey = {
    0x75, 0x10, 0xac, 0xf9, 0x3a, 0x1c, 0xb8, 0xa9, 0x28, 0x70, 0xd2,
    0x9a, 0xd0, 0x0b, 0x59, 0xe1, 0xac, 0x2b, 0xb7, 0xd5, 0xca, 0x1f,
    0x64, 0x90, 0x08, 0x8e, 0xa8, 0xe0, 0x56, 0x3a, 0x04, 0xd0,
};

}  // namespace

ShellOriginTrialPolicy::ShellOriginTrialPolicy() {
  public_keys_.push_back(kOriginTrialPublicKey);
}

ShellOriginTrialPolicy::~ShellOriginTrialPolicy() {}

bool ShellOriginTrialPolicy::IsOriginTrialsSupported() const {
  return true;
}

const std::vector<blink::OriginTrialPublicKey>&
ShellOriginTrialPolicy::GetPublicKeys() const {
  return public_keys_;
}

bool ShellOriginTrialPolicy::IsOriginSecure(const GURL& url) const {
  return network::IsUrlPotentiallyTrustworthy(url);
}

}  // namespace content
