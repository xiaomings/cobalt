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

#include <stdint.h>

#include <memory>
#include <variant>

#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_timeouts.h"
#include "base/threading/thread_restrictions.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "cobalt/shell/browser/shell.h"
#include "cobalt/testing/browser_tests/content_browser_test.h"
#include "cobalt/testing/browser_tests/content_browser_test_content_browser_client.h"
#include "cobalt/testing/browser_tests/content_browser_test_utils.h"
#include "cobalt/testing/browser_tests/content_browser_test_utils_internal.h"
#include "components/ukm/test_ukm_recorder.h"
#include "content/browser/browser_url_handler_impl.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/renderer_host/navigation_request.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/common/content_navigation_policy.h"
#include "content/common/features.h"
#include "content/common/frame_messages.mojom.h"
#include "content/common/navigation_client.mojom-forward.h"
#include "content/common/navigation_client.mojom.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_message_filter.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/browser_url_handler.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/network_service_util.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/back_forward_cache_util.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_mock_cert_verifier.h"
#include "content/public/test/hit_test_region_observer.h"
#include "content/public/test/navigation_handle_observer.h"
#include "content/public/test/no_renderer_crashes_assertion.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_navigation_throttle.h"
#include "content/public/test/test_navigation_throttle_inserter.h"
#include "content/public/test/test_utils.h"
#include "content/public/test/url_loader_monitor.h"
#include "content/test/did_commit_navigation_interceptor.h"
#include "content/test/fake_network_url_loader_factory.h"
#include "content/test/task_runner_deferring_throttle.h"
#include "content/test/test_render_frame_host_factory.h"
#include "ipc/ipc_security_test_util.h"
#include "mojo/public/cpp/bindings/pending_associated_remote.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "net/base/features.h"
#include "net/base/load_flags.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/controllable_http_response.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/test/url_request/url_request_failed_job.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/cpp/web_sandbox_flags.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"
#include "third_party/blink/public/mojom/frame/sudden_termination_disabler_type.mojom-shared.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"
#include "url/url_util.h"

namespace content {

namespace {

class InterceptAndCancelDidCommitProvisionalLoad
    : public DidCommitNavigationInterceptor {
 public:
  explicit InterceptAndCancelDidCommitProvisionalLoad(WebContents* web_contents)
      : DidCommitNavigationInterceptor(web_contents) {}
  ~InterceptAndCancelDidCommitProvisionalLoad() override {}

  void Wait(size_t number_of_messages) {
    while (intercepted_messages_.size() < number_of_messages) {
      loop_ = std::make_unique<base::RunLoop>();
      loop_->Run();
    }
  }

  const std::vector<NavigationRequest*>& intercepted_navigations() const {
    return intercepted_navigations_;
  }

  const std::vector<mojom::DidCommitProvisionalLoadParamsPtr>&
  intercepted_messages() const {
    return intercepted_messages_;
  }

 protected:
  bool WillProcessDidCommitNavigation(
      RenderFrameHost* render_frame_host,
      NavigationRequest* navigation_request,
      mojom::DidCommitProvisionalLoadParamsPtr* params,
      mojom::DidCommitProvisionalLoadInterfaceParamsPtr* interface_params)
      override {
    intercepted_navigations_.push_back(navigation_request);
    intercepted_messages_.push_back(std::move(*params));
    if (loop_) {
      loop_->Quit();
    }
    // Do not send the message to the RenderFrameHostImpl.
    return false;
  }

  // Note: Do not dereference the intercepted_navigations_, they are used as
  // indices in the RenderFrameHostImpl and not for themselves.
  std::vector<NavigationRequest*> intercepted_navigations_;
  std::vector<mojom::DidCommitProvisionalLoadParamsPtr> intercepted_messages_;
  std::unique_ptr<base::RunLoop> loop_;
};

class RenderFrameHostImplForHistoryBackInterceptor
    : public RenderFrameHostImpl {
 public:
  using RenderFrameHostImpl::RenderFrameHostImpl;

  void GoToEntryAtOffset(int32_t offset,
                         bool has_user_gesture,
                         absl::optional<blink::scheduler::TaskAttributionId>
                             soft_navigation_heuristics_task_id) override {
    if (quit_handler_) {
      std::move(quit_handler_).Run();
    }
  }

  void set_quit_handler(base::OnceClosure handler) {
    quit_handler_ = std::move(handler);
  }

 private:
  friend class RenderFrameHostFactoryForHistoryBackInterceptor;
  base::OnceClosure quit_handler_;
};

class RenderFrameHostFactoryForHistoryBackInterceptor
    : public TestRenderFrameHostFactory {
 protected:
  std::unique_ptr<RenderFrameHostImpl> CreateRenderFrameHost(
      SiteInstance* site_instance,
      scoped_refptr<RenderViewHostImpl> render_view_host,
      RenderFrameHostDelegate* delegate,
      FrameTree* frame_tree,
      FrameTreeNode* frame_tree_node,
      int32_t routing_id,
      mojo::PendingAssociatedRemote<mojom::Frame> frame_remote,
      const blink::LocalFrameToken& frame_token,
      const blink::DocumentToken& document_token,
      base::UnguessableToken devtools_frame_token,
      bool renderer_initiated_creation,
      RenderFrameHostImpl::LifecycleStateImpl lifecycle_state,
      scoped_refptr<BrowsingContextState> browsing_context_state) override {
    return base::WrapUnique(new RenderFrameHostImplForHistoryBackInterceptor(
        site_instance, std::move(render_view_host), delegate, frame_tree,
        frame_tree_node, routing_id, std::move(frame_remote), frame_token,
        document_token, devtools_frame_token, renderer_initiated_creation,
        lifecycle_state, std::move(browsing_context_state),
        frame_tree_node->frame_owner_element_type(), frame_tree_node->parent(),
        frame_tree_node->fenced_frame_status()));
  }
};

// Simulate embedders of content/ keeping track of the current visible URL using
// NavigateStateChanged() and GetVisibleURL() API.
class EmbedderVisibleUrlTracker : public WebContentsDelegate {
 public:
  const GURL& url() { return url_; }

  // WebContentsDelegate's implementation:
  void NavigationStateChanged(WebContents* source,
                              InvalidateTypes changed_flags) override {
    if (!(changed_flags & INVALIDATE_TYPE_URL)) {
      return;
    }
    url_ = source->GetVisibleURL();
    if (on_url_invalidated_) {
      std::move(on_url_invalidated_).Run();
    }
  }

  void WaitUntilUrlInvalidated() {
    base::RunLoop loop;
    on_url_invalidated_ = loop.QuitClosure();
    loop.Run();
  }

 private:
  GURL url_;
  base::OnceClosure on_url_invalidated_;
};

// Helper class. Immediately run a callback when a navigation starts.
class DidStartNavigationCallback final : public WebContentsObserver {
 public:
  explicit DidStartNavigationCallback(
      WebContents* web_contents,
      base::OnceCallback<void(NavigationHandle*)> callback)
      : WebContentsObserver(web_contents), callback_(std::move(callback)) {}
  ~DidStartNavigationCallback() override = default;

 private:
  void DidStartNavigation(NavigationHandle* navigation_handle) override {
    if (callback_) {
      std::move(callback_).Run(navigation_handle);
    }
  }
  base::OnceCallback<void(NavigationHandle*)> callback_;
};

// Helper class. Immediately run a callback when a navigation finishes.
class DidFinishNavigationCallback final : public WebContentsObserver {
 public:
  explicit DidFinishNavigationCallback(
      WebContents* web_contents,
      base::OnceCallback<void(NavigationHandle*)> callback)
      : WebContentsObserver(web_contents), callback_(std::move(callback)) {}
  ~DidFinishNavigationCallback() override = default;

 private:
  void DidFinishNavigation(NavigationHandle* navigation_handle) override {
    if (callback_) {
      std::move(callback_).Run(navigation_handle);
    }
  }
  base::OnceCallback<void(NavigationHandle*)> callback_;
};

const char* non_cacheable_html_response =
    "HTTP/1.1 200 OK\n"
    "cache-control: no-cache, no-store, must-revalidate\n"
    "content-type: text/html; charset=UTF-8\n"
    "\n"
    "HTML content.";

// Insert a navigation throttle blocking every navigation in its
// WillProcessResponse handler.
std::unique_ptr<content::TestNavigationThrottleInserter>
BlockNavigationWillProcessResponse(WebContentsImpl* web_content) {
  return std::make_unique<content::TestNavigationThrottleInserter>(
      web_content,
      base::BindLambdaForTesting(
          [&](NavigationHandle* handle) -> std::unique_ptr<NavigationThrottle> {
            auto throttle = std::make_unique<TestNavigationThrottle>(handle);
            throttle->SetResponse(TestNavigationThrottle::WILL_PROCESS_RESPONSE,
                                  TestNavigationThrottle::SYNCHRONOUS,
                                  NavigationThrottle::BLOCK_RESPONSE);

            return throttle;
          }));
}

}  // namespace

// Test about navigation.
// If you don't need a custom embedded test server, please use the next class
// below (NavigationBrowserTest), it will automatically start the
// default server.
class NavigationBaseBrowserTest : public ContentBrowserTest {
 public:
  NavigationBaseBrowserTest() {}

  void PreRunTestOnMainThread() override {
    ContentBrowserTest::PreRunTestOnMainThread();
    test_ukm_recorder_ = std::make_unique<ukm::TestAutoSetUkmRecorder>();
  }

  // Currently, Cobalt runs in single-process as a way to save memory during
  // runtime. However, comments in //content/public/test/content_browser_test.h
  // strongly discourage the use of single-process. For testing purposes, it
  // should be okay to parallelize these tests as cobalt does not require
  // running in single-process.
  void SetUpCommandLine(base::CommandLine* command_line) {
    command_line->RemoveSwitch("single-process");
  }

  const ukm::TestAutoSetUkmRecorder& test_ukm_recorder() const {
    return *test_ukm_recorder_;
  }

 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
  }

  WebContentsImpl* web_contents() const {
    return static_cast<WebContentsImpl*>(shell()->web_contents());
  }

  FrameTreeNode* main_frame() {
    return web_contents()->GetPrimaryFrameTree().root();
  }

  RenderFrameHostImpl* current_frame_host() {
    return main_frame()->current_frame_host();
  }

 private:
  std::unique_ptr<ukm::TestAutoSetUkmRecorder> test_ukm_recorder_;
};

class NavigationBrowserTest : public NavigationBaseBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    NavigationBaseBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }
};

class NavigationGoToEntryAtOffsetBrowserTest : public NavigationBrowserTest {
 public:
  void SetQuitHandlerForGoToEntryAtOffset(base::OnceClosure handler) {
    RenderFrameHostImplForHistoryBackInterceptor* render_frame_host =
        static_cast<RenderFrameHostImplForHistoryBackInterceptor*>(
            current_frame_host());
    render_frame_host->set_quit_handler(std::move(handler));
  }

 private:
  RenderFrameHostFactoryForHistoryBackInterceptor render_frame_host_factory_;
};

class NetworkIsolationNavigationBrowserTest : public ContentBrowserTest {
 public:
  NetworkIsolationNavigationBrowserTest() = default;

 protected:
  void SetUpOnMainThread() override {
    ASSERT_TRUE(embedded_test_server()->Start());
    ContentBrowserTest::SetUpOnMainThread();
  }
};

class NavigationBrowserTestReferrerPolicy
    : public NavigationBrowserTest,
      public ::testing::WithParamInterface<network::mojom::ReferrerPolicy> {
 protected:
  network::mojom::ReferrerPolicy GetReferrerPolicy() const {
    return GetParam();
  }
};

INSTANTIATE_TEST_SUITE_P(
    All,
    NavigationBrowserTestReferrerPolicy,
    ::testing::Values(
        network::mojom::ReferrerPolicy::kAlways,
        network::mojom::ReferrerPolicy::kDefault,
        network::mojom::ReferrerPolicy::kNoReferrerWhenDowngrade,
        network::mojom::ReferrerPolicy::kNever,
        network::mojom::ReferrerPolicy::kOrigin,
        network::mojom::ReferrerPolicy::kOriginWhenCrossOrigin,
        network::mojom::ReferrerPolicy::kStrictOriginWhenCrossOrigin,
        network::mojom::ReferrerPolicy::kSameOrigin,
        network::mojom::ReferrerPolicy::kStrictOrigin));

// Ensure that browser initiated basic navigations work.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_BrowserInitiatedNavigations) {
  // Perform a navigation with no live renderer.
  {
    TestNavigationObserver observer(web_contents());
    GURL url(embedded_test_server()->GetURL("/title1.html"));
    EXPECT_TRUE(NavigateToURL(shell(), url));
    EXPECT_EQ(url, observer.last_navigation_url());
    EXPECT_TRUE(observer.last_navigation_succeeded());
    EXPECT_FALSE(observer.last_initiator_origin().has_value());
    EXPECT_FALSE(observer.last_initiator_frame_token().has_value());
    EXPECT_EQ(ChildProcessHost::kInvalidUniqueID,
              observer.last_initiator_process_id());
  }

  RenderFrameHost* initial_rfh = current_frame_host();

  // Perform a same site navigation.
  {
    TestNavigationObserver observer(web_contents());
    GURL url(embedded_test_server()->GetURL("/title2.html"));
    EXPECT_TRUE(NavigateToURL(shell(), url));
    EXPECT_EQ(url, observer.last_navigation_url());
    EXPECT_TRUE(observer.last_navigation_succeeded());
    EXPECT_FALSE(observer.last_initiator_origin().has_value());
    EXPECT_FALSE(observer.last_initiator_frame_token().has_value());
    EXPECT_EQ(ChildProcessHost::kInvalidUniqueID,
              observer.last_initiator_process_id());
  }

  RenderFrameHost* second_rfh = current_frame_host();

  if (CanSameSiteMainFrameNavigationsChangeRenderFrameHosts()) {
    // If same-site ProactivelySwapBrowsingInstance or main-frame RenderDocument
    // is enabled, the navigation will result in a new RFH.
    EXPECT_NE(initial_rfh, second_rfh);
  } else {
    EXPECT_EQ(initial_rfh, second_rfh);
  }

  // Perform a cross-site navigation.
  {
    TestNavigationObserver observer(web_contents());
    GURL url = embedded_test_server()->GetURL("foo.com", "/title3.html");
    EXPECT_TRUE(NavigateToURL(shell(), url));
    EXPECT_EQ(url, observer.last_navigation_url());
    EXPECT_TRUE(observer.last_navigation_succeeded());
    EXPECT_FALSE(observer.last_initiator_origin().has_value());
    EXPECT_FALSE(observer.last_initiator_frame_token().has_value());
    EXPECT_EQ(ChildProcessHost::kInvalidUniqueID,
              observer.last_initiator_process_id());
  }

  // The RenderFrameHost should have changed.
  EXPECT_NE(second_rfh, current_frame_host());

  // Check the UKM for navigation responses received.
  EXPECT_EQ(3u, test_ukm_recorder()
                    .GetEntriesByName("Navigation.ReceivedResponse")
                    .size());
}

// Ensure that renderer initiated same-site navigations work.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_RendererInitiatedSameSiteNavigation) {
  // Perform a navigation with no live renderer.
  {
    TestNavigationObserver observer(web_contents());
    GURL url(embedded_test_server()->GetURL("/simple_links.html"));
    EXPECT_TRUE(NavigateToURL(shell(), url));
    EXPECT_EQ(url, observer.last_navigation_url());
    EXPECT_TRUE(observer.last_navigation_succeeded());
    EXPECT_FALSE(observer.last_initiator_origin().has_value());
    EXPECT_FALSE(observer.last_initiator_frame_token().has_value());
    EXPECT_EQ(ChildProcessHost::kInvalidUniqueID,
              observer.last_initiator_process_id());
  }

  RenderFrameHost* initial_rfh = current_frame_host();

  blink::LocalFrameToken initial_rfh_frame_token = initial_rfh->GetFrameToken();
  int initial_rfh_process_id = initial_rfh->GetProcess()->GetID();

  // Simulate clicking on a same-site link.
  {
    TestNavigationObserver observer(web_contents());
    GURL url(embedded_test_server()->GetURL("/title2.html"));
    EXPECT_EQ(true, EvalJs(shell(), "clickSameSiteLink();"));
    EXPECT_TRUE(WaitForLoadStop(web_contents()));
    EXPECT_EQ(url, observer.last_navigation_url());
    EXPECT_TRUE(observer.last_navigation_succeeded());

    EXPECT_EQ(current_frame_host()->GetLastCommittedOrigin(),
              observer.last_initiator_origin());

    EXPECT_TRUE(observer.last_initiator_frame_token().has_value());
    if (CanSameSiteMainFrameNavigationsChangeRenderFrameHosts()) {
      // If same-site ProactivelySwapBrowsingInstance or main-frame
      // RenderDocument is enabled, the navigation will result in a new RFH, so
      // we need to compare with |initial_rfh|.
      EXPECT_NE(current_frame_host(), initial_rfh);
      EXPECT_EQ(initial_rfh_frame_token,
                observer.last_initiator_frame_token().value());
      EXPECT_EQ(initial_rfh_process_id, observer.last_initiator_process_id());
    } else {
      EXPECT_EQ(current_frame_host(), initial_rfh);
      EXPECT_EQ(current_frame_host()->GetFrameToken(),
                observer.last_initiator_frame_token().value());
      EXPECT_EQ(current_frame_host()->GetProcess()->GetID(),
                observer.last_initiator_process_id());
    }
  }

  RenderFrameHost* second_rfh = current_frame_host();

  if (CanSameSiteMainFrameNavigationsChangeRenderFrameHosts()) {
    // If same-site ProactivelySwapBrowsingInstance or main-frame RenderDocument
    // is enabled, the navigation will result in a new RFH.
    EXPECT_NE(initial_rfh, second_rfh);
  } else {
    EXPECT_EQ(initial_rfh, second_rfh);
  }
}

// Ensure that renderer initiated cross-site navigations work.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_RendererInitiatedCrossSiteNavigation) {
  // Perform a navigation with no live renderer.
  {
    TestNavigationObserver observer(web_contents());
    GURL url(embedded_test_server()->GetURL("/simple_links.html"));
    EXPECT_TRUE(NavigateToURL(shell(), url));
    EXPECT_EQ(url, observer.last_navigation_url());
    EXPECT_TRUE(observer.last_navigation_succeeded());
  }

  RenderFrameHost* initial_rfh = current_frame_host();
  url::Origin initial_origin = initial_rfh->GetLastCommittedOrigin();
  blink::LocalFrameToken initiator_frame_token = initial_rfh->GetFrameToken();
  int initiator_process_id = initial_rfh->GetProcess()->GetID();

  // Simulate clicking on a cross-site link.
  {
    TestNavigationObserver observer(web_contents());
    const char kReplacePortNumber[] = "setPortNumber(%d);";
    uint16_t port_number = embedded_test_server()->port();
    GURL url = embedded_test_server()->GetURL("foo.com", "/title2.html");
    EXPECT_EQ(true, EvalJs(shell(), base::StringPrintf(kReplacePortNumber,
                                                       port_number)));
    EXPECT_EQ(true, EvalJs(shell(), "clickCrossSiteLink();"));
    EXPECT_TRUE(WaitForLoadStop(web_contents()));
    EXPECT_EQ(url, observer.last_navigation_url());
    EXPECT_TRUE(observer.last_navigation_succeeded());
    EXPECT_EQ(initial_origin, observer.last_initiator_origin().value());
    EXPECT_TRUE(observer.last_initiator_frame_token().has_value());
    EXPECT_EQ(initiator_frame_token,
              observer.last_initiator_frame_token().value());
    EXPECT_EQ(initiator_process_id, observer.last_initiator_process_id());
  }

  // The RenderFrameHost should have changed unless default SiteInstances
  // are enabled and proactive BrowsingInstance swaps are disabled.
  if (AreDefaultSiteInstancesEnabled() &&
      !CanCrossSiteNavigationsProactivelySwapBrowsingInstances()) {
    EXPECT_EQ(initial_rfh, current_frame_host());
  } else {
    EXPECT_NE(initial_rfh, current_frame_host());
  }
}

// Ensure navigation failures are handled.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest, DISABLED_FailedNavigation) {
  // Perform a navigation with no live renderer.
  {
    TestNavigationObserver observer(web_contents());
    GURL url(embedded_test_server()->GetURL("/title1.html"));
    EXPECT_TRUE(NavigateToURL(shell(), url));
    EXPECT_EQ(url, observer.last_navigation_url());
    EXPECT_TRUE(observer.last_navigation_succeeded());
    // Check the UKM for navigation responses received.
    EXPECT_EQ(1u, test_ukm_recorder()
                      .GetEntriesByName("Navigation.ReceivedResponse")
                      .size());
  }

  // Now navigate to an unreachable url.
  {
    TestNavigationObserver observer(web_contents());
    GURL error_url(embedded_test_server()->GetURL("/close-socket"));
    GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&net::URLRequestFailedJob::AddUrlHandler));
    EXPECT_FALSE(NavigateToURL(shell(), error_url));
    EXPECT_EQ(error_url, observer.last_navigation_url());
    NavigationEntry* entry =
        web_contents()->GetController().GetLastCommittedEntry();
    EXPECT_EQ(PAGE_TYPE_ERROR, entry->GetPageType());
    // No response on an unreachable URL, so the ReceivedResponse event should
    // not have increased.
    EXPECT_EQ(1u, test_ukm_recorder()
                      .GetEntriesByName("Navigation.ReceivedResponse")
                      .size());
  }
}

// Ensure that browser initiated navigations to view-source URLs works.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_ViewSourceNavigation_BrowserInitiated) {
  TestNavigationObserver observer(web_contents());
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  GURL view_source_url(content::kViewSourceScheme + std::string(":") +
                       url.spec());
  EXPECT_TRUE(NavigateToURL(shell(), view_source_url));
  EXPECT_EQ(url, observer.last_navigation_url());
  EXPECT_TRUE(observer.last_navigation_succeeded());
}

// Ensure that content initiated navigations to view-sources URLs are blocked.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_ViewSourceNavigation_RendererInitiated) {
  TestNavigationObserver observer(web_contents());
  GURL kUrl(embedded_test_server()->GetURL("/simple_links.html"));
  EXPECT_TRUE(NavigateToURL(shell(), kUrl));
  EXPECT_EQ(kUrl, observer.last_navigation_url());
  EXPECT_TRUE(observer.last_navigation_succeeded());

  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern(
      "Not allowed to load local resource: view-source:about:blank");

  EXPECT_EQ(true, EvalJs(web_contents(), "clickViewSourceLink();"));
  ASSERT_TRUE(console_observer.Wait());
  // Original page shouldn't navigate away.
  EXPECT_EQ(kUrl, web_contents()->GetLastCommittedURL());
  EXPECT_FALSE(shell()
                   ->web_contents()
                   ->GetController()
                   .GetLastCommittedEntry()
                   ->IsViewSourceMode());
}

// Ensure that content initiated navigations to googlechrome: URLs are blocked.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_GoogleChromeNavigation_RendererInitiated) {
  TestNavigationObserver observer(web_contents());
  GURL kUrl(embedded_test_server()->GetURL("/simple_links.html"));
  EXPECT_TRUE(NavigateToURL(shell(), kUrl));
  EXPECT_EQ(kUrl, observer.last_navigation_url());
  EXPECT_TRUE(observer.last_navigation_succeeded());

  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern(
      "Not allowed to load local resource: googlechrome://");

  EXPECT_EQ(true, EvalJs(web_contents(), "clickGoogleChromeLink();"));
  ASSERT_TRUE(console_observer.Wait());
  // Original page shouldn't navigate away.
  EXPECT_EQ(kUrl, web_contents()->GetLastCommittedURL());
}

// Ensure that closing a page by running its beforeunload handler doesn't hang
// if there's an ongoing navigation.
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest, UnloadDuringNavigation) {
  WebContentsDestroyedWatcher close_observer(web_contents());
  GURL url("chrome://resources/css/tabs.css");
  NavigationHandleObserver handle_observer(web_contents(), url);
  shell()->LoadURL(url);
  web_contents()->DispatchBeforeUnload(false /* auto_cancel */);
  close_observer.Wait();
  EXPECT_EQ(net::ERR_ABORTED, handle_observer.net_error_code());
}

// Ensure that the referrer of a navigation is properly sanitized.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest, DISABLED_SanitizeReferrer) {
  const GURL kInsecureUrl(embedded_test_server()->GetURL("/title1.html"));
  const Referrer kSecureReferrer(
      GURL("https://secure-url.com"),
      network::mojom::ReferrerPolicy::kNoReferrerWhenDowngrade);

  // Navigate to an insecure url with a secure referrer with a policy of no
  // referrer on downgrades. The referrer url should be rewritten right away.
  NavigationController::LoadURLParams load_params(kInsecureUrl);
  load_params.referrer = kSecureReferrer;
  TestNavigationManager manager(web_contents(), kInsecureUrl);
  web_contents()->GetController().LoadURLWithParams(load_params);
  EXPECT_TRUE(manager.WaitForRequestStart());

  // The referrer should have been sanitized.
  ASSERT_TRUE(main_frame()->navigation_request());
  EXPECT_EQ(GURL(), main_frame()->navigation_request()->GetReferrer().url);

  // The navigation should commit without being blocked.
  EXPECT_TRUE(manager.WaitForResponse());
  ASSERT_TRUE(manager.WaitForNavigationFinished());
  EXPECT_EQ(kInsecureUrl, web_contents()->GetLastCommittedURL());
}

// Ensure the correctness of a navigation request's referrer. This is a
// regression test for https://crbug.com/1004083.
// TODO: b/432530025 - Investigate test failure
IN_PROC_BROWSER_TEST_P(NavigationBrowserTestReferrerPolicy,
                       DISABLED_ReferrerPolicy) {
  const GURL kDestination(embedded_test_server()->GetURL("/title1.html"));
  const GURL kReferrerURL(embedded_test_server()->GetURL("/referrer-page"));
  const url::Origin kReferrerOrigin = url::Origin::Create(kReferrerURL);

  // It is possible that the referrer URL does not match what the policy
  // demands (e.g., non-empty URL and kNever policy), so we'll test that the
  // correct referrer is generated, and that the navigation succeeds.
  const Referrer referrer(kReferrerURL, GetReferrerPolicy());

  // Navigate to a resource whose destination URL is same-origin with the
  // navigation's referrer. The final referrer should be generated correctly.
  NavigationController::LoadURLParams load_params(kDestination);
  load_params.referrer = referrer;
  TestNavigationManager manager(web_contents(), kDestination);
  web_contents()->GetController().LoadURLWithParams(load_params);
  EXPECT_TRUE(manager.WaitForRequestStart());

  // The referrer should have been sanitized.
  ASSERT_TRUE(main_frame()->navigation_request());
  switch (GetReferrerPolicy()) {
    case network::mojom::ReferrerPolicy::kAlways:
    case network::mojom::ReferrerPolicy::kDefault:
    case network::mojom::ReferrerPolicy::kNoReferrerWhenDowngrade:
    case network::mojom::ReferrerPolicy::kOriginWhenCrossOrigin:
    case network::mojom::ReferrerPolicy::kStrictOriginWhenCrossOrigin:
    case network::mojom::ReferrerPolicy::kSameOrigin:
      EXPECT_EQ(kReferrerURL,
                main_frame()->navigation_request()->GetReferrer().url);
      break;
    case network::mojom::ReferrerPolicy::kNever:
      EXPECT_EQ(GURL(), main_frame()->navigation_request()->GetReferrer().url);
      break;
    case network::mojom::ReferrerPolicy::kOrigin:
    case network::mojom::ReferrerPolicy::kStrictOrigin:
      EXPECT_EQ(kReferrerOrigin.GetURL(),
                main_frame()->navigation_request()->GetReferrer().url);
      break;
  }

  // The navigation should commit without being blocked.
  EXPECT_TRUE(manager.WaitForResponse());
  ASSERT_TRUE(manager.WaitForNavigationFinished());
  EXPECT_EQ(kDestination, web_contents()->GetLastCommittedURL());
}

// Test to verify that an exploited renderer process trying to upload a file
// it hasn't been explicitly granted permissions to is correctly terminated.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_PostUploadIllegalFilePath) {
  GURL form_url(
      embedded_test_server()->GetURL("/form_that_posts_to_echoall.html"));
  EXPECT_TRUE(NavigateToURL(shell(), form_url));

  // Prepare a file for the upload form.
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir temp_dir;
  base::FilePath file_path;
  std::string file_content("test-file-content");
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::CreateTemporaryFileInDir(temp_dir.GetPath(), &file_path));
  ASSERT_TRUE(base::WriteFile(file_path, file_content));

  base::RunLoop run_loop;
  // Fill out the form to refer to the test file.
  std::unique_ptr<FileChooserDelegate> delegate(
      new FileChooserDelegate(file_path, run_loop.QuitClosure()));
  web_contents()->SetDelegate(delegate.get());
  EXPECT_TRUE(
      ExecJs(web_contents(), "document.getElementById('file').click();"));
  run_loop.Run();

  // Ensure that the process is allowed to access to the chosen file and
  // does not have access to the other file name.
  EXPECT_TRUE(ChildProcessSecurityPolicyImpl::GetInstance()->CanReadFile(
      current_frame_host()->GetProcess()->GetID(), file_path));

  // Revoke the access to the file and submit the form. The renderer process
  // should be terminated.
  RenderProcessHostBadIpcMessageWaiter process_kill_waiter(
      current_frame_host()->GetProcess());
  ChildProcessSecurityPolicyImpl* security_policy =
      ChildProcessSecurityPolicyImpl::GetInstance();
  security_policy->RevokeAllPermissionsForFile(
      current_frame_host()->GetProcess()->GetID(), file_path);

  // Use EvalJs and respond back to the browser process before doing the actual
  // submission. This will ensure that the process termination is guaranteed to
  // arrive after the response from the executed JavaScript.
  EXPECT_EQ(
      true,
      EvalJs(
          shell(),
          "setTimeout(() => document.getElementById('file-form').submit(), 0);"
          "true;"));
  EXPECT_EQ(bad_message::ILLEGAL_UPLOAD_PARAMS, process_kill_waiter.Wait());
}

