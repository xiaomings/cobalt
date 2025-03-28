// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_LOGIN_USERS_CHROME_USER_MANAGER_H_
#define CHROME_BROWSER_ASH_LOGIN_USERS_CHROME_USER_MANAGER_H_

#include "base/containers/flat_set.h"
#include "base/memory/ref_counted.h"
#include "base/scoped_observation.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ash/login/users/user_manager_interface.h"
#include "chrome/browser/ash/policy/core/device_local_account_policy_service.h"
#include "chrome/browser/ash/policy/core/reporting_user_tracker.h"
#include "chromeos/ash/components/login/login_state/login_state.h"
#include "components/account_id/account_id.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager_base.h"

class PrefRegistrySimple;

namespace policy {
class ReportingUserTrackerTest;
}  // namespace policy

namespace ash {

// Chrome specific interface of the UserManager.
class ChromeUserManager : public user_manager::UserManagerBase,
                          public UserManagerInterface {
 public:
  explicit ChromeUserManager(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  ChromeUserManager(const ChromeUserManager&) = delete;
  ChromeUserManager& operator=(const ChromeUserManager&) = delete;

  ~ChromeUserManager() override;

  // Registers the preferences that this class uses.
  static void RegisterPrefs(PrefRegistrySimple* registry);

  // user_manager::UserManagerBase:
  bool IsCurrentUserNew() const override;
  void UpdateLoginState(const user_manager::User* active_user,
                        const user_manager::User* primary_user,
                        bool is_current_user_owner) const override;
  bool GetPlatformKnownUserId(const std::string& user_email,
                              AccountId* out_account_id) const override;

  // Returns current ChromeUserManager or NULL if instance hasn't been
  // yet initialized.
  static ChromeUserManager* Get();

  // TODO(b/278643115): Consider to move following methods out from
  // ChromeUserManager to a dedicated place.

  // Sets affiliation status for the user identified with `account_id`
  // judging by `user_affiliation_ids` and device affiliation IDs.
  virtual void SetUserAffiliation(
      const AccountId& account_id,
      const base::flat_set<std::string>& user_affiliation_ids) = 0;

  // Return whether the given user should be reported (see
  // policy::DeviceStatusCollector).
  bool ShouldReportUser(const std::string& user_id) const;

  // Checks whether full management disclosure is needed for the public/managed
  // session login screen UI. Full disclosure is needed if the session is
  // managed and any risky extensions or network certificates are forced
  // through the policies.
  virtual bool IsFullManagementDisclosureNeeded(
      policy::DeviceLocalAccountPolicyBroker* broker) const = 0;

 protected:
  // TODO(b/267685577): Move this out to DeviceCloudPolicyManagerAsh.
  friend policy::ReportingUserTrackerTest;
  policy::ReportingUserTracker reporting_user_tracker_;

  base::ScopedObservation<user_manager::UserManager,
                          user_manager::UserManager::Observer>
      reporting_user_tracker_observation_{&reporting_user_tracker_};

 private:
  LoginState::LoggedInUserType GetLoggedInUserType(
      const user_manager::User& active_user,
      bool is_current_user_owner) const;
};

}  // namespace ash

#endif  // CHROME_BROWSER_ASH_LOGIN_USERS_CHROME_USER_MANAGER_H_
