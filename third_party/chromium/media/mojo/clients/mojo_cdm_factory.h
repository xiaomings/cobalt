// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_CHROMIUM_MEDIA_MOJO_CLIENTS_MOJO_CDM_FACTORY_H_
#define THIRD_PARTY_CHROMIUM_MEDIA_MOJO_CLIENTS_MOJO_CDM_FACTORY_H_

#include "base/macros.h"
#include "third_party/chromium/media/base/cdm_factory.h"

namespace media_m96 {

namespace mojom {
class InterfaceFactory;
}

class MojoCdmFactory final : public CdmFactory {
 public:
  explicit MojoCdmFactory(media_m96::mojom::InterfaceFactory* interface_factory);

  MojoCdmFactory(const MojoCdmFactory&) = delete;
  MojoCdmFactory& operator=(const MojoCdmFactory&) = delete;

  ~MojoCdmFactory() final;

  // CdmFactory implementation.
  void Create(const std::string& key_system,
              const CdmConfig& cdm_config,
              const SessionMessageCB& session_message_cb,
              const SessionClosedCB& session_closed_cb,
              const SessionKeysChangeCB& session_keys_change_cb,
              const SessionExpirationUpdateCB& session_expiration_update_cb,
              CdmCreatedCB cdm_created_cb) final;

 private:
  media_m96::mojom::InterfaceFactory* interface_factory_;
};

}  // namespace media_m96

#endif  // THIRD_PARTY_CHROMIUM_MEDIA_MOJO_CLIENTS_MOJO_CDM_FACTORY_H_