// Test case to verify that redirects to data: URLs are properly disallowed,
// even when invoked through a reload.
// See https://crbug.com/723796.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_VerifyBlockedErrorPageURL_Reload) {
  NavigationControllerImpl& controller = web_contents()->GetController();

  GURL start_url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), start_url));
  EXPECT_EQ(0, controller.GetLastCommittedEntryIndex());

  // Navigate to an URL, which redirects to a data: URL, since it is an
  // unsafe redirect and will result in a blocked navigation and error page.
  GURL redirect_to_blank_url(
      embedded_test_server()->GetURL("/server-redirect?data:text/html,Hello!"));
  EXPECT_FALSE(NavigateToURL(shell(), redirect_to_blank_url));
  EXPECT_EQ(1, controller.GetLastCommittedEntryIndex());
  EXPECT_EQ(PAGE_TYPE_ERROR, controller.GetLastCommittedEntry()->GetPageType());

  TestNavigationObserver reload_observer(web_contents());
  EXPECT_TRUE(ExecJs(shell(), "location.reload()"));
  reload_observer.Wait();

  // The expectation is that the blocked URL is present in the NavigationEntry,
  // and shows up in both GetURL and GetVirtualURL.
  EXPECT_EQ(1, controller.GetLastCommittedEntryIndex());
  EXPECT_FALSE(
      controller.GetLastCommittedEntry()->GetURL().SchemeIs(url::kDataScheme));
  EXPECT_EQ(redirect_to_blank_url,
            controller.GetLastCommittedEntry()->GetURL());
  EXPECT_EQ(redirect_to_blank_url,
            controller.GetLastCommittedEntry()->GetVirtualURL());
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest, DISABLED_BackFollowedByReload) {
  // First, make two history entries.
  GURL url1(embedded_test_server()->GetURL("/title1.html"));
  GURL url2(embedded_test_server()->GetURL("/title2.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url1));
  EXPECT_TRUE(NavigateToURL(shell(), url2));

  // Then execute a back navigation in Javascript followed by a reload.
  TestNavigationObserver navigation_observer(web_contents());
  EXPECT_TRUE(ExecJs(web_contents(), "history.back(); location.reload();"));
  navigation_observer.Wait();

  // The reload should have cancelled the back navigation, and the last
  // committed URL should still be the second URL.
  EXPECT_EQ(url2, web_contents()->GetLastCommittedURL());
}

// Test that a navigation response can be entirely fetched, even after the
// NavigationURLLoader has been deleted.
IN_PROC_BROWSER_TEST_F(NavigationBaseBrowserTest,
                       FetchResponseAfterNavigationURLLoaderDeleted) {
  net::test_server::ControllableHttpResponse response(embedded_test_server(),
                                                      "/main_document");
  ASSERT_TRUE(embedded_test_server()->Start());

  // Load a new document.
  GURL url(embedded_test_server()->GetURL("/main_document"));
  TestNavigationManager navigation_manager(web_contents(), url);
  shell()->LoadURL(url);

  // The navigation starts.
  EXPECT_TRUE(navigation_manager.WaitForRequestStart());
  navigation_manager.ResumeNavigation();

  // A NavigationRequest exists at this point.
  EXPECT_TRUE(main_frame()->navigation_request());

  // The response's headers are received.
  response.WaitForRequest();
  response.Send(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "\r\n"
      "...");
  EXPECT_TRUE(navigation_manager.WaitForResponse());
  navigation_manager.ResumeNavigation();

  // The renderer commits the navigation and the browser deletes its
  // NavigationRequest.
  ASSERT_TRUE(navigation_manager.WaitForNavigationFinished());
  EXPECT_FALSE(main_frame()->navigation_request());

  // The NavigationURLLoader has been deleted by now. Check that the renderer
  // can still receive more bytes.
  DOMMessageQueue dom_message_queue(web_contents());
  response.Send(
      "<script>window.domAutomationController.send('done');</script>");
  std::string done;
  EXPECT_TRUE(dom_message_queue.WaitForMessage(&done));
  EXPECT_EQ("\"done\"", done);
}

// TODO: b/432529457 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NetworkIsolationNavigationBrowserTest,
                       DISABLED_BrowserNavigationNetworkIsolationKey) {
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  url::Origin origin = url::Origin::Create(url);
  URLLoaderMonitor monitor({url});
  EXPECT_TRUE(NavigateToURL(shell(), url));
  monitor.WaitForUrls();

  absl::optional<network::ResourceRequest> request =
      monitor.GetRequestInfo(url);
  ASSERT_TRUE(request->trusted_params);
  EXPECT_TRUE(net::IsolationInfo::Create(
                  net::IsolationInfo::RequestType::kMainFrame, origin, origin,
                  net::SiteForCookies::FromOrigin(origin),
                  std::set<net::SchemefulSite>())
                  .IsEqualForTesting(request->trusted_params->isolation_info));
}

// TODO: b/432529457 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NetworkIsolationNavigationBrowserTest,
                       DISABLED_RenderNavigationIsolationInfo) {
  GURL url(embedded_test_server()->GetURL("/title2.html"));
  url::Origin origin = url::Origin::Create(url);
  EXPECT_TRUE(NavigateToURL(shell(), GURL("about:blank")));
  URLLoaderMonitor monitor({url});
  EXPECT_TRUE(NavigateToURLFromRenderer(shell(), url));
  monitor.WaitForUrls();

  absl::optional<network::ResourceRequest> request =
      monitor.GetRequestInfo(url);
  ASSERT_TRUE(request->trusted_params);
  EXPECT_TRUE(net::IsolationInfo::Create(
                  net::IsolationInfo::RequestType::kMainFrame, origin, origin,
                  net::SiteForCookies::FromOrigin(origin),
                  std::set<net::SchemefulSite>())
                  .IsEqualForTesting(request->trusted_params->isolation_info));
}

// TODO: b/432529457 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NetworkIsolationNavigationBrowserTest,
                       DISABLED_SubframeIsolationInfo) {
  GURL url(embedded_test_server()->GetURL("/page_with_iframe.html"));
  GURL iframe_document = embedded_test_server()->GetURL("/title1.html");
  url::Origin origin = url::Origin::Create(url);
  url::Origin iframe_origin = url::Origin::Create(iframe_document);
  URLLoaderMonitor monitor({iframe_document});
  EXPECT_TRUE(NavigateToURL(shell(), url));
  monitor.WaitForUrls();

  absl::optional<network::ResourceRequest> main_frame_request =
      monitor.GetRequestInfo(url);
  ASSERT_TRUE(main_frame_request.has_value());
  ASSERT_TRUE(main_frame_request->trusted_params);
  EXPECT_TRUE(net::IsolationInfo::Create(
                  net::IsolationInfo::RequestType::kMainFrame, origin, origin,
                  net::SiteForCookies::FromOrigin(origin),
                  std::set<net::SchemefulSite>())
                  .IsEqualForTesting(
                      main_frame_request->trusted_params->isolation_info));

  absl::optional<network::ResourceRequest> iframe_request =
      monitor.GetRequestInfo(iframe_document);
  ASSERT_TRUE(iframe_request->trusted_params);
  EXPECT_TRUE(
      net::IsolationInfo::Create(net::IsolationInfo::RequestType::kSubFrame,
                                 origin, iframe_origin,
                                 net::SiteForCookies::FromOrigin(origin),
                                 std::set<net::SchemefulSite>())
          .IsEqualForTesting(iframe_request->trusted_params->isolation_info));
}

// Tests that the initiator is not set for a browser initiated top frame
// navigation.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_BrowserNavigationInitiator) {
  GURL url(embedded_test_server()->GetURL("/title1.html"));

  URLLoaderMonitor monitor;

  // Perform the actual navigation.
  EXPECT_TRUE(NavigateToURL(shell(), url));

  absl::optional<network::ResourceRequest> request =
      monitor.GetRequestInfo(url);
  ASSERT_TRUE(request.has_value());
  ASSERT_FALSE(request->request_initiator.has_value());
}

// Test that the initiator is set to the starting page when a renderer initiated
// navigation goes from the starting page to another page.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_RendererNavigationInitiator) {
  GURL starting_page(embedded_test_server()->GetURL("a.com", "/title1.html"));
  url::Origin starting_page_origin;
  starting_page_origin = starting_page_origin.Create(starting_page);

  EXPECT_TRUE(NavigateToURL(shell(), starting_page));

  GURL url(embedded_test_server()->GetURL("/title2.html"));

  URLLoaderMonitor monitor;

  // Perform the actual navigation.
  EXPECT_TRUE(NavigateToURLFromRenderer(shell(), url));

  absl::optional<network::ResourceRequest> request =
      monitor.GetRequestInfo(url);
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(starting_page_origin, request->request_initiator);
}

// Test that the initiator is set to the starting page when a sub frame is
// navigated by Javascript from some starting page to another page.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_SubFrameJsNavigationInitiator) {
  GURL starting_page(embedded_test_server()->GetURL("/frame_tree/top.html"));
  EXPECT_TRUE(NavigateToURL(shell(), starting_page));

  // The main_frame() and subframe should each have a live RenderFrame.
  EXPECT_TRUE(main_frame()
                  ->current_frame_host()
                  ->render_view_host()
                  ->IsRenderViewLive());
  EXPECT_TRUE(main_frame()->current_frame_host()->IsRenderFrameLive());
  EXPECT_TRUE(
      main_frame()->child_at(0)->current_frame_host()->IsRenderFrameLive());

  GURL url(embedded_test_server()->GetURL("a.com", "/title1.html"));

  URLLoaderMonitor monitor({url});
  std::string script = "location.href='" + url.spec() + "'";

  // Perform the actual navigation.
  EXPECT_TRUE(ExecJs(main_frame()->child_at(0)->current_frame_host(), script));

  EXPECT_TRUE(main_frame()
                  ->current_frame_host()
                  ->render_view_host()
                  ->IsRenderViewLive());
  EXPECT_TRUE(main_frame()->current_frame_host()->IsRenderFrameLive());
  EXPECT_TRUE(
      main_frame()->child_at(0)->current_frame_host()->IsRenderFrameLive());

  url::Origin starting_page_origin;
  starting_page_origin = starting_page_origin.Create(starting_page);

  monitor.WaitForUrls();
  absl::optional<network::ResourceRequest> request =
      monitor.GetRequestInfo(url);
  EXPECT_EQ(starting_page_origin, request->request_initiator);
}

