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

#include "cobalt/shell/browser/shell_web_contents_view_delegate.h"

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "cobalt/shell/browser/shell_devtools_frontend.h"
#include "cobalt/shell/common/shell_switches.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/web_contents.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/window.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

namespace content {
namespace {

// Model for the "Debug" menu
class ContextMenuModel : public ui::SimpleMenuModel,
                         public ui::SimpleMenuModel::Delegate {
 public:
  ContextMenuModel(WebContents* web_contents, const ContextMenuParams& params)
      : ui::SimpleMenuModel(this),
        web_contents_(web_contents),
        params_(params) {
    AddItem(COMMAND_OPEN_DEVTOOLS, u"Inspect Element");
  }

  ContextMenuModel(const ContextMenuModel&) = delete;
  ContextMenuModel& operator=(const ContextMenuModel&) = delete;

  ~ContextMenuModel() override {}

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override { return false; }
  bool IsCommandIdEnabled(int command_id) const override { return true; }
  void ExecuteCommand(int command_id, int event_flags) override {
    switch (command_id) {
      case COMMAND_OPEN_DEVTOOLS:
        ShellDevToolsFrontend* devtools_frontend =
            ShellDevToolsFrontend::Show(web_contents_);
        devtools_frontend->Activate();
        devtools_frontend->InspectElementAt(params_.x, params_.y);
        break;
    };
  }

 private:
  enum CommandID { COMMAND_OPEN_DEVTOOLS };

  raw_ptr<WebContents> web_contents_;
  ContextMenuParams params_;
};

}  // namespace

std::unique_ptr<WebContentsViewDelegate> CreateShellWebContentsViewDelegate(
    WebContents* web_contents) {
  return std::make_unique<ShellWebContentsViewDelegate>(web_contents);
}

ShellWebContentsViewDelegate::ShellWebContentsViewDelegate(
    WebContents* web_contents)
    : web_contents_(web_contents) {}

ShellWebContentsViewDelegate::~ShellWebContentsViewDelegate() {}

void ShellWebContentsViewDelegate::ShowContextMenu(
    RenderFrameHost& render_frame_host,
    const ContextMenuParams& params) {
  if (switches::IsRunWebTestsSwitchPresent()) {
    return;
  }

  gfx::Point screen_point(params.x, params.y);

  // Convert from content coordinates to window coordinates.
  // This code copied from chrome_web_contents_view_delegate_views.cc
  aura::Window* web_contents_window = web_contents_->GetNativeView();
  aura::Window* root_window = web_contents_window->GetRootWindow();
  aura::client::ScreenPositionClient* screen_position_client =
      aura::client::GetScreenPositionClient(root_window);
  if (screen_position_client) {
    screen_position_client->ConvertPointToScreen(web_contents_window,
                                                 &screen_point);
  }

  context_menu_model_ =
      std::make_unique<ContextMenuModel>(web_contents_, params);
  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);

  views::Widget* widget = views::Widget::GetWidgetForNativeView(
      web_contents_->GetTopLevelNativeWindow());
  context_menu_runner_->RunMenuAt(
      widget, nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopRight, ui::MENU_SOURCE_NONE);
}

}  // namespace content
