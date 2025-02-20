// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/webui/web_ui_managed_interface.h"

#include <vector>

#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/webui/web_ui_impl.h"
#include "content/public/browser/document_user_data.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_ui.h"

namespace content {

namespace internal {

class WebUIManagedInterfaceStorage
    : public DocumentUserData<WebUIManagedInterfaceStorage> {
 public:
  ~WebUIManagedInterfaceStorage() override = default;

  void AddInterfaceImpl(std::unique_ptr<WebUIManagedInterfaceBase> impl) {
    impls_.push_back(std::move(impl));
  }

 private:
  explicit WebUIManagedInterfaceStorage(RenderFrameHost* render_frame_host)
      : DocumentUserData<WebUIManagedInterfaceStorage>(render_frame_host) {}

  friend DocumentUserData;
  DOCUMENT_USER_DATA_KEY_DECL();

  std::vector<std::unique_ptr<WebUIManagedInterfaceBase>> impls_;
};

DOCUMENT_USER_DATA_KEY_IMPL(WebUIManagedInterfaceStorage);

void SaveWebUIManagedInterfaceInDocument(
    WebUIController* controller,
    std::unique_ptr<WebUIManagedInterfaceBase> data) {
  auto* storage = WebUIManagedInterfaceStorage::GetOrCreateForCurrentDocument(
      controller->web_ui()->GetRenderFrameHost());
  storage->AddInterfaceImpl(std::move(data));
}

}  // namespace internal

void RemoveWebUIManagedInterfaces(WebUIController* webui_controller) {
  RenderFrameHost* rfh = webui_controller->web_ui()->GetRenderFrameHost();
  if (rfh &&
      internal::WebUIManagedInterfaceStorage::GetForCurrentDocument(rfh)) {
    internal::WebUIManagedInterfaceStorage::DeleteForCurrentDocument(rfh);
  }
}

}  // namespace content