// Test that the initiator is set to the starting page when a sub frame,
// selected by Id, is navigated by Javascript from some starting page to another
// page.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_SubframeNavigationByTopFrameInitiator) {
  // Go to a page on a.com with an iframe that is on b.com
  GURL starting_page(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  EXPECT_TRUE(NavigateToURL(shell(), starting_page));

  // The main_frame and subframe should each have a live RenderFrame.
  EXPECT_TRUE(main_frame()
                  ->current_frame_host()
                  ->render_view_host()
                  ->IsRenderViewLive());
  EXPECT_TRUE(main_frame()->current_frame_host()->IsRenderFrameLive());
  EXPECT_TRUE(
      main_frame()->child_at(0)->current_frame_host()->IsRenderFrameLive());

  GURL url(embedded_test_server()->GetURL("c.com", "/title1.html"));

  URLLoaderMonitor monitor;

  // Perform the actual navigation.
  NavigateIframeToURL(web_contents(), "child-0", url);

  EXPECT_TRUE(main_frame()
                  ->current_frame_host()
                  ->render_view_host()
                  ->IsRenderViewLive());
  EXPECT_TRUE(main_frame()->current_frame_host()->IsRenderFrameLive());
  EXPECT_TRUE(
      main_frame()->child_at(0)->current_frame_host()->IsRenderFrameLive());

  url::Origin starting_page_origin;
  starting_page_origin = starting_page_origin.Create(starting_page);

  absl::optional<network::ResourceRequest> request =
      monitor.GetRequestInfo(url);
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(starting_page_origin, request->request_initiator);
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_RendererInitiatedCrossSiteNewWindowInitator) {
  GURL url(embedded_test_server()->GetURL("/simple_links.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  blink::LocalFrameToken initiator_frame_token =
      current_frame_host()->GetFrameToken();
  int initiator_process_id = current_frame_host()->GetProcess()->GetID();

  // Simulate clicking on a cross-site link.
  {
    const char kReplacePortNumber[] = "setPortNumber(%d);";
    uint16_t port_number = embedded_test_server()->port();
    url = embedded_test_server()->GetURL("foo.com", "/title2.html");
    EXPECT_TRUE(
        ExecJs(shell(), base::StringPrintf(kReplacePortNumber, port_number)));

    TestNavigationObserver observer(url);
    observer.StartWatchingNewWebContents();
    EXPECT_EQ(true, EvalJs(shell(), "clickCrossSiteNewWindowLink();"));

    observer.Wait();
    EXPECT_EQ(url, observer.last_navigation_url());
    EXPECT_TRUE(observer.last_navigation_succeeded());
    EXPECT_TRUE(observer.last_initiator_frame_token().has_value());
    EXPECT_EQ(initiator_frame_token,
              observer.last_initiator_frame_token().value());
    EXPECT_EQ(initiator_process_id, observer.last_initiator_process_id());
  }
}

// Ensure that renderer initiated navigations which have the opener suppressed
// work.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_RendererInitiatedNewWindowNoOpenerNavigation) {
  GURL url(embedded_test_server()->GetURL("/simple_links.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  RenderFrameHost* initial_rfh = current_frame_host();
  url::Origin initial_origin = initial_rfh->GetLastCommittedOrigin();
  blink::LocalFrameToken initiator_frame_token = initial_rfh->GetFrameToken();
  int initiator_process_id = initial_rfh->GetProcess()->GetID();

  // Simulate clicking on a cross-site link which has rel="noopener".
  {
    const char kReplacePortNumber[] = "setPortNumber(%d);";
    uint16_t port_number = embedded_test_server()->port();
    url = embedded_test_server()->GetURL("foo.com", "/title2.html");
    EXPECT_TRUE(
        ExecJs(shell(), base::StringPrintf(kReplacePortNumber, port_number)));

    TestNavigationObserver observer(url);
    observer.StartWatchingNewWebContents();
    EXPECT_EQ(true, EvalJs(shell(), "clickCrossSiteNewWindowNoOpenerLink();"));

    observer.Wait();

    EXPECT_EQ(url, observer.last_navigation_url());
    EXPECT_TRUE(observer.last_navigation_succeeded());
    EXPECT_EQ(initial_origin, observer.last_initiator_origin().value());
    EXPECT_TRUE(observer.last_initiator_frame_token().has_value());
    EXPECT_EQ(initiator_frame_token,
              observer.last_initiator_frame_token().value());
    EXPECT_EQ(initiator_process_id, observer.last_initiator_process_id());
  }
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_RendererInitiatedWithSubframeInitator) {
  GURL url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(a())"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  GURL subframe_url =
      embedded_test_server()->GetURL("a.com", "/simple_links.html");
  EXPECT_TRUE(
      NavigateToURLFromRenderer(main_frame()->child_at(0), subframe_url));

  RenderFrameHostImpl* subframe_rfh =
      current_frame_host()->child_at(0)->current_frame_host();
  blink::LocalFrameToken initiator_frame_token = subframe_rfh->GetFrameToken();
  int initiator_process_id = subframe_rfh->GetProcess()->GetID();

  // Simulate clicking on a cross-site link.
  {
    const char kReplacePortNumber[] = "setPortNumber(%d);";
    uint16_t port_number = embedded_test_server()->port();
    url = embedded_test_server()->GetURL("foo.com", "/title2.html");
    EXPECT_TRUE(ExecJs(subframe_rfh,
                       base::StringPrintf(kReplacePortNumber, port_number)));

    TestNavigationObserver observer(url);
    observer.StartWatchingNewWebContents();
    EXPECT_EQ(true, EvalJs(subframe_rfh, "clickCrossSiteNewWindowLink();"));

    observer.Wait();
    EXPECT_EQ(url, observer.last_navigation_url());
    EXPECT_TRUE(observer.last_navigation_succeeded());
    EXPECT_TRUE(observer.last_initiator_frame_token().has_value());
    EXPECT_EQ(initiator_frame_token,
              observer.last_initiator_frame_token().value());
    EXPECT_EQ(initiator_process_id, observer.last_initiator_process_id());
  }
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_InitiatorFrameStateConsistentAtDidStartNavigation) {
  GURL form_page_url(embedded_test_server()->GetURL(
      "a.com", "/form_that_posts_to_echoall.html"));
  EXPECT_TRUE(NavigateToURL(shell(), form_page_url));

  // Give the form an action that will navigate to a slow page.
  GURL form_action_url(embedded_test_server()->GetURL("b.com", "/slow?100"));
  EXPECT_TRUE(
      ExecJs(shell(), JsReplace("document.getElementById('form').action = $1",
                                form_action_url)));

  // Open a new window that can be targeted by the form submission.
  WebContents* form_contents = web_contents();
  ShellAddedObserver new_shell_observer;
  EXPECT_TRUE(ExecJs(shell(), "window.open('about:blank', 'target_frame');"));
  WebContents* popup_contents = new_shell_observer.GetShell()->web_contents();

  EXPECT_TRUE(
      ExecJs(form_contents,
             "document.getElementById('form').target = 'target_frame';"));

  TestNavigationManager popup_manager(popup_contents, form_action_url);
  TestNavigationManager form_manager(
      form_contents, embedded_test_server()->GetURL("a.com", "/title2.html"));

  // Submit the form and navigate the form's page.
  EXPECT_TRUE(ExecJs(form_contents, "window.location.href = 'title2.html'"));
  EXPECT_TRUE(
      ExecJs(form_contents, "document.getElementById('form').submit();"));

  // The form page's navigation should start prior to the form navigation.
  EXPECT_TRUE(form_manager.WaitForRequestStart());
  EXPECT_FALSE(popup_manager.GetNavigationHandle());

  // When the navigation starts for the popup, ensure that the original page has
  // not finished navigating. If this was not the case, we could not make any
  // statements on the validity of initiator state during a navigation.
  // Navigation handles are only available prior to DidFinishNavigation().
  EXPECT_TRUE(popup_manager.WaitForRequestStart());
  EXPECT_TRUE(form_manager.GetNavigationHandle());
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_RendererInitiatedMiddleClickInitator) {
  GURL url(embedded_test_server()->GetURL("/simple_links.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  blink::LocalFrameToken initiator_frame_token =
      current_frame_host()->GetFrameToken();
  int initiator_process_id = current_frame_host()->GetProcess()->GetID();

  // Simulate middle-clicking on a cross-site link.
  {
    const char kReplacePortNumber[] = "setPortNumber(%d);";
    uint16_t port_number = embedded_test_server()->port();
    url = embedded_test_server()->GetURL("foo.com", "/title2.html");
    EXPECT_TRUE(
        ExecJs(shell(), base::StringPrintf(kReplacePortNumber, port_number)));

    TestNavigationObserver observer(url);
    observer.StartWatchingNewWebContents();
    EXPECT_EQ(true, EvalJs(shell(), R"(
      target = document.getElementById('cross_site_link');
      var evt = new MouseEvent("click", {"button": 1 /* middle_button */});
      target.dispatchEvent(evt);)"));

    observer.Wait();
    EXPECT_EQ(url, observer.last_navigation_url());
    EXPECT_TRUE(observer.last_navigation_succeeded());
    EXPECT_TRUE(observer.last_initiator_frame_token().has_value());
    EXPECT_EQ(initiator_frame_token,
              observer.last_initiator_frame_token().value());
    EXPECT_EQ(initiator_process_id, observer.last_initiator_process_id());
  }
}

// Data URLs can have a reference fragment like any other URLs. This test makes
// sure it is taken into account.
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest, DataURLWithReferenceFragment) {
  GURL url("data:text/html,body#foo");
  EXPECT_TRUE(NavigateToURL(shell(), url));

  EXPECT_EQ("body", EvalJs(shell(), "document.body.textContent;"));

  EXPECT_EQ("#foo", EvalJs(shell(), "location.hash;"));
}

// Regression test for https://crbug.com/796561.
// 1) Start on a document with history.length == 1.
// 2) Create an iframe and call history.pushState at the same time.
// 3) history.back() must work.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_IframeAndPushStateSimultaneously) {
  GURL main_url = embedded_test_server()->GetURL("/simple_page.html");
  GURL iframe_url = embedded_test_server()->GetURL("/hello.html");

  // 1) Start on a new document such that history.length == 1.
  {
    EXPECT_TRUE(NavigateToURL(shell(), main_url));

    EXPECT_EQ(1, EvalJs(shell(), "history.length"));
  }

  // 2) Create an iframe and call history.pushState at the same time.
  {
    TestNavigationManager iframe_navigation(web_contents(), iframe_url);
    ExecuteScriptAsync(shell(),
                       "let iframe = document.createElement('iframe');"
                       "iframe.src = '/hello.html';"
                       "document.body.appendChild(iframe);");
    EXPECT_TRUE(iframe_navigation.WaitForRequestStart());

    // The iframe navigation is paused. In the meantime, a pushState navigation
    // begins and ends.
    TestNavigationManager push_state_navigation(web_contents(), main_url);
    ExecuteScriptAsync(shell(), "window.history.pushState({}, null);");
    ASSERT_TRUE(push_state_navigation.WaitForNavigationFinished());

    // The iframe navigation is resumed.
    ASSERT_TRUE(iframe_navigation.WaitForNavigationFinished());
  }

  // 3) history.back() must work.
  {
    TestNavigationObserver navigation_observer(web_contents());
    EXPECT_TRUE(ExecJs(web_contents(), "history.back();"));
    navigation_observer.Wait();
  }
}

// Regression test for https://crbug.com/260144
// Back/Forward navigation in an iframe must not stop ongoing XHR.
// TODO: b/432532766 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBaseBrowserTest,
                       DISABLED_IframeNavigationsDoNotStopXHR) {
  // A response for the XHR request. It will be delayed until the end of all the
  // navigations.
  net::test_server::ControllableHttpResponse xhr_response(
      embedded_test_server(), "/xhr");
  EXPECT_TRUE(embedded_test_server()->Start());

  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  DOMMessageQueue dom_message_queue(web_contents());
  std::string message;

  // 1) Send an XHR.
  ExecuteScriptAsync(
      shell(),
      "let xhr = new XMLHttpRequest();"
      "xhr.open('GET', './xhr', true);"
      "xhr.onabort = () => window.domAutomationController.send('xhr.onabort');"
      "xhr.onerror = () => window.domAutomationController.send('xhr.onerror');"
      "xhr.onload = () => window.domAutomationController.send('xhr.onload');"
      "xhr.send();");

  // 2) Create an iframe and wait for the initial load.
  {
    ExecuteScriptAsync(
        shell(),
        "var iframe = document.createElement('iframe');"
        "iframe.src = './title1.html';"
        "iframe.onload = function() {"
        "   window.domAutomationController.send('iframe.onload');"
        "};"
        "document.body.appendChild(iframe);");

    EXPECT_TRUE(dom_message_queue.WaitForMessage(&message));
    EXPECT_EQ("\"iframe.onload\"", message);
  }

  // 3) Navigate the iframe elsewhere.
  {
    ExecuteScriptAsync(shell(),
                       "var iframe = document.querySelector('iframe');"
                       "iframe.src = './title2.html';");

    EXPECT_TRUE(dom_message_queue.WaitForMessage(&message));
    EXPECT_EQ("\"iframe.onload\"", message);
  }

  // 4) history.back() in the iframe.
  {
    ExecuteScriptAsync(shell(),
                       "var iframe = document.querySelector('iframe');"
                       "iframe.contentWindow.history.back()");

    EXPECT_TRUE(dom_message_queue.WaitForMessage(&message));
    EXPECT_EQ("\"iframe.onload\"", message);
  }

  // 5) history.forward() in the iframe.
  {
    ExecuteScriptAsync(shell(),
                       "var iframe = document.querySelector('iframe');"
                       "iframe.contentWindow.history.forward()");

    EXPECT_TRUE(dom_message_queue.WaitForMessage(&message));
    EXPECT_EQ("\"iframe.onload\"", message);
  }

  // 6) Wait for the XHR.
  {
    xhr_response.WaitForRequest();
    xhr_response.Send(
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "Content-Length: 2\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "\r\n"
        "OK");
    xhr_response.Done();
    EXPECT_TRUE(dom_message_queue.WaitForMessage(&message));
    EXPECT_EQ("\"xhr.onload\"", message);
  }

  EXPECT_FALSE(dom_message_queue.PopMessage(&message));
}

// Regression test for https://crbug.com/856396.
// Note that original issue for the bug is not applicable anymore, because there
// is no provisional document loader which has not committed yet. We keep the
// modified version of this test to check removing iframe from the load event
// handler.
IN_PROC_BROWSER_TEST_F(NavigationBaseBrowserTest,
                       ReplacingDocumentLoaderFiresLoadEvent) {
  net::test_server::ControllableHttpResponse main_document_response(
      embedded_test_server(), "/main_document");
  net::test_server::ControllableHttpResponse iframe_response(
      embedded_test_server(), "/iframe");

  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Load the main document.
  shell()->LoadURL(embedded_test_server()->GetURL("/main_document"));
  main_document_response.WaitForRequest();
  main_document_response.Send(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "\r\n"
      "<script>"
      "  var detach_iframe = function() {"
      "    var iframe = document.querySelector('iframe');"
      "    iframe.parentNode.removeChild(iframe);"
      "  }"
      "</script>"
      "<body onload='detach_iframe()'>"
      "  <iframe src='/iframe'></iframe>"
      "</body>");
  main_document_response.Done();

  // 2) The iframe starts to load, but the server only have time to send the
  // response's headers, not the response's body. This should commit the
  // iframe's load.
  iframe_response.WaitForRequest();
  iframe_response.Send(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "\r\n");

  // 3) In the meantime the iframe navigates elsewhere. It causes the previous
  // DocumentLoader to be replaced by the new one. Removing it may
  // trigger the 'load' event and delete the iframe.
  EXPECT_TRUE(
      ExecJs(shell(), "document.querySelector('iframe').src = '/title1.html'"));

  // 4) Finish the original request.
  iframe_response.Done();

  // Wait for the iframe to be deleted and check the renderer process is still
  // alive.
  int iframe_count = 1;
  while (iframe_count != 0) {
    iframe_count =
        EvalJs(
            shell(),
            "var iframe_count = document.getElementsByTagName('iframe').length;"
            "iframe_count;")
            .ExtractInt();
  }
}

// Same as 'HistoryBackInBeforeUnload', but wraps history.back() inside
// window.setTimeout(). Thus it is executed "outside" of its beforeunload
// handler and thus avoid basic navigation circumventions.
// Regression test for: https://crbug.com/879965.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationGoToEntryAtOffsetBrowserTest,
                       DISABLED_HistoryBackInBeforeUnloadAfterSetTimeout) {
  GURL url_1(embedded_test_server()->GetURL("/title1.html"));
  GURL url_2(embedded_test_server()->GetURL("/title2.html"));

  EXPECT_TRUE(NavigateToURL(shell(), url_1));
  EXPECT_TRUE(ExecJs(web_contents(),
                     "onbeforeunload = function() {"
                     "  history.pushState({}, null, '/');"
                     "  setTimeout(()=>history.back());"
                     "};",
                     EXECUTE_SCRIPT_NO_USER_GESTURE));
  TestNavigationManager navigation(web_contents(), url_2);

  base::RunLoop run_loop;
  SetQuitHandlerForGoToEntryAtOffset(run_loop.QuitClosure());
  shell()->LoadURL(url_2);
  run_loop.Run();

  ASSERT_TRUE(navigation.WaitForNavigationFinished());

  EXPECT_TRUE(navigation.was_successful());
}

// Renderer initiated back/forward navigation can't cancel an ongoing browser
// initiated navigation if it is not user initiated.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_HistoryBackCancelPendingNavigationNoUserGesture) {
  GURL url_1(embedded_test_server()->GetURL("/title1.html"));
  GURL url_2(embedded_test_server()->GetURL("/title2.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_1));

  // 1) A pending browser initiated navigation (omnibox, ...) starts.
  TestNavigationManager navigation(web_contents(), url_2);
  shell()->LoadURL(url_2);
  EXPECT_TRUE(navigation.WaitForRequestStart());

  // 2) history.back() is sent but is not user initiated.
  EXPECT_TRUE(ExecJs(web_contents(),
                     "history.pushState({}, null, '/');"
                     "history.back();",
                     EXECUTE_SCRIPT_NO_USER_GESTURE));

  // 3) The first pending navigation is not canceled and can continue.
  ASSERT_TRUE(navigation.WaitForNavigationFinished());  // Resume navigation.
  EXPECT_TRUE(navigation.was_successful());
}

// Renderer initiated back/forward navigation can cancel an ongoing browser
// initiated navigation if it is user initiated.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_HistoryBackCancelPendingNavigationUserGesture) {
  GURL url_1(embedded_test_server()->GetURL("/title1.html"));
  GURL url_2(embedded_test_server()->GetURL("/title2.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_1));

  // 1) A pending browser initiated navigation (omnibox, ...) starts.
  TestNavigationManager navigation(web_contents(), url_2);
  shell()->LoadURL(url_2);
  EXPECT_TRUE(navigation.WaitForRequestStart());

  // 2) history.back() is sent and is user initiated.
  EXPECT_TRUE(ExecJs(web_contents(),
                     "history.pushState({}, null, '/');"
                     "history.back();"));

  // 3) Check the first pending navigation has been canceled.
  ASSERT_TRUE(navigation.WaitForNavigationFinished());  // Resume navigation.
  EXPECT_FALSE(navigation.was_successful());
}

// Ensure the renderer process doesn't send too many IPC to the browser process
// when history.pushState() and history.back() are called in a loop.
// Failing to do so causes the browser to become unresponsive.
// See https://crbug.com/882238
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_IPCFlood_GoToEntryAtOffset) {
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern(
      "Throttling navigation to prevent the browser from hanging. See "
      "https://crbug.com/1038223. Command line switch "
      "--disable-ipc-flooding-protection can be used to bypass the "
      "protection");

  EXPECT_TRUE(ExecJs(shell(), R"(
    for(let i = 0; i<1000; ++i) {
      history.pushState({},"page 2", "bar.html");
      history.back();
    }
  )"));

  ASSERT_TRUE(console_observer.Wait());
}

// Ensure the renderer process doesn't send too many IPC to the browser process
// when doing a same-document navigation is requested in a loop.
// Failing to do so causes the browser to become unresponsive.
// TODO(arthursonzogni): Make the same test, but when the navigation is
// requested from a remote frame.
// See https://crbug.com/882238
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest, DISABLED_IPCFlood_Navigation) {
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern(
      "Throttling navigation to prevent the browser from hanging. See "
      "https://crbug.com/1038223. Command line switch "
      "--disable-ipc-flooding-protection can be used to bypass the "
      "protection");

  EXPECT_TRUE(ExecJs(shell(), R"(
    for(let i = 0; i<1000; ++i) {
      location.href = "#" + i;
      ++i;
    }
  )"));

  ASSERT_TRUE(console_observer.Wait());
}

// Add header on redirect.
IN_PROC_BROWSER_TEST_F(NavigationBaseBrowserTest, AddRequestHeaderOnRedirect) {
  net::test_server::ControllableHttpResponse response_1(embedded_test_server(),
                                                        "", true);
  net::test_server::ControllableHttpResponse response_2(embedded_test_server(),
                                                        "", true);
  ASSERT_TRUE(embedded_test_server()->Start());

  content::TestNavigationThrottleInserter throttle_inserter(
      web_contents(),
      base::BindLambdaForTesting(
          [](NavigationHandle* handle) -> std::unique_ptr<NavigationThrottle> {
            auto throttle = std::make_unique<TestNavigationThrottle>(handle);
            NavigationRequest* request = NavigationRequest::From(handle);
            throttle->SetCallback(TestNavigationThrottle::WILL_REDIRECT_REQUEST,
                                  base::BindLambdaForTesting([request]() {
                                    request->SetRequestHeader("header_name",
                                                              "header_value");
                                  }));
            return throttle;
          }));

  // 1) There is no "header_name" header in the initial request.
  shell()->LoadURL(embedded_test_server()->GetURL("/doc"));
  response_1.WaitForRequest();
  EXPECT_FALSE(
      base::Contains(response_1.http_request()->headers, "header_name"));
  response_1.Send(
      "HTTP/1.1 302 Moved Temporarily\r\nLocation: /new_doc\r\n\r\n");
  response_1.Done();

  // 2) The header is added to the second request after the redirect.
  response_2.WaitForRequest();
  EXPECT_EQ("header_value",
            response_2.http_request()->headers.at("header_name"));

  // Redirect should not record a ReceivedResponse event.
  EXPECT_EQ(0u, test_ukm_recorder()
                    .GetEntriesByName("Navigation.ReceivedResponse")
                    .size());
}

// Add header on request start, modify it on redirect.
IN_PROC_BROWSER_TEST_F(NavigationBaseBrowserTest,
                       AddRequestHeaderModifyOnRedirect) {
  net::test_server::ControllableHttpResponse response_1(embedded_test_server(),
                                                        "", true);
  net::test_server::ControllableHttpResponse response_2(embedded_test_server(),
                                                        "", true);
  ASSERT_TRUE(embedded_test_server()->Start());

  content::TestNavigationThrottleInserter throttle_inserter(
      web_contents(),
      base::BindLambdaForTesting(
          [](NavigationHandle* handle) -> std::unique_ptr<NavigationThrottle> {
            auto throttle = std::make_unique<TestNavigationThrottle>(handle);
            NavigationRequest* request = NavigationRequest::From(handle);
            throttle->SetCallback(TestNavigationThrottle::WILL_START_REQUEST,
                                  base::BindLambdaForTesting([request]() {
                                    request->SetRequestHeader("header_name",
                                                              "header_value");
                                  }));
            throttle->SetCallback(TestNavigationThrottle::WILL_REDIRECT_REQUEST,
                                  base::BindLambdaForTesting([request]() {
                                    request->SetRequestHeader("header_name",
                                                              "other_value");
                                  }));
            return throttle;
          }));

  // 1) The header is added to the initial request.
  shell()->LoadURL(embedded_test_server()->GetURL("/doc"));
  response_1.WaitForRequest();
  EXPECT_EQ("header_value",
            response_1.http_request()->headers.at("header_name"));
  response_1.Send(
      "HTTP/1.1 302 Moved Temporarily\r\nLocation: /new_doc\r\n\r\n");
  response_1.Done();

  // 2) The header is modified in the second request after the redirect.
  response_2.WaitForRequest();
  EXPECT_EQ("other_value",
            response_2.http_request()->headers.at("header_name"));
}

// Add header on request start, remove it on redirect.
IN_PROC_BROWSER_TEST_F(NavigationBaseBrowserTest,
                       AddRequestHeaderRemoveOnRedirect) {
  net::test_server::ControllableHttpResponse response_1(embedded_test_server(),
                                                        "", true);
  net::test_server::ControllableHttpResponse response_2(embedded_test_server(),
                                                        "", true);
  ASSERT_TRUE(embedded_test_server()->Start());

  content::TestNavigationThrottleInserter throttle_inserter(
      web_contents(),
      base::BindLambdaForTesting(
          [](NavigationHandle* handle) -> std::unique_ptr<NavigationThrottle> {
            NavigationRequest* request = NavigationRequest::From(handle);
            auto throttle = std::make_unique<TestNavigationThrottle>(handle);
            throttle->SetCallback(TestNavigationThrottle::WILL_START_REQUEST,
                                  base::BindLambdaForTesting([request]() {
                                    request->SetRequestHeader("header_name",
                                                              "header_value");
                                  }));
            throttle->SetCallback(TestNavigationThrottle::WILL_REDIRECT_REQUEST,
                                  base::BindLambdaForTesting([request]() {
                                    request->RemoveRequestHeader("header_name");
                                  }));
            return throttle;
          }));

  // 1) The header is added to the initial request.
  shell()->LoadURL(embedded_test_server()->GetURL("/doc"));
  response_1.WaitForRequest();
  EXPECT_EQ("header_value",
            response_1.http_request()->headers.at("header_name"));
  response_1.Send(
      "HTTP/1.1 302 Moved Temporarily\r\nLocation: /new_doc\r\n\r\n");
  response_1.Done();

  // 2) The header is removed from the second request after the redirect.
  response_2.WaitForRequest();
  EXPECT_FALSE(
      base::Contains(response_2.http_request()->headers, "header_name"));
}

// Name of header used by CorsInjectingUrlLoader.
const std::string kCorsHeaderName = "test-header";

// URLLoaderThrottle that stores the last value of |kCorsHeaderName|.
class CorsInjectingUrlLoader : public blink::URLLoaderThrottle {
 public:
  explicit CorsInjectingUrlLoader(std::string* last_cors_header_value)
      : last_cors_header_value_(last_cors_header_value) {}

  // blink::URLLoaderThrottle:
  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override {
    if (!request->cors_exempt_headers.GetHeader(kCorsHeaderName,
                                                last_cors_header_value_)) {
      last_cors_header_value_->clear();
    }
  }

 private:
  // See |NavigationCorsExemptBrowserTest::last_cors_header_value_| for details.
  raw_ptr<std::string> last_cors_header_value_;
};

// ContentBrowserClient responsible for creating CorsInjectingUrlLoader.
class CorsContentBrowserClient : public ContentBrowserTestContentBrowserClient {
 public:
  explicit CorsContentBrowserClient(std::string* last_cors_header_value)
      : last_cors_header_value_(last_cors_header_value) {}

  // ContentBrowserClient overrides:
  std::vector<std::unique_ptr<blink::URLLoaderThrottle>>
  CreateURLLoaderThrottles(
      const network::ResourceRequest& request,
      BrowserContext* browser_context,
      const base::RepeatingCallback<WebContents*()>& wc_getter,
      NavigationUIData* navigation_ui_data,
      int frame_tree_node_id) override {
    std::vector<std::unique_ptr<blink::URLLoaderThrottle>> throttles;
    throttles.push_back(
        std::make_unique<CorsInjectingUrlLoader>(last_cors_header_value_));
    return throttles;
  }

 private:
  // See |NavigationCorsExemptBrowserTest::last_cors_header_value_| for details.
  raw_ptr<std::string> last_cors_header_value_;
};

class NavigationCorsExemptBrowserTest : public NavigationBaseBrowserTest {
 public:
  NavigationCorsExemptBrowserTest() = default;

 protected:
  const std::string& last_cors_header_value() const {
    return last_cors_header_value_;
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    ShellContentBrowserClient::set_allow_any_cors_exempt_header_for_browser(
        true);
    NavigationBaseBrowserTest::SetUpCommandLine(command_line);
  }
  void SetUpOnMainThread() override {
    cors_content_browser_client_ =
        std::make_unique<CorsContentBrowserClient>(&last_cors_header_value_);
    host_resolver()->AddRule("*", "127.0.0.1");
  }
  void TearDownOnMainThread() override {
    cors_content_browser_client_.reset();
    ShellContentBrowserClient::set_allow_any_cors_exempt_header_for_browser(
        false);
  }

 private:
  // Last value of kCorsHeaderName. Set by CorsInjectingUrlLoader.
  std::string last_cors_header_value_;
  std::unique_ptr<CorsContentBrowserClient> cors_content_browser_client_;
};

// Verifies a header added by way of SetRequestHeader() makes it into
// |cors_exempt_headers|.
IN_PROC_BROWSER_TEST_F(NavigationCorsExemptBrowserTest,
                       SetCorsExemptRequestHeader) {
  net::test_server::ControllableHttpResponse response(embedded_test_server(),
                                                      "", true);
  ASSERT_TRUE(embedded_test_server()->Start());

  const std::string header_value = "value";
  content::TestNavigationThrottleInserter throttle_inserter(
      web_contents(),
      base::BindLambdaForTesting([header_value](NavigationHandle* handle)
                                     -> std::unique_ptr<NavigationThrottle> {
        NavigationRequest* request = NavigationRequest::From(handle);
        auto throttle = std::make_unique<TestNavigationThrottle>(handle);
        throttle->SetCallback(
            TestNavigationThrottle::WILL_START_REQUEST,
            base::BindLambdaForTesting([request, header_value]() {
              request->SetCorsExemptRequestHeader(kCorsHeaderName,
                                                  header_value);
            }));
        return throttle;
      }));
  shell()->LoadURL(embedded_test_server()->GetURL("/doc"));
  response.WaitForRequest();
  EXPECT_EQ(header_value, response.http_request()->headers.at(kCorsHeaderName));
  EXPECT_EQ(header_value, last_cors_header_value());
}

struct NewWebContentsData {
  NewWebContentsData() = default;
  NewWebContentsData(NewWebContentsData&& other)
      : new_web_contents(std::move(other.new_web_contents)),
        manager(std::move(other.manager)) {}

  std::unique_ptr<WebContents> new_web_contents;
  std::unique_ptr<TestNavigationManager> manager;
};

class CreateWebContentsOnCrashObserver : public NotificationObserver {
 public:
  CreateWebContentsOnCrashObserver(const GURL& url,
                                   WebContents* first_web_contents)
      : url_(url), first_web_contents_(first_web_contents) {}

  CreateWebContentsOnCrashObserver(const CreateWebContentsOnCrashObserver&) =
      delete;
  CreateWebContentsOnCrashObserver& operator=(
      const CreateWebContentsOnCrashObserver&) = delete;

  void Observe(int type,
               const NotificationSource& source,
               const NotificationDetails& details) override {
    EXPECT_EQ(content::NOTIFICATION_RENDERER_PROCESS_CLOSED, type);

    // Only do this once in the test.
    if (observed_) {
      return;
    }
    observed_ = true;

    WebContents::CreateParams new_contents_params(
        first_web_contents_->GetBrowserContext(),
        first_web_contents_->GetSiteInstance());
    data_.new_web_contents = WebContents::Create(new_contents_params);
    data_.manager = std::make_unique<TestNavigationManager>(
        data_.new_web_contents.get(), url_);
    NavigationController::LoadURLParams load_params(url_);
    data_.new_web_contents->GetController().LoadURLWithParams(load_params);
  }

  NewWebContentsData TakeNewWebContentsData() { return std::move(data_); }

 private:
  NewWebContentsData data_;
  bool observed_ = false;

  GURL url_;
  raw_ptr<WebContents> first_web_contents_;

  ScopedAllowRendererCrashes scoped_allow_renderer_crashes_;
};

// Test NavigationRequest::CheckAboutSrcDoc()
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_BlockedSrcDocBrowserInitiated) {
  const char* about_srcdoc_urls[] = {"about:srcdoc", "about:srcdoc?foo",
                                     "about:srcdoc#foo"};
  // 1. Main frame navigations to about:srcdoc and its variations are blocked.
  for (const char* url : about_srcdoc_urls) {
    NavigationHandleObserver handle_observer(web_contents(), GURL(url));
    EXPECT_FALSE(NavigateToURL(shell(), GURL(url)));
    EXPECT_TRUE(handle_observer.has_committed());
    EXPECT_TRUE(handle_observer.is_error());
    EXPECT_EQ(net::ERR_INVALID_URL, handle_observer.net_error_code());
  }

  // 2. Subframe navigations to variations of about:srcdoc are not blocked.
  for (const char* url : about_srcdoc_urls) {
    GURL main_url =
        embedded_test_server()->GetURL("/frame_tree/page_with_one_frame.html");
    EXPECT_TRUE(NavigateToURL(shell(), main_url));

    NavigationHandleObserver handle_observer(web_contents(), GURL(url));
    shell()->LoadURLForFrame(GURL(url), "child-name-0",
                             ui::PAGE_TRANSITION_FORWARD_BACK);
    EXPECT_TRUE(WaitForLoadStop(web_contents()));
    EXPECT_TRUE(handle_observer.has_committed());
    EXPECT_FALSE(handle_observer.is_error());
    EXPECT_EQ(net::OK, handle_observer.net_error_code());
  }
}

// Test NavigationRequest::CheckAboutSrcDoc().
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_BlockedSrcDocRendererInitiated) {
  EXPECT_TRUE(
      NavigateToURL(shell(), embedded_test_server()->GetURL("/title1.html")));
  const char* about_srcdoc_urls[] = {"about:srcdoc", "about:srcdoc?foo",
                                     "about:srcdoc#foo"};

  // 1. Main frame navigations to about:srcdoc and its variations are blocked.
  for (const char* url : about_srcdoc_urls) {
    DidStartNavigationObserver start_observer(web_contents());
    NavigationHandleObserver handle_observer(web_contents(), GURL(url));
    // TODO(arthursonzogni): It shouldn't be possible to navigate to
    // about:srcdoc by executing location.href= "about:srcdoc". Other web
    // browsers like Firefox aren't allowing this.
    EXPECT_TRUE(ExecJs(main_frame(), JsReplace("location.href = $1", url)));
    start_observer.Wait();
    WaitForLoadStop(web_contents());
    EXPECT_TRUE(handle_observer.has_committed());
    EXPECT_TRUE(handle_observer.is_error());
    EXPECT_EQ(net::ERR_INVALID_URL, handle_observer.net_error_code());
  }

  // 2. Subframe navigations to variations of about:srcdoc are not blocked.
  for (const char* url : about_srcdoc_urls) {
    GURL main_url =
        embedded_test_server()->GetURL("/frame_tree/page_with_one_frame.html");
    EXPECT_TRUE(NavigateToURL(shell(), main_url));

    DidStartNavigationObserver start_observer(web_contents());
    NavigationHandleObserver handle_observer(web_contents(), GURL(url));
    FrameTreeNode* subframe = main_frame()->child_at(0);
    // TODO(arthursonzogni): It shouldn't be possible to navigate to
    // about:srcdoc by executing location.href= "about:srcdoc". Other web
    // browsers like Firefox aren't allowing this.
    EXPECT_TRUE(ExecJs(subframe, JsReplace("location.href = $1", url)));
    start_observer.Wait();
    EXPECT_TRUE(WaitForLoadStop(web_contents()));

    EXPECT_TRUE(handle_observer.has_committed());
    EXPECT_FALSE(handle_observer.is_error());
    EXPECT_EQ(net::OK, handle_observer.net_error_code());
  }
}

// Test renderer initiated navigations to about:srcdoc are routed through the
// browser process. It means RenderFrameHostImpl::BeginNavigation() is called.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_AboutSrcDocUsesBeginNavigation) {
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // If DidStartNavigation is called before DidCommitProvisionalLoad, then it
  // means the navigation was driven by the browser process, otherwise by the
  // renderer process. This tests it was driven by the browser process:
  InterceptAndCancelDidCommitProvisionalLoad interceptor(web_contents());
  DidStartNavigationObserver observer(web_contents());

  EXPECT_TRUE(ExecJs(shell(), R"(
    let iframe = document.createElement("iframe");
    iframe.srcdoc = "foo"
    document.body.appendChild(iframe);
  )"));

  observer.Wait();      // BeginNavigation is called.
  interceptor.Wait(1);  // DidCommitNavigation is called.
}

// Regression test for https://crbug.com/996044
//  1) Navigate an iframe to srcdoc (about:srcdoc);
//  2) Same-document navigation to about:srcdoc#1.
//  3) Same-document navigation to about:srcdoc#2.
//  4) history.back() to about:srcdoc#1.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_SrcDocWithFragmentHistoryNavigation) {
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  //  1) Navigate an iframe to srcdoc (about:srcdoc)
  EXPECT_TRUE(ExecJs(shell(), R"(
    new Promise(async resolve => {
      let iframe = document.createElement('iframe');
      iframe.srcdoc = "test";
      iframe.onload = resolve;
      document.body.appendChild(iframe);
    });
  )"));

  //  2) Same-document navigation to about:srcdoc#1.
  //  3) Same-document navigation to about:srcdoc#2.
  EXPECT_TRUE(ExecJs(shell(), R"(
    let subwindow = document.querySelector('iframe').contentWindow;
    subwindow.location.hash = "1";
    subwindow.location.hash = "2";
  )"));

  // Inspect the session history.
  NavigationControllerImpl& controller = web_contents()->GetController();
  ASSERT_EQ(3, controller.GetEntryCount());
  ASSERT_EQ(2, controller.GetCurrentEntryIndex());

  FrameNavigationEntry* entry[3];
  for (int i = 0; i < 3; ++i) {
    entry[i] = controller.GetEntryAtIndex(i)
                   ->root_node()
                   ->children[0]
                   ->frame_entry.get();
  }

  EXPECT_EQ(entry[0]->url(), "about:srcdoc");
  EXPECT_EQ(entry[1]->url(), "about:srcdoc#1");
  EXPECT_EQ(entry[2]->url(), "about:srcdoc#2");

  //  4) history.back() to about:srcdoc#1.
  EXPECT_TRUE(ExecJs(shell(), "history.back()"));

  ASSERT_EQ(3, controller.GetEntryCount());
  ASSERT_EQ(1, controller.GetCurrentEntryIndex());
}

// Regression test for https://crbug.com/996044.
//  1) Navigate an iframe to srcdoc (about:srcdoc).
//  2) Cross-document navigation to about:srcdoc?1.
//  3) Cross-document navigation to about:srcdoc?2.
//  4) history.back() to about:srcdoc?1.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_SrcDocWithQueryHistoryNavigation) {
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  //  1) Navigate an iframe to srcdoc (about:srcdoc).
  EXPECT_TRUE(ExecJs(shell(), R"(
    new Promise(async resolve => {
      let iframe = document.createElement('iframe');
      iframe.srcdoc = "test";
      iframe.onload = resolve;
      document.body.appendChild(iframe);
    });
  )"));

  //  2) Cross-document navigation to about:srcdoc?1.
  {
    TestNavigationManager commit_waiter(web_contents(), GURL("about:srcdoc?1"));
    EXPECT_TRUE(ExecJs(shell(), R"(
      let subwindow = document.querySelector('iframe').contentWindow;
      subwindow.location.search = "1";
    )"));
    ASSERT_TRUE(commit_waiter.WaitForNavigationFinished());
  }

  //  3) Cross-document navigation to about:srcdoc?2.
  {
    TestNavigationManager commit_waiter(web_contents(), GURL("about:srcdoc?2"));
    EXPECT_TRUE(ExecJs(shell(), R"(
      let subwindow = document.querySelector('iframe').contentWindow;
      subwindow.location.search = "2";
    )"));
    ASSERT_TRUE(commit_waiter.WaitForNavigationFinished());
  }

  // Inspect the session history.
  NavigationControllerImpl& controller = web_contents()->GetController();
  ASSERT_EQ(3, controller.GetEntryCount());
  ASSERT_EQ(2, controller.GetCurrentEntryIndex());

  FrameNavigationEntry* entry[3];
  for (int i = 0; i < 3; ++i) {
    entry[i] = controller.GetEntryAtIndex(i)
                   ->root_node()
                   ->children[0]
                   ->frame_entry.get();
  }

  EXPECT_EQ(entry[0]->url(), "about:srcdoc");
  EXPECT_EQ(entry[1]->url(), "about:srcdoc?1");
  EXPECT_EQ(entry[2]->url(), "about:srcdoc?2");

  //  4) history.back() to about:srcdoc#1.
  EXPECT_TRUE(ExecJs(shell(), "history.back()"));

  ASSERT_EQ(3, controller.GetEntryCount());
  ASSERT_EQ(1, controller.GetCurrentEntryIndex());
}

// Make sure embedders are notified about visible URL changes in this scenario:
// 1. Navigate to A.
// 2. Navigate to B.
// 3. Add a forward entry in the history for later (same-document).
// 4. Start navigation to C.
// 5. Start history cross-document navigation, cancelling 4.
// 6. Start history same-document navigation, cancelling 5.
//
// Regression test for https://crbug.com/998284.
IN_PROC_BROWSER_TEST_F(NavigationBaseBrowserTest,
                       BackForwardInOldDocumentCancelPendingNavigation) {
  // This test expects a new request to be made when navigating back, which is
  // not happening with back-forward cache enabled.
  // See BackForwardCacheBrowserTest.RestoreWhilePendingCommit which covers the
  // same scenario for back-forward cache.
  web_contents()->GetController().GetBackForwardCache().DisableForTesting(
      BackForwardCacheImpl::TEST_REQUIRES_NO_CACHING);

  using Response = net::test_server::ControllableHttpResponse;
  Response response_A1(embedded_test_server(), "/A");
  Response response_A2(embedded_test_server(), "/A");
  Response response_B1(embedded_test_server(), "/B");
  Response response_C1(embedded_test_server(), "/C");

  ASSERT_TRUE(embedded_test_server()->Start());

  GURL url_a = embedded_test_server()->GetURL("a.com", "/A");
  GURL url_b = embedded_test_server()->GetURL("b.com", "/B");
  GURL url_c = embedded_test_server()->GetURL("c.com", "/C");

  EmbedderVisibleUrlTracker embedder_url_tracker;
  web_contents()->SetDelegate(&embedder_url_tracker);

  // 1. Navigate to A.
  shell()->LoadURL(url_a);
  response_A1.WaitForRequest();
  response_A1.Send(non_cacheable_html_response);
  response_A1.Done();
  EXPECT_TRUE(WaitForLoadStop(web_contents()));

  // 2. Navigate to B.
  shell()->LoadURL(url_b);
  response_B1.WaitForRequest();
  response_B1.Send(non_cacheable_html_response);
  response_B1.Done();
  EXPECT_TRUE(WaitForLoadStop(web_contents()));

  // 3. Add a forward entry in the history for later (same-document).
  EXPECT_TRUE(ExecJs(web_contents(), R"(
    history.pushState({},'');
    history.back();
  )"));

  // 4. Start navigation to C.
  {
    EXPECT_EQ(url_b, web_contents()->GetVisibleURL());
    EXPECT_EQ(url_b, embedder_url_tracker.url());
  }
  shell()->LoadURL(url_c);
  // TODO(arthursonzogni): The embedder_url_tracker should update to url_c at
  // this point, but we currently rely on FrameTreeNode::DidStopLoading for
  // invalidation and it does not occur when a prior navigation is already in
  // progress. The browser is still waiting on the same-document
  // "history.back()" to complete.
  {
    EXPECT_EQ(url_c, web_contents()->GetVisibleURL());
    EXPECT_EQ(url_b, embedder_url_tracker.url());
  }
  embedder_url_tracker.WaitUntilUrlInvalidated();
  {
    EXPECT_EQ(url_c, web_contents()->GetVisibleURL());
    EXPECT_EQ(url_c, embedder_url_tracker.url());
  }
  response_C1.WaitForRequest();

  // 5. Start history cross-document navigation, cancelling 4.
  EXPECT_TRUE(ExecJs(web_contents(), "history.back()"));
  {
    EXPECT_EQ(url_b, web_contents()->GetVisibleURL());
    EXPECT_EQ(url_b, embedder_url_tracker.url());
  }
  response_A2.WaitForRequest();
  {
    EXPECT_EQ(url_b, web_contents()->GetVisibleURL());
    EXPECT_EQ(url_b, embedder_url_tracker.url());
  }

  // 6. Start history same-document navigation, cancelling 5.
  EXPECT_TRUE(ExecJs(web_contents(), "history.forward()"));
  {
    EXPECT_EQ(url_b, web_contents()->GetVisibleURL());
    EXPECT_EQ(url_b, embedder_url_tracker.url());
  }
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  {
    EXPECT_EQ(url_b, web_contents()->GetVisibleURL());
    EXPECT_EQ(url_b, embedder_url_tracker.url());
  }
}

// Regression test for https://crbug.com/999932.
IN_PROC_BROWSER_TEST_F(NavigationBaseBrowserTest, CanceledNavigationBug999932) {
  using Response = net::test_server::ControllableHttpResponse;
  Response response_A1(embedded_test_server(), "/A");
  Response response_A2(embedded_test_server(), "/A");
  Response response_B1(embedded_test_server(), "/B");

  ASSERT_TRUE(embedded_test_server()->Start());

  GURL url_a = embedded_test_server()->GetURL("a.com", "/A");
  GURL url_b = embedded_test_server()->GetURL("b.com", "/B");

  // 1. Navigate to A.
  shell()->LoadURL(url_a);
  response_A1.WaitForRequest();
  response_A1.Send(non_cacheable_html_response);
  response_A1.Done();
  EXPECT_TRUE(WaitForLoadStop(web_contents()));

  // 2. Start pending navigation to B.
  shell()->LoadURL(url_b);
  EXPECT_EQ(url_b, web_contents()->GetVisibleURL());
  EXPECT_TRUE(web_contents()->GetController().GetPendingEntry());

  // 3. Cancel (2) with renderer-initiated reload with a UserGesture.
  EXPECT_TRUE(ExecJs(web_contents(), "location.reload()"));
  EXPECT_EQ(url_a, web_contents()->GetVisibleURL());
  EXPECT_FALSE(web_contents()->GetController().GetPendingEntry());

  // 4. Cancel (3) using document.open();
  EXPECT_TRUE(ExecJs(web_contents(), "document.open()"));
  EXPECT_EQ(url_a, web_contents()->GetVisibleURL());
  EXPECT_FALSE(web_contents()->GetController().GetPendingEntry());
}

// Regression test for https://crbug.com/1001283
// 1) Load main document with CSP: script-src 'none'
// 2) Open an about:srcdoc iframe. It inherits the CSP.
// 3) The iframe navigates elsewhere.
// 4) The iframe navigates back to about:srcdoc.
// Check Javascript is never allowed.
IN_PROC_BROWSER_TEST_F(NavigationBaseBrowserTest,
                       SrcDocCSPInheritedAfterSameSiteHistoryNavigation) {
  using Response = net::test_server::ControllableHttpResponse;
  Response main_document_response(embedded_test_server(), "/main_document");

  ASSERT_TRUE(embedded_test_server()->Start());

  GURL url_a = embedded_test_server()->GetURL("a.com", "/main_document");
  GURL url_b = embedded_test_server()->GetURL("a.com", "/title1.html");

  {
    WebContentsConsoleObserver console_observer(web_contents());
    console_observer.SetPattern("Refused to execute inline script *");

    // 1) Load main document with CSP: script-src 'none'
    // 2) Open an about:srcdoc iframe. It inherits the CSP from its parent.
    shell()->LoadURL(url_a);
    main_document_response.WaitForRequest();
    main_document_response.Send(
        "HTTP/1.1 200 OK\n"
        "content-type: text/html; charset=UTF-8\n"
        "Content-Security-Policy: script-src 'none'\n"
        "\n"
        "<iframe name='theiframe' srcdoc='"
        "  <script>"
        "    console.error(\"CSP failure\");"
        "  </script>"
        "'>"
        "</iframe>");
    main_document_response.Done();
    EXPECT_TRUE(WaitForLoadStop(web_contents()));

    // Check Javascript was blocked the first time.
    ASSERT_TRUE(console_observer.Wait());
  }

  // 3) The iframe navigates elsewhere.
  shell()->LoadURLForFrame(url_b, "theiframe",
                           ui::PAGE_TRANSITION_MANUAL_SUBFRAME);
  EXPECT_TRUE(WaitForLoadStop(web_contents()));

  {
    WebContentsConsoleObserver console_observer(web_contents());
    console_observer.SetPattern("Refused to execute inline script *");

    // 4) The iframe navigates back to about:srcdoc.
    web_contents()->GetController().GoBack();
    EXPECT_TRUE(WaitForLoadStop(web_contents()));

    // Check Javascript was blocked the second time.
    ASSERT_TRUE(console_observer.Wait());
  }
}

IN_PROC_BROWSER_TEST_F(NavigationBaseBrowserTest,
                       SrcDocCSPInheritedAfterCrossSiteHistoryNavigation) {
  using Response = net::test_server::ControllableHttpResponse;
  Response main_document_response(embedded_test_server(), "/main_document");

  ASSERT_TRUE(embedded_test_server()->Start());

  GURL url_a = embedded_test_server()->GetURL("a.com", "/main_document");
  GURL url_b = embedded_test_server()->GetURL("b.com", "/title1.html");

  {
    WebContentsConsoleObserver console_observer(web_contents());
    console_observer.SetPattern("Refused to execute inline script *");

    // 1) Load main document with CSP: script-src 'none'
    // 2) Open an about:srcdoc iframe. It inherits the CSP from its parent.
    shell()->LoadURL(url_a);
    main_document_response.WaitForRequest();
    main_document_response.Send(
        "HTTP/1.1 200 OK\n"
        "content-type: text/html; charset=UTF-8\n"
        "Content-Security-Policy: script-src 'none'\n"
        "\n"
        "<iframe name='theiframe' srcdoc='"
        "  <script>"
        "    console.error(\"CSP failure\");"
        "  </script>"
        "'>"
        "</iframe>");
    main_document_response.Done();
    EXPECT_TRUE(WaitForLoadStop(web_contents()));

    // Check Javascript was blocked the first time.
    ASSERT_TRUE(console_observer.Wait());
  }

  // 3) The iframe navigates elsewhere.
  shell()->LoadURLForFrame(url_b, "theiframe",
                           ui::PAGE_TRANSITION_MANUAL_SUBFRAME);
  EXPECT_TRUE(WaitForLoadStop(web_contents()));

  {
    WebContentsConsoleObserver console_observer(web_contents());
    console_observer.SetPattern("Refused to execute inline script *");

    // 4) The iframe navigates back to about:srcdoc.
    web_contents()->GetController().GoBack();
    EXPECT_TRUE(WaitForLoadStop(web_contents()));

    // Check Javascript was blocked the second time.
    ASSERT_TRUE(console_observer.Wait());
  }
}

// Test that NavigationRequest::GetNextPageUkmSourceId returns the eventual
// value of RenderFrameHost::GetPageUkmSourceId() --- unremarkable top-level
// navigation case.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_NavigationRequest_GetNextPageUkmSourceId_Basic) {
  const GURL kUrl(embedded_test_server()->GetURL("/title1.html"));
  TestNavigationManager manager(web_contents(), kUrl);
  shell()->LoadURL(kUrl);

  EXPECT_TRUE(manager.WaitForRequestStart());
  ASSERT_TRUE(main_frame()->navigation_request());

  ukm::SourceId nav_request_id =
      main_frame()->navigation_request()->GetNextPageUkmSourceId();

  EXPECT_TRUE(manager.WaitForResponse());
  ASSERT_TRUE(manager.WaitForNavigationFinished());
  EXPECT_EQ(current_frame_host()->GetPageUkmSourceId(), nav_request_id);
}

// Test that NavigationRequest::GetNextPageUkmSourceId returns the eventual
// value of RenderFrameHost::GetPageUkmSourceId() --- child frame case.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_NavigationRequest_GetNextPageUkmSourceId_ChildFrame) {
  const GURL kUrl(
      embedded_test_server()->GetURL("/frame_tree/page_with_one_frame.html"));
  const GURL kDestUrl(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), kUrl));
  FrameTreeNode* subframe = main_frame()->child_at(0);
  ASSERT_TRUE(subframe);

  TestNavigationManager manager(web_contents(), kDestUrl);
  EXPECT_TRUE(
      ExecJs(subframe, JsReplace("location.href = $1", kDestUrl.spec())));
  EXPECT_TRUE(manager.WaitForRequestStart());
  ASSERT_TRUE(subframe->navigation_request());

  ukm::SourceId nav_request_id =
      subframe->navigation_request()->GetNextPageUkmSourceId();

  EXPECT_TRUE(manager.WaitForResponse());
  ASSERT_TRUE(manager.WaitForNavigationFinished());

  // Should have the same page UKM ID in navigation as page post commit, and as
  // the top-level frame.
  EXPECT_EQ(current_frame_host()->GetPageUkmSourceId(), nav_request_id);
  EXPECT_EQ(subframe->current_frame_host()->GetPageUkmSourceId(),
            nav_request_id);
}

// Test that NavigationRequest::GetNextPageUkmSourceId returns the eventual
// value of RenderFrameHost::GetPageUkmSourceId() --- same document navigation.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_NavigationRequest_GetNextPageUkmSourceId_SameDocument) {
  const GURL kUrl(embedded_test_server()->GetURL("/title1.html"));
  const GURL kFragment(kUrl.Resolve("#here"));
  EXPECT_TRUE(NavigateToURL(shell(), kUrl));

  NavigationHandleObserver handle_observer(web_contents(), kFragment);
  EXPECT_TRUE(
      ExecJs(main_frame(), JsReplace("location.href = $1", kFragment.spec())));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));

  EXPECT_TRUE(handle_observer.is_same_document());
  EXPECT_EQ(current_frame_host()->GetPageUkmSourceId(),
            handle_observer.next_page_ukm_source_id());
}

// Test that NavigationRequest::GetNextPageUkmSourceId returns the eventual
// value of RenderFrameHost::GetPageUkmSourceId() --- back navigation;
// this case matters because of back-forward cache.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_NavigationRequest_GetNextPageUkmSourceId_Back) {
  const GURL kUrl1(embedded_test_server()->GetURL("a.com", "/title1.html"));
  const GURL kUrl2(embedded_test_server()->GetURL("b.com", "/title2.html"));
  EXPECT_TRUE(NavigateToURL(shell(), kUrl1));
  EXPECT_TRUE(NavigateToURL(shell(), kUrl2));

  NavigationHandleObserver handle_observer(web_contents(), kUrl1);
  web_contents()->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(web_contents()));

  EXPECT_EQ(current_frame_host()->GetPageUkmSourceId(),
            handle_observer.next_page_ukm_source_id());
}

// Tests for cookies. Provides an HTTPS server.
class NavigationCookiesBrowserTest : public NavigationBaseBrowserTest {
 protected:
  NavigationCookiesBrowserTest() = default;
  net::EmbeddedTestServer* https_server() { return &https_server_; }

 private:
  void SetUpOnMainThread() override {
    NavigationBaseBrowserTest::SetUpOnMainThread();
    mock_cert_verifier_.mock_cert_verifier()->set_default_result(net::OK);
    https_server()->AddDefaultHandlers(GetTestDataFilePath());
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    NavigationBaseBrowserTest::SetUpCommandLine(command_line);
    mock_cert_verifier_.SetUpCommandLine(command_line);
  }

  void SetUpInProcessBrowserTestFixture() override {
    NavigationBaseBrowserTest::SetUpInProcessBrowserTestFixture();
    mock_cert_verifier_.SetUpInProcessBrowserTestFixture();
  }

  void TearDownInProcessBrowserTestFixture() override {
    NavigationBaseBrowserTest::TearDownInProcessBrowserTestFixture();
    mock_cert_verifier_.TearDownInProcessBrowserTestFixture();
  }

  content::ContentMockCertVerifier mock_cert_verifier_;
  net::EmbeddedTestServer https_server_{net::EmbeddedTestServer::TYPE_HTTPS};
};

