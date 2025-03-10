// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEB_APPLICATIONS_OS_INTEGRATION_SHORTCUT_MENU_HANDLING_SUB_MANAGER_H_
#define CHROME_BROWSER_WEB_APPLICATIONS_OS_INTEGRATION_SHORTCUT_MENU_HANDLING_SUB_MANAGER_H_

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "chrome/browser/web_applications/os_integration/os_integration_sub_manager.h"
#include "chrome/browser/web_applications/proto/web_app_os_integration_state.pb.h"
#include "chrome/browser/web_applications/web_app_icon_manager.h"
#include "chrome/browser/web_applications/web_app_id.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace web_app {

class WebAppRegistrar;

// Used to track when information, like shortcut menu icons, app title and app
// launch url in shortcut menu were last updated at and update them once they
// are changed.
class ShortcutMenuHandlingSubManager : public OsIntegrationSubManager {
 public:
  ShortcutMenuHandlingSubManager(const base::FilePath& profile_path,
                                 WebAppIconManager& icon_manager,
                                 WebAppRegistrar& registrar);
  ~ShortcutMenuHandlingSubManager() override;

  void Configure(const AppId& app_id,
                 proto::WebAppOsIntegrationState& desired_state,
                 base::OnceClosure configure_done) override;
  void Execute(const AppId& app_id,
               const absl::optional<SynchronizeOsOptions>& synchronize_options,
               const proto::WebAppOsIntegrationState& desired_state,
               const proto::WebAppOsIntegrationState& current_state,
               base::OnceClosure execute_complete) override;
  void ForceUnregister(const AppId& app_id,
                       base::OnceClosure callback) override;

 private:
  void StoreShortcutMenuData(const AppId& app_id,
                             proto::ShortcutMenus* shortcut_menus,
                             WebAppIconManager::ShortcutIconDataVector data);
  void StartShortcutsMenuUnregistration(
      const AppId& app_id,
      const proto::WebAppOsIntegrationState& current_state,
      base::OnceClosure registration_callback);
  void ReadIconDataForShortcutsMenu(
      const AppId& app_id,
      const proto::WebAppOsIntegrationState& desired_state,
      base::OnceClosure execute_complete);
  void OnIconDataLoadedRegisterShortcutsMenu(
      const AppId& app_id,
      const proto::WebAppOsIntegrationState& desired_state,
      base::OnceClosure execute_complete,
      ShortcutsMenuIconBitmaps shortcut_menu_icon_bitmaps);

  const base::FilePath profile_path_;
  const raw_ref<WebAppIconManager> icon_manager_;
  const raw_ref<WebAppRegistrar> registrar_;

  base::WeakPtrFactory<ShortcutMenuHandlingSubManager> weak_ptr_factory_{this};
};
}  // namespace web_app

#endif  // CHROME_BROWSER_WEB_APPLICATIONS_OS_INTEGRATION_SHORTCUT_MENU_HANDLING_SUB_MANAGER_H_
