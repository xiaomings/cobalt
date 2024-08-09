// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_CHROMIUM_MEDIA_CDM_DEFAULT_CDM_FACTORY_H_
#define THIRD_PARTY_CHROMIUM_MEDIA_CDM_DEFAULT_CDM_FACTORY_H_

#include "base/macros.h"
#include "third_party/chromium/media/base/cdm_factory.h"
#include "third_party/chromium/media/base/media_export.h"

namespace media_m96 {

struct CdmConfig;

class MEDIA_EXPORT DefaultCdmFactory final : public CdmFactory {
 public:
  DefaultCdmFactory();

  DefaultCdmFactory(const DefaultCdmFactory&) = delete;
  DefaultCdmFactory& operator=(const DefaultCdmFactory&) = delete;

  ~DefaultCdmFactory() final;

  // CdmFactory implementation.
  void Create(const std::string& key_system,
              const CdmConfig& cdm_config,
              const SessionMessageCB& session_message_cb,
              const SessionClosedCB& session_closed_cb,
              const SessionKeysChangeCB& session_keys_change_cb,
              const SessionExpirationUpdateCB& session_expiration_update_cb,
              CdmCreatedCB cdm_created_cb) final;
};

}  // namespace media_m96

#endif  // THIRD_PARTY_CHROMIUM_MEDIA_CDM_DEFAULT_CDM_FACTORY_H_