// Test how cookies are inherited in about:srcdoc iframes.
//
// Regression test: https://crbug.com/1003167.
// TODO: b/432532805 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationCookiesBrowserTest,
                       DISABLED_CookiesInheritedSrcDoc) {
  using Response = net::test_server::ControllableHttpResponse;
  Response response_1(https_server(), "/response_1");
  Response response_2(https_server(), "/response_2");
  Response response_3(https_server(), "/response_3");

  ASSERT_TRUE(https_server()->Start());

  GURL url_a(https_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.com", "/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_a));

  EXPECT_TRUE(ExecJs(shell(), R"(
    let iframe = document.createElement("iframe");
    iframe.srcdoc = "foo";
    document.body.appendChild(iframe);
  )"));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));

  RenderFrameHostImpl* main_document = current_frame_host();
  RenderFrameHostImpl* sub_document_1 =
      main_document->child_at(0)->current_frame_host();
  EXPECT_EQ(url::kAboutSrcdocURL, sub_document_1->GetLastCommittedURL());
  EXPECT_EQ(url::Origin::Create(url_a),
            sub_document_1->GetLastCommittedOrigin());
  EXPECT_EQ(main_document->GetSiteInstance(),
            sub_document_1->GetSiteInstance());

  // 0. The default state doesn't contain any cookies.
  EXPECT_EQ("", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("", EvalJs(sub_document_1, "document.cookie"));

  // 1. Set a cookie in the main document, it affects its child too.
  EXPECT_TRUE(ExecJs(main_document, "document.cookie = 'a=0';"));

  EXPECT_EQ("a=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0", EvalJs(sub_document_1, "document.cookie"));

  // 2. Set a cookie in the child, it affects its parent too.
  EXPECT_TRUE(ExecJs(sub_document_1, "document.cookie = 'b=0';"));

  EXPECT_EQ("a=0; b=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0; b=0", EvalJs(sub_document_1, "document.cookie"));

  // 3. Checks cookies are sent while requesting resources.
  ExecuteScriptAsync(sub_document_1, "fetch('/response_1');");
  response_1.WaitForRequest();
  EXPECT_EQ("a=0; b=0", response_1.http_request()->headers.at("Cookie"));

  // 4. Navigate the iframe elsewhere.
  EXPECT_TRUE(ExecJs(sub_document_1, JsReplace("location.href = $1", url_b)));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  RenderFrameHostImpl* sub_document_2 =
      main_document->child_at(0)->current_frame_host();

  EXPECT_EQ("a=0; b=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("", EvalJs(sub_document_2, "document.cookie"));

  // 5. Set a cookie in the main document. It doesn't affect its child.
  EXPECT_TRUE(ExecJs(main_document, "document.cookie = 'c=0';"));

  EXPECT_EQ("a=0; b=0; c=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("", EvalJs(sub_document_2, "document.cookie"));

  // 6. Set a cookie in the child. It doesn't affect its parent.
  EXPECT_TRUE(ExecJs(sub_document_2,
                     "document.cookie = 'd=0; SameSite=none; Secure';"));

  EXPECT_EQ("a=0; b=0; c=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("d=0", EvalJs(sub_document_2, "document.cookie"));

  // 7. Checks cookies are sent while requesting resources.
  ExecuteScriptAsync(sub_document_2, "fetch('/response_2');");
  response_2.WaitForRequest();
  EXPECT_EQ("d=0", response_2.http_request()->headers.at("Cookie"));

  // 8. Navigate the iframe back to about:srcdoc.
  web_contents()->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  RenderFrameHostImpl* sub_document_3 =
      main_document->child_at(0)->current_frame_host();
  EXPECT_EQ(url_a, main_document->GetLastCommittedURL());
  EXPECT_EQ(url::kAboutSrcdocURL, sub_document_3->GetLastCommittedURL());
  EXPECT_EQ(url::Origin::Create(url_a),
            sub_document_3->GetLastCommittedOrigin());
  EXPECT_EQ(main_document->GetSiteInstance(),
            sub_document_3->GetSiteInstance());

  EXPECT_EQ("a=0; b=0; c=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0; b=0; c=0", EvalJs(sub_document_3, "document.cookie"));

  // 9. Set cookie in the main document. It should be inherited by the child.
  EXPECT_TRUE(ExecJs(main_document, "document.cookie = 'e=0';"));

  EXPECT_EQ("a=0; b=0; c=0; e=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0; b=0; c=0; e=0", EvalJs(sub_document_3, "document.cookie"));

  // 11. Set cookie in the child document. It should be reflected on its parent.
  EXPECT_TRUE(ExecJs(sub_document_3, "document.cookie = 'f=0';"));

  EXPECT_EQ("a=0; b=0; c=0; e=0; f=0",
            EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0; b=0; c=0; e=0; f=0",
            EvalJs(sub_document_3, "document.cookie"));

  // 12. Checks cookies are sent while requesting resources.
  ExecuteScriptAsync(sub_document_3, "fetch('/response_3');");
  response_3.WaitForRequest();
  EXPECT_EQ("a=0; b=0; c=0; e=0; f=0",
            response_3.http_request()->headers.at("Cookie"));
}

// Test how cookies are inherited in about:blank iframes.
IN_PROC_BROWSER_TEST_F(NavigationCookiesBrowserTest,
                       CookiesInheritedAboutBlank) {
  // This test expects several cross-site navigation to happen.
  if (!AreAllSitesIsolatedForTesting()) {
    return;
  }

  using Response = net::test_server::ControllableHttpResponse;
  Response response_1(https_server(), "/response_1");
  Response response_2(https_server(), "/response_2");
  Response response_3(https_server(), "/response_3");

  ASSERT_TRUE(https_server()->Start());

  GURL url_a(https_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.com", "/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_a));

  EXPECT_TRUE(
      ExecJs(shell(), JsReplace("let iframe = document.createElement('iframe');"
                                "iframe.src = $1;"
                                "document.body.appendChild(iframe);",
                                url_b)));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  EXPECT_TRUE(ExecJs(shell(), R"(
    document.querySelector('iframe').src = "about:blank"
  )"));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));

  RenderFrameHostImpl* main_document = current_frame_host();
  RenderFrameHostImpl* sub_document_1 =
      main_document->child_at(0)->current_frame_host();

  EXPECT_EQ(url::kAboutBlankURL, sub_document_1->GetLastCommittedURL());
  EXPECT_EQ(url::Origin::Create(url_a),
            sub_document_1->GetLastCommittedOrigin());
  EXPECT_EQ(main_document->GetSiteInstance(),
            sub_document_1->GetSiteInstance());

  // 0. The default state doesn't contain any cookies.
  EXPECT_EQ("", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("", EvalJs(sub_document_1, "document.cookie"));

  // 1. Set a cookie in the main document, it affects its child too.
  EXPECT_TRUE(ExecJs(main_document, "document.cookie = 'a=0';"));

  EXPECT_EQ("a=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0", EvalJs(sub_document_1, "document.cookie"));

  // 2. Set a cookie in the child, it affects its parent too.
  EXPECT_TRUE(ExecJs(sub_document_1, "document.cookie = 'b=0';"));

  EXPECT_EQ("a=0; b=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0; b=0", EvalJs(sub_document_1, "document.cookie"));

  // 3. Checks cookies are sent while requesting resources.
  GURL url_response_1 = https_server()->GetURL("a.com", "/response_1");
  ExecuteScriptAsync(sub_document_1, JsReplace("fetch($1)", url_response_1));
  response_1.WaitForRequest();
  EXPECT_EQ("a=0; b=0", response_1.http_request()->headers.at("Cookie"));

  // 4. Navigate the iframe elsewhere.
  EXPECT_TRUE(ExecJs(sub_document_1, JsReplace("location.href = $1", url_b)));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  RenderFrameHostImpl* sub_document_2 =
      main_document->child_at(0)->current_frame_host();

  EXPECT_EQ("a=0; b=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("", EvalJs(sub_document_2, "document.cookie"));

  // 5. Set a cookie in the main document. It doesn't affect its child.
  EXPECT_TRUE(ExecJs(main_document, "document.cookie = 'c=0';"));

  EXPECT_EQ("a=0; b=0; c=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("", EvalJs(sub_document_2, "document.cookie"));

  // 6. Set a cookie in the child. It doesn't affect its parent.
  EXPECT_TRUE(ExecJs(sub_document_2,
                     "document.cookie = 'd=0; SameSite=none; Secure';"));

  EXPECT_EQ("a=0; b=0; c=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("d=0", EvalJs(sub_document_2, "document.cookie"));

  // 7. Checks cookies are sent while requesting resources.
  ExecuteScriptAsync(sub_document_2, "fetch('/response_2');");
  response_2.WaitForRequest();
  EXPECT_EQ("d=0", response_2.http_request()->headers.at("Cookie"));

  // 8. Navigate the iframe back to about:blank.
  web_contents()->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  RenderFrameHostImpl* sub_document_3 =
      main_document->child_at(0)->current_frame_host();
  EXPECT_EQ(url_a, main_document->GetLastCommittedURL());
  EXPECT_EQ(url::kAboutBlankURL, sub_document_3->GetLastCommittedURL());
  EXPECT_EQ(url::Origin::Create(url_a),
            sub_document_3->GetLastCommittedOrigin());
  EXPECT_EQ(main_document->GetSiteInstance(),
            sub_document_3->GetSiteInstance());

  EXPECT_EQ("a=0; b=0; c=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0; b=0; c=0", EvalJs(sub_document_3, "document.cookie"));

  // 9. Set cookie in the main document. It affects the iframe.
  EXPECT_TRUE(ExecJs(main_document, "document.cookie = 'e=0';"));

  EXPECT_EQ("a=0; b=0; c=0; e=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0; b=0; c=0; e=0", EvalJs(sub_document_3, "document.cookie"));

  // 10. Set cookie in the iframe. It affects the main frame.
  EXPECT_TRUE(ExecJs(sub_document_3, "document.cookie = 'f=0';"));
  EXPECT_EQ("a=0; b=0; c=0; e=0; f=0",
            EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0; b=0; c=0; e=0; f=0",
            EvalJs(sub_document_3, "document.cookie"));

  // 11. Even if document.cookie is empty, cookies are sent.
  ExecuteScriptAsync(sub_document_3, "fetch('/response_3');");
  response_3.WaitForRequest();
  EXPECT_EQ("a=0; b=0; c=0; e=0; f=0",
            response_3.http_request()->headers.at("Cookie"));
}

// Test how cookies are inherited in about:blank iframes.
//
// This is a variation of
// NavigationCookiesBrowserTest.CookiesInheritedAboutBlank. Instead of
// requesting an history navigation, a new navigation is requested from the main
// frame. The navigation is cross-site instead of being same-site.
IN_PROC_BROWSER_TEST_F(NavigationCookiesBrowserTest,
                       CookiesInheritedAboutBlank2) {
  // This test expects several cross-site navigation to happen.
  if (!AreAllSitesIsolatedForTesting()) {
    return;
  }

  using Response = net::test_server::ControllableHttpResponse;
  Response response_1(https_server(), "/response_1");
  Response response_2(https_server(), "/response_2");
  Response response_3(https_server(), "/response_3");

  ASSERT_TRUE(https_server()->Start());

  GURL url_a(https_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.com", "/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_a));

  EXPECT_TRUE(
      ExecJs(shell(), JsReplace("let iframe = document.createElement('iframe');"
                                "iframe.src = $1;"
                                "document.body.appendChild(iframe);",
                                url_b)));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  EXPECT_TRUE(ExecJs(shell(), R"(
    document.querySelector('iframe').src = "about:blank"
  )"));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));

  RenderFrameHostImpl* main_document = current_frame_host();
  RenderFrameHostImpl* sub_document_1 =
      main_document->child_at(0)->current_frame_host();
  EXPECT_EQ(url::kAboutBlankURL, sub_document_1->GetLastCommittedURL());
  EXPECT_EQ(url::Origin::Create(url_a),
            sub_document_1->GetLastCommittedOrigin());
  EXPECT_EQ(main_document->GetSiteInstance(),
            sub_document_1->GetSiteInstance());

  // 0. The default state doesn't contain any cookies.
  EXPECT_EQ("", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("", EvalJs(sub_document_1, "document.cookie"));

  // 1. Set a cookie in the main document, it affects its child too.
  EXPECT_TRUE(ExecJs(main_document, "document.cookie = 'a=0';"));

  EXPECT_EQ("a=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0", EvalJs(sub_document_1, "document.cookie"));

  // 2. Set a cookie in the child, it affects its parent too.
  EXPECT_TRUE(ExecJs(sub_document_1, "document.cookie = 'b=0';"));

  EXPECT_EQ("a=0; b=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0; b=0", EvalJs(sub_document_1, "document.cookie"));

  // 3. Checks cookies are sent while requesting resources.
  ExecuteScriptAsync(sub_document_1, "fetch('/response_1');");
  response_1.WaitForRequest();
  EXPECT_EQ("a=0; b=0", response_1.http_request()->headers.at("Cookie"));

  // 4. Navigate the iframe elsewhere.
  EXPECT_TRUE(ExecJs(sub_document_1, JsReplace("location.href = $1", url_b)));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  RenderFrameHostImpl* sub_document_2 =
      main_document->child_at(0)->current_frame_host();

  EXPECT_EQ("a=0; b=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("", EvalJs(sub_document_2, "document.cookie"));

  // 5. Set a cookie in the main document. It doesn't affect its child.
  EXPECT_TRUE(ExecJs(main_document, "document.cookie = 'c=0';"));

  EXPECT_EQ("a=0; b=0; c=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("", EvalJs(sub_document_2, "document.cookie"));

  // 6. Set a cookie in the child. It doesn't affect its parent.
  EXPECT_TRUE(ExecJs(sub_document_2,
                     "document.cookie = 'd=0; SameSite=none; Secure';"));

  EXPECT_EQ("a=0; b=0; c=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("d=0", EvalJs(sub_document_2, "document.cookie"));

  // 7. Checks cookies are sent while requesting resources.
  ExecuteScriptAsync(sub_document_2, "fetch('/response_2');");
  response_2.WaitForRequest();
  EXPECT_EQ("d=0", response_2.http_request()->headers.at("Cookie"));

  // 8. Ask the top-level, a.com frame to navigate the subframe to about:blank.
  EXPECT_TRUE(ExecJs(shell(), R"(
    document.querySelector('iframe').src = "about:blank";
  )"));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  RenderFrameHostImpl* sub_document_3 =
      main_document->child_at(0)->current_frame_host();
  EXPECT_EQ(url::kAboutBlankURL, sub_document_3->GetLastCommittedURL());
  EXPECT_EQ(url::Origin::Create(url_a),
            sub_document_3->GetLastCommittedOrigin());
  EXPECT_EQ(main_document->GetSiteInstance(),
            sub_document_3->GetSiteInstance());

  EXPECT_EQ("a=0; b=0; c=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0; b=0; c=0", EvalJs(sub_document_3, "document.cookie"));

  // 9. Set cookie in the main document.
  EXPECT_TRUE(ExecJs(main_document, "document.cookie = 'e=0';"));

  EXPECT_EQ("a=0; b=0; c=0; e=0", EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0; b=0; c=0; e=0", EvalJs(sub_document_3, "document.cookie"));

  // 10. Set cookie in the child document.
  EXPECT_TRUE(ExecJs(sub_document_3, "document.cookie = 'f=0';"));

  EXPECT_EQ("a=0; b=0; c=0; e=0; f=0",
            EvalJs(main_document, "document.cookie"));
  EXPECT_EQ("a=0; b=0; c=0; e=0; f=0",
            EvalJs(sub_document_3, "document.cookie"));

  // 11. Checks cookies are sent while requesting resources.
  ExecuteScriptAsync(sub_document_3, "fetch('/response_3');");
  response_3.WaitForRequest();
  EXPECT_EQ("a=0; b=0; c=0; e=0; f=0",
            response_3.http_request()->headers.at("Cookie"));
}

// Test how cookies are inherited in data-URL iframes.
// TODO: b/432532805 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationCookiesBrowserTest,
                       DISABLED_CookiesInheritedDataUrl) {
  using Response = net::test_server::ControllableHttpResponse;
  Response response_1(https_server(), "/response_1");
  Response response_2(https_server(), "/response_2");
  Response response_3(https_server(), "/response_3");

  ASSERT_TRUE(https_server()->Start());

  GURL url_a(https_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.com", "/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_a));

  EXPECT_TRUE(ExecJs(shell(), R"(
    let iframe = document.createElement("iframe");
    iframe.src = "data:text/html,";
    document.body.appendChild(iframe);
  )"));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));

  RenderFrameHostImpl* main_document = current_frame_host();
  RenderFrameHostImpl* sub_document_1 =
      main_document->child_at(0)->current_frame_host();
  EXPECT_EQ("data:text/html,", sub_document_1->GetLastCommittedURL());
  EXPECT_TRUE(sub_document_1->GetLastCommittedOrigin().opaque());
  EXPECT_EQ(main_document->GetSiteInstance(),
            sub_document_1->GetSiteInstance());

  // 1. Writing a cookie inside a data-URL document is forbidden.
  {
    WebContentsConsoleObserver console_observer(web_contents());
    console_observer.SetPattern(
        "*Failed to set the 'cookie' property on 'Document': Cookies are "
        "disabled inside 'data:' URLs.*");
    ExecuteScriptAsync(sub_document_1, "document.cookie = 'a=0';");
    ASSERT_TRUE(console_observer.Wait());
  }

  // 2. Reading a cookie inside a data-URL document is forbidden.
  {
    WebContentsConsoleObserver console_observer(web_contents());
    console_observer.SetPattern(
        "*Failed to read the 'cookie' property from 'Document': Cookies are "
        "disabled inside 'data:' URLs.*");
    ExecuteScriptAsync(sub_document_1, "document.cookie");
    ASSERT_TRUE(console_observer.Wait());
  }

  // 3. Set cookie in the main document. No cookies are sent when requested from
  // the data-URL.
  EXPECT_TRUE(ExecJs(main_document, "document.cookie = 'a=0;SameSite=Lax'"));
  EXPECT_TRUE(ExecJs(main_document, "document.cookie = 'b=0;SameSite=Strict'"));
  GURL url_response_1 = https_server()->GetURL("a.com", "/response_1");
  ExecuteScriptAsync(sub_document_1, JsReplace("fetch($1)", url_response_1));
  response_1.WaitForRequest();
  EXPECT_EQ(0u, response_1.http_request()->headers.count("Cookie"));

  // 4. Navigate the iframe elsewhere and back using history navigation.
  EXPECT_TRUE(ExecJs(sub_document_1, JsReplace("location.href = $1", url_b)));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  web_contents()->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  RenderFrameHostImpl* sub_document_2 =
      main_document->child_at(0)->current_frame_host();
  EXPECT_EQ(url_a, main_document->GetLastCommittedURL());
  EXPECT_EQ("data:text/html,", sub_document_2->GetLastCommittedURL());
  EXPECT_TRUE(sub_document_2->GetLastCommittedOrigin().opaque());
  EXPECT_EQ(main_document->GetSiteInstance(),
            sub_document_2->GetSiteInstance());

  // 5. Writing a cookie inside a data-URL document is still forbidden.
  {
    WebContentsConsoleObserver console_observer(web_contents());
    console_observer.SetPattern(
        "*Failed to set the 'cookie' property on 'Document': Cookies are "
        "disabled inside 'data:' URLs.*");
    ExecuteScriptAsync(sub_document_2, "document.cookie = 'c=0';");
    ASSERT_TRUE(console_observer.Wait());
  }

  // 6. Reading a cookie inside a data-URL document is still forbidden.
  {
    WebContentsConsoleObserver console_observer(web_contents());
    console_observer.SetPattern(
        "*Failed to read the 'cookie' property from 'Document': Cookies are "
        "disabled inside 'data:' URLs.*");
    ExecuteScriptAsync(sub_document_2, "document.cookie");
    ASSERT_TRUE(console_observer.Wait());
  }

  // 7. No cookies are sent when requested from the data-URL.
  GURL url_response_2 = https_server()->GetURL("a.com", "/response_2");
  ExecuteScriptAsync(sub_document_2, JsReplace("fetch($1)", url_response_2));
  response_2.WaitForRequest();
  EXPECT_EQ(0u, response_2.http_request()->headers.count("Cookie"));
}

// Tests for validating URL rewriting behavior like chrome://history to
// chrome-native://history.
class NavigationUrlRewriteBrowserTest : public NavigationBaseBrowserTest {
 protected:
  static constexpr const char* kRewriteURL = "http://a.com/rewrite";
  static constexpr const char* kNoAccessScheme = "no-access";
  static constexpr const char* kNoAccessURL = "no-access://testing/";

  class BrowserClient : public ContentBrowserTestContentBrowserClient {
   public:
    void BrowserURLHandlerCreated(BrowserURLHandler* handler) override {
      handler->AddHandlerPair(RewriteUrl,
                              BrowserURLHandlerImpl::null_handler());
      fake_url_loader_factory_ = std::make_unique<FakeNetworkURLLoaderFactory>(
          "HTTP/1.1 200 OK\nContent-Type: text/html\n\n", "This is a test",
          /* network_accessed */ true, net::OK);
    }

    void RegisterNonNetworkNavigationURLLoaderFactories(
        int frame_tree_node_id,
        ukm::SourceIdObj ukm_source_id,
        NonNetworkURLLoaderFactoryMap* factories) override {
      mojo::PendingRemote<network::mojom::URLLoaderFactory> pending_remote;
      fake_url_loader_factory_->Clone(
          pending_remote.InitWithNewPipeAndPassReceiver());
      factories->emplace(std::string(kNoAccessScheme),
                         std::move(pending_remote));
    }

    static bool RewriteUrl(GURL* url, BrowserContext* browser_context) {
      if (*url == GURL(kRewriteURL)) {
        *url = GURL(kNoAccessURL);
        return true;
      }
      return false;
    }

   private:
    std::unique_ptr<FakeNetworkURLLoaderFactory> fake_url_loader_factory_;
  };

  NavigationUrlRewriteBrowserTest() {
    url::AddStandardScheme(kNoAccessScheme, url::SCHEME_WITH_HOST);
    url::AddNoAccessScheme(kNoAccessScheme);

    // This test needs to use an unassigned SiteInstance for kNoAccessScheme,
    // which requires adding it as an empty document scheme.
    url::AddEmptyDocumentScheme(kNoAccessScheme);
  }

  void SetUpOnMainThread() override {
    NavigationBaseBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());

    browser_client_ = std::make_unique<BrowserClient>();
  }

  void TearDownOnMainThread() override {
    browser_client_.reset();

    NavigationBaseBrowserTest::TearDownOnMainThread();
  }

  GURL GetRewriteToNoAccessURL() const { return GURL(kRewriteURL); }

 private:
  std::unique_ptr<BrowserClient> browser_client_;
  url::ScopedSchemeRegistryForTests scoped_registry_;
};

// Tests navigating to a URL that gets rewritten to a "no access" URL. This
// mimics the behavior of navigating to special URLs like chrome://newtab and
// chrome://history which get rewritten to "no access" chrome-native:// URLs.
// TODO: b/432532306 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationUrlRewriteBrowserTest,
                       DISABLED_RewriteToNoAccess) {
  // Perform an initial navigation.
  {
    TestNavigationObserver observer(web_contents());
    GURL url = embedded_test_server()->GetURL("a.com", "/title1.html");
    EXPECT_TRUE(NavigateToURL(shell(), url));
    EXPECT_EQ(url, observer.last_navigation_url());
    EXPECT_TRUE(observer.last_navigation_succeeded());
    EXPECT_FALSE(observer.last_initiator_origin().has_value());
  }

  // Navigate to the URL that will get rewritten to a "no access" URL.
  {
    TestNavigationObserver observer(web_contents());

    // Note: We are using LoadURLParams here because we need to have the
    // initiator_origin set and NavigateToURL() does not do that.
    NavigationController::LoadURLParams params(GetRewriteToNoAccessURL());
    params.initiator_origin = current_frame_host()->GetLastCommittedOrigin();
    web_contents()->GetController().LoadURLWithParams(params);
    web_contents()->Focus();
    observer.Wait();

    EXPECT_EQ(GURL(kNoAccessURL), observer.last_navigation_url());
    EXPECT_TRUE(observer.last_navigation_succeeded());
    EXPECT_TRUE(observer.last_initiator_origin().has_value());
  }
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest, DISABLED_SameDocumentNavigation) {
  WebContents* wc = shell()->web_contents();
  GURL url1 = embedded_test_server()->GetURL("a.com", "/title1.html#frag1");
  GURL url2 = embedded_test_server()->GetURL("a.com", "/title1.html#frag2");
  NavigationHandleCommitObserver navigation_0(wc, url1);
  NavigationHandleCommitObserver navigation_1(wc, url2);

  EXPECT_TRUE(NavigateToURL(shell(), url1));
  NavigationEntry* entry =
      web_contents()->GetController().GetLastCommittedEntry();
  EXPECT_TRUE(NavigateToURL(shell(), url2));
  // The NavigationEntry changes on a same-document navigation.
  EXPECT_NE(web_contents()->GetController().GetLastCommittedEntry(), entry);

  EXPECT_TRUE(navigation_0.has_committed());
  EXPECT_TRUE(navigation_1.has_committed());
  EXPECT_FALSE(navigation_0.was_same_document());
  EXPECT_TRUE(navigation_1.was_same_document());
}

// Some navigations are not allowed, such as when they fail the content security
// policy, or for trying to load about:srcdoc in the main frame. These result in
// us redirecting the navigation to an error page via
// RenderFrameHostImpl::FailedNavigation().
// Repeating the request with a different URL fragment results in attempting a
// same-document navigation, but error pages do not support such navigations. In
// this case treat each failed navigation request as a separate load, with the
// resulting navigation being performed as a cross-document navigation. This is
// regression test for https://crbug.com/1018385.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_SameDocumentNavigationOnBlockedPage) {
  GURL url1("about:srcdoc#0");
  GURL url2("about:srcdoc#1");
  NavigationHandleCommitObserver navigation_0(web_contents(), url1);
  NavigationHandleCommitObserver navigation_1(web_contents(), url2);

  // Big warning: about:srcdoc is not supposed to be valid browser-initiated
  // main-frame navigation, it is currently blocked by the NavigationRequest.
  // It is used here to reproduce bug https://crbug.com/1018385. Please avoid
  // copying this kind of navigation in your own tests.
  EXPECT_FALSE(NavigateToURL(shell(), url1));
  EXPECT_FALSE(NavigateToURL(shell(), url2));

  EXPECT_TRUE(navigation_0.has_committed());
  EXPECT_TRUE(navigation_1.has_committed());
  EXPECT_FALSE(navigation_0.was_same_document());
  EXPECT_FALSE(navigation_1.was_same_document());
}

// This navigation is allowed by the browser, but the network will not be able
// to connect to the site, so the NavigationRequest fails on the browser side
// and is redirected to an error page. Performing another navigation should
// make the full attempt again, in case the network request succeeds this time.
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       SameDocumentNavigationOnBadServerErrorPage) {
  GURL url1("http://badserver.com:9/");
  GURL url2("http://badserver.com:9/#1");
  NavigationHandleCommitObserver navigation_0(web_contents(), url1);
  NavigationHandleCommitObserver navigation_1(web_contents(), url2);

  // The navigation is okay from the browser's perspective, so NavigateToURL()
  // will return true. But the network request ultimately fails, so the request
  // is redirected to an error page.
  EXPECT_FALSE(NavigateToURL(shell(), url1));
  EXPECT_TRUE(navigation_0.has_committed());
  EXPECT_FALSE(navigation_0.was_same_document());

  // The 2nd request shares a URL but it should be another cross-document
  // navigation, rather than trying to navigate inside the error page.
  EXPECT_FALSE(NavigateToURL(shell(), url2));
  EXPECT_TRUE(navigation_1.has_committed());
  EXPECT_FALSE(navigation_1.was_same_document());
}

// This navigation is allowed by the browser, and the request to the server is
// successful, but it returns 404 error headers, and (optionally) an error page.
// When another request is made for the same page but with a different fragment,
// the browser will attempt to perform a same-document navigation but that
// navigation is intended for the actual document not the error page that has
// been loaded instead. A same-document navigation in the renderer-loaded error
// page should be performed as a cross-document navigation in order to attempt
// to reload the page.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_SameDocumentNavigationOn404ErrorPage) {
  // This case is a non-empty 404 page. It makes different choices about where
  // to load the page on a same-document navigation.
  {
    GURL url1 = embedded_test_server()->GetURL("a.com", "/page404.html");
    GURL url2 = embedded_test_server()->GetURL("a.com", "/page404.html#1");
    NavigationHandleCommitObserver navigation_0(web_contents(), url1);
    NavigationHandleCommitObserver navigation_1(web_contents(), url2);

    EXPECT_TRUE(NavigateToURL(shell(), url1));
    EXPECT_TRUE(navigation_0.has_committed());
    EXPECT_FALSE(navigation_0.was_same_document());

    // This is another navigation to the non-existent URL, but with a different
    // fragment. We have successfully loaded content from a.com. The fact that
    // it is 404 response does not mean it is an error page, since the term
    // "error page" is used for cases where the browser encounters an error
    // loading a document from the origin. HTTP responses with >400 status codes
    // are just like regular documents from the origin and we render their
    // response body just like we would a 200 response. This is why it can make
    // sense for a same document navigation to be performed from a 404 page.
    EXPECT_TRUE(NavigateToURL(shell(), url2));
    EXPECT_TRUE(navigation_1.has_committed());
    EXPECT_TRUE(navigation_1.was_same_document());
  }
  // This case is an empty 404 page. It makes different choices about where
  // to load the page on a same-document navigation. Since the server has only
  // replied with an error, the browser will display its own error page and
  // therefore it is not one coming from the server's origin.
  {
    GURL url1 = embedded_test_server()->GetURL("a.com", "/empty404.html");
    GURL url2 = embedded_test_server()->GetURL("a.com", "/empty404.html#1");
    NavigationHandleCommitObserver navigation_0(web_contents(), url1);
    NavigationHandleCommitObserver navigation_1(web_contents(), url2);

    EXPECT_FALSE(NavigateToURL(shell(), url1));
    EXPECT_TRUE(navigation_0.has_committed());
    EXPECT_FALSE(navigation_0.was_same_document());

    // This is another navigation to the non-existent URL, but with a different
    // fragment. Since we did not load a document from the server (we got
    // `false` from `NavigateToURL()`) there is no server-provided document to
    // navigate within. The result should be a cross-document navigation in
    // order to attempt to load the document at the given path from the server
    // again.
    EXPECT_FALSE(NavigateToURL(shell(), url2));
    EXPECT_TRUE(navigation_1.has_committed());
    EXPECT_FALSE(navigation_1.was_same_document());
  }

  // This case is also an empty 404 page, but we do replaceState and pushState
  // afterwards, creating successful same-document navigations.
  {
    // Navigate to empty 404, committing an error page.
    GURL url1 = embedded_test_server()->GetURL("a.com", "/empty404.html");
    NavigationHandleCommitObserver navigation(web_contents(), url1);
    EXPECT_FALSE(NavigateToURL(shell(), url1));
    EXPECT_TRUE(navigation.has_committed());
    EXPECT_FALSE(navigation.was_same_document());

    // replaceState on an error page, without changing the URL.
    {
      FrameNavigateParamsCapturer capturer(main_frame());
      capturer.set_wait_for_load(false);
      EXPECT_TRUE(ExecJs(shell(), "history.replaceState('foo', '')"));
      capturer.Wait();
      EXPECT_TRUE(capturer.is_same_document());
    }

    // pushState on an error page, without changing the URL.
    {
      FrameNavigateParamsCapturer capturer(main_frame());
      capturer.set_wait_for_load(false);
      EXPECT_TRUE(ExecJs(shell(), "history.pushState('foo', '')"));
      capturer.Wait();
      EXPECT_TRUE(capturer.is_same_document());
    }
  }
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_SameDocumentNavigationFromCrossDocumentRedirect) {
  WebContents* wc = shell()->web_contents();
  GURL url0 = embedded_test_server()->GetURL("/title1.html#frag1");
  GURL url1 =
      embedded_test_server()->GetURL("/server-redirect?title1.html#frag2");
  GURL url2 = embedded_test_server()->GetURL("/title1.html#frag2");
  NavigationHandleCommitObserver navigation_0(wc, url0);
  NavigationHandleCommitObserver navigation_1(wc, url1);
  NavigationHandleCommitObserver navigation_2(wc, url2);

  EXPECT_TRUE(NavigateToURL(shell(), url0));
  // Since the redirect does not land at the URL we passed in, we get a false
  // return here.
  EXPECT_FALSE(NavigateToURL(shell(), url1));

  // The navigation to |url1| is redirected and so |url1| does not commit. Then
  // the resulting navigation to |url2| lands at the same document URL as |url0|
  // which would be a same-document navigation if there wasn't a redirect
  // involved. But since it started as a cross-document navigation it results in
  // loading a new document instead of doing a same-document navigation.
  EXPECT_TRUE(navigation_0.has_committed());
  EXPECT_FALSE(navigation_1.has_committed());
  EXPECT_TRUE(navigation_2.has_committed());
  EXPECT_FALSE(navigation_0.was_same_document());
  EXPECT_FALSE(navigation_1.was_same_document());
  EXPECT_FALSE(navigation_2.was_same_document());

  EXPECT_EQ(wc->GetPrimaryMainFrame()->GetLastCommittedURL(), url2);

  // Redirect should not record a ReceivedResponse event.
  EXPECT_EQ(1u, test_ukm_recorder()
                    .GetEntriesByName("Navigation.ReceivedResponse")
                    .size());
}

// 1. The browser navigates to a.html.
// 2. The renderer uses history.pushState() to change the URL of the current
//    document from a.html to b.html.
// 3. The browser tries to perform a same-document navigation to a.html#foo,
//    since it did not hear about the document's URL changing yet. When it gets
//    to the renderer, we discover a race has happened.
// 4. Meanwhile, the browser hears about the URL change to b.html and applies
//    it.
// Now - how do we resolve the race?
// 5. We will reorder the a.html#foo navigation to start over in the browser
//    after the b.html navigation.
// Technically, this is still a same-document navigation! The URL changed but
// the document did not. Currently, however, the browser only considers the URL
// when performing a non-history navigation to decide if it's a same-document
// navigation, so..
// 6. The browser will perform a cross-document navigation to a.html#foo.
//
// TODO(https://crbug.com/1262032): Test is flaky on various platforms.

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_SameDocumentNavigationRacesPushStateURLChange) {
  WebContents* wc = shell()->web_contents();
  GURL url0 = embedded_test_server()->GetURL("/title1.html");
  GURL url1 = embedded_test_server()->GetURL("/title2.html");
  GURL url2 = embedded_test_server()->GetURL("/title1.html#frag2");
  NavigationHandleCommitObserver navigation_0(wc, url0);
  NavigationHandleCommitObserver navigation_1(wc, url1);
  NavigationHandleCommitObserver navigation_2(wc, url2);

  // Start at `url0`.
  EXPECT_TRUE(NavigateToURL(shell(), url0));

  // Have the renderer `history.pushState()` to `url1`, which leaves it on the
  // `url0` document, but with a different URL now.
  ExecuteScriptAsync(shell(), JsReplace("history.pushState('', '', $1);"
                                        "window.location.href == $1;",
                                        url1));

  // The browser didn't hear about the change yet.
  EXPECT_EQ(wc->GetPrimaryMainFrame()->GetLastCommittedURL(), url0);

  {
    // We will wait for 2 navigations: one will be the pushState() and the other
    // will be the navigation to `url2` started below.
    TestNavigationObserver nav_observer(wc, 2);

    // Start a same-document navigation to url2 that is racing with the
    // renderer's history.pushState().
    shell()->LoadURL(url2);

    nav_observer.Wait();
  }

  // The last navigation to resolve is the one to `url2` as it's reordered to
  // come after the race with the already-completed history.pushState().
  EXPECT_EQ(wc->GetPrimaryMainFrame()->GetLastCommittedURL(), url2);

  // Navigation 0 was a cross-document navigation, to initially load the
  // document.
  EXPECT_TRUE(navigation_0.has_committed());
  EXPECT_FALSE(navigation_0.was_same_document());

  // Navigation 1 was a same-document navigation, from the renderer's
  // history.pushState() call.
  EXPECT_TRUE(navigation_1.has_committed());
  EXPECT_TRUE(navigation_1.was_same_document());

  // Navigation 2 was restarted and came after. When it restarted, it saw the
  // URL did not match and did a cross-document navigation. Technically the same
  // document was still loaded from `url0`, but the browser makes its choice
  // on the document's current URL.
  EXPECT_TRUE(navigation_2.has_committed());
  EXPECT_FALSE(navigation_2.was_same_document());
}

class GetEffectiveUrlClient : public ContentBrowserTestContentBrowserClient {
 public:
  GURL GetEffectiveURL(content::BrowserContext* browser_context,
                       const GURL& url) override {
    if (effective_url_) {
      return *effective_url_;
    }
    return url;
  }

  bool IsSuitableHost(RenderProcessHost* process_host,
                      const GURL& site_url) override {
    if (!disallowed_process_id_) {
      return true;
    }
    return process_host->GetID() != disallowed_process_id_;
  }

  void set_effective_url(const GURL& url) { effective_url_ = url; }

  void set_disallowed_process(int id) { disallowed_process_id_ = id; }

 private:
  absl::optional<GURL> effective_url_;
  int disallowed_process_id_ = 0;
};

// While a document is open, state in the browser may change such that loading
// the document would choose a different SiteInstance. A cross-document
// navigation would pick up this different SiteInstance, but a same-document
// navigation should not. It should just navigate inside the currently loaded
// document instead of reloading the document.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_SameDocumentNavigationWhenSiteInstanceWouldChange) {
  auto* wc = static_cast<WebContentsImpl*>(shell()->web_contents());
  GURL url0 = embedded_test_server()->GetURL("a.com", "/title1.html#ref1");
  GURL url1 = embedded_test_server()->GetURL("a.com", "/title1.html#ref2");

  GetEffectiveUrlClient new_client;

  NavigationHandleCommitObserver navigation_0(wc, url0);
  EXPECT_TRUE(NavigateToURL(shell(), url0));
  EXPECT_TRUE(navigation_0.has_committed());
  EXPECT_FALSE(navigation_0.was_same_document());

  RenderFrameHost* main_frame_host = wc->GetPrimaryMainFrame();
  RenderProcessHost* main_frame_process_host = main_frame_host->GetProcess();

  // When we both change the effective URL and also disallow the current
  // renderer process, a new load of the current document would get a different
  // SiteInstance.
  GURL modified_url0 =
      embedded_test_server()->GetURL("c.com", "/title1.html#ref1");
  new_client.set_effective_url(modified_url0);
  new_client.set_disallowed_process(main_frame_process_host->GetID());

  NavigationHandleCommitObserver navigation_1(wc, url1);
  EXPECT_TRUE(NavigateToURL(shell(), url1));
  EXPECT_TRUE(navigation_1.has_committed());
  EXPECT_TRUE(navigation_1.was_same_document());

  // The RenderFrameHost should not have changed, we should perform the
  // navigation in the currently loaded document.
  EXPECT_EQ(main_frame_host, wc->GetPrimaryMainFrame());
  EXPECT_EQ(main_frame_process_host, wc->GetPrimaryMainFrame()->GetProcess());
}

// This tests the same ideas as the above test except in this case the same-
// document navigation is done through a history navigation, which exercises
// different codepaths in the NavigationControllerImpl.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_SameDocumentHistoryNavigationWhenSiteInstanceWouldChange) {
  auto* wc = static_cast<WebContentsImpl*>(shell()->web_contents());
  GURL url0 = embedded_test_server()->GetURL("a.com", "/title1.html#ref1");
  GURL url1 = embedded_test_server()->GetURL("a.com", "/title1.html#ref2");
  NavigationHandleCommitObserver navigation_0(wc, url0);
  NavigationHandleCommitObserver navigation_1(wc, url1);

  GetEffectiveUrlClient new_client;

  EXPECT_TRUE(NavigateToURL(shell(), url0));
  EXPECT_TRUE(navigation_0.has_committed());
  EXPECT_FALSE(navigation_0.was_same_document());

  EXPECT_TRUE(NavigateToURL(shell(), url1));
  EXPECT_TRUE(navigation_1.has_committed());
  EXPECT_TRUE(navigation_1.was_same_document());

  RenderFrameHost* main_frame_host = wc->GetPrimaryMainFrame();
  RenderProcessHost* main_frame_process_host = main_frame_host->GetProcess();

  // When we both change the effective URL and also disallow the current
  // renderer process, a new load of the current document would get a different
  // SiteInstance.
  GURL modified_url0 =
      embedded_test_server()->GetURL("c.com", "/title1.html#ref1");
  new_client.set_effective_url(modified_url0);
  new_client.set_disallowed_process(main_frame_process_host->GetID());

  // Navigates to the same-document. Since the SiteInstance changed, we would
  // normally try isolate this navigation by using a different RenderProcessHost
  // and RenderFrameHost. But since it is same-document, we want to avoid that
  // and perform the navigation inside the loaded |url0| document.
  wc->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(wc));

  // The RenderFrameHost should not have changed, we should perform the
  // navigation in the currently loaded document.
  EXPECT_EQ(main_frame_host, wc->GetPrimaryMainFrame());
  EXPECT_EQ(main_frame_process_host, wc->GetPrimaryMainFrame()->GetProcess());
}

IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       NonDeterministicUrlRewritesUseLastUrl) {
  // Lambda expressions cannot be assigned to function pointers if they use
  // captures, so track how many times the handler is called using a non-const
  // static variable.
  static int rewrite_count;
  rewrite_count = 0;

  BrowserURLHandler::URLHandler handler_method =
      [](GURL* url, BrowserContext* browser_context) {
        GURL::Replacements replace_path;
        if (rewrite_count > 0) {
          replace_path.SetPathStr("title2.html");
        } else {
          replace_path.SetPathStr("title1.html");
        }
        *url = url->ReplaceComponents(replace_path);
        rewrite_count++;
        return true;
      };
  BrowserURLHandler::GetInstance()->AddHandlerPair(
      handler_method, BrowserURLHandler::null_handler());

  TestNavigationObserver observer(web_contents());
  shell()->LoadURL(embedded_test_server()->GetURL("/virtual-url.html"));
  observer.Wait();
  EXPECT_EQ("/title2.html", observer.last_navigation_url().path());
  EXPECT_EQ(2, rewrite_count);
}

// Create two windows. When the second is deleted, it initiates a navigation in
// the first. This is a situation where the navigation has an initiator frame
// token, but no corresponding RenderFrameHost.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_RendererInitiatedCrossWindowNavigationInUnload) {
  GURL url(embedded_test_server()->GetURL("/empty.html"));
  GURL always_referrer_url(embedded_test_server()->GetURL(
      "/set-header?Referrer-Policy: unsafe-url"));

  // Setup the opener window.
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // Setup the openee window;
  ShellAddedObserver new_shell_observer;
  EXPECT_TRUE(
      ExecJs(shell(), JsReplace("window.open($1);", always_referrer_url)));
  Shell* openee_shell = new_shell_observer.GetShell();
  EXPECT_TRUE(WaitForLoadStop(openee_shell->web_contents()));

  // When deleted, the openee will initiate a navigation in its opener.
  EXPECT_TRUE(ExecJs(openee_shell, R"(
    window.addEventListener("unload", () => {
      opener.location.href = "about:blank";
    })
  )"));

  RenderFrameHost* openee_rfh =
      static_cast<WebContentsImpl*>(openee_shell->web_contents())
          ->GetPrimaryMainFrame();
  blink::LocalFrameToken initiator_frame_token = openee_rfh->GetFrameToken();
  int initiator_process_id = openee_rfh->GetProcess()->GetID();
  base::RunLoop loop;
  DidStartNavigationCallback callback(
      web_contents(), base::BindLambdaForTesting([&](NavigationHandle* handle) {
        auto* request = NavigationRequest::From(handle);

        const absl::optional<blink::LocalFrameToken>& frame_token =
            request->GetInitiatorFrameToken();
        EXPECT_TRUE(frame_token.has_value());
        EXPECT_EQ(initiator_frame_token, frame_token.value());
        EXPECT_EQ(initiator_process_id, request->GetInitiatorProcessID());

        auto* initiator_rfh = RenderFrameHostImpl::FromFrameToken(
            request->GetInitiatorProcessID(), *frame_token);
        ASSERT_FALSE(initiator_rfh);

        // Even if the initiator RenderFrameHost is gone, its policy container
        // should still be around since the LocalFrame has not been destroyed
        // yet.
        auto* initiator_policy_container =
            PolicyContainerHost::FromFrameToken(frame_token.value());
        ASSERT_TRUE(initiator_policy_container);
        ASSERT_EQ(network::mojom::ReferrerPolicy::kAlways,
                  initiator_policy_container->referrer_policy());

        // Even if the initiator RenderFrameHost is gone, the navigation request
        // (to "about:blank") should have inherited its policy container.
        auto* initiator_policies =
            request->GetInitiatorPolicyContainerPolicies();
        ASSERT_TRUE(initiator_policies);
        ASSERT_EQ(network::mojom::ReferrerPolicy::kAlways,
                  initiator_policies->referrer_policy);

        loop.Quit();
      }));

  // Delete the openee, which trigger the navigation in the opener.
  openee_shell->Close();
  loop.Run();
}

// A document initiates a form submission in another frame, then deletes itself.
// Check the initiator frame token.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_FormSubmissionThenDeleteFrame) {
  GURL url(embedded_test_server()->GetURL("/empty.html"));
  GURL always_referrer_url(embedded_test_server()->GetURL(
      "/set-header?Referrer-Policy: unsafe-url"));

  // Setup the opener window.
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // Setup the openee window;
  ShellAddedObserver new_shell_observer;
  EXPECT_TRUE(ExecJs(shell(), JsReplace("window.open($1);", url)));
  Shell* openee_shell = new_shell_observer.GetShell();

  // Create a 'named' iframe in the first window. This will be the target of the
  // form submission.
  EXPECT_TRUE(ExecJs(shell(), R"(
    new Promise(resolve => {
      let iframe = document.createElement("iframe");
      iframe.onload = resolve;
      iframe.name = 'form-submission-target';
      iframe.src = location.href;
      document.body.appendChild(iframe);
    });
  )"));

  // Create an iframe in the second window. It will be initiating a form
  // submission and removing itself before the scheduled form navigation occurs.
  // This iframe will have referrer policy "unsafe-url".
  EXPECT_TRUE(WaitForLoadStop(openee_shell->web_contents()));
  EXPECT_TRUE(ExecJs(openee_shell, JsReplace(R"(
    new Promise(resolve => {
      let iframe = document.createElement('iframe');
      iframe.onload = resolve;
      iframe.src = $1;
      document.body.appendChild(iframe);
    });
  )",
                                             always_referrer_url)));
  EXPECT_TRUE(WaitForLoadStop(openee_shell->web_contents()));

  RenderFrameHost* initiator_rfh =
      static_cast<WebContentsImpl*>(openee_shell->web_contents())
          ->GetPrimaryMainFrame()
          ->child_at(0)
          ->current_frame_host();
  blink::LocalFrameToken initiator_frame_token = initiator_rfh->GetFrameToken();
  int initiator_process_id = initiator_rfh->GetProcess()->GetID();
  base::RunLoop loop;
  DidStartNavigationCallback callback(
      web_contents(), base::BindLambdaForTesting([&](NavigationHandle* handle) {
        auto* request = NavigationRequest::From(handle);
        ASSERT_TRUE(request->IsPost());

        const absl::optional<blink::LocalFrameToken>& frame_token =
            request->GetInitiatorFrameToken();
        EXPECT_TRUE(frame_token.has_value());
        EXPECT_EQ(initiator_frame_token, frame_token.value());
        EXPECT_EQ(initiator_process_id, request->GetInitiatorProcessID());

        auto* initiator_rfh = RenderFrameHostImpl::FromFrameToken(
            request->GetInitiatorProcessID(), frame_token.value());
        ASSERT_FALSE(initiator_rfh);

        // Even if the initiator RenderFrameHost is gone, its policy container
        // should still be around since the LocalFrame has not been destroyed
        // yet.
        auto* initiator_policy_container =
            PolicyContainerHost::FromFrameToken(frame_token.value());
        ASSERT_TRUE(initiator_policy_container);
        EXPECT_EQ(network::mojom::ReferrerPolicy::kAlways,
                  initiator_policy_container->referrer_policy());

        auto* initiator_policies =
            request->GetInitiatorPolicyContainerPolicies();
        ASSERT_TRUE(initiator_policies);
        ASSERT_EQ(network::mojom::ReferrerPolicy::kAlways,
                  initiator_policies->referrer_policy);

        loop.Quit();
      }));

  // Initiate a form submission into the first window and delete the initiator.
  EXPECT_TRUE(WaitForLoadStop(openee_shell->web_contents()));
  ExecuteScriptAsync(initiator_rfh, R"(
    let input = document.createElement("input");
    input.setAttribute("type", "hidden");
    input.setAttribute("name", "my_token");
    input.setAttribute("value", "my_value");

    // Schedule a form submission navigation (which will occur in a separate
    // task).
    let form = document.createElement('form');
    form.appendChild(input);
    form.setAttribute("method", "POST");
    form.setAttribute("action", "about:blank");
    form.setAttribute("target", "form-submission-target");
    document.body.appendChild(form);
    form.submit();

    // Delete this frame before the scheduled navigation occurs in the target
    // frame.
    parent.document.querySelector("iframe").remove();
  )");
  loop.Run();
}

// Same as the previous test, but for a remote frame navigation:
// A document initiates a form submission in a cross-origin frame, then deletes
// itself. Check the initiator frame token.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_FormSubmissionInRemoteFrameThenDeleteFrame) {
  GURL url(embedded_test_server()->GetURL("/empty.html"));
  GURL cross_origin_always_referrer_url(embedded_test_server()->GetURL(
      "foo.com", "/set-header?Referrer-Policy: unsafe-url"));

  // Setup the main page.
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // Create a cross origin child iframe. This iframe will embed another iframe,
  // which will initiate the navigation. The only purpose of this iframe is to
  // allow its child to delete itself by issuing
  //      parent.document.querySelector("iframe").remove();
  // (The main frame cannot do it because it is cross-origin.)
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  EXPECT_TRUE(ExecJs(shell(), JsReplace(R"(
      let iframe = document.createElement('iframe');
      iframe.src = $1;
      document.body.appendChild(iframe);
  )",
                                        cross_origin_always_referrer_url)));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  RenderFrameHostImpl* middle_rfh =
      current_frame_host()->child_at(0)->current_frame_host();

  // Now create a grandchild iframe, which is same-origin with the parent (but
  // cross-origin with the grandparent). The grandchild will initiate a form
  // submission in the top frame and remove itself before the scheduled form
  // navigation occurs. This iframe will have referrer policy "unsafe-url".
  EXPECT_TRUE(ExecJs(middle_rfh, JsReplace(R"(
      let iframe = document.createElement('iframe');
      iframe.src = $1;
      document.body.appendChild(iframe);
  )",
                                           cross_origin_always_referrer_url)));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  RenderFrameHost* initiator_rfh =
      middle_rfh->child_at(0)->current_frame_host();
  blink::LocalFrameToken initiator_frame_token = initiator_rfh->GetFrameToken();
  int initiator_process_id = initiator_rfh->GetProcess()->GetID();

  base::RunLoop loop;
  DidStartNavigationCallback callback(
      shell()->web_contents(),
      base::BindLambdaForTesting([&](NavigationHandle* handle) {
        auto* request = NavigationRequest::From(handle);
        ASSERT_TRUE(request->IsPost());

        const absl::optional<blink::LocalFrameToken>& frame_token =
            request->GetInitiatorFrameToken();
        EXPECT_TRUE(frame_token.has_value());
        EXPECT_EQ(initiator_frame_token, frame_token.value());
        EXPECT_EQ(initiator_process_id, request->GetInitiatorProcessID());

        auto* initiator_rfh = RenderFrameHostImpl::FromFrameToken(
            request->GetInitiatorProcessID(), frame_token.value());
        ASSERT_FALSE(initiator_rfh);

        // Even if the initiator RenderFrameHost is gone, its policy container
        // should still be around since the LocalFrame has not been destroyed
        // yet.
        auto* initiator_policy_container =
            PolicyContainerHost::FromFrameToken(frame_token.value());
        ASSERT_TRUE(initiator_policy_container);
        EXPECT_EQ(network::mojom::ReferrerPolicy::kAlways,
                  initiator_policy_container->referrer_policy());
        EXPECT_EQ(
            network::mojom::ReferrerPolicy::kAlways,
            request->GetInitiatorPolicyContainerPolicies()->referrer_policy);

        loop.Quit();
      }));

  // Initiate a form submission into the main frame and delete the initiator.
  ExecuteScriptAsync(initiator_rfh, R"(
    let input = document.createElement("input");
    input.setAttribute("type", "hidden");
    input.setAttribute("name", "my_token");
    input.setAttribute("value", "my_value");

    // Schedule a form submission navigation (which will occur in a separate
    // task).
    let form = document.createElement('form');
    form.appendChild(input);
    form.setAttribute("method", "POST");
    form.setAttribute("action", "about:blank");
    form.setAttribute("target", "_top");
    document.body.appendChild(form);
    form.submit();

    // Delete this frame before the scheduled navigation occurs in the main
    // frame.
    parent.document.querySelector("iframe").remove();
  )");
  loop.Run();
}

using MediaNavigationBrowserTest = NavigationBaseBrowserTest;

// Media navigations synchronously complete the time of the `CommitNavigation`
// IPC call. Ensure that the renderer does not crash if the media navigation
// results in an HTTP error with no body, since the renderer will reentrantly
// commit an error page while handling the `CommitNavigation` IPC.
IN_PROC_BROWSER_TEST_F(MediaNavigationBrowserTest, FailedNavigation) {
  embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
      [](const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_code(net::HTTP_NOT_FOUND);
        response->set_content_type("video/mp4");
        return response;
      }));
  ASSERT_TRUE(embedded_test_server()->Start());

  const GURL error_url(embedded_test_server()->GetURL("/moo.mp4"));
  EXPECT_FALSE(NavigateToURL(shell(), error_url));
  EXPECT_EQ(error_url, current_frame_host()->GetLastCommittedURL());
  NavigationEntry* entry =
      web_contents()->GetController().GetLastCommittedEntry();
  EXPECT_EQ(PAGE_TYPE_ERROR, entry->GetPageType());
}

class DocumentPolicyBrowserTest : public NavigationBaseBrowserTest {
 public:
  DocumentPolicyBrowserTest() {
    feature_list_.InitAndEnableFeature(features::kDocumentPolicy);
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

// Test that scroll restoration can be disabled with
// Document-Policy: force-load-at-top
IN_PROC_BROWSER_TEST_F(DocumentPolicyBrowserTest,
                       ScrollRestorationDisabledByDocumentPolicy) {
  net::test_server::ControllableHttpResponse response(embedded_test_server(),
                                                      "/target.html");
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url(embedded_test_server()->GetURL("/target.html"));
  TestNavigationManager navigation_manager(web_contents(), url);
  // This test expects the document is freshly loaded on the back navigation
  // so that the document policy to force-load-at-top will run. This will not
  // happen if the document is back-forward cached, so we need to disable it.
  DisableBackForwardCacheForTesting(web_contents(),
                                    BackForwardCache::TEST_REQUIRES_NO_CACHING);

  // Load the document with document policy force-load-at-top
  shell()->LoadURL(url);
  EXPECT_TRUE(navigation_manager.WaitForRequestStart());
  navigation_manager.ResumeNavigation();
  response.WaitForRequest();
  response.Send(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "Document-Policy: force-load-at-top\r\n"
      "\r\n"
      "<p style='position: absolute; top: 10000px;'>Some text</p>");
  response.Done();

  EXPECT_TRUE(navigation_manager.WaitForResponse());
  navigation_manager.ResumeNavigation();
  ASSERT_TRUE(navigation_manager.WaitForNavigationFinished());
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  EXPECT_TRUE(WaitForRenderFrameReady(current_frame_host()));

  {
    RenderFrameSubmissionObserver frame_observer(web_contents());
    // Scroll down the page a bit
    EXPECT_TRUE(ExecJs(web_contents(), "window.scrollTo(0, 1000)"));
    frame_observer.WaitForScrollOffsetAtTop(false);
  }

  // Navigate away
  EXPECT_TRUE(ExecJs(web_contents(), "window.location = 'about:blank'"));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  EXPECT_TRUE(WaitForRenderFrameReady(current_frame_host()));

  // Navigate back
  EXPECT_TRUE(ExecJs(web_contents(), "history.back()"));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  EXPECT_TRUE(WaitForRenderFrameReady(current_frame_host()));

  // Wait a short amount of time to ensure the page does not scroll.
  base::RunLoop run_loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), TestTimeouts::tiny_timeout());
  run_loop.Run();
  RunUntilInputProcessed(RenderWidgetHostImpl::From(
      web_contents()->GetRenderViewHost()->GetWidget()));
  const cc::RenderFrameMetadata& last_metadata =
      RenderFrameSubmissionObserver(web_contents()).LastRenderFrameMetadata();
  EXPECT_TRUE(last_metadata.is_scroll_offset_at_top);
}

// Test that scroll restoration works as expected with
// Document-Policy: force-load-at-top=?0
IN_PROC_BROWSER_TEST_F(DocumentPolicyBrowserTest,
                       ScrollRestorationEnabledByDocumentPolicy) {
  net::test_server::ControllableHttpResponse response(embedded_test_server(),
                                                      "/target.html");
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url(embedded_test_server()->GetURL("/target.html"));
  RenderFrameSubmissionObserver frame_observer(web_contents());
  TestNavigationManager navigation_manager(web_contents(), url);

  // Load the document with document policy force-load-at-top set to false.
  shell()->LoadURL(url);
  EXPECT_TRUE(navigation_manager.WaitForRequestStart());
  navigation_manager.ResumeNavigation();
  response.WaitForRequest();
  response.Send(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "Document-Policy: force-load-at-top=?0\r\n"
      "\r\n"
      "<p style='position: absolute; top: 10000px;'>Some text</p>");
  response.Done();

  EXPECT_TRUE(navigation_manager.WaitForResponse());
  navigation_manager.ResumeNavigation();
  ASSERT_TRUE(navigation_manager.WaitForNavigationFinished());
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  EXPECT_TRUE(WaitForRenderFrameReady(current_frame_host()));

  // Scroll down the page a bit
  EXPECT_TRUE(ExecJs(web_contents(), "window.scrollTo(0, 1000)"));
  frame_observer.WaitForScrollOffsetAtTop(false);

  // Navigate away
  EXPECT_TRUE(ExecJs(web_contents(), "window.location = 'about:blank'"));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  EXPECT_TRUE(WaitForRenderFrameReady(current_frame_host()));

  // Navigate back
  EXPECT_TRUE(ExecJs(web_contents(), "history.back()"));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  EXPECT_TRUE(WaitForRenderFrameReady(current_frame_host()));

  // Ensure scroll restoration activated
  frame_observer.WaitForScrollOffsetAtTop(false);
  const cc::RenderFrameMetadata& last_metadata =
      RenderFrameSubmissionObserver(web_contents()).LastRenderFrameMetadata();
  EXPECT_FALSE(last_metadata.is_scroll_offset_at_top);
}

// Test that element fragment anchor scrolling can be disabled with
// Document-Policy: force-load-at-top
IN_PROC_BROWSER_TEST_F(DocumentPolicyBrowserTest,
                       FragmentAnchorDisabledByDocumentPolicy) {
  net::test_server::ControllableHttpResponse response(embedded_test_server(),
                                                      "/target.html");

  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url(embedded_test_server()->GetURL("/target.html#text"));

  // Load the target document
  TestNavigationManager navigation_manager(web_contents(), url);
  shell()->LoadURL(url);

  // Start navigation
  EXPECT_TRUE(navigation_manager.WaitForRequestStart());
  navigation_manager.ResumeNavigation();

  // Send Document-Policy header
  response.WaitForRequest();
  response.Send(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "Document-Policy: force-load-at-top\r\n"
      "\r\n"
      "<p id='text' style='position: absolute; top: 10000px;'>Some text</p>");
  response.Done();

  EXPECT_TRUE(navigation_manager.WaitForResponse());
  navigation_manager.ResumeNavigation();
  ASSERT_TRUE(navigation_manager.WaitForNavigationFinished());

  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  EXPECT_TRUE(WaitForRenderFrameReady(current_frame_host()));
  // Wait a short amount of time to ensure the page does not scroll.
  base::RunLoop run_loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), TestTimeouts::tiny_timeout());
  run_loop.Run();
  RunUntilInputProcessed(RenderWidgetHostImpl::From(
      web_contents()->GetRenderViewHost()->GetWidget()));
  const cc::RenderFrameMetadata& last_metadata =
      RenderFrameSubmissionObserver(web_contents()).LastRenderFrameMetadata();
  EXPECT_TRUE(last_metadata.is_scroll_offset_at_top);
}

// Test that element fragment anchor scrolling works as expected with
// Document-Policy: force-load-at-top=?0
IN_PROC_BROWSER_TEST_F(DocumentPolicyBrowserTest,
                       FragmentAnchorEnabledByDocumentPolicy) {
  net::test_server::ControllableHttpResponse response(embedded_test_server(),
                                                      "/target.html");

  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url(embedded_test_server()->GetURL("/target.html#text"));
  RenderFrameSubmissionObserver frame_observer(web_contents());

  // Load the target document
  TestNavigationManager navigation_manager(web_contents(), url);
  shell()->LoadURL(url);

  // Start navigation
  EXPECT_TRUE(navigation_manager.WaitForRequestStart());
  navigation_manager.ResumeNavigation();

  // Send Document-Policy header
  response.WaitForRequest();
  response.Send(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "Document-Policy: force-load-at-top=?0\r\n"
      "\r\n"
      "<p id='text' style='position: absolute; top: 10000px;'>Some text</p>");
  response.Done();

  EXPECT_TRUE(navigation_manager.WaitForResponse());
  navigation_manager.ResumeNavigation();
  ASSERT_TRUE(navigation_manager.WaitForNavigationFinished());

  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  EXPECT_TRUE(WaitForRenderFrameReady(current_frame_host()));
  frame_observer.WaitForScrollOffsetAtTop(
      /*expected_scroll_offset_at_top=*/false);
  const cc::RenderFrameMetadata& last_metadata =
      RenderFrameSubmissionObserver(web_contents()).LastRenderFrameMetadata();
  EXPECT_FALSE(last_metadata.is_scroll_offset_at_top);
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest, DISABLED_OriginToCommitBasic) {
  GURL url = embedded_test_server()->GetURL("a.com", "/empty.html");
  auto origin_expected = url::Origin::Create(url);
  TestNavigationManager manager(web_contents(), url);
  shell()->LoadURL(url);
  EXPECT_TRUE(manager.WaitForResponse());
  NavigationRequest* navigation = main_frame()->navigation_request();
  absl::optional<url::Origin> origin_to_commit =
      navigation->GetOriginToCommit();
  ASSERT_TRUE(origin_to_commit.has_value());
  ASSERT_TRUE(manager.WaitForNavigationFinished());
  url::Origin origin_committed = current_frame_host()->GetLastCommittedOrigin();

  EXPECT_FALSE(origin_to_commit->opaque());
  EXPECT_FALSE(origin_committed.opaque());
  EXPECT_EQ(origin_expected, *origin_to_commit);
  EXPECT_EQ(origin_expected, origin_committed);
}

IN_PROC_BROWSER_TEST_F(NavigationBrowserTest, OriginToCommit204) {
  GURL url = embedded_test_server()->GetURL("a.com", "/nocontent");
  TestNavigationManager manager(web_contents(), url);
  shell()->LoadURL(url);
  EXPECT_TRUE(manager.WaitForResponse());
  NavigationRequest* navigation = main_frame()->navigation_request();
  absl::optional<url::Origin> origin_to_commit =
      navigation->GetOriginToCommit();
  EXPECT_FALSE(origin_to_commit.has_value());
  ASSERT_TRUE(manager.WaitForNavigationFinished());
}

IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       OriginToCommitSandboxFromResponse) {
  GURL url = embedded_test_server()->GetURL(
      "a.com", "/set-header?Content-Security-Policy: sandbox");
  TestNavigationManager manager(web_contents(), url);
  shell()->LoadURL(url);
  EXPECT_TRUE(manager.WaitForResponse());
  NavigationRequest* navigation = main_frame()->navigation_request();
  url::Origin origin_to_commit = navigation->GetOriginToCommit().value();
  ASSERT_TRUE(manager.WaitForNavigationFinished());
  url::Origin origin_committed = current_frame_host()->GetLastCommittedOrigin();

  EXPECT_TRUE(origin_to_commit.opaque());
  EXPECT_TRUE(origin_committed.opaque());
  // TODO(https://crbug.com/888079). The nonce must match.
  EXPECT_NE(origin_to_commit, origin_committed);
}

IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       OriginToCommitSandboxFromParentDocument) {
  GURL url_top = embedded_test_server()->GetURL(
      "a.com", "/set-header?Content-Security-Policy: sandbox allow-scripts");
  EXPECT_TRUE(NavigateToURL(shell(), url_top));
  GURL url_iframe = embedded_test_server()->GetURL("a.com", "/empty.html");
  TestNavigationManager manager(web_contents(), url_iframe);
  ExecuteScriptAsync(current_frame_host(), R"(
    let iframe = document.createElement("iframe");
    iframe.src = "./empty.html";
    document.body.appendChild(iframe);
  )");
  EXPECT_TRUE(manager.WaitForResponse());
  FrameTreeNode* iframe = current_frame_host()->child_at(0);
  NavigationRequest* navigation = iframe->navigation_request();
  url::Origin origin_to_commit = navigation->GetOriginToCommit().value();
  ASSERT_TRUE(manager.WaitForNavigationFinished());
  url::Origin origin_committed =
      iframe->current_frame_host()->GetLastCommittedOrigin();

  EXPECT_TRUE(origin_to_commit.opaque());
  EXPECT_TRUE(origin_committed.opaque());
  // TODO(https://crbug.com/888079). The nonce must match.
  EXPECT_NE(origin_to_commit, origin_committed);

  // Both document have the same URL. Only the first sets CSP:sandbox, but both
  // are sandboxed. They get an opaque origin different from each others.
  EXPECT_NE(current_frame_host()->GetLastCommittedOrigin(), origin_committed);
}

// Regression test for https://crbug.com/1158306.
// Navigate to a response, which set Content-Security-Policy: sandbox AND block
// the response. The error page shouldn't set sandbox flags.
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest, ErrorPageFromCspSandboxResponse) {
  // Block every navigation in WillProcessResponse.
  std::unique_ptr<content::TestNavigationThrottleInserter> blocker =
      BlockNavigationWillProcessResponse(web_contents());

  // Navigate toward a document witch sets CSP:sandbox.
  GURL url = embedded_test_server()->GetURL(
      "a.com", "/set-header?Content-Security-Policy: sandbox");
  TestNavigationManager manager(web_contents(), url);
  shell()->LoadURL(url);
  ASSERT_TRUE(manager.WaitForNavigationFinished());

  // An error page committed. It doesn't have any sandbox flags, despite the
  // original response headers.
  EXPECT_TRUE(current_frame_host()->IsErrorDocument());
  EXPECT_EQ(network::mojom::WebSandboxFlags::kNone,
            current_frame_host()->active_sandbox_flags());

  EXPECT_EQ(url, current_frame_host()->GetLastCommittedURL());
  EXPECT_TRUE(current_frame_host()->GetLastCommittedOrigin().opaque());
  EXPECT_TRUE(
      current_frame_host()->GetLastCommittedOrigin().CanBeDerivedFrom(url));
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_ProcessShutdownDuringDeferredNavigationThrottle) {
  GURL url = embedded_test_server()->GetURL("a.com", "/empty.html");
  EXPECT_TRUE(NavigateToURL(shell(), url));

  class ShutdownThrottle : public TaskRunnerDeferringThrottle,
                           WebContentsObserver {
   public:
    explicit ShutdownThrottle(WebContents* web_contents,
                              NavigationHandle* handle)
        : TaskRunnerDeferringThrottle(
              base::SingleThreadTaskRunner::GetCurrentDefault(),
              /*defer_start=*/false,
              /*defer_redirect=*/false,
              /*defer_response=*/true,
              handle),
          web_contents_(web_contents) {
      WebContentsObserver::Observe(web_contents_);
    }

    void AsyncResume() override {
      // Shutdown the renderer and delay Resume() until then.
      web_contents_->GetPrimaryMainFrame()->GetProcess()->Shutdown(1);
    }

    void RenderFrameDeleted(RenderFrameHost* frame_host) override {
      TaskRunnerDeferringThrottle::AsyncResume();
    }

   private:
    raw_ptr<WebContents> web_contents_;
  };

  auto inserter = std::make_unique<TestNavigationThrottleInserter>(
      shell()->web_contents(),
      base::BindLambdaForTesting(
          [&](NavigationHandle* handle) -> std::unique_ptr<NavigationThrottle> {
            return std::make_unique<ShutdownThrottle>(shell()->web_contents(),
                                                      handle);
          }));

  class DoesNotReadyToCommitObserver : public WebContentsObserver {
   public:
    explicit DoesNotReadyToCommitObserver(WebContents* contents)
        : WebContentsObserver(contents) {}

    // WebContentsObserver overrides.
    void ReadyToCommitNavigation(NavigationHandle* handle) override {
      // This method should not happen. Since the process is destroyed before
      // we become ready to commit, we can not ever reach
      // ReadyToCommitNavigation. Doing so would fail because the renderer is
      // gone.
      ADD_FAILURE() << "ReadyToCommitNavigation but renderer has crashed. "
                       "IsRenderFrameLive: "
                    << handle->GetRenderFrameHost()->IsRenderFrameLive();
      navigation_was_ready_to_commit_ = true;
    }

    void DidFinishNavigation(NavigationHandle* handle) override {
      navigation_finished_ = true;
      navigation_committed_ = handle->HasCommitted();
    }

    bool navigation_was_ready_to_commit() {
      return navigation_was_ready_to_commit_;
    }
    bool navigation_finished() { return navigation_finished_; }
    bool navigation_committed() { return navigation_committed_; }

   private:
    bool navigation_was_ready_to_commit_ = false;
    bool navigation_finished_ = false;
    bool navigation_committed_ = false;
  };

  // Watch that ReadyToCommitNavigation() will not happen when the renderer is
  // gone.
  DoesNotReadyToCommitObserver no_commit_obs(shell()->web_contents());

  // We will shutdown the renderer during this navigation.
  ScopedAllowRendererCrashes scoped_allow_renderer_crashes;

  // Important: This is a browser-initiated navigation, so the NavigationRequest
  // does not have an open connection (NavigationClient) to the renderer that it
  // is listening to for termination while running NavigationThrottles.
  //
  // Expect this navigation to be aborted, so we stop waiting after the
  // uncommitted navigation is done.
  GURL url2 = embedded_test_server()->GetURL("a.com", "/title1.html");
  NavigateToURLBlockUntilNavigationsComplete(
      shell(), url2, /*number_of_navigations=*/1,
      /*ignore_uncommitted_navigations=*/false);

  // The renderer was shutdown mid-navigation.
  EXPECT_FALSE(
      shell()->web_contents()->GetPrimaryMainFrame()->IsRenderFrameLive());

  // The navigation was aborted, which means it finished but did not commit, and
  // _importantly_ it never reported "ReadyToCommitNavigation" without a live
  // renderer.
  EXPECT_TRUE(no_commit_obs.navigation_finished());
  EXPECT_FALSE(no_commit_obs.navigation_was_ready_to_commit());
  EXPECT_FALSE(no_commit_obs.navigation_committed());
}

// Sandbox flags defined by the parent must not apply to Chrome's error page.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_ErrorPageFromInSandboxedIframe) {
  GURL url = embedded_test_server()->GetURL("a.com", "/empty.html");
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // Block every navigation in WillProcessResponse.
  std::unique_ptr<content::TestNavigationThrottleInserter> blocker =
      BlockNavigationWillProcessResponse(web_contents());

  TestNavigationManager manager(web_contents(), url);
  ExecuteScriptAsync(current_frame_host(), R"(
    let iframe = document.createElement("iframe");
    iframe.src = location.href;
    iframe.sandbox = "allow-orientation-lock";
    document.body.appendChild(iframe);
  )");
  ASSERT_TRUE(manager.WaitForNavigationFinished());

  RenderFrameHostImpl* child_rfh =
      current_frame_host()->child_at(0)->current_frame_host();

  EXPECT_TRUE(child_rfh->IsErrorDocument());
  EXPECT_EQ(network::mojom::WebSandboxFlags::kNone,
            child_rfh->active_sandbox_flags());
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_OriginToCommitSandboxFromFrame) {
  GURL url = embedded_test_server()->GetURL("a.com", "/empty.html");
  EXPECT_TRUE(NavigateToURL(shell(), url));
  TestNavigationManager manager(web_contents(), url);
  ExecuteScriptAsync(current_frame_host(), R"(
    let iframe = document.createElement("iframe");
    iframe.src = location.href;
    iframe.sandbox = "";
    document.body.appendChild(iframe);
  )");
  EXPECT_TRUE(manager.WaitForResponse());
  FrameTreeNode* iframe = current_frame_host()->child_at(0);
  NavigationRequest* navigation = iframe->navigation_request();
  url::Origin origin_to_commit = navigation->GetOriginToCommit().value();
  ASSERT_TRUE(manager.WaitForNavigationFinished());
  url::Origin origin_committed =
      iframe->current_frame_host()->GetLastCommittedOrigin();

  EXPECT_TRUE(origin_to_commit.opaque());
  EXPECT_TRUE(origin_committed.opaque());
  // TODO(https://crbug.com/888079). Make the nonce to match.
  EXPECT_NE(origin_to_commit, origin_committed);
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_NavigateToAboutBlankWhileFirstNavigationPending) {
  GURL url_a = embedded_test_server()->GetURL("a.com", "/empty.html");
  GURL url_b = embedded_test_server()->GetURL("b.com", "/empty.html");

  EXPECT_TRUE(NavigateToURL(shell(), url_a));

  ShellAddedObserver new_shell_observer;
  ExecuteScriptAsync(
      current_frame_host(),
      JsReplace("window.open($1, '_blank').location = 'about:blank'", url_b));

  WebContents* popup_contents = new_shell_observer.GetShell()->web_contents();
  TestNavigationManager manager_1(popup_contents, url_b);
  TestNavigationManager manager_2(popup_contents, GURL("about:blank"));

  ASSERT_TRUE(manager_1.WaitForNavigationFinished());
  ASSERT_TRUE(manager_2.WaitForNavigationFinished());

  EXPECT_EQ(popup_contents->GetLastCommittedURL(), "about:blank");
}

class NetworkIsolationSplitCacheAppendIframeOrigin
    : public NavigationBaseBrowserTest {
 public:
  NetworkIsolationSplitCacheAppendIframeOrigin() {
    feature_list_.InitAndEnableFeature(
        net::features::kSplitCacheByNetworkIsolationKey);
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

// Make a main document, have it request a cacheable subresources. Then make a
// same-site document in an iframe that serves the CSP:Sandbox header. Stop the
// test server, have the sandboxed document requests the same subresource. The
// request should fail. To make sure the request is actually in the cache, the
// main document should be able to request it again.
// TODO: b/432532266 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NetworkIsolationSplitCacheAppendIframeOrigin,
                       DISABLED_SandboxedUsesDifferentCache) {
  auto server = std::make_unique<net::EmbeddedTestServer>();
  server->AddDefaultHandlers(GetTestDataFilePath());
  EXPECT_TRUE(server->Start());

  GURL url_main_document = server->GetURL("a.com", "/empty.html");

  EXPECT_TRUE(NavigateToURL(shell(), url_main_document));
  EXPECT_TRUE(ExecJs(current_frame_host(), R"(
    new Promise(resolve => {
      let iframe = document.createElement("iframe");
      iframe.onload = resolve;
      iframe.src = "/set-header?Content-Security-Policy: sandbox allow-scripts";
      document.body.appendChild(iframe);
    })
  )"));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));

  RenderFrameHostImpl* main_rfh = current_frame_host();
  RenderFrameHostImpl* sub_rfh = main_rfh->child_at(0)->current_frame_host();

  EXPECT_FALSE(main_rfh->GetLastCommittedOrigin().opaque());
  EXPECT_TRUE(sub_rfh->GetLastCommittedOrigin().opaque());

  const char* fetch_cacheable = R"(
    fetch("cacheable.svg")
      .then(() => "success")
      .catch(() => "error")
  )";

  EXPECT_EQ("success", EvalJs(main_rfh, fetch_cacheable));

  server.reset();

  EXPECT_EQ("error", EvalJs(sub_rfh, fetch_cacheable));
  EXPECT_EQ("success", EvalJs(main_rfh, fetch_cacheable));
}

// The Content Security Policy directive 'treat-as-public-address' is parsed
// into the parsed headers by services/network and applied there. That directive
// is ignored in report-only policies. Here we check that Blink reports a
// console message if 'treat-as-public-address' is delivered in a report-only
// policy. This serves also as a regression test for https://crbug.com/1150314
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       TreatAsPublicAddressInReportOnly) {
  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern(
      "The Content Security Policy directive 'treat-as-public-address' is "
      "ignored when delivered in a report-only policy.");

  GURL url = embedded_test_server()->GetURL(
      "/set-header?"
      "Content-Security-Policy-Report-Only: treat-as-public-address");
  EXPECT_TRUE(NavigateToURL(shell(), url));

  ASSERT_TRUE(console_observer.Wait());
}

// The Content Security Policy directive 'plugin-types' has been removed. Here
// we check that Blink reports a console message if 'plugin-type' is delivered
// in a policy.
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       ContentSecurityPolicyErrorPluginTypes) {
  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern(
      "The Content-Security-Policy directive 'plugin-types' has been removed "
      "from the specification. "
      "If you want to block plugins, consider specifying \"object-src 'none'\" "
      "instead.");

  GURL url = embedded_test_server()->GetURL(
      "/set-header?"
      "Content-Security-Policy: plugin-types application/pdf");
  EXPECT_TRUE(NavigateToURL(shell(), url));

  ASSERT_TRUE(console_observer.Wait());
}

class SubresourceLoadingTest : public NavigationBrowserTest {
 public:
  SubresourceLoadingTest() = default;
  SubresourceLoadingTest(const SubresourceLoadingTest&) = delete;
  SubresourceLoadingTest& operator=(const SubresourceLoadingTest&) = delete;

  void DontTestNetworkServiceCrashes() {
    test_network_service_crashes_ = false;
  }

  void VerifyResultsOfAboutBlankNavigation(RenderFrameHost* target_frame,
                                           RenderFrameHost* initiator_frame) {
    // Verify that `target_frame` has been navigated to "about:blank".
    EXPECT_EQ(GURL(url::kAboutBlankURL), target_frame->GetLastCommittedURL());

    // Verify that "about:blank" committed with the expected origin, and in the
    // expected SiteInstance.
    EXPECT_EQ(target_frame->GetLastCommittedOrigin(),
              initiator_frame->GetLastCommittedOrigin());
    EXPECT_EQ(target_frame->GetSiteInstance(),
              initiator_frame->GetSiteInstance());

    // Ask for cookies in the `target_frame`.  One implicit verification here
    // is whether this step will hit any `cookie_url`-related NOTREACHED or DwoC
    // in RestrictedCookieManager::ValidateAccessToCookiesAt.  This verification
    // is non-racey, because `document.cookie` must have heard back from the
    // RestrictedCookieManager before returning the value of cookies (this
    // ignores possible Blink-side caching, but this is the first time the
    // renderer needs the cookies and so this is okay for this test).
    EXPECT_EQ("", EvalJs(target_frame, "document.cookie"));

    // Verify that the "about:blank" frame is able to load an image.
    VerifyImageSubresourceLoads(target_frame);
  }

  void VerifyImageSubresourceLoads(
      const ToRenderFrameHost& target,
      const std::string& target_document = "document") {
    RenderFrameHostImpl* target_frame =
        static_cast<RenderFrameHostImpl*>(target.render_frame_host());
    VerifySingleImageSubresourceLoad(target_frame, target_document);

    // Verify detecting and recovering from a NetworkService crash (e.g. via the
    // `network_service_disconnect_handler_holder_mojo` field and the
    // UpdateSubresourceLoaderFactories method of RenderFrameHostImpl).
    if (!IsInProcessNetworkService() && test_network_service_crashes_) {
      SimulateNetworkServiceCrash();

      // In addition to waiting (inside SimulateNetworkServiceCrash above) for
      // getting notified about being disconnected from
      // network::mojom::NetworkServiceTest, we also want to make sure that the
      // relevant RenderFrameHost realizes that the NetworkService has crashed.
      // Which RenderFrameHost is relevant varies from test to test, so we
      // flush multiple frames and use kDoNothingIfNoNetworkServiceConnection.
      FlushNetworkInterfacesInOpenerChain(target_frame);

      // Rerun the test after the NetworkService crash.
      VerifySingleImageSubresourceLoad(target_frame, target_document);
    }
  }

 private:
  void FlushNetworkInterfacesInOpenerChain(RenderFrameHostImpl* current_frame) {
    std::set<WebContents*> visited_contents;
    while (true) {
      // Check if we've already visited the current frame tree.
      DCHECK(current_frame);
      WebContents* current_contents =
          WebContents::FromRenderFrameHost(current_frame);
      DCHECK(current_contents);
      if (base::Contains(visited_contents, current_contents)) {
        break;
      }
      visited_contents.insert(current_contents);

      // Flush all the frames in the `current_contents's active page.
      current_contents->GetPrimaryMainFrame()->ForEachRenderFrameHost(
          [](RenderFrameHost* frame_to_flush) {
            constexpr bool kDoNothingIfNoNetworkServiceConnection = true;
            frame_to_flush->FlushNetworkAndNavigationInterfacesForTesting(
                kDoNothingIfNoNetworkServiceConnection);
          });

      // Traverse the `current_frame`'s opener chain.
      if (FrameTreeNode* opener_node =
              current_frame->frame_tree_node()->opener()) {
        current_frame = opener_node->current_frame_host();
      } else {
        break;  // Break out of the loop if there is no opener.
      }
    }
  }

  void VerifySingleImageSubresourceLoad(RenderFrameHost* target,
                                        const std::string& target_document) {
    // Use a random, GUID-based hostname, to avoid hitting the network cache.
    GURL image_url = embedded_test_server()->GetURL(
        base::Uuid::GenerateRandomV4().AsLowercaseString() + ".com",
        "/blank.jpg");
    const char kScriptTemplate[] = R"(
        new Promise(resolve => {
            let img = document.createElement('img');
            img.src = $1;  // `$1` is replaced with the value of `image_url`.
            img.addEventListener('load', () => {
                resolve('allowed');
            });
            img.addEventListener('error', err => {
                resolve(`error: ${err}`);
            });

            // `%%s` is replaced with the value of `target_document`.
            %s.body.appendChild(img);
        }); )";
    std::string script = base::StringPrintf(
        JsReplace(kScriptTemplate, image_url).c_str(), target_document.c_str());
    EXPECT_EQ("allowed", EvalJs(target, script));
  }

  bool test_network_service_crashes_ = true;
};

// The test below verifies that an "about:blank" navigation commits with the
// right origin, even when the initiator of the navigation is not the parent or
// opener of the frame targeted by the navigation.  In the
// GrandchildToAboutBlank... testcases, the navigation is initiated by the
// grandparent of the target frame.
//
// In this test case there are no process swaps and the parent of the navigated
// frame is a local frame (even in presence of site-per-process).  See also
// GrandchildToAboutBlank_ABA_CrossSite and
// GrandchildToAboutBlank_ABB_CrossSite.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(SubresourceLoadingTest,
                       DISABLED_GrandchildToAboutBlank_ABA_SameSite) {
  GURL url(embedded_test_server()->GetURL(
      "a.example.com",
      "/cross_site_iframe_factory.html?"
      "a.example.com(b.example.com(a.example.com))"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // Verify the desired properties of the test setup.
  RenderFrameHostImpl* main_frame = static_cast<RenderFrameHostImpl*>(
      shell()->web_contents()->GetPrimaryMainFrame());
  RenderFrameHostImpl* child_frame =
      main_frame->child_at(0)->current_frame_host();
  RenderFrameHostImpl* grandchild_frame =
      child_frame->child_at(0)->current_frame_host();
  EXPECT_EQ(main_frame->GetSiteInstance(), child_frame->GetSiteInstance());
  EXPECT_EQ(main_frame->GetSiteInstance(), grandchild_frame->GetSiteInstance());
  EXPECT_EQ(main_frame->GetLastCommittedOrigin(),
            grandchild_frame->GetLastCommittedOrigin());
  EXPECT_NE(main_frame->GetLastCommittedOrigin(),
            child_frame->GetLastCommittedOrigin());

  // Navigate the grandchild frame to about:blank
  ASSERT_TRUE(ExecJs(grandchild_frame, "window.name = 'grandchild'"));
  TestNavigationObserver nav_observer(shell()->web_contents(), 1);
  ASSERT_TRUE(
      ExecJs(main_frame,
             "grandchild_window = window.open('about:blank', 'grandchild')"));
  nav_observer.Wait();

  // Verify that the grandchild has the same origin as the main frame (*not* the
  // origin of the parent frame).
  main_frame = static_cast<RenderFrameHostImpl*>(
      shell()->web_contents()->GetPrimaryMainFrame());
  child_frame = main_frame->child_at(0)->current_frame_host();
  grandchild_frame = child_frame->child_at(0)->current_frame_host();
  VerifyResultsOfAboutBlankNavigation(grandchild_frame, main_frame);
}

// The test below verifies that an "about:blank" navigation commits with the
// right origin, even when the initiator of the navigation is not the parent or
// opener of the frame targeted by the navigation.  In the
// GrandchildToAboutBlank... testcases, the navigation is initiated by the
// grandparent of the target frame.
//
// In this test case there are no process swaps and the parent of the navigated
// frame is a remote frame (in presence of site-per-process).  See also
// GrandchildToAboutBlank_ABA_SameSite and GrandchildToAboutBlank_ABB_CrossSite.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(SubresourceLoadingTest,
                       DISABLED_GrandchildToAboutBlank_ABA_CrossSite) {
  GURL url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b(a))"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // Verify the desired properties of the test setup.
  RenderFrameHostImpl* main_frame = static_cast<RenderFrameHostImpl*>(
      shell()->web_contents()->GetPrimaryMainFrame());
  RenderFrameHostImpl* child_frame =
      main_frame->child_at(0)->current_frame_host();
  RenderFrameHostImpl* grandchild_frame =
      child_frame->child_at(0)->current_frame_host();
  if (AreDefaultSiteInstancesEnabled()) {
    EXPECT_EQ(main_frame->GetSiteInstance(), child_frame->GetSiteInstance());
  } else {
    EXPECT_NE(main_frame->GetSiteInstance(), child_frame->GetSiteInstance());
  }
  EXPECT_EQ(main_frame->GetSiteInstance(), grandchild_frame->GetSiteInstance());
  EXPECT_EQ(main_frame->GetLastCommittedOrigin(),
            grandchild_frame->GetLastCommittedOrigin());
  EXPECT_NE(main_frame->GetLastCommittedOrigin(),
            child_frame->GetLastCommittedOrigin());

  // Navigate the grandchild frame to about:blank
  ASSERT_TRUE(ExecJs(grandchild_frame, "window.name = 'grandchild'"));
  TestNavigationObserver nav_observer(shell()->web_contents(), 1);
  ASSERT_TRUE(
      ExecJs(main_frame,
             "grandchild_window = window.open('about:blank', 'grandchild')"));
  nav_observer.Wait();

  // Verify that the grandchild has the same origin as the main frame (*not* the
  // origin of the parent frame).
  main_frame = static_cast<RenderFrameHostImpl*>(
      shell()->web_contents()->GetPrimaryMainFrame());
  child_frame = main_frame->child_at(0)->current_frame_host();
  grandchild_frame = child_frame->child_at(0)->current_frame_host();
  VerifyResultsOfAboutBlankNavigation(grandchild_frame, main_frame);
}

// The test below verifies that an "about:blank" navigation commits with the
// right origin, even when the initiator of the navigation is not the parent or
// opener of the frame targeted by the navigation.  In the
// GrandchildToAboutBlank... testcases, the navigation is initiated by the
// grandparent of the target frame.
//
// In this test case the navigation forces a process swap of the target frame.
// See also GrandchildToAboutBlank_ABA_SameSite and
// GrandchildToAboutBlank_ABA_CrossSite.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(SubresourceLoadingTest,
                       DISABLED_GrandchildToAboutBlank_ABB_CrossSite) {
  GURL url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b(b))"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // Verify the desired properties of the test setup.
  RenderFrameHostImpl* main_frame = static_cast<RenderFrameHostImpl*>(
      shell()->web_contents()->GetPrimaryMainFrame());
  RenderFrameHostImpl* child_frame =
      main_frame->child_at(0)->current_frame_host();
  RenderFrameHostImpl* grandchild_frame =
      child_frame->child_at(0)->current_frame_host();
  if (AreDefaultSiteInstancesEnabled()) {
    EXPECT_EQ(main_frame->GetSiteInstance(), child_frame->GetSiteInstance());
  } else {
    EXPECT_NE(main_frame->GetSiteInstance(), child_frame->GetSiteInstance());
  }
  EXPECT_EQ(child_frame->GetSiteInstance(),
            grandchild_frame->GetSiteInstance());
  EXPECT_EQ(child_frame->GetLastCommittedOrigin(),
            grandchild_frame->GetLastCommittedOrigin());
  EXPECT_NE(main_frame->GetLastCommittedOrigin(),
            grandchild_frame->GetLastCommittedOrigin());

  // Navigate the grandchild frame to about:blank
  ASSERT_TRUE(ExecJs(grandchild_frame, "window.name = 'grandchild'"));
  TestNavigationObserver nav_observer(shell()->web_contents(), 1);
  ASSERT_TRUE(
      ExecJs(main_frame,
             "grandchild_window = window.open('about:blank', 'grandchild')"));
  nav_observer.Wait();

  // Verify that the grandchild has the same origin as the main frame (*not* the
  // origin of the parent frame).
  main_frame = static_cast<RenderFrameHostImpl*>(
      shell()->web_contents()->GetPrimaryMainFrame());
  child_frame = main_frame->child_at(0)->current_frame_host();
  grandchild_frame = child_frame->child_at(0)->current_frame_host();
  VerifyResultsOfAboutBlankNavigation(grandchild_frame, main_frame);
}

// The test below verifies that an "about:blank" navigation commits with the
// right origin, even when the initiator of the navigation is not the parent or
// opener of the frame targeted by the navigation.  In the
// TopToAboutBlank_CrossSite testcase, the top-level navigation is initiated by
// a cross-site subframe.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(SubresourceLoadingTest,
                       DISABLED_TopToAboutBlank_CrossSite) {
  GURL url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // Verify the desired properties of the test setup.
  RenderFrameHostImpl* main_frame = static_cast<RenderFrameHostImpl*>(
      shell()->web_contents()->GetPrimaryMainFrame());
  RenderFrameHostImpl* child_frame =
      main_frame->child_at(0)->current_frame_host();
  if (AreDefaultSiteInstancesEnabled()) {
    EXPECT_EQ(main_frame->GetSiteInstance(), child_frame->GetSiteInstance());
  } else {
    EXPECT_NE(main_frame->GetSiteInstance(), child_frame->GetSiteInstance());
  }
  url::Origin a_origin =
      url::Origin::Create(embedded_test_server()->GetURL("a.com", "/"));
  url::Origin b_origin =
      url::Origin::Create(embedded_test_server()->GetURL("b.com", "/"));
  EXPECT_EQ(a_origin, main_frame->GetLastCommittedOrigin());
  EXPECT_EQ(b_origin, child_frame->GetLastCommittedOrigin());

  // Have the subframe initiate navigation of the main frame to about:blank.
  //
  // (Note that this scenario is a bit artificial/silly, because the final
  // about:blank frame won't have any same-origin friends that could populate
  // it.  OTOH, it is still important to maintain all the invariants in this
  // scenario.  And it is still possible that a same-origin frame (e.g. in
  // another window in the same BrowsingInstance) exists and can populate the
  // about:blank frame.
  TestNavigationObserver nav_observer(shell()->web_contents(), 1);
  ASSERT_TRUE(ExecJs(child_frame, "window.top.location = 'about:blank'"));
  nav_observer.Wait();

  // Verify that the main frame is the only remaining frame and that it has the
  // same origin as the navigation initiator.
  main_frame = static_cast<RenderFrameHostImpl*>(
      shell()->web_contents()->GetPrimaryMainFrame());
  EXPECT_EQ(0u, main_frame->child_count());
  EXPECT_EQ(b_origin, main_frame->GetLastCommittedOrigin());
  EXPECT_EQ(GURL(url::kAboutBlankURL), main_frame->GetLastCommittedURL());
}

// The test below verifies that an "about:blank" navigation commits with the
// right origin, even when the initiator of the navigation is not the parent or
// opener of the frame targeted by the navigation.  In the
// SameSiteSiblingToAboutBlank_CrossSiteTop testcase, the navigation is
// initiated by a same-origin sibling (notably, not by one of target frame's
// ancestors) and both siblings are subframes of a cross-site main frame.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(SubresourceLoadingTest,
                       DISABLED_SameSiteSiblingToAboutBlank_CrossSiteTop) {
  GURL url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b,b)"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // Name the 2nd child.
  RenderFrameHostImpl* main_frame = static_cast<RenderFrameHostImpl*>(
      shell()->web_contents()->GetPrimaryMainFrame());
  RenderFrameHostImpl* child_frame1 =
      main_frame->child_at(0)->current_frame_host();
  RenderFrameHostImpl* child_frame2 =
      main_frame->child_at(1)->current_frame_host();
  ASSERT_TRUE(ExecJs(child_frame2, "window.name = 'child2'"));

  // Grab `child2` window from the 1st child...
  ASSERT_TRUE(ExecJs(child_frame1, "child2 = window.open('', 'child2')"));
  // ...but make sure that child2's opener doesn't point to child1.
  ASSERT_TRUE(ExecJs(main_frame, "child2 = window.open('', 'child2')"));
  EXPECT_EQ(true, EvalJs(child_frame2, "window.opener == window.top"));

  // From child1 initiate navigation of child2 to about:blank.
  TestNavigationObserver nav_observer(shell()->web_contents(), 1);
  ASSERT_TRUE(ExecJs(child_frame1, "child2.location = 'about:blank'"));
  nav_observer.Wait();

  // Verify that child2 has the origin of the initiator of the navigation.
  main_frame = static_cast<RenderFrameHostImpl*>(
      shell()->web_contents()->GetPrimaryMainFrame());
  child_frame1 = main_frame->child_at(0)->current_frame_host();
  child_frame2 = main_frame->child_at(1)->current_frame_host();
  VerifyResultsOfAboutBlankNavigation(child_frame2, child_frame1);
}

// The test below verifies that an initial empty document has a functional
// URLLoaderFactory.  Note that some aspects of the current behavior (e.g. the
// synchronous re-navigation) are not spec-compliant - see
// https://crbug.com/778318 and https://github.com/whatwg/html/issues/3267.
// Note that the same behavior is expected in the ...NewFrameWithoutSrc and
// in the ...NewFrameWithAboutBlank testcases.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    SubresourceLoadingTest,
    DISABLED_URLLoaderFactoryInInitialEmptyDoc_NewFrameWithoutSrc) {
  GURL opener_url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), opener_url));

  // This inserts an `iframe` element without an `src` attribute.  According to
  // some specs "the browsing context will remain at the initial about:blank
  // page", although other specs suggest that there is an explicit, separate
  // navigation.  See:
  // https://html.spec.whatwg.org/dev/iframe-embed-object.html#the-iframe-element
  // https://html.spec.whatwg.org/multipage/iframe-embed-object.html#shared-attribute-processing-steps-for-iframe-and-frame-elements
  ASSERT_TRUE(ExecJs(shell(), R"( let ifr = document.createElement('iframe');
                                  document.body.appendChild(ifr); )"));
  WaitForLoadStop(shell()->web_contents());
  RenderFrameHost* main_frame = shell()->web_contents()->GetPrimaryMainFrame();
  RenderFrameHost* subframe = ChildFrameAt(main_frame, 0);

  VerifyResultsOfAboutBlankNavigation(subframe, main_frame);
}

// See the doc comment for the
// URLLoaderFactoryInInitialEmptyDoc_NewFrameWithoutSrc test case.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    SubresourceLoadingTest,
    DISABLED_URLLoaderFactoryInInitialEmptyDoc_NewFrameWithAboutBlank) {
  GURL opener_url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), opener_url));

  ASSERT_TRUE(ExecJs(shell(), R"( ifr = document.createElement('iframe');
                                  ifr.src = 'about:blank';
                                  document.body.appendChild(ifr); )"));
  WaitForLoadStop(shell()->web_contents());
  RenderFrameHost* main_frame = shell()->web_contents()->GetPrimaryMainFrame();
  RenderFrameHost* subframe = ChildFrameAt(main_frame, 0);

  VerifyResultsOfAboutBlankNavigation(subframe, main_frame);
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_SameOriginFlagOfSameOriginAboutBlankNavigation) {
  GURL parent_url(embedded_test_server()->GetURL("a.com", "/empty.html"));
  GURL iframe_url(embedded_test_server()->GetURL("a.com", "/empty.html"));
  EXPECT_TRUE(NavigateToURL(shell(), parent_url));

  EXPECT_TRUE(ExecJs(current_frame_host(), JsReplace(R"(
    let iframe = document.createElement('iframe');
    iframe.src = $1;
    document.body.appendChild(iframe);
  )",
                                                     iframe_url)));
  WaitForLoadStop(shell()->web_contents());

  base::RunLoop loop;
  DidFinishNavigationCallback callback(
      shell()->web_contents(),
      base::BindLambdaForTesting([&](NavigationHandle* handle) {
        ASSERT_TRUE(handle->HasCommitted());
        EXPECT_TRUE(handle->IsSameOrigin());
        loop.Quit();
      }));

  // Changing the src to trigger DidFinishNavigationCallback
  EXPECT_TRUE(ExecJs(current_frame_host(), R"(
    document.querySelector("iframe").src = 'about:blank';
  )"));
  loop.Run();
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_SameOriginFlagOfCrossOriginAboutBlankNavigation) {
  GURL parent_url(embedded_test_server()->GetURL("a.com", "/empty.html"));
  GURL iframe_url(embedded_test_server()->GetURL("b.com", "/empty.html"));
  EXPECT_TRUE(NavigateToURL(shell(), parent_url));

  EXPECT_TRUE(ExecJs(current_frame_host(), JsReplace(R"(
    let iframe = document.createElement('iframe');
    iframe.src = $1;
    document.body.appendChild(iframe);
  )",
                                                     iframe_url)));
  WaitForLoadStop(shell()->web_contents());

  base::RunLoop loop;
  DidFinishNavigationCallback callback(
      shell()->web_contents(),
      base::BindLambdaForTesting([&](NavigationHandle* handle) {
        ASSERT_TRUE(handle->HasCommitted());
        EXPECT_FALSE(handle->IsSameOrigin());
        loop.Quit();
      }));

  // Changing the src to trigger DidFinishNavigationCallback
  EXPECT_TRUE(ExecJs(current_frame_host(), R"(
    document.querySelector("iframe").src = 'about:blank';
  )"));
  loop.Run();
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_SameOriginFlagOfSrcdocNavigation) {
  GURL url = embedded_test_server()->GetURL("a.com", "/empty.html");
  GURL cross_origin = embedded_test_server()->GetURL("b.com", "/empty.html");
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // Navigating to about:srcdoc from the initial empty document is always a
  // same-origin navigation:
  // - about:srcdoc is same-origin with the parent.
  // - the initial empty document is same-origin with the parent.
  {
    base::RunLoop loop;
    DidFinishNavigationCallback callback(
        shell()->web_contents(),
        base::BindLambdaForTesting([&](NavigationHandle* handle) {
          ASSERT_TRUE(handle->HasCommitted());
          EXPECT_TRUE(handle->IsSameOrigin());
          loop.Quit();
        }));
    EXPECT_TRUE(ExecJs(current_frame_host(), R"(
      let iframe = document.createElement('iframe');
      iframe.srcdoc = "dummy content";
      document.body.appendChild(iframe);
    )"));
    loop.Run();
  }

  // Now, navigate cross-origin, and back to about:srcdoc with a brand new
  // iframe. The navigation is now considered cross-origin.
  // - the previous document is cross-origin with the parent.
  // - about:srcdoc is same-origin with the parent.
  {
    EXPECT_TRUE(ExecJs(current_frame_host(), JsReplace(R"(
      let iframe2 = document.createElement('iframe');
      iframe2.src = $1;
      iframe2.id = 'iframe2';
      document.body.appendChild(iframe2);
    )",
                                                       cross_origin)));
    WaitForLoadStop(shell()->web_contents());

    base::RunLoop loop;
    DidFinishNavigationCallback callback(
        shell()->web_contents(),
        base::BindLambdaForTesting([&](NavigationHandle* handle) {
          ASSERT_TRUE(handle->HasCommitted());
          EXPECT_FALSE(handle->IsSameOrigin());
          loop.Quit();
        }));
    EXPECT_TRUE(ExecJs(current_frame_host(), R"(
      document.getElementById("iframe2").srcdoc = "dummy content";
    )"));
    loop.Run();
  }
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_SameOriginFlagOfAboutBlankToAboutBlankNavigation) {
  GURL parent_url(embedded_test_server()->GetURL("a.com", "/empty.html"));
  GURL iframe_url(embedded_test_server()->GetURL("b.com", "/empty.html"));
  EXPECT_TRUE(NavigateToURL(shell(), parent_url));

  EXPECT_TRUE(ExecJs(main_frame(), JsReplace(R"(
    let iframe = document.createElement('iframe');
    iframe.src = $1;
    document.body.appendChild(iframe);
  )",
                                             iframe_url)));
  WaitForLoadStop(shell()->web_contents());

  // Test a same-origin about:blank navigation
  {
    base::RunLoop loop;
    DidFinishNavigationCallback callback(
        shell()->web_contents(),
        base::BindLambdaForTesting([&](NavigationHandle* handle) {
          ASSERT_TRUE(handle->HasCommitted());
          EXPECT_TRUE(handle->IsSameOrigin());
          loop.Quit();
        }));
    RenderFrameHostImpl* child_document =
        current_frame_host()->child_at(0)->current_frame_host();
    EXPECT_TRUE(ExecJs(child_document, R"(location.href = "about:blank";)"));
    loop.Run();
  }

  // Test another same-origin about:blank navigation
  {
    base::RunLoop loop;
    DidFinishNavigationCallback callback(
        shell()->web_contents(),
        base::BindLambdaForTesting([&](NavigationHandle* handle) {
          ASSERT_TRUE(handle->HasCommitted());
          EXPECT_TRUE(handle->IsSameOrigin());
          loop.Quit();
        }));
    RenderFrameHostImpl* child_document =
        current_frame_host()->child_at(0)->current_frame_host();
    EXPECT_TRUE(ExecJs(child_document, R"(location.href = "about:blank";)"));
    loop.Run();
  }

  // Test a cross-origin about:blank navigation
  {
    base::RunLoop loop;
    DidFinishNavigationCallback callback(
        shell()->web_contents(),
        base::BindLambdaForTesting([&](NavigationHandle* handle) {
          ASSERT_TRUE(handle->HasCommitted());
          EXPECT_FALSE(handle->IsSameOrigin());
          loop.Quit();
        }));
    EXPECT_TRUE(ExecJs(current_frame_host(), R"(
      document.querySelector('iframe').src = "about:blank";
    )"));
    loop.Run();
  }
}

// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       DISABLED_SameOriginOfSandboxedIframe) {
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.com", "/empty.html")));

  base::RunLoop loop;
  DidFinishNavigationCallback callback(
      shell()->web_contents(),
      base::BindLambdaForTesting([&](NavigationHandle* handle) {
        ASSERT_TRUE(handle->HasCommitted());
        // TODO(https://crbug.com/888079) Take sandbox into account. Same Origin
        // should be true
        EXPECT_FALSE(handle->IsSameOrigin());
        loop.Quit();
      }));
  EXPECT_TRUE(ExecJs(current_frame_host(), R"(
    let iframe = document.createElement('iframe');
    iframe.sandbox = "allow-scripts";
    iframe.src = "/empty.html";
    document.body.appendChild(iframe);
  )"));
  loop.Run();
}

// The test below verifies that an initial empty document has a functional
// URLLoaderFactory.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    SubresourceLoadingTest,
    DISABLED_URLLoaderFactoryInInitialEmptyDoc_NewPopupToEmptyUrl) {
  GURL opener_url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), opener_url));

  WebContentsImpl* popup = nullptr;
  {
    WebContentsAddedObserver popup_observer;
    ASSERT_TRUE(ExecJs(shell(), "window.open('', '_blank')"));
    popup = static_cast<WebContentsImpl*>(popup_observer.GetWebContents());
  }
  WaitForLoadStop(popup);

  // Verify that we are at the initial empty document.
  EXPECT_EQ(1, popup->GetController().GetEntryCount());
  EXPECT_TRUE(popup->GetController().GetLastCommittedEntry()->IsInitialEntry());
  EXPECT_TRUE(
      popup->GetPrimaryFrameTree().root()->is_on_initial_empty_document());

  // Verify that the `popup` is at "about:blank", with expected origin, with
  // working `document.cookie`, and with working subresource loads.
  VerifyResultsOfAboutBlankNavigation(
      popup->GetPrimaryMainFrame(),
      shell()->web_contents()->GetPrimaryMainFrame());
}

// See the doc comment for the
// URLLoaderFactoryInInitialEmptyDoc_NewPopupToEmptyUrl test case.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    SubresourceLoadingTest,
    DISABLED_URLLoaderFactoryInInitialEmptyDoc_NewPopupToAboutBlank) {
  GURL opener_url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), opener_url));

  WebContentsImpl* popup = nullptr;
  {
    WebContentsAddedObserver popup_observer;
    ASSERT_TRUE(ExecJs(shell(), "window.open('about:blank', '_blank')"));
    popup = static_cast<WebContentsImpl*>(popup_observer.GetWebContents());
  }
  WaitForLoadStop(popup);

  // Verify that we are at the synchronously committed about:blank document.
  EXPECT_EQ(1, popup->GetController().GetEntryCount());
  EXPECT_TRUE(popup->GetController().GetLastCommittedEntry()->IsInitialEntry());
  EXPECT_TRUE(
      popup->GetPrimaryFrameTree().root()->is_on_initial_empty_document());

  // Verify other about:blank things.
  VerifyResultsOfAboutBlankNavigation(
      popup->GetPrimaryMainFrame(),
      shell()->web_contents()->GetPrimaryMainFrame());
}

// The test below verifies that error pages have a functional URLLoaderFactory.
// TODO: b/432513261 - Investigate test failure
IN_PROC_BROWSER_TEST_F(SubresourceLoadingTest,
                       DISABLED_URLLoaderFactoryInErrorPage) {
  GURL error_url(embedded_test_server()->GetURL("/close-socket"));
  EXPECT_FALSE(NavigateToURL(shell(), error_url));
  VerifyImageSubresourceLoads(shell()->web_contents()->GetPrimaryMainFrame());
}

// The test below verifies that an initial empty document has a functional
// URLLoaderFactory.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    SubresourceLoadingTest,
    DISABLED_URLLoaderFactoryInInitialEmptyDoc_HungNavigationInSubframe) {
  ASSERT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.com", "/title1.html")));

  // Add a subframe that will never commit a navigation (i.e. that will be stuck
  // on the initial empty document).
  const GURL hung_url = embedded_test_server()->GetURL("a.com", "/hung");
  ASSERT_TRUE(
      ExecJs(shell(), JsReplace(R"(ifr = document.createElement('iframe');
                                   ifr.src = $1;
                                   document.body.appendChild(ifr); )",
                                hung_url)));

  // No process swaps are expected before ReadyToCommit (which will never happen
  // for a navigation to "/hung").  This test assertion double-checks that the
  // test will cover inheriting URLLoaderFactory from the creator/opener/parent
  // frame.
  RenderFrameHost* main_frame = shell()->web_contents()->GetPrimaryMainFrame();
  RenderFrameHost* subframe = ChildFrameAt(main_frame, 0);
  EXPECT_EQ(main_frame->GetProcess()->GetID(), subframe->GetProcess()->GetID());

  // Ask the parent to script the same-origin subframe and trigger some HTTP
  // subresource loads within the subframe.
  //
  // This tests the functionality of the URLLoaderFactory that gets used by the
  // initial empty document.  In this test, the `request_initiator` will be a
  // non-opaque origin - it requires that the URLLoaderFactory will have a
  // matching `request_initiator_origin_lock` (e.g. inherited from the parent).
  VerifyImageSubresourceLoads(shell(), "ifr.contentDocument");
}

// The test below verifies that an initial empty document has a functional
// URLLoaderFactory.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    SubresourceLoadingTest,
    DISABLED_URLLoaderFactoryInInitialEmptyDoc_HungNavigationInPopup) {
  ASSERT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.com", "/title1.html")));

  // Open a popup window that will never commit a navigation (i.e. that will be
  // stuck on the initial empty document).
  const GURL hung_url = embedded_test_server()->GetURL("a.com", "/hung");
  WebContents* popup = nullptr;
  {
    WebContentsAddedObserver popup_observer;
    ASSERT_TRUE(
        ExecJs(shell(), JsReplace("popup = window.open($1)", hung_url)));
    popup = popup_observer.GetWebContents();
  }

  // No process swaps are expected before ReadyToCommit (which will never happen
  // for a navigation to "/hung").  This test assertion double-checks that the
  // test will cover inheriting URLLoaderFactory from the creator/opener/parent
  // frame.
  RenderFrameHost* opener_frame =
      shell()->web_contents()->GetPrimaryMainFrame();
  RenderFrameHost* popup_frame = popup->GetPrimaryMainFrame();
  EXPECT_EQ(opener_frame->GetProcess()->GetID(),
            popup_frame->GetProcess()->GetID());

  // Ask the opener to script the (same-origin) popup window and trigger some
  // HTTP subresource loads within the popup.
  //
  // This tests the functionality of the URLLoaderFactory that gets used by the
  // initial empty document.  In this test, the `request_initiator` will be a
  // non-opaque origin - it requires that the URLLoaderFactory will have a
  // matching `request_initiator_origin_lock` (e.g. inherited from the opener).
  VerifyImageSubresourceLoads(shell(), "popup.document");

  // TODO(https://crbug.com/1194763): Crash recovery doesn't work when there is
  // no opener.
  DontTestNetworkServiceCrashes();
  // Test again after closing the opener..
  shell()->Close();
  VerifyImageSubresourceLoads(popup);
}

// The test below verifies that an initial empty document has a functional
// URLLoaderFactory.  The ...WithClearedOpener testcase is a regression test for
// https://crbug.com/1191203.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    SubresourceLoadingTest,
    DISABLED_URLLoaderFactoryInInitialEmptyDoc_HungNavigationInPopupWithClearedOpener) {
  ASSERT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.com", "/title1.html")));

  // Open a new window that will never commit a navigation (i.e. that will be
  // stuck on the initial empty document).  Clearing of `popup.opener` tests if
  // inheriting of URLLoaderFactory from the opener will work when the opener
  // has been cleared in DOM/Javascript.
  const GURL hung_url = embedded_test_server()->GetURL("a.com", "/hung");
  const char kScriptTemplate[] = R"(
      popup = window.open($1);
      popup.opener = null;
  )";
  content::WebContents* popup = nullptr;
  {
    WebContentsAddedObserver popup_observer;
    ASSERT_TRUE(ExecJs(shell(), JsReplace(kScriptTemplate, hung_url)));
    popup = popup_observer.GetWebContents();
  }

  // No process swaps are expected before ReadyToCommit (which will never happen
  // for a navigation to "/hung").  This test assertion double-checks that the
  // test will cover inheriting URLLoaderFactory from the creator/opener/parent
  // frame.  This differentiates the test from the "noopener" case covered in
  // another testcase.
  RenderFrameHost* opener_frame =
      shell()->web_contents()->GetPrimaryMainFrame();
  RenderFrameHost* popup_frame = popup->GetPrimaryMainFrame();
  EXPECT_EQ(opener_frame->GetProcess()->GetID(),
            popup_frame->GetProcess()->GetID());

  // Double-check that the popup didn't commit any navigation and that it has
  // an the same origin as the initial opener.
  EXPECT_EQ(GURL(), popup->GetPrimaryMainFrame()->GetLastCommittedURL());
  EXPECT_NE("null", EvalJs(popup, "window.origin"));
  EXPECT_EQ(shell()
                ->web_contents()
                ->GetPrimaryMainFrame()
                ->GetLastCommittedOrigin()
                .Serialize(),
            EvalJs(popup, "window.origin"));

  // Use the parent frame's `popup` reference to script the same-origin popup
  // window and trigger some HTTP subresource loads within the popup.
  //
  // This tests the functionality of the URLLoaderFactory that gets used by the
  // initial empty document.  In this test, the `request_initiator` will be a
  // non-opaque origin - it requires that the URLLoaderFactory will have a
  // matching `request_initiator_origin_lock` (e.g. inherited from the opener).
  VerifyImageSubresourceLoads(popup);

  // TODO: b/1194763): Crash recovery doesn't work when there is
  // no opener.
  DontTestNetworkServiceCrashes();
  // Test again after closing the opener..
  shell()->Close();
  VerifyImageSubresourceLoads(popup);
}

// The test below verifies that an initial empty document has a functional
// URLLoaderFactory.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    SubresourceLoadingTest,
    DISABLED_URLLoaderFactoryInInitialEmptyDoc_204NoOpenerPopup) {
  ASSERT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.com", "/title1.html")));

  // Open a new window by following a no-opener link to /nocontent (204).
  const GURL no_content_url =
      embedded_test_server()->GetURL("a.com", "/nocontent");
  const char kScriptTemplate[] = R"(
      let anchor = document.createElement('a');
      anchor.href = $1;
      anchor.rel = 'noopener';
      anchor.target = '_blank';
      anchor.innerText = 'test link';
      document.body.appendChild(anchor);
      anchor.click();
  )";
  content::WebContents* popup = nullptr;
  {
    WebContentsAddedObserver popup_observer;
    ASSERT_TRUE(ExecJs(shell(), JsReplace(kScriptTemplate, no_content_url)));
    popup = popup_observer.GetWebContents();
  }
  WaitForLoadStop(popup);

  // Double-check that the `popup` didn't commit any navigation and that it has
  // an opaque origin.
  EXPECT_EQ(GURL(), popup->GetPrimaryMainFrame()->GetLastCommittedURL());
  EXPECT_EQ("null", EvalJs(popup, "window.origin"));

  // Process swap is expected because of 'noopener'.  This test assertion
  // double-checks that in the test it is not possible to inheriting
  // URLLoaderFactory from the creator/opener/parent frame (because the popup is
  // in another process).
  RenderFrameHost* opener_frame =
      shell()->web_contents()->GetPrimaryMainFrame();
  RenderFrameHost* popup_frame = popup->GetPrimaryMainFrame();
  EXPECT_NE(opener_frame->GetProcess()->GetID(),
            popup_frame->GetProcess()->GetID());

  // Inject Javascript that triggers some subresource loads over HTTP.
  //
  // To some extent, this simulates an ability of 1) Android WebView (see
  // https://crbug.com/1189838) and 2) Chrome Extensions, to inject Javascript
  // into an initial empty document (even when no web/renderer content has
  // access to the document).
  //
  // This tests the functionality of the URLLoaderFactory that gets used by the
  // initial empty document.  In this test, the `request_initiator` will be an
  // opaque, unique origin (since nothing has committed yet) and will be
  // compatible with `request_initiator_origin_lock` of the URLLoaderFactory.
  VerifyImageSubresourceLoads(popup);
}

// The test below verifies that an initial empty document has a functional
// URLLoaderFactory.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    SubresourceLoadingTest,
    DISABLED_URLLoaderFactoryInInitialEmptyDoc_HungNavigationInNewWindow) {
  // Open a new shell, starting at the "/hung" URL.
  const GURL hung_url = embedded_test_server()->GetURL("a.com", "/hung");
  Shell* new_shell =
      Shell::CreateNewWindow(shell()->web_contents()->GetBrowserContext(),
                             hung_url, nullptr, gfx::Size());

  // Wait until the renderer process launches (this will flush the CreateView
  // IPC and make sure that ExecJs and EvalJs are able to work).
  RenderFrameHost* main_frame =
      new_shell->web_contents()->GetPrimaryMainFrame();
  {
    RenderProcessHostWatcher process_watcher(
        main_frame->GetProcess(),
        RenderProcessHostWatcher::WATCH_FOR_PROCESS_READY);
    process_watcher.Wait();
  }

  // Double-check that the new shell didn't commit any navigation and that it
  // has an opaque origin.
  EXPECT_EQ(1, new_shell->web_contents()->GetController().GetEntryCount());
  EXPECT_TRUE(new_shell->web_contents()
                  ->GetController()
                  .GetLastCommittedEntry()
                  ->IsInitialEntry());
  EXPECT_EQ(GURL(), main_frame->GetLastCommittedURL());
  EXPECT_EQ("null", EvalJs(main_frame, "window.origin"));

  // Inject Javascript that triggers some subresource loads over HTTP.
  //
  // To some extent, this simulates an ability of 1) Android WebView (see
  // https://crbug.com/1189838) and 2) Chrome Extensions, to inject Javascript
  // into an initial empty document (even when no web/renderer content has
  // access to the document).
  //
  // This tests the functionality of the URLLoaderFactory that gets used by the
  // initial empty document.  In this test, the `request_initiator` will be an
  // opaque, unique origin (since nothing has committed yet) and will be
  // compatible with `request_initiator_origin_lock` of the URLLoaderFactory.
  VerifyImageSubresourceLoads(main_frame);
}

namespace {

struct Result {
  GURL url;
  bool committed;
};

class NavigationLogger : public WebContentsObserver {
 public:
  explicit NavigationLogger(WebContents* contents)
      : WebContentsObserver(contents) {}

  // WebContentsObserver overrides:
  void DidFinishNavigation(NavigationHandle* handle) override {
    results_.push_back(
        {.url = handle->GetURL(), .committed = handle->HasCommitted()});
  }

  const std::vector<Result>& results() const { return results_; }

 private:
  std::vector<Result> results_;
};

}  // namespace

class CommitNavigationRaceBrowserTest
    : public NavigationBrowserTest,
      public ::testing::WithParamInterface<bool> {
 public:
  CommitNavigationRaceBrowserTest() {
    std::map<std::string, std::string> parameters = {
        {"level", GetParam() ? "full" : "none"},
    };
    feature_list_.InitAndEnableFeatureWithParameters(
        kQueueNavigationsWhileWaitingForCommit, parameters);
  }

  void SetUpOnMainThread() override {
    // These navigation tests require full site isolation since they test races
    // with committing a navigation in a speculative RenderFrameHost..
    if (!AreAllSitesIsolatedForTesting()) {
      GTEST_SKIP();
    }

    NavigationBrowserTest::SetUpOnMainThread();
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    NavigationBrowserTest::SetUpCommandLine(command_line);

    // PerformanceManager maintains its own parallel frame tree and has
    // sometimes been confused by things like `UndoCommitNavigation()`.
    // Force-enable it for test coverage; otherwise, by default,
    // PerformanceManager uses the dummy implementation.
    //
    // TODO: b/1222647): Enable this by default in content_shell.
    command_line->AppendSwitchASCII(switches::kEnableBlinkFeatures,
                                    "PerformanceManagerInstrumentation");
  }

  static std::string DescribeParams(
      const testing::TestParamInfo<ParamType>& info) {
    return info.param ? "UndoCommitNavigation" : "NavigationQueueing";
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

// TODO: b/432536450 - Investigate test failure
IN_PROC_BROWSER_TEST_P(
    CommitNavigationRaceBrowserTest,
    DISABLED_BeginNewNavigationAfterCommitNavigationInMainFrame) {
  ASSERT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.com", "/title1.html")));

  // The crash, if any, will manifest in the b.com renderer. Open a b.com window
  // in the same browsing instance to ensure that the b.com renderer stays
  // around even if the b.com speculative RenderFrameHost is discarded.
  ASSERT_TRUE(ExecJs(
      shell(), JsReplace("window.open($1)", embedded_test_server()->GetURL(
                                                "b.com", "/title1.html"))));
  ASSERT_EQ(2u, Shell::windows().size());
  WebContentsImpl* new_web_contents =
      static_cast<WebContentsImpl*>(Shell::windows()[1]->web_contents());
  WaitForLoadStop(new_web_contents);
  RenderProcessHost* const b_com_render_process_host =
      new_web_contents->GetPrimaryMainFrame()->GetProcess();

  NavigationLogger logger(shell()->web_contents());

  // Start a navigation that will create a speculative RFH in the existing
  // render process for b.com.
  const GURL infinitely_loading_url =
      embedded_test_server()->GetURL("b.com", "/infinitely_loading_image.html");
  ASSERT_TRUE(BeginNavigateToURLFromRenderer(shell(), infinitely_loading_url));

  // Ensure the speculative RFH is in the expected process (i.e. the b.com
  // process that was created for the navigation in the new window earlier).
  WebContentsImpl* web_contents =
      static_cast<WebContentsImpl*>(shell()->web_contents());
  RenderFrameHostImpl* speculative_render_frame_host =
      web_contents->GetPrimaryFrameTree()
          .root()
          ->render_manager()
          ->speculative_frame_host();
  ASSERT_TRUE(speculative_render_frame_host);
  EXPECT_EQ(b_com_render_process_host,
            speculative_render_frame_host->GetProcess());

  // Simulates a race where a new navigation to c.com begins after the browser
  // sends `CommitNavigation() to the b.com renderer, but before
  // `DidCommitNavigation()` has been received from the b.com renderer.
  const GURL final_url =
      embedded_test_server()->GetURL("c.com", "/title1.html");
  BeginNavigationInCommitCallbackInterceptor interceptor(
      web_contents->GetPrimaryFrameTree().root(), final_url);
  speculative_render_frame_host->SetCommitCallbackInterceptorForTesting(
      &interceptor);

  EXPECT_TRUE(WaitForLoadStop(web_contents));
  EXPECT_EQ(final_url, web_contents->GetLastCommittedURL());

  auto results = logger.results();
  ASSERT_EQ(2u, results.size());
  EXPECT_EQ(infinitely_loading_url, results[0].url);
  // If navigation queueing is enabled, the first navigation will complete the
  // commit as the new navigation gets queued until the first navigation's
  // commit finished. If navigation queueing is disabled, the pending commit
  // navigation will be cancelled.
  if (ShouldQueueNavigationsWhenPendingCommitRFHExists()) {
    EXPECT_TRUE(results[0].committed);
  } else {
    EXPECT_FALSE(results[0].committed);
  }
  EXPECT_TRUE(results[1].committed);
  EXPECT_EQ(final_url, results[1].url);
}

// TODO: b/432536450 - Investigate test failure
IN_PROC_BROWSER_TEST_P(
    CommitNavigationRaceBrowserTest,
    DISABLED_BeginNewNavigationAfterCommitNavigationInSubFrame) {
  // This test's process layout is structured a bit differently from the main
  // frame case. PerformanceManager reports when a remote frame is attached to
  // a local parent, and it was previously getting confused by the fact that
  // a `blink::RemoteFrame` with matching RemoteFrameTokens was being reported
  // as attached twice: once by the initial page loaded in the next statement,
  // and the next when the browser needs to send a `UndoCommitNavigation()` to
  // the a.com renderer.
  ASSERT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL(
                   "a.com", "/cross_site_iframe_factory.html?a(b)")));

  WebContentsImpl* web_contents =
      static_cast<WebContentsImpl*>(shell()->web_contents());
  FrameTreeNode* first_subframe_node =
      web_contents->GetPrimaryMainFrame()->child_at(0);
  RenderProcessHost* const a_com_render_process_host =
      web_contents->GetPrimaryFrameTree()
          .root()
          ->render_manager()
          ->current_frame_host()
          ->GetProcess();

  NavigationLogger logger(web_contents);

  // Start a navigation that will create a speculative RFH in the existing
  // render process for a.com.
  const GURL infinitely_loading_url =
      embedded_test_server()->GetURL("a.com", "/infinitely_loading_image.html");
  ASSERT_TRUE(BeginNavigateToURLFromRenderer(first_subframe_node,
                                             infinitely_loading_url));

  // Ensure the speculative RFH is in the expected process.
  RenderFrameHostImpl* speculative_render_frame_host =
      first_subframe_node->render_manager()->speculative_frame_host();
  ASSERT_TRUE(speculative_render_frame_host);
  EXPECT_EQ(a_com_render_process_host,
            speculative_render_frame_host->GetProcess());

  // Update the id attribute to exercise a PerformanceManager-specific code
  // path: when the renderer swaps in a `blink::RemoteFrame` to undo the
  // `CommitNavigation()`, it will report the iframe attribution data again. The
  // PerformanceManager should not complain that V8ContextTracker already has
  // the iframe attribution data, nor should it update the iframe attribution
  // data, to preserve existing behavior (unfortunately, the latter part is not
  // really tested in this browser test).
  EXPECT_TRUE(ExecJs(web_contents,
                     "document.querySelector('iframe').id = 'new-name';"));

  // Simulates a race where a new navigation to c.com begins after the browser
  // sends `CommitNavigation() to the a.com renderer, but before
  // `DidCommitNavigation()` has been received from the a.com renderer.
  const GURL final_url =
      embedded_test_server()->GetURL("c.com", "/title1.html");
  BeginNavigationInCommitCallbackInterceptor interceptor(first_subframe_node,
                                                         final_url);
  speculative_render_frame_host->SetCommitCallbackInterceptorForTesting(
      &interceptor);

  EXPECT_TRUE(WaitForLoadStop(web_contents));
  EXPECT_EQ(final_url, first_subframe_node->render_manager()
                           ->current_frame_host()
                           ->GetLastCommittedURL());

  auto results = logger.results();
  ASSERT_EQ(2u, results.size());
  // If navigation queueing is enabled, the first navigation will complete the
  // commit as the new navigation gets queued until the first navigation's
  // commit finished. If navigation queueing is disabled, the pending commit
  // navigation will be cancelled.
  if (ShouldQueueNavigationsWhenPendingCommitRFHExists()) {
    EXPECT_TRUE(results[0].committed);
  } else {
    EXPECT_FALSE(results[0].committed);
  }
  EXPECT_EQ(infinitely_loading_url, results[0].url);
  EXPECT_TRUE(results[1].committed);
  EXPECT_EQ(final_url, results[1].url);
}

// Helper that ignores a request from the renderer to commit a navigation and
// detaches the nth child (0-indexed) of `frame_tree_node` instead.
class DetachChildFrameInCommitCallbackInterceptor
    : public RenderFrameHostImpl::CommitCallbackInterceptor {
 public:
  DetachChildFrameInCommitCallbackInterceptor(FrameTreeNode* frame_tree_node,
                                              int child_to_detach)
      : frame_tree_node_(frame_tree_node), child_to_detach_(child_to_detach) {}

  bool WillProcessDidCommitNavigation(
      NavigationRequest* request,
      mojom::DidCommitProvisionalLoadParamsPtr* params,
      mojom::DidCommitProvisionalLoadInterfaceParamsPtr* interface_params)
      override {
    request->GetRenderFrameHost()->SetCommitCallbackInterceptorForTesting(
        nullptr);
    // At this point, the renderer has already committed the RenderFrame, but
    // on the browser side, the RenderFrameHost is still speculative.

    // Intentionally do not wait for script completion here. This runs an event
    // loop that pumps incoming messages, but that would cause us to process
    // IPCs from b.com out of order (since process DidCommitNavigation has been
    // interrupted by this hook).
    ExecuteScriptAsync(
        frame_tree_node_.get(),
        JsReplace("document.querySelectorAll('iframe')[$1].remove()",
                  child_to_detach_));

    // However, since it's not possible to wait for `remove()` to take effect,
    // the test must cheat a little and directly call the Mojo IPC that the JS
    // above would eventually trigger.
    frame_tree_node_->child_at(child_to_detach_)
        ->render_manager()
        ->current_frame_host()
        ->Detach();

    // Ignore the commit message and pretend it never arrived.
    return false;
  }

 private:
  const raw_ptr<FrameTreeNode> frame_tree_node_;
  const int child_to_detach_;
};

// Regression test for https://crbug.com/1223837. Previously, if a child frame
// was in the middle of committing a navigation to a provisional frame in render
// process B while render process A simultaneously detaches that child frame,
// the detach message would never be received by render process B.
// TODO: b/432536450 - Investigate test failure
IN_PROC_BROWSER_TEST_P(CommitNavigationRaceBrowserTest,
                       DISABLED_DetachAfterCommitNavigationInSubFrame) {
  // This test checks an edge case that is only relevant when using
  // `UndoCommitNavigation()`.
  if (ShouldQueueNavigationsWhenPendingCommitRFHExists()) {
    GTEST_SKIP();
  }

  ASSERT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL(
                   "a.com", "/cross_site_iframe_factory.html?a(b,a)")));

  WebContentsImpl* const web_contents =
      static_cast<WebContentsImpl*>(shell()->web_contents());
  FrameTreeNode* const first_subframe_node =
      web_contents->GetPrimaryMainFrame()->child_at(0);
  FrameTreeNode* const second_subframe_node =
      web_contents->GetPrimaryMainFrame()->child_at(1);
  RenderProcessHost* const b_com_render_process_host =
      first_subframe_node->render_manager()->current_frame_host()->GetProcess();

  // Start a navigation in the second child frame that will create a speculative
  // RFH in the existing render process for b.com. The first child frame is
  // already hosted in the render process for b.com: this is to ensure the
  // render process remains live even after the second child frame is detached
  // later in this test.
  ASSERT_TRUE(BeginNavigateToURLFromRenderer(
      second_subframe_node,
      embedded_test_server()->GetURL("b.com", "/title1.html")));

  // Ensure the speculative RFH is in the expected process.
  RenderFrameHostImpl* speculative_render_frame_host =
      second_subframe_node->render_manager()->speculative_frame_host();
  ASSERT_TRUE(speculative_render_frame_host);
  EXPECT_EQ(b_com_render_process_host,
            speculative_render_frame_host->GetProcess());

  // Simulates a race where the a.com renderer detaches the second child frame
  // after the browser sends `CommitNavigation()` to the b.com renderer.
  DetachChildFrameInCommitCallbackInterceptor interceptor(
      web_contents->GetPrimaryFrameTree().root(), 1);
  speculative_render_frame_host->SetCommitCallbackInterceptorForTesting(
      &interceptor);

  EXPECT_TRUE(WaitForLoadStop(web_contents));
  // Validate that render process for b.com has handled the detach message for
  // the provisional frame that was committing. Before the fix, the render
  // process for b.com still had the proxy for the second child frame, because
  // the browser process's request to delete it was sent via a broken message
  // pipe. Thus, the frame tree in the render process for b.com incorrectly
  // thought there were still two child frames.
  EXPECT_EQ(1, EvalJs(first_subframe_node, "top.length"));
}

INSTANTIATE_TEST_SUITE_P(,
                         CommitNavigationRaceBrowserTest,
                         ::testing::Bool(),
                         &CommitNavigationRaceBrowserTest::DescribeParams);

// The following test checks what happens if a WebContentsDelegate navigates
// away in response to the NavigationStateChanged event. Previously
// (https://crbug.com/1210234), this was triggering a crash when creating the
// new NavigationRequest, because it was trying to access the current
// RenderFrameHost's PolicyContainerHost, which had not been set up yet by
// RenderFrameHostImpl::DidNavigate.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest, DISABLED_Bug1210234) {
  class NavigationWebContentsDelegate : public WebContentsDelegate {
   public:
    NavigationWebContentsDelegate(const GURL& url_to_intercept,
                                  const GURL& url_to_navigate_to)
        : url_to_intercept_(url_to_intercept),
          url_to_navigate_to_(url_to_navigate_to) {}
    void NavigationStateChanged(WebContents* source,
                                InvalidateTypes changed_flags) override {
      if (!navigated_ && source->GetLastCommittedURL() == url_to_intercept_) {
        navigated_ = true;
        source->GetController().LoadURL(url_to_navigate_to_, Referrer(),
                                        ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                        std::string());
      }
    }

   private:
    bool navigated_ = false;
    GURL url_to_intercept_;
    GURL url_to_navigate_to_;
  };

  GURL warmup_url = embedded_test_server()->GetURL("a.com", "/title1.html");
  GURL initial_url = embedded_test_server()->GetURL("b.com", "/title1.html");
  GURL redirection_url =
      embedded_test_server()->GetURL("c.com", "/title1.html");

  NavigationWebContentsDelegate delegate(initial_url, redirection_url);
  web_contents()->SetDelegate(&delegate);

  ASSERT_TRUE(NavigateToURL(shell(), warmup_url));

  // Note that since we committed a navigation, the next cross-origin navigation
  // will create a speculative RenderFrameHost (when site isolation is enabled).

  // Start the navigation to `initial_url` and wait until the web contents
  // navigates to `redirection_url`. We cannot use helper functions like
  // `NavigateToURLBlockUntilNavigationsComplete` because they wait for
  // DidStopLoading and check the LastCommittedURL when they receive it.
  // However, without SiteIsolation, an earlier DidStopLoading might be received
  // when the WebContents has not yet committed the `redirection_url`.

  // Prepare for the navigation.
  WaitForLoadStop(web_contents());
  TestNavigationObserver navigation_observer(redirection_url);
  navigation_observer.WatchExistingWebContents();

  shell()->LoadURL(initial_url);

  navigation_observer.Wait();

  EXPECT_TRUE(IsLastCommittedEntryOfPageType(web_contents(), PAGE_TYPE_NORMAL));
  EXPECT_EQ(redirection_url, web_contents()->GetLastCommittedURL());
}

class NavigationBrowserTestCredentiallessIframe : public NavigationBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    NavigationBrowserTest::SetUpCommandLine(command_line);

    command_line->AppendSwitch(switches::kEnableBlinkTestFeatures);
  }
};

// TODO: b/432532822 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTestCredentiallessIframe,
                       DISABLED_CredentiallessAttributeIsHonoredByNavigation) {
  GURL main_url = embedded_test_server()->GetURL("/page_with_iframe.html");
  GURL iframe_url_1 = embedded_test_server()->GetURL("/title1.html");
  GURL iframe_url_2 = embedded_test_server()->GetURL("/title2.html");
  EXPECT_TRUE(NavigateToURL(shell(), main_url));

  // The main page has a child iframe with url `iframe_url_1`.
  EXPECT_EQ(1U, main_frame()->child_count());
  FrameTreeNode* child = main_frame()->child_at(0);
  EXPECT_EQ(iframe_url_1, child->current_url());
  EXPECT_FALSE(child->Credentialless());
  EXPECT_FALSE(child->current_frame_host()->IsCredentialless());
  EXPECT_EQ(false,
            EvalJs(child->current_frame_host(), "window.credentialless"));

  // Changes to the iframe 'credentialless' attribute are propagated to the
  // FrameTreeNode. The RenderFrameHost, however, is updated only on navigation.
  EXPECT_TRUE(
      ExecJs(main_frame(),
             "document.getElementById('test_iframe').credentialless = true;"));
  EXPECT_TRUE(child->Credentialless());
  EXPECT_FALSE(child->current_frame_host()->IsCredentialless());
  EXPECT_EQ(false,
            EvalJs(child->current_frame_host(), "window.credentialless"));

  // Create a grandchild iframe.
  EXPECT_TRUE(ExecJs(
      child, JsReplace("let grandchild = document.createElement('iframe');"
                       "grandchild.src = $1;"
                       "document.body.appendChild(grandchild);",
                       iframe_url_2)));
  WaitForLoadStop(web_contents());
  EXPECT_EQ(1U, child->child_count());
  FrameTreeNode* grandchild = child->child_at(0);

  // The grandchild FrameTreeNode does not set the 'credentialless'
  // attribute. The grandchild RenderFrameHost is not credentialless, since its
  // parent RenderFrameHost is not credentialless.
  EXPECT_FALSE(grandchild->Credentialless());
  EXPECT_FALSE(grandchild->current_frame_host()->IsCredentialless());
  EXPECT_EQ(false,
            EvalJs(grandchild->current_frame_host(), "window.credentialless"));

  // Navigate the child iframe same-document. This does not change anything.
  EXPECT_TRUE(ExecJs(main_frame(),
                     JsReplace("document.getElementById('test_iframe')"
                               "        .contentWindow.location.href = $1;",
                               iframe_url_1.Resolve("#here").spec())));
  WaitForLoadStop(web_contents());
  EXPECT_TRUE(child->Credentialless());
  EXPECT_FALSE(child->current_frame_host()->IsCredentialless());
  EXPECT_EQ(false,
            EvalJs(child->current_frame_host(), "window.credentialless"));

  // Now navigate the child iframe cross-document.
  EXPECT_TRUE(ExecJs(
      main_frame(), JsReplace("document.getElementById('test_iframe').src = $1",
                              iframe_url_2)));
  WaitForLoadStop(web_contents());
  EXPECT_TRUE(child->Credentialless());
  EXPECT_TRUE(child->current_frame_host()->IsCredentialless());
  EXPECT_EQ(true, EvalJs(child->current_frame_host(), "window.credentialless"));
  // A credentialless document has a storage key with a nonce.
  EXPECT_TRUE(child->current_frame_host()->storage_key().nonce().has_value());
  base::UnguessableToken credentialless_nonce =
      current_frame_host()->credentialless_iframes_nonce();
  EXPECT_EQ(credentialless_nonce,
            child->current_frame_host()->storage_key().nonce().value());

  // Create a grandchild iframe.
  EXPECT_TRUE(ExecJs(
      child, JsReplace("let grandchild = document.createElement('iframe');"
                       "grandchild.id = 'grandchild_iframe';"
                       "document.body.appendChild(grandchild);",
                       iframe_url_1)));
  EXPECT_EQ(1U, child->child_count());
  grandchild = child->child_at(0);

  // The grandchild does not set the 'credentialless' attribute, but the
  // grandchild document is credentialless.
  EXPECT_FALSE(grandchild->Credentialless());
  EXPECT_TRUE(grandchild->current_frame_host()->IsCredentialless());
  EXPECT_EQ(true,
            EvalJs(grandchild->current_frame_host(), "window.credentialless"));

  // The storage key's nonce is the same for all credentialless documents in the
  // same page.
  EXPECT_TRUE(child->current_frame_host()->storage_key().nonce().has_value());
  EXPECT_EQ(credentialless_nonce,
            child->current_frame_host()->storage_key().nonce().value());

  // Now navigate the grandchild iframe.
  EXPECT_TRUE(ExecJs(
      child, JsReplace("document.getElementById('grandchild_iframe').src = $1",
                       iframe_url_2)));
  WaitForLoadStop(web_contents());
  EXPECT_TRUE(grandchild->current_frame_host()->IsCredentialless());
  EXPECT_EQ(true,
            EvalJs(grandchild->current_frame_host(), "window.credentialless"));

  // The storage key's nonce is still the same.
  EXPECT_TRUE(child->current_frame_host()->storage_key().nonce().has_value());
  EXPECT_EQ(credentialless_nonce,
            child->current_frame_host()->storage_key().nonce().value());

  // Remove the 'credentialless' attribute from the iframe. This propagates to
  // the FrameTreeNode. The RenderFrameHost, however, is updated only on
  // navigation.
  EXPECT_TRUE(
      ExecJs(main_frame(),
             "document.getElementById('test_iframe').credentialless = false;"));
  EXPECT_FALSE(child->Credentialless());
  EXPECT_TRUE(child->current_frame_host()->IsCredentialless());
  EXPECT_EQ(true, EvalJs(child->current_frame_host(), "window.credentialless"));
  EXPECT_TRUE(child->current_frame_host()->storage_key().nonce().has_value());
  EXPECT_EQ(credentialless_nonce,
            child->current_frame_host()->storage_key().nonce().value());

  // Create another grandchild iframe. Even if the parent iframe element does
  // not have the 'credentialless' attribute anymore, the grandchild document is
  // still loaded inside of a credentialless RenderFrameHost, so it will be
  // credentialless.
  EXPECT_TRUE(ExecJs(
      child, JsReplace("let grandchild2 = document.createElement('iframe');"
                       "document.body.appendChild(grandchild2);",
                       iframe_url_1)));
  EXPECT_EQ(2U, child->child_count());
  FrameTreeNode* grandchild2 = child->child_at(1);
  EXPECT_FALSE(grandchild2->Credentialless());
  EXPECT_TRUE(grandchild2->current_frame_host()->IsCredentialless());
  EXPECT_EQ(true,
            EvalJs(grandchild2->current_frame_host(), "window.credentialless"));
  EXPECT_TRUE(
      grandchild2->current_frame_host()->storage_key().nonce().has_value());
  EXPECT_EQ(credentialless_nonce,
            grandchild2->current_frame_host()->storage_key().nonce().value());

  // Navigate the child iframe. Since the iframe element does not set the
  // 'credentialless' attribute, the resulting RenderFrameHost will not be
  // credentialless.
  EXPECT_TRUE(
      ExecJs(main_frame(),
             JsReplace("document.getElementById('test_iframe').src = $1;",
                       iframe_url_2)));
  WaitForLoadStop(web_contents());
  EXPECT_FALSE(child->Credentialless());
  EXPECT_FALSE(child->current_frame_host()->IsCredentialless());
  EXPECT_EQ(false,
            EvalJs(child->current_frame_host(), "window.credentialless"));
  EXPECT_FALSE(child->current_frame_host()->storage_key().nonce().has_value());

  // Now navigate the whole page away.
  GURL main_url_b = embedded_test_server()->GetURL(
      "b.com", "/page_with_credentialless_iframe.html");
  GURL iframe_url_b = embedded_test_server()->GetURL("b.com", "/title1.html");
  EXPECT_TRUE(NavigateToURL(shell(), main_url_b));

  // The main page has a credentialless child iframe with url `iframe_url_b`.
  EXPECT_EQ(1U, main_frame()->child_count());
  FrameTreeNode* child_b = main_frame()->child_at(0);
  EXPECT_EQ(iframe_url_b, child_b->current_url());
  EXPECT_TRUE(child_b->Credentialless());
  EXPECT_TRUE(child_b->current_frame_host()->IsCredentialless());
  EXPECT_EQ(true,
            EvalJs(child_b->current_frame_host(), "window.credentialless"));

  EXPECT_TRUE(child_b->current_frame_host()->storage_key().nonce().has_value());
  base::UnguessableToken credentialless_nonce_b =
      current_frame_host()->credentialless_iframes_nonce();
  EXPECT_NE(credentialless_nonce, credentialless_nonce_b);
  EXPECT_EQ(credentialless_nonce_b,
            child_b->current_frame_host()->storage_key().nonce().value());
}

// Ensures that OpenURLParams::FromNavigationHandle translates navigation params
// correctly when used to initiate a navigation in another WebContents.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_FromNavigationHandleTranslatesNavigationParamsCorrectly) {
  // Test that the params are translated correctly for a redirected navigation.
  const GURL kRedirectedURL(
      embedded_test_server()->GetURL("/server-redirect?/simple_page.html"));
  NavigationController::LoadURLParams load_params(kRedirectedURL);
  TestNavigationManager first_tab_manager(web_contents(), kRedirectedURL);
  web_contents()->GetController().LoadURLWithParams(load_params);

  // Wait for response to allow the navigation to resolve the redirect.
  EXPECT_TRUE(first_tab_manager.WaitForResponse());

  // Create LoadURLParams from the navigation after redirection.
  NavigationController::LoadURLParams load_url_params(
      OpenURLParams::FromNavigationHandle(
          first_tab_manager.GetNavigationHandle()));
  Shell* second_tab = CreateBrowser();
  TestNavigationManager second_tab_manager(second_tab->web_contents(),
                                           load_url_params.url);
  second_tab->web_contents()->GetController().LoadURLWithParams(
      load_url_params);

  EXPECT_TRUE(second_tab_manager.WaitForResponse());

  // Ensure params from the navigation in the first tab are translated to the
  // navigation in the second tab as expected.
  auto* first_tab_handle = first_tab_manager.GetNavigationHandle();
  auto* second_tab_handle = second_tab_manager.GetNavigationHandle();
  EXPECT_EQ(embedded_test_server()->GetURL("/simple_page.html"),
            second_tab_handle->GetURL());
  EXPECT_EQ(first_tab_handle->GetReferrer(), second_tab_handle->GetReferrer());
  EXPECT_TRUE(
      ui::PageTransitionCoreTypeIs(first_tab_handle->GetPageTransition(),
                                   second_tab_handle->GetPageTransition()));
  EXPECT_EQ(first_tab_handle->IsRendererInitiated(),
            second_tab_handle->IsRendererInitiated());
  EXPECT_EQ(first_tab_handle->GetInitiatorOrigin(),
            second_tab_handle->GetInitiatorOrigin());
  EXPECT_EQ(first_tab_handle->GetSourceSiteInstance(),
            second_tab_handle->GetSourceSiteInstance());
  EXPECT_EQ(first_tab_handle->HasUserGesture(),
            second_tab_handle->HasUserGesture());
  EXPECT_EQ(first_tab_handle->WasStartedFromContextMenu(),
            second_tab_handle->WasStartedFromContextMenu());
  EXPECT_EQ(first_tab_handle->GetHrefTranslate(),
            second_tab_handle->GetHrefTranslate());
  EXPECT_EQ(first_tab_handle->GetReloadType(),
            second_tab_handle->GetReloadType());
  EXPECT_EQ(first_tab_handle->GetRedirectChain(),
            second_tab_handle->GetRedirectChain());
}

// Regression test for https://crbug.com/1392653.  Ensure that loading a URL
// that doesn't go through the network stack but does assign a site for its
// SiteInstance in an unassigned SiteInstance does not fail.  An example of
// such a URL is about:srcdoc. This ensures that the SiteInstance's site is set
// even on the WillCommitWithoutUrlLoader() path in NavigationRequest.
IN_PROC_BROWSER_TEST_F(NavigationBrowserTest,
                       AboutSrcdocInjectedOnAboutBlankPage) {
  // Start on an about:blank page, which should stay in an unassigned
  // SiteInstance.
  EXPECT_TRUE(NavigateToURL(shell(), GURL(url::kAboutBlankURL)));
  SiteInstanceImpl* site_instance = current_frame_host()->GetSiteInstance();
  EXPECT_FALSE(site_instance->HasSite());

  // The process should be considered unused at this point.
  EXPECT_TRUE(site_instance->GetProcess()->IsUnused());

  // Inject a srcdoc iframe into the blank document.  This shouldn't really be
  // possible on the open web, since an about:blank page with an unassigned
  // SiteInstance shouldn't be scriptable by other pages, but it could still
  // happen in automation scenarios or through DevTools.
  TestNavigationObserver navigation_observer(web_contents());
  EXPECT_TRUE(ExecJs(current_frame_host(), JsReplace(R"(
    let frame = document.createElement('iframe');
    frame.srcdoc = 'test';
    document.body.appendChild(frame);
  )")));
  navigation_observer.Wait();
  EXPECT_TRUE(navigation_observer.last_navigation_succeeded());
  EXPECT_EQ("about:srcdoc", navigation_observer.last_navigation_url());

  // The srcdoc child should stay in its about:blank parent SiteInstance.
  EXPECT_EQ(1U, main_frame()->child_count());
  FrameTreeNode* child = main_frame()->child_at(0);
  EXPECT_EQ(child->current_frame_host()->GetSiteInstance(), site_instance);

  // Committing an about:srcdoc navigation currently forces the SiteInstance's
  // site to be set. Prior to fixing https://crbug.com/1392653, this happened
  // after the actual commit was processed at DidNavigate() time, which is a
  // path that is no longer supported, and hence this triggered a NOTREACHED().
  // Now, the site should be set before we send the CommitNavigation IPC.
  EXPECT_TRUE(site_instance->HasSite());

  if (AreDefaultSiteInstancesEnabled()) {
    EXPECT_TRUE(site_instance->IsDefaultSiteInstance());
    EXPECT_EQ(SiteInstanceImpl::GetDefaultSiteURL(),
              site_instance->GetSiteInfo().site_url());
  } else {
    // When we get into this situation with strict site isolation, the site URL
    // currently used is "about:". This may be changed in the future (e.g., to
    // an opaque ID).
    EXPECT_EQ("about:", site_instance->GetSiteInfo().site_url());
  }

  // Ensure that the process was marked as used as part of setting the site.
  EXPECT_FALSE(site_instance->GetProcess()->IsUnused());
}

class CacheTransparencyNavigationBrowserTest : public ContentBrowserTest {
 public:
  CacheTransparencyNavigationBrowserTest() {
    EXPECT_TRUE(embedded_test_server()->Start());

    pervasive_payload_url_ = embedded_test_server()->GetURL(kPervasivePayload);
    std::string pervasive_payloads_params = base::StrCat(
        {"1,", pervasive_payload_url_.spec(),
         ",2478392C652868C0AAF0316A28284610DBDACF02D66A00B39F3BA75D887F4829"});

    feature_list_.InitWithFeaturesAndParameters(
        {{features::kNetworkServiceInProcess, {}},
         {network::features::kPervasivePayloadsList,
          {{"pervasive-payloads", pervasive_payloads_params}}},
         {network::features::kCacheTransparency, {}},
         {net::features::kSplitCacheByNetworkIsolationKey, {}}},
        {/* disabled_features */});
  }

  void ExpectCacheUsed() const {
    histogram_tester_.ExpectUniqueSample(kCacheUsedHistogram, 0, 1);
  }

  void ExpectCacheNotUsed() const {
    histogram_tester_.ExpectTotalCount(kCacheUsedHistogram, 0);
  }

 private:
  static constexpr char kPervasivePayload[] =
      "/cache_transparency/pervasive.js";
  static constexpr char kCacheUsedHistogram[] =
      "Network.CacheTransparency2.SingleKeyedCacheIsUsed";

  base::test::ScopedFeatureList feature_list_;
  GURL pervasive_payload_url_;
  base::HistogramTester histogram_tester_;
};

// TODO: b/432532281 - Investigate test failure
IN_PROC_BROWSER_TEST_F(CacheTransparencyNavigationBrowserTest,
                       DISABLED_SuccessfulPervasivePayload) {
  GURL url_main_document =
      embedded_test_server()->GetURL("/cache_transparency/pervasive.html");

  EXPECT_TRUE(NavigateToURL(shell(), url_main_document));

  ExpectCacheUsed();
}

// TODO: b/432532281 - Investigate test failure
IN_PROC_BROWSER_TEST_F(CacheTransparencyNavigationBrowserTest,
                       DISABLED_NotAPervasivePayload) {
  GURL url_main_document =
      embedded_test_server()->GetURL("/cache_transparency/cacheable.html");

  EXPECT_TRUE(NavigateToURL(shell(), url_main_document));

  ExpectCacheNotUsed();
}

class NavigationBrowserTestWarnSandboxIneffective
    : public NavigationBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    NavigationBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(switches::kEnableBlinkFeatures,
                                    "WarnSandboxIneffective");
  }

  static constexpr char kSandboxEscapeWarningMessage[] =
      "An iframe which has both allow-scripts and allow-same-origin for its "
      "sandbox attribute can escape its sandboxing.";
};

// TODO: b/432529537 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTestWarnSandboxIneffective,
                       DISABLED_WarnEscapableSandboxSameOrigin) {
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.com", "/empty.html")));

  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern(kSandboxEscapeWarningMessage);

  // Create same-origin iframe.
  EXPECT_TRUE(ExecJs(current_frame_host(), R"(
      const iframe = document.createElement("iframe");
      iframe.src = location.href;  // Same-origin iframe.
      iframe.sandbox = "allow-same-origin allow-scripts";
      document.body.appendChild(iframe);
  )"));
  ASSERT_TRUE(console_observer.Wait());
}

// TODO: b/432529537 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTestWarnSandboxIneffective,
                       DISABLED_WarnEscapableSandboxCrossOrigin) {
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.com", "/empty.html")));

  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern(kSandboxEscapeWarningMessage);

  // Create cross-origin iframe.
  EXPECT_TRUE(ExecJs(current_frame_host(), R"(
      const iframe = document.createElement("iframe");
      // Cross-origin iframe:
      iframe.src = location.href.replace("a.com", "b.com");
      iframe.sandbox = "allow-same-origin allow-scripts";
      document.body.appendChild(iframe);
  )"));

  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  EXPECT_EQ(console_observer.messages().size(), 0u);
}

// TODO: b/432529537 - Investigate test failure
IN_PROC_BROWSER_TEST_F(NavigationBrowserTestWarnSandboxIneffective,
                       DISABLED_WarnEscapableSandboxSameOriginGrandChild) {
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.com", "/empty.html")));

  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern(kSandboxEscapeWarningMessage);

  // Create a same-origin doubly nested sandboxed iframe.
  EXPECT_TRUE(ExecJs(current_frame_host(), R"(
      const child = document.createElement("iframe");
      document.body.appendChild(child);

      const grand_child = child.contentDocument.createElement("iframe");
      grand_child.src = location.href;
      grand_child.sandbox = "allow-same-origin allow-scripts";
      child.contentDocument.body.appendChild(grand_child);
  )"));

  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  EXPECT_EQ(console_observer.messages().size(), 0u);
}

// We may have an unload handler in the main frame or a subframe or nowhere.
enum class UnloadFrameType {
  kMainFrame,
  kSubFrame,
  kNone,
};

static std::string ToString(UnloadFrameType v) {
  switch (v) {
    case UnloadFrameType::kMainFrame:
      return "MainFrame";
    case UnloadFrameType::kSubFrame:
      return "SubFrame";
    case UnloadFrameType::kNone:
      return "None";
  }
}

// We may navigate the main frame, the subframe that may have an unload handler
// or another subframe that will never have an unload handler.
enum class NavigateFrameType {
  kMainFrame,
  kSubFrame,
  kOther,
};

static std::string ToString(NavigateFrameType v) {
  switch (v) {
    case NavigateFrameType::kMainFrame:
      return "MainFrame";
    case NavigateFrameType::kSubFrame:
      return "SubFrame";
    case NavigateFrameType::kOther:
      return "Other";
  }
}

void AddUnloadHandler(RenderFrameHostImpl* rfh) {
  ASSERT_TRUE(ExecJs(rfh, "addEventListener('unload', () => {})"));
  ASSERT_TRUE(rfh->GetSuddenTerminationDisablerState(
      blink::mojom::SuddenTerminationDisablerType::kUnloadHandler));
}

class NavigationSuddenTerminationDisablerTypeBrowserTest
    : public NavigationBrowserTest,
      public ::testing::WithParamInterface<
          std::tuple<UnloadFrameType, NavigateFrameType>> {
 public:
  static std::string DescribeParams(
      const ::testing::TestParamInfo<ParamType>& info) {
    return "Unload" + ToString(std::get<0>(info.param)) + "Navigate" +
           ToString(std::get<1>(info.param));
  }

 protected:
  UnloadFrameType GetUnloadFrameType() { return std::get<0>(GetParam()); }
  NavigateFrameType GetNavigateFrameType() { return std::get<1>(GetParam()); }

  void MaybeAddUnloadHandler() {
    RenderFrameHostImpl* rfh = nullptr;
    switch (GetUnloadFrameType()) {
      case UnloadFrameType::kMainFrame:
        rfh = current_frame_host();
        break;
      case UnloadFrameType::kSubFrame:
        rfh = DescendantRenderFrameHostImplAt(current_frame_host(), {0});
        break;
      case UnloadFrameType::kNone:
        break;
    }
    if (rfh) {
      AddUnloadHandler(rfh);
    }
  }

  RenderFrameHostImpl* GetFrameToNavigate() {
    switch (GetNavigateFrameType()) {
      case NavigateFrameType::kMainFrame:
        return current_frame_host();
      case NavigateFrameType::kSubFrame:
        return DescendantRenderFrameHostImplAt(current_frame_host(), {0});
      case NavigateFrameType::kOther:
        return DescendantRenderFrameHostImplAt(current_frame_host(), {1});
    }
  }

  bool NavigatedFrameHasUnload() {
    switch (GetNavigateFrameType()) {
      case NavigateFrameType::kOther:
        // It never has unload.
        return false;
      case NavigateFrameType::kMainFrame:
        // Navigating the main will have unload if there is any unload.
        return GetUnloadFrameType() != UnloadFrameType::kNone;
      case NavigateFrameType::kSubFrame:
        // Navigating the subframe will have unload if the subframe has unload.
        return GetUnloadFrameType() == UnloadFrameType::kSubFrame;
    }
  }
};

// Set up a page with 2 subframes. The main frame or one of the subframes may
// have an unload handler. Then navigate one of the frames and verify that we
// correctly record which type of frame navigates combined with whether it
// involved an unload handler.
// TODO: b/432530029 - Investigate test failure
IN_PROC_BROWSER_TEST_P(NavigationSuddenTerminationDisablerTypeBrowserTest,
                       DISABLED_RecordUma) {
  ASSERT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL(
                   "a.com", "/cross_site_iframe_factory.html?a(a,a)")));

  // Set up the unload handler if needed.
  MaybeAddUnloadHandler();

  // Navigate the relevant frame and capture histograms.
  base::HistogramTester histograms;
  ASSERT_TRUE(NavigateFrameToURL(GetFrameToNavigate()->frame_tree_node(),
                                 GURL("about:blank")));

  // Check that we got the expected histogram value.
  uint32_t expected_histogram_value = 0;
  if (GetNavigateFrameType() == NavigateFrameType::kMainFrame) {
    expected_histogram_value |= RenderFrameHostImpl::
        NavigationSuddenTerminationDisablerType::kMainFrame;
  }
  if (NavigatedFrameHasUnload()) {
    expected_histogram_value |=
        RenderFrameHostImpl::NavigationSuddenTerminationDisablerType::kUnload;
  }
  histograms.ExpectUniqueSample(
      "Navigation.SuddenTerminationDisabler.AllOrigins",
      expected_histogram_value, 1);
  histograms.ExpectUniqueSample(
      "Navigation.SuddenTerminationDisabler.SameOrigin",
      expected_histogram_value, 1);
}

INSTANTIATE_TEST_SUITE_P(
    All,
    NavigationSuddenTerminationDisablerTypeBrowserTest,
    ::testing::Combine(::testing::Values(UnloadFrameType::kMainFrame,
                                         UnloadFrameType::kSubFrame,
                                         UnloadFrameType::kNone),
                       ::testing::Values(NavigateFrameType::kMainFrame,
                                         NavigateFrameType::kSubFrame,
                                         NavigateFrameType::kOther)),
    &NavigationSuddenTerminationDisablerTypeBrowserTest::DescribeParams);

// Test that "SameOrigin" only considers frames that have an unbroken path of
// same-origin frames from the frame that navigates.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_NavigationSuddenTerminationDisablerTypeRecordUmaSameOrigin) {
  ASSERT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL(
                   "a.com", "/cross_site_iframe_factory.html?a(b(a))")));

  // Set up the unload handler in the a.com subframe.
  AddUnloadHandler(
      DescendantRenderFrameHostImplAt(current_frame_host(), {0, 0}));
  // Navigate the main frame and capture histograms.
  base::HistogramTester histograms;
  ASSERT_TRUE(NavigateToURL(shell(), GURL("about:blank")));

  histograms.ExpectUniqueSample(
      "Navigation.SuddenTerminationDisabler.AllOrigins",
      RenderFrameHostImpl::NavigationSuddenTerminationDisablerType::kMainFrame |
          RenderFrameHostImpl::NavigationSuddenTerminationDisablerType::kUnload,
      1);
  histograms.ExpectUniqueSample(
      "Navigation.SuddenTerminationDisabler.SameOrigin",
      RenderFrameHostImpl::NavigationSuddenTerminationDisablerType::kMainFrame,
      1);
}

// Test that we record when the navigation involves restoring from BFCache.
// This is tested because the code path for a navigation involving activation
// is different from one involving a pageload.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_NavigationSuddenTerminationDisablerTypeRecordUmaActivation) {
  ASSERT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.com", "/title1.html")));

  ASSERT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  // Set up the unload handler in the b.com page.
  AddUnloadHandler(current_frame_host());
  // Navigate the main frame and capture histograms.
  base::HistogramTester histograms;
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  histograms.ExpectUniqueSample(
      "Navigation.SuddenTerminationDisabler.AllOrigins",
      RenderFrameHostImpl::NavigationSuddenTerminationDisablerType::kMainFrame |
          RenderFrameHostImpl::NavigationSuddenTerminationDisablerType::kUnload,
      1);
  histograms.ExpectUniqueSample(
      "Navigation.SuddenTerminationDisabler.SameOrigin",
      RenderFrameHostImpl::NavigationSuddenTerminationDisablerType::kMainFrame |
          RenderFrameHostImpl::NavigationSuddenTerminationDisablerType::kUnload,
      1);
}

// Ensure that the first navigation of a subframe away from the initial empty
// document is recorded correctly. This does not test all possibilities of
// histogram value, just that the scenario is counted under the correct
// histogram.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_NavigationSuddenTerminationDisablerTypeRecordUmaInitialEmptyDocument) {
  GURL url = embedded_test_server()->GetURL("a.com", "/title1.html");
  ASSERT_TRUE(NavigateToURL(shell(), url));

  // Create a subframe with an unload handler.
  ASSERT_TRUE(ExecJs(web_contents(), R"(
    var i = document.createElement("iframe");
    document.body.appendChild(i);
  )"));

  AddUnloadHandler(DescendantRenderFrameHostImplAt(current_frame_host(), {0}));

  // Navigate the subframe and capture histograms.
  base::HistogramTester histograms;
  uint32_t expected_histogram_value =
      RenderFrameHostImpl::NavigationSuddenTerminationDisablerType::kUnload |
      RenderFrameHostImpl::NavigationSuddenTerminationDisablerType::
          kInitialEmptyDocument |
      RenderFrameHostImpl::NavigationSuddenTerminationDisablerType::kNotHttp;
  ASSERT_TRUE(NavigateToURLFromRenderer(
      DescendantRenderFrameHostImplAt(current_frame_host(), {0}), url));
  histograms.ExpectUniqueSample(
      "Navigation.SuddenTerminationDisabler.AllOrigins",
      expected_histogram_value, 1);
  histograms.ExpectUniqueSample(
      "Navigation.SuddenTerminationDisabler.SameOrigin",
      expected_histogram_value, 1);
}

// Ensure that navigations from non-HTTP(S) pages are recorded correctly.
// TODO: b/432503432 - Investigate test failure
IN_PROC_BROWSER_TEST_F(
    NavigationBrowserTest,
    DISABLED_NavigationSuddenTerminationDisablerTypeRecordUmaNotHttp) {
  GURL blank_url("about:blank");
  GURL url = embedded_test_server()->GetURL("a.com", "/title1.html");
  ASSERT_TRUE(NavigateToURL(shell(), blank_url));

  AddUnloadHandler(current_frame_host());

  // Navigate the subframe and capture histograms.
  base::HistogramTester histograms;
  uint32_t expected_histogram_value =
      RenderFrameHostImpl::NavigationSuddenTerminationDisablerType::kMainFrame |
      RenderFrameHostImpl::NavigationSuddenTerminationDisablerType::kUnload |
      RenderFrameHostImpl::NavigationSuddenTerminationDisablerType::kNotHttp;
  ASSERT_TRUE(NavigateToURL(web_contents(), url));
  histograms.ExpectUniqueSample(
      "Navigation.SuddenTerminationDisabler.AllOrigins",
      expected_histogram_value, 1);
  histograms.ExpectUniqueSample(
      "Navigation.SuddenTerminationDisabler.SameOrigin",
      expected_histogram_value, 1);
}

}  // namespace content
