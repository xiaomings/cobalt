// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/peerconnection/rtc_peer_connection_handler.h"

#include <stddef.h>
#include <string.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/values.h"
#include "build/build_config.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/platform/modules/mediastream/web_media_stream_track.h"
#include "third_party/blink/public/platform/modules/mediastream/web_platform_media_stream_source.h"
#include "third_party/blink/public/platform/scheduler/test/renderer_scheduler_test_support.h"
#include "third_party/blink/public/platform/web_url.h"
#include "third_party/blink/public/web/web_heap.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/testing/sim/sim_test.h"
#include "third_party/blink/renderer/modules/mediastream/media_stream_video_track.h"
#include "third_party/blink/renderer/modules/mediastream/mock_media_stream_video_source.h"
#include "third_party/blink/renderer/modules/mediastream/processed_local_audio_source.h"
#include "third_party/blink/renderer/modules/mediastream/testing_platform_support_with_mock_audio_capture_source.h"
#include "third_party/blink/renderer/modules/peerconnection/adapters/web_rtc_cross_thread_copier.h"
#include "third_party/blink/renderer/modules/peerconnection/mock_data_channel_impl.h"
#include "third_party/blink/renderer/modules/peerconnection/mock_peer_connection_dependency_factory.h"
#include "third_party/blink/renderer/modules/peerconnection/mock_peer_connection_impl.h"
#include "third_party/blink/renderer/modules/peerconnection/mock_rtc_peer_connection_handler_client.h"
#include "third_party/blink/renderer/modules/peerconnection/mock_rtc_peer_connection_handler_platform.h"
#include "third_party/blink/renderer/modules/peerconnection/peer_connection_tracker.h"
#include "third_party/blink/renderer/modules/peerconnection/testing/fake_resource_listener.h"
#include "third_party/blink/renderer/modules/webrtc/webrtc_audio_device_impl.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/mediastream/media_stream_audio_source.h"
#include "third_party/blink/renderer/platform/mediastream/media_stream_audio_track.h"
#include "third_party/blink/renderer/platform/mediastream/media_stream_component_impl.h"
#include "third_party/blink/renderer/platform/mediastream/media_stream_descriptor.h"
#include "third_party/blink/renderer/platform/peerconnection/rtc_dtmf_sender_handler.h"
#include "third_party/blink/renderer/platform/peerconnection/rtc_ice_candidate_platform.h"
#include "third_party/blink/renderer/platform/peerconnection/rtc_peer_connection_handler_client.h"
#include "third_party/blink/renderer/platform/peerconnection/rtc_rtp_receiver_platform.h"
#include "third_party/blink/renderer/platform/peerconnection/rtc_session_description_platform.h"
#include "third_party/blink/renderer/platform/peerconnection/rtc_stats.h"
#include "third_party/blink/renderer/platform/peerconnection/rtc_stats_request.h"
#include "third_party/blink/renderer/platform/peerconnection/rtc_void_request.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/webrtc/api/data_channel_interface.h"
#include "third_party/webrtc/api/peer_connection_interface.h"
#include "third_party/webrtc/api/rtp_receiver_interface.h"
#include "third_party/webrtc/stats/test/rtc_test_stats.h"

static const char kDummySdp[] = "dummy sdp";
static const char kDummySdpType[] = "dummy type";

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::SaveArg;
using testing::WithArg;

namespace blink {

// Action SaveArgPointeeMove<k>(pointer) saves the value pointed to by the k-th
// (0-based) argument of the mock function by moving it to *pointer.
ACTION_TEMPLATE(SaveArgPointeeMove,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_1_VALUE_PARAMS(pointer)) {
  *pointer = std::move(*testing::get<k>(args));
}

class MockRTCStatsResponse : public LocalRTCStatsResponse {
 public:
  MockRTCStatsResponse() : report_count_(0), statistic_count_(0) {}

  void addStats(const RTCLegacyStats& stats) override {
    ++report_count_;
    for (std::unique_ptr<RTCLegacyStatsMemberIterator> member(stats.Iterator());
         !member->IsEnd(); member->Next()) {
      ++statistic_count_;
    }
  }

  int report_count() const { return report_count_; }

 private:
  int report_count_;
  int statistic_count_;
};

// Mocked wrapper for RTCStatsRequest
class MockRTCStatsRequest : public LocalRTCStatsRequest {
 public:
  MockRTCStatsRequest()
      : has_selector_(false), request_succeeded_called_(false) {}

  bool hasSelector() const override { return has_selector_; }
  MediaStreamComponent* component() const override { return component_; }
  scoped_refptr<LocalRTCStatsResponse> createResponse() override {
    DCHECK(!response_.get());
    response_ = new rtc::RefCountedObject<MockRTCStatsResponse>();
    return response_;
  }

  void requestSucceeded(const LocalRTCStatsResponse* response) override {
    EXPECT_EQ(response, response_.get());
    request_succeeded_called_ = true;
  }

  // Function for setting whether or not a selector is available.
  void setSelector(MediaStreamComponent* component) {
    has_selector_ = true;
    component_ = component;
  }

  // Function for inspecting the result of a stats request.
  MockRTCStatsResponse* result() {
    if (request_succeeded_called_) {
      return response_.get();
    } else {
      return nullptr;
    }
  }

 private:
  bool has_selector_;
  Persistent<MediaStreamComponent> component_;
  scoped_refptr<MockRTCStatsResponse> response_;
  bool request_succeeded_called_;
};

class MockPeerConnectionTracker : public PeerConnectionTracker {
 public:
  MockPeerConnectionTracker()
      : PeerConnectionTracker(
            mojo::Remote<mojom::blink::PeerConnectionTrackerHost>(),
            blink::scheduler::GetSingleThreadTaskRunnerForTesting(),
            base::PassKey<MockPeerConnectionTracker>()) {}

  MOCK_METHOD1(UnregisterPeerConnection,
               void(RTCPeerConnectionHandler* pc_handler));
  // TODO(jiayl): add coverage for the following methods
  MOCK_METHOD2(TrackCreateOffer,
               void(RTCPeerConnectionHandler* pc_handler,
                    RTCOfferOptionsPlatform* options));
  MOCK_METHOD2(TrackCreateAnswer,
               void(RTCPeerConnectionHandler* pc_handler,
                    RTCAnswerOptionsPlatform* options));
  MOCK_METHOD4(TrackSetSessionDescription,
               void(RTCPeerConnectionHandler* pc_handler,
                    const String& sdp,
                    const String& type,
                    Source source));
  MOCK_METHOD1(TrackSetSessionDescriptionImplicit,
               void(RTCPeerConnectionHandler* pc_handler));
  MOCK_METHOD2(
      TrackSetConfiguration,
      void(RTCPeerConnectionHandler* pc_handler,
           const webrtc::PeerConnectionInterface::RTCConfiguration& config));
  MOCK_METHOD4(TrackAddIceCandidate,
               void(RTCPeerConnectionHandler* pc_handler,
                    RTCIceCandidatePlatform* candidate,
                    Source source,
                    bool succeeded));
  MOCK_METHOD4(TrackAddTransceiver,
               void(RTCPeerConnectionHandler* pc_handler,
                    TransceiverUpdatedReason reason,
                    const RTCRtpTransceiverPlatform& transceiver,
                    size_t transceiver_index));
  MOCK_METHOD4(TrackModifyTransceiver,
               void(RTCPeerConnectionHandler* pc_handler,
                    TransceiverUpdatedReason reason,
                    const RTCRtpTransceiverPlatform& transceiver,
                    size_t transceiver_index));
  MOCK_METHOD4(TrackRemoveTransceiver,
               void(RTCPeerConnectionHandler* pc_handler,
                    TransceiverUpdatedReason reason,
                    const RTCRtpTransceiverPlatform& transceiver,
                    size_t transceiver_index));
  MOCK_METHOD1(TrackOnIceComplete, void(RTCPeerConnectionHandler* pc_handler));
  MOCK_METHOD3(TrackCreateDataChannel,
               void(RTCPeerConnectionHandler* pc_handler,
                    const webrtc::DataChannelInterface* data_channel,
                    Source source));
  MOCK_METHOD1(TrackStop, void(RTCPeerConnectionHandler* pc_handler));
  MOCK_METHOD2(TrackSignalingStateChange,
               void(RTCPeerConnectionHandler* pc_handler,
                    webrtc::PeerConnectionInterface::SignalingState state));
  MOCK_METHOD2(TrackIceConnectionStateChange,
               void(RTCPeerConnectionHandler* pc_handler,
                    webrtc::PeerConnectionInterface::IceConnectionState state));
  MOCK_METHOD2(
      TrackConnectionStateChange,
      void(RTCPeerConnectionHandler* pc_handler,
           webrtc::PeerConnectionInterface::PeerConnectionState state));
  MOCK_METHOD2(TrackIceGatheringStateChange,
               void(RTCPeerConnectionHandler* pc_handler,
                    webrtc::PeerConnectionInterface::IceGatheringState state));
  MOCK_METHOD4(TrackSessionDescriptionCallback,
               void(RTCPeerConnectionHandler* pc_handler,
                    Action action,
                    const String& type,
                    const String& value));
  MOCK_METHOD1(TrackOnRenegotiationNeeded,
               void(RTCPeerConnectionHandler* pc_handler));
};

class DummyRTCVoidRequest final : public RTCVoidRequest {
 public:
  ~DummyRTCVoidRequest() override {}

  bool was_called() const { return was_called_; }

  void RequestSucceeded() override { was_called_ = true; }
  void RequestFailed(const webrtc::RTCError&) override { was_called_ = true; }
  void Trace(Visitor* visitor) const override {
    RTCVoidRequest::Trace(visitor);
  }

 private:
  bool was_called_ = false;
};

void OnStatsDelivered(std::unique_ptr<RTCStatsReportPlatform>* result,
                      scoped_refptr<base::SingleThreadTaskRunner> main_thread,
                      std::unique_ptr<RTCStatsReportPlatform> report) {
  EXPECT_TRUE(main_thread->BelongsToCurrentThread());
  EXPECT_TRUE(report);
  *result = std::move(report);
}

template <typename T>
std::vector<T> ToSequence(T value) {
  std::vector<T> vec;
  vec.push_back(value);
  return vec;
}

template <typename T>
std::map<std::string, T> ToMap(const std::string& key, T value) {
  std::map<std::string, T> map;
  map[key] = value;
  return map;
}

template <typename T>
void ExpectSequenceEquals(const Vector<T>& sequence, T value) {
  EXPECT_EQ(sequence.size(), static_cast<size_t>(1));
  EXPECT_EQ(sequence[0], value);
}

template <typename T>
void ExpectMapEquals(const HashMap<String, T>& map,
                     const String& key,
                     T value) {
  EXPECT_EQ(map.size(), static_cast<size_t>(1));
  auto it = map.find(key);
  EXPECT_NE(it, map.end());
  EXPECT_EQ(it->value, value);
}

class RTCPeerConnectionHandlerUnderTest : public RTCPeerConnectionHandler {
 public:
  RTCPeerConnectionHandlerUnderTest(
      RTCPeerConnectionHandlerClient* client,
      blink::PeerConnectionDependencyFactory* dependency_factory,
      bool encoded_insertable_streams = false)
      : RTCPeerConnectionHandler(
            client,
            dependency_factory,
            blink::scheduler::GetSingleThreadTaskRunnerForTesting(),
            encoded_insertable_streams) {}

  blink::MockPeerConnectionImpl* native_peer_connection() {
    return static_cast<blink::MockPeerConnectionImpl*>(
        RTCPeerConnectionHandler::native_peer_connection());
  }

  webrtc::PeerConnectionObserver* observer() {
    return native_peer_connection()->observer();
  }

  bool HasThermalUmaListener() const { return thermal_uma_listener(); }
  bool HasSpeedLimitUmaListener() const { return speed_limit_uma_listener(); }
};

class RTCPeerConnectionHandlerTest : public SimTest {
 public:
  RTCPeerConnectionHandlerTest() : mock_peer_connection_(nullptr) {}

  void SetUp() override {
    SimTest::SetUp();
    mock_client_ =
        MakeGarbageCollected<NiceMock<MockRTCPeerConnectionHandlerClient>>();
    mock_dependency_factory_ =
        MakeGarbageCollected<MockPeerConnectionDependencyFactory>();

    pc_handler_ = CreateRTCPeerConnectionHandlerUnderTest();
    mock_tracker_ = MakeGarbageCollected<NiceMock<MockPeerConnectionTracker>>();
    DummyExceptionStateForTesting exception_state;
    EXPECT_TRUE(pc_handler_->InitializeForTest(
        webrtc::PeerConnectionInterface::RTCConfiguration(),
        mock_tracker_.Get(), exception_state));
    mock_peer_connection_ = pc_handler_->native_peer_connection();
    ASSERT_TRUE(mock_peer_connection_);
    EXPECT_CALL(*mock_peer_connection_, Close());
  }

  void TearDown() override {
    SimTest::TearDown();
    pc_handler_ = nullptr;
    mock_tracker_ = nullptr;
    mock_dependency_factory_ = nullptr;
    mock_client_ = nullptr;
    blink::WebHeap::CollectAllGarbageForTesting();
  }

  std::unique_ptr<RTCPeerConnectionHandlerUnderTest>
  CreateRTCPeerConnectionHandlerUnderTest() {
    return std::make_unique<RTCPeerConnectionHandlerUnderTest>(
        mock_client_.Get(), mock_dependency_factory_.Get());
  }

  // Creates a local MediaStream.
  MediaStreamDescriptor* CreateLocalMediaStream(const String& stream_label) {
    String video_track_label("video-label");
    String audio_track_label("audio-label");
    auto processed_audio_source = std::make_unique<ProcessedLocalAudioSource>(
        *LocalFrameRoot().GetFrame(),
        MediaStreamDevice(blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE,
                          "mock_device_id", "Mock device",
                          media::AudioParameters::kAudioCDSampleRate,
                          media::ChannelLayoutConfig::Stereo(),
                          media::AudioParameters::kAudioCDSampleRate / 100),
        false /* disable_local_echo */, blink::AudioProcessingProperties(),
        1 /* num_requested_channels */, base::DoNothing(),
        blink::scheduler::GetSingleThreadTaskRunnerForTesting());
    auto* processed_audio_source_ptr = processed_audio_source.get();
    processed_audio_source->SetAllowInvalidRenderFrameIdForTesting(true);
    auto* audio_source = MakeGarbageCollected<MediaStreamSource>(
        audio_track_label, MediaStreamSource::kTypeAudio,
        String::FromUTF8("audio_track"), false /* remote */,
        std::move(processed_audio_source));

    auto native_video_source = std::make_unique<MockMediaStreamVideoSource>();
    auto* native_video_source_ptr = native_video_source.get();

    // Dropping the MediaStreamSource reference here is ok, as
    // native_video_source will have a weak pointer to it as Owner(), which is
    // picked up by the MediaStreamComponent created with CreateVideoTrack()
    // below.
    // TODO(https://crbug.com/1302689): Fix this crazy lifecycle jumping back
    // and forth between GCed and non-GCed objects...
    MakeGarbageCollected<MediaStreamSource>(
        video_track_label, MediaStreamSource::kTypeVideo,
        String::FromUTF8("video_track"), false /* remote */,
        std::move(native_video_source));

    HeapVector<Member<MediaStreamComponent>> audio_components(
        static_cast<size_t>(1));
    audio_components[0] = MakeGarbageCollected<MediaStreamComponentImpl>(
        audio_source->Id(), audio_source,
        std::make_unique<MediaStreamAudioTrack>(/*is_local=*/true));
    EXPECT_CALL(
        *webrtc_audio_device_platform_support_->mock_audio_capturer_source(),
        Initialize(_, _));
    EXPECT_CALL(
        *webrtc_audio_device_platform_support_->mock_audio_capturer_source(),
        SetAutomaticGainControl(true));
    EXPECT_CALL(
        *webrtc_audio_device_platform_support_->mock_audio_capturer_source(),
        Start());
    EXPECT_CALL(
        *webrtc_audio_device_platform_support_->mock_audio_capturer_source(),
        Stop());
    CHECK(processed_audio_source_ptr->ConnectToInitializedTrack(
        audio_components[0]));

    HeapVector<Member<MediaStreamComponent>> video_components(
        static_cast<size_t>(1));
    video_components[0] = *MediaStreamVideoTrack::CreateVideoTrack(
        native_video_source_ptr,
        MediaStreamVideoSource::ConstraintsOnceCallback(), true);

    auto* local_stream = MakeGarbageCollected<MediaStreamDescriptor>(
        stream_label, audio_components, video_components);
    return local_stream;
  }

  // Creates a remote MediaStream and adds it to the mocked native
  // peer connection.
  rtc::scoped_refptr<webrtc::MediaStreamInterface> AddRemoteMockMediaStream(
      const String& stream_label,
      const String& video_track_label,
      const String& audio_track_label) {
    rtc::scoped_refptr<webrtc::MediaStreamInterface> stream(
        mock_dependency_factory_->CreateLocalMediaStream(stream_label).get());
    if (!video_track_label.empty()) {
      InvokeAddTrack<webrtc::VideoTrackInterface>(
          stream, MockWebRtcVideoTrack::Create(video_track_label.Utf8()));
    }
    if (!audio_track_label.empty()) {
      InvokeAddTrack<webrtc::AudioTrackInterface>(
          stream, MockWebRtcAudioTrack::Create(audio_track_label.Utf8()));
    }
    mock_peer_connection_->AddRemoteStream(stream.get());
    return stream;
  }

  void StopAllTracks(MediaStreamDescriptor* descriptor) {
    for (auto component : descriptor->AudioComponents())
      MediaStreamAudioTrack::From(component.Get())->Stop();

    for (auto component : descriptor->VideoComponents()) {
      MediaStreamVideoTrack::From(component.Get())->Stop();
    }
  }

  bool AddStream(MediaStreamDescriptor* descriptor) {
    size_t senders_size_before_add = senders_.size();
    for (auto component : descriptor->AudioComponents()) {
      auto error_or_transceiver = pc_handler_->AddTrack(
          component, MediaStreamDescriptorVector({descriptor}));
      if (error_or_transceiver.ok()) {
        auto sender = error_or_transceiver.value()->Sender();
        senders_.push_back(std::unique_ptr<blink::RTCRtpSenderImpl>(
            static_cast<blink::RTCRtpSenderImpl*>(sender.release())));
      }
    }
    for (auto component : descriptor->VideoComponents()) {
      auto error_or_transceiver = pc_handler_->AddTrack(
          component, MediaStreamDescriptorVector({descriptor}));
      if (error_or_transceiver.ok()) {
        auto sender = error_or_transceiver.value()->Sender();
        senders_.push_back(std::unique_ptr<blink::RTCRtpSenderImpl>(
            static_cast<blink::RTCRtpSenderImpl*>(sender.release())));
      }
    }
    return senders_size_before_add < senders_.size();
  }

  std::vector<std::unique_ptr<blink::RTCRtpSenderImpl>>::iterator
  FindSenderForTrack(MediaStreamComponent* component) {
    for (auto it = senders_.begin(); it != senders_.end(); ++it) {
      if ((*it)->Track()->UniqueId() == component->UniqueId())
        return it;
    }
    return senders_.end();
  }

  bool RemoveStream(MediaStreamDescriptor* descriptor) {
    size_t senders_size_before_remove = senders_.size();
    // TODO(hbos): With Unified Plan senders are not removed.
    // https://crbug.com/799030
    for (auto component : descriptor->AudioComponents()) {
      auto it = FindSenderForTrack(component);
      if (it != senders_.end() && pc_handler_->RemoveTrack((*it).get()).ok())
        senders_.erase(it);
    }
    for (auto component : descriptor->VideoComponents()) {
      auto it = FindSenderForTrack(component);
      if (it != senders_.end() && pc_handler_->RemoveTrack((*it).get()).ok())
        senders_.erase(it);
    }
    return senders_size_before_remove > senders_.size();
  }

  void InvokeOnAddStream(
      const rtc::scoped_refptr<webrtc::MediaStreamInterface>& remote_stream) {
    for (const auto& audio_track : remote_stream->GetAudioTracks()) {
      InvokeOnAddTrack(audio_track, remote_stream);
    }
    for (const auto& video_track : remote_stream->GetVideoTracks()) {
      InvokeOnAddTrack(video_track, remote_stream);
    }
  }

  void InvokeOnAddTrack(
      const rtc::scoped_refptr<webrtc::MediaStreamTrackInterface>& remote_track,
      const rtc::scoped_refptr<webrtc::MediaStreamInterface>& remote_stream) {
    rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver(
        new rtc::RefCountedObject<blink::FakeRtpReceiver>(remote_track));
    receivers_by_track_.insert(std::make_pair(remote_track.get(), receiver));
    std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>
        receiver_streams;
    receiver_streams.push_back(remote_stream);
    InvokeOnSignalingThread(
        CrossThreadBindOnce(&webrtc::PeerConnectionObserver::OnAddTrack,
                            CrossThreadUnretained(pc_handler_->observer()),
                            receiver, receiver_streams));
  }

  void InvokeOnRemoveStream(
      const rtc::scoped_refptr<webrtc::MediaStreamInterface>& remote_stream) {
    for (const auto& audio_track : remote_stream->GetAudioTracks()) {
      InvokeOnRemoveTrack(audio_track);
    }
    for (const auto& video_track : remote_stream->GetVideoTracks()) {
      InvokeOnRemoveTrack(video_track);
    }
  }

  void InvokeOnRemoveTrack(
      const rtc::scoped_refptr<webrtc::MediaStreamTrackInterface>&
          remote_track) {
    rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver =
        receivers_by_track_.find(remote_track.get())->second;
    InvokeOnSignalingThread(CrossThreadBindOnce(
        &webrtc::PeerConnectionObserver::OnRemoveTrack,
        CrossThreadUnretained(pc_handler_->observer()), receiver));
  }

  template <typename T>
  void InvokeAddTrack(
      const rtc::scoped_refptr<webrtc::MediaStreamInterface>& remote_stream,
      const scoped_refptr<T>& webrtc_track) {
    InvokeOnSignalingThread(CrossThreadBindOnce(
        [](webrtc::MediaStreamInterface* remote_stream, T* webrtc_track) {
          EXPECT_TRUE(
              remote_stream->AddTrack(rtc::scoped_refptr<T>(webrtc_track)));
        },
        CrossThreadUnretained(remote_stream.get()),
        CrossThreadUnretained(webrtc_track.get())));
  }

  template <typename T>
  void InvokeRemoveTrack(
      const rtc::scoped_refptr<webrtc::MediaStreamInterface>& remote_stream,
      const scoped_refptr<T> webrtc_track) {
    InvokeOnSignalingThread(CrossThreadBindOnce(
        [](webrtc::MediaStreamInterface* remote_stream, T* webrtc_track) {
          EXPECT_TRUE(remote_stream->RemoveTrack(webrtc_track));
        },
        CrossThreadUnretained(remote_stream.get()),
        CrossThreadUnretained(webrtc_track.get())));
  }

  bool HasReceiverForEveryTrack(
      const rtc::scoped_refptr<webrtc::MediaStreamInterface>& remote_stream,
      const std::vector<std::unique_ptr<RTCRtpReceiverPlatform>>& receivers) {
    for (const auto& audio_track : remote_stream->GetAudioTracks()) {
      if (!HasReceiverForTrack(*audio_track.get(), receivers))
        return false;
    }
    for (const auto& video_track : remote_stream->GetAudioTracks()) {
      if (!HasReceiverForTrack(*video_track.get(), receivers))
        return false;
    }
    return true;
  }

  bool HasReceiverForTrack(
      const webrtc::MediaStreamTrackInterface& track,
      const std::vector<std::unique_ptr<RTCRtpReceiverPlatform>>& receivers) {
    for (const auto& receiver : receivers) {
      if (receiver->Track()->Id().Utf8() == track.id())
        return true;
    }
    return false;
  }

  void InvokeOnSignalingThread(WTF::CrossThreadOnceFunction<void()> callback) {
    mock_dependency_factory_->GetWebRtcSignalingTaskRunner()->PostTask(
        FROM_HERE, ConvertToBaseOnceCallback(std::move(callback)));
    RunMessageLoopsUntilIdle();
  }

  // Wait for all current posts to the webrtc signaling thread to run and then
  // run the message loop until idle on the main thread.
  void RunMessageLoopsUntilIdle() {
    base::WaitableEvent waitable_event(
        base::WaitableEvent::ResetPolicy::MANUAL,
        base::WaitableEvent::InitialState::NOT_SIGNALED);
    mock_dependency_factory_->GetWebRtcSignalingTaskRunner()->PostTask(
        FROM_HERE, base::BindOnce(&base::WaitableEvent::Signal,
                                  base::Unretained(&waitable_event)));
    waitable_event.Wait();
    base::RunLoop().RunUntilIdle();
  }

 private:
  void SignalWaitableEvent(base::WaitableEvent* waitable_event) {
    waitable_event->Signal();
  }

 protected:
  ScopedTestingPlatformSupport<AudioCapturerSourceTestingPlatformSupport>
      webrtc_audio_device_platform_support_;
  Persistent<MockRTCPeerConnectionHandlerClient> mock_client_;
  Persistent<MockPeerConnectionDependencyFactory> mock_dependency_factory_;
  Persistent<NiceMock<MockPeerConnectionTracker>> mock_tracker_;
  std::unique_ptr<RTCPeerConnectionHandlerUnderTest> pc_handler_;

  // Weak reference to the mocked native peer connection implementation.
  blink::MockPeerConnectionImpl* mock_peer_connection_;

  std::vector<std::unique_ptr<blink::RTCRtpSenderImpl>> senders_;
  std::map<webrtc::MediaStreamTrackInterface*,
           rtc::scoped_refptr<webrtc::RtpReceiverInterface>>
      receivers_by_track_;
};

TEST_F(RTCPeerConnectionHandlerTest, Destruct) {
  EXPECT_CALL(*mock_tracker_.Get(), UnregisterPeerConnection(pc_handler_.get()))
      .Times(1);
  pc_handler_.reset(nullptr);
}

TEST_F(RTCPeerConnectionHandlerTest, NoCallbacksToClientAfterStop) {
  pc_handler_->Close();

  EXPECT_CALL(*mock_client_.Get(), NegotiationNeeded()).Times(0);
  pc_handler_->observer()->OnRenegotiationNeeded();

  EXPECT_CALL(*mock_client_.Get(), DidGenerateICECandidate(_)).Times(0);
  std::unique_ptr<webrtc::IceCandidateInterface> native_candidate(
      mock_dependency_factory_->CreateIceCandidate("sdpMid", 1, kDummySdp));
  pc_handler_->observer()->OnIceCandidate(native_candidate.get());

  EXPECT_CALL(*mock_client_.Get(), DidChangeIceGatheringState(_)).Times(0);
  pc_handler_->observer()->OnIceGatheringChange(
      webrtc::PeerConnectionInterface::kIceGatheringNew);

  EXPECT_CALL(*mock_client_.Get(), DidModifyTransceiversForMock(_, _, _))
      .Times(0);
  rtc::scoped_refptr<webrtc::MediaStreamInterface> remote_stream(
      AddRemoteMockMediaStream("remote_stream", "video", "audio"));
  InvokeOnAddStream(remote_stream);

  EXPECT_CALL(*mock_client_.Get(), DidModifyTransceiversForMock(_, _, _))
      .Times(0);
  InvokeOnRemoveStream(remote_stream);

  EXPECT_CALL(*mock_client_.Get(), DidAddRemoteDataChannel(_)).Times(0);
  webrtc::DataChannelInit config;
  rtc::scoped_refptr<webrtc::DataChannelInterface> remote_data_channel(
      new rtc::RefCountedObject<blink::MockDataChannel>("dummy", &config));
  pc_handler_->observer()->OnDataChannel(remote_data_channel);

  RunMessageLoopsUntilIdle();
}

TEST_F(RTCPeerConnectionHandlerTest, CreateOffer) {
  EXPECT_CALL(*mock_tracker_.Get(), TrackCreateOffer(pc_handler_.get(), _));

  // TODO(perkj): Can blink::RTCSessionDescriptionRequest be changed so
  // the |request| requestSucceeded can be tested? Currently the |request|
  // object can not be initialized from a unit test.
  EXPECT_FALSE(mock_peer_connection_->created_session_description());
  pc_handler_->CreateOffer(nullptr /*RTCSessionDescriptionRequest*/, nullptr);
  EXPECT_TRUE(mock_peer_connection_->created_session_description());
}

TEST_F(RTCPeerConnectionHandlerTest, CreateAnswer) {
  EXPECT_CALL(*mock_tracker_.Get(), TrackCreateAnswer(pc_handler_.get(), _));
  // TODO(perkj): Can blink::RTCSessionDescriptionRequest be changed so
  // the |request| requestSucceeded can be tested? Currently the |request|
  // object can not be initialized from a unit test.
  EXPECT_FALSE(mock_peer_connection_->created_session_description());
  pc_handler_->CreateAnswer(nullptr /*RTCSessionDescriptionRequest*/, nullptr);
  EXPECT_TRUE(mock_peer_connection_->created_session_description());
}

TEST_F(RTCPeerConnectionHandlerTest, setLocalDescription) {
  // PeerConnectionTracker::TrackSetSessionDescription is expected to be called
  // before |mock_peer_connection| is called.
  testing::InSequence sequence;
  EXPECT_CALL(*mock_tracker_.Get(),
              TrackSetSessionDescription(pc_handler_.get(), String(kDummySdp),
                                         String(kDummySdpType),
                                         PeerConnectionTracker::kSourceLocal));
  EXPECT_CALL(*mock_peer_connection_, SetLocalDescriptionForMock(_, _));

  pc_handler_->SetLocalDescription(
      nullptr /*RTCVoidRequest*/,
      MockParsedSessionDescription(kDummySdpType, kDummySdp));
  RunMessageLoopsUntilIdle();

  std::string sdp_string;
  ASSERT_TRUE(mock_peer_connection_->local_description());
  EXPECT_EQ(kDummySdpType, mock_peer_connection_->local_description()->type());
  mock_peer_connection_->local_description()->ToString(&sdp_string);
  EXPECT_EQ(kDummySdp, sdp_string);

  // TODO(deadbeef): Also mock the "success" callback from the PeerConnection
  // and ensure that the sucessful result is tracked by PeerConnectionTracker.
}

// Test that setLocalDescription with invalid SDP will result in a failure, and
// is tracked as a failure with PeerConnectionTracker.
TEST_F(RTCPeerConnectionHandlerTest, setLocalDescriptionParseError) {
  auto* description = MakeGarbageCollected<RTCSessionDescriptionPlatform>(
      kDummySdpType, kDummySdp);
  testing::InSequence sequence;
  // Expect two "Track" calls, one for the start of the attempt and one for the
  // failure.
  EXPECT_CALL(*mock_tracker_.Get(),
              TrackSetSessionDescription(pc_handler_.get(), String(kDummySdp),
                                         String(kDummySdpType),
                                         PeerConnectionTracker::kSourceLocal));
  EXPECT_CALL(
      *mock_tracker_.Get(),
      TrackSessionDescriptionCallback(
          pc_handler_.get(), PeerConnectionTracker::kActionSetLocalDescription,
          String("OnFailure"), _));

  // Used to simulate a parse failure.
  mock_dependency_factory_->SetFailToCreateSessionDescription(true);
  pc_handler_->SetLocalDescription(
      nullptr /*RTCVoidRequest*/, ParsedSessionDescription::Parse(description));
  RunMessageLoopsUntilIdle();
}

TEST_F(RTCPeerConnectionHandlerTest, setRemoteDescription) {
  // PeerConnectionTracker::TrackSetSessionDescription is expected to be called
  // before |mock_peer_connection| is called.
  testing::InSequence sequence;
  EXPECT_CALL(*mock_tracker_.Get(),
              TrackSetSessionDescription(pc_handler_.get(), String(kDummySdp),
                                         String(kDummySdpType),
                                         PeerConnectionTracker::kSourceRemote));
  EXPECT_CALL(*mock_peer_connection_, SetRemoteDescriptionForMock(_, _));

  pc_handler_->SetRemoteDescription(
      nullptr /*RTCVoidRequest*/,
      MockParsedSessionDescription(kDummySdpType, kDummySdp));
  RunMessageLoopsUntilIdle();

  std::string sdp_string;
  ASSERT_TRUE(mock_peer_connection_->remote_description());
  EXPECT_EQ(kDummySdpType, mock_peer_connection_->remote_description()->type());
  mock_peer_connection_->remote_description()->ToString(&sdp_string);
  EXPECT_EQ(kDummySdp, sdp_string);

  // TODO(deadbeef): Also mock the "success" callback from the PeerConnection
  // and ensure that the sucessful result is tracked by PeerConnectionTracker.
}

// Test that setRemoteDescription with invalid SDP will result in a failure, and
// is tracked as a failure with PeerConnectionTracker.
TEST_F(RTCPeerConnectionHandlerTest, setRemoteDescriptionParseError) {
  auto* description = MakeGarbageCollected<RTCSessionDescriptionPlatform>(
      kDummySdpType, kDummySdp);
  testing::InSequence sequence;
  // Expect two "Track" calls, one for the start of the attempt and one for the
  // failure.
  EXPECT_CALL(*mock_tracker_.Get(),
              TrackSetSessionDescription(pc_handler_.get(), String(kDummySdp),
                                         String(kDummySdpType),
                                         PeerConnectionTracker::kSourceRemote));
  EXPECT_CALL(
      *mock_tracker_.Get(),
      TrackSessionDescriptionCallback(
          pc_handler_.get(), PeerConnectionTracker::kActionSetRemoteDescription,
          String("OnFailure"), _));

  // Used to simulate a parse failure.
  mock_dependency_factory_->SetFailToCreateSessionDescription(true);
  pc_handler_->SetRemoteDescription(
      nullptr /*RTCVoidRequest*/, ParsedSessionDescription::Parse(description));
  RunMessageLoopsUntilIdle();
}

TEST_F(RTCPeerConnectionHandlerTest, setConfiguration) {
  webrtc::PeerConnectionInterface::RTCConfiguration config;

  EXPECT_CALL(*mock_tracker_.Get(),
              TrackSetConfiguration(pc_handler_.get(), _));
  EXPECT_EQ(webrtc::RTCErrorType::NONE, pc_handler_->SetConfiguration(config));
}

// Test that when an error occurs in SetConfiguration, it's converted to a
// blink error and false is returned.
TEST_F(RTCPeerConnectionHandlerTest, setConfigurationError) {
  webrtc::PeerConnectionInterface::RTCConfiguration config;

  mock_peer_connection_->set_setconfiguration_error_type(
      webrtc::RTCErrorType::INVALID_MODIFICATION);
  EXPECT_CALL(*mock_tracker_.Get(),
              TrackSetConfiguration(pc_handler_.get(), _));
  EXPECT_EQ(webrtc::RTCErrorType::INVALID_MODIFICATION,
            pc_handler_->SetConfiguration(config));
}

TEST_F(RTCPeerConnectionHandlerTest, addICECandidate) {
  auto* candidate =
      MakeGarbageCollected<RTCIceCandidatePlatform>(kDummySdp, "sdpMid", 1);

  EXPECT_CALL(*mock_tracker_.Get(),
              TrackAddIceCandidate(pc_handler_.get(), candidate,
                                   PeerConnectionTracker::kSourceRemote, true));
  auto* request = MakeGarbageCollected<DummyRTCVoidRequest>();
  pc_handler_->AddIceCandidate(request, candidate);
  RunMessageLoopsUntilIdle();
  EXPECT_TRUE(request->was_called());
  EXPECT_EQ(kDummySdp, mock_peer_connection_->ice_sdp());
  EXPECT_EQ(1, mock_peer_connection_->sdp_mline_index());
  EXPECT_EQ("sdpMid", mock_peer_connection_->sdp_mid());
}

TEST_F(RTCPeerConnectionHandlerTest, addAndRemoveStream) {
  String stream_label = "local_stream";
  MediaStreamDescriptor* local_stream = CreateLocalMediaStream(stream_label);

  EXPECT_CALL(
      *mock_tracker_.Get(),
      TrackAddTransceiver(
          pc_handler_.get(),
          PeerConnectionTracker::TransceiverUpdatedReason::kAddTrack, _, _))
      .Times(2);
  EXPECT_TRUE(AddStream(local_stream));
  EXPECT_EQ(stream_label.Utf8(), mock_peer_connection_->stream_label());
  EXPECT_EQ(2u, mock_peer_connection_->GetSenders().size());

  EXPECT_FALSE(AddStream(local_stream));
  EXPECT_TRUE(RemoveStream(local_stream));
  // Senders are not removed, only their tracks are nulled.
  ASSERT_EQ(2u, mock_peer_connection_->GetSenders().size());
  EXPECT_EQ(mock_peer_connection_->GetSenders()[0]->track(), nullptr);
  EXPECT_EQ(mock_peer_connection_->GetSenders()[0]->track(), nullptr);

  StopAllTracks(local_stream);
}

TEST_F(RTCPeerConnectionHandlerTest, addStreamWithStoppedAudioAndVideoTrack) {
  String stream_label = "local_stream";
  MediaStreamDescriptor* local_stream = CreateLocalMediaStream(stream_label);

  auto audio_components = local_stream->AudioComponents();
  auto* native_audio_source =
      MediaStreamAudioSource::From(audio_components[0]->Source());
  native_audio_source->StopSource();

  auto video_tracks = local_stream->VideoComponents();
  auto* native_video_source = static_cast<MediaStreamVideoSource*>(
      video_tracks[0]->Source()->GetPlatformSource());
  native_video_source->StopSource();

  EXPECT_TRUE(AddStream(local_stream));
  EXPECT_EQ(stream_label.Utf8(), mock_peer_connection_->stream_label());
  EXPECT_EQ(2u, mock_peer_connection_->GetSenders().size());

  StopAllTracks(local_stream);
}

TEST_F(RTCPeerConnectionHandlerTest, GetStatsNoSelector) {
  scoped_refptr<MockRTCStatsRequest> request(
      new rtc::RefCountedObject<MockRTCStatsRequest>());
  pc_handler_->getStats(request.get());
  RunMessageLoopsUntilIdle();
  ASSERT_TRUE(request->result());
  EXPECT_LT(1, request->result()->report_count());
}

TEST_F(RTCPeerConnectionHandlerTest, GetStatsAfterClose) {
  scoped_refptr<MockRTCStatsRequest> request(
      new rtc::RefCountedObject<MockRTCStatsRequest>());
  pc_handler_->Close();
  RunMessageLoopsUntilIdle();
  pc_handler_->getStats(request.get());
  RunMessageLoopsUntilIdle();
  ASSERT_TRUE(request->result());
  EXPECT_LT(1, request->result()->report_count());
}

TEST_F(RTCPeerConnectionHandlerTest, GetStatsWithLocalSelector) {
  MediaStreamDescriptor* local_stream = CreateLocalMediaStream("local_stream");
  EXPECT_TRUE(AddStream(local_stream));
  auto components = local_stream->AudioComponents();
  ASSERT_LE(1ul, components.size());

  scoped_refptr<MockRTCStatsRequest> request(
      new rtc::RefCountedObject<MockRTCStatsRequest>());
  request->setSelector(components[0]);
  pc_handler_->getStats(request.get());
  RunMessageLoopsUntilIdle();
  EXPECT_EQ(1, request->result()->report_count());

  StopAllTracks(local_stream);
}

TEST_F(RTCPeerConnectionHandlerTest, GetStatsWithBadSelector) {
  // The setup is the same as GetStatsWithLocalSelector, but the stream is not
  // added to the PeerConnection.
  MediaStreamDescriptor* local_stream =
      CreateLocalMediaStream("local_stream_2");
  auto tracks = local_stream->AudioComponents();
  Member<MediaStreamComponent> component = tracks[0];
  mock_peer_connection_->SetGetStatsResult(false);

  scoped_refptr<MockRTCStatsRequest> request(
      new rtc::RefCountedObject<MockRTCStatsRequest>());
  request->setSelector(component);
  pc_handler_->getStats(request.get());
  RunMessageLoopsUntilIdle();
  EXPECT_EQ(0, request->result()->report_count());

  StopAllTracks(local_stream);
}

TEST_F(RTCPeerConnectionHandlerTest, GetRTCStats) {
  rtc::scoped_refptr<webrtc::RTCStatsReport> report =
      webrtc::RTCStatsReport::Create(webrtc::Timestamp::Micros(42));

  report->AddStats(
      std::unique_ptr<const webrtc::RTCStats>(new webrtc::RTCTestStats(
          "RTCUndefinedStats", webrtc::Timestamp::Micros(1000))));

  std::unique_ptr<webrtc::RTCTestStats> stats_defined_members(
      new webrtc::RTCTestStats("RTCDefinedStats",
                               webrtc::Timestamp::Micros(2000)));
  stats_defined_members->m_bool = true;
  stats_defined_members->m_int32 = 42;
  stats_defined_members->m_uint32 = 42;
  stats_defined_members->m_int64 = 42;
  stats_defined_members->m_uint64 = 42;
  stats_defined_members->m_double = 42.0;
  stats_defined_members->m_string = "42";
  stats_defined_members->m_sequence_bool = ToSequence<bool>(true);
  stats_defined_members->m_sequence_int32 = ToSequence<int32_t>(42);
  stats_defined_members->m_sequence_uint32 = ToSequence<uint32_t>(42);
  stats_defined_members->m_sequence_int64 = ToSequence<int64_t>(42);
  stats_defined_members->m_sequence_uint64 = ToSequence<uint64_t>(42);
  stats_defined_members->m_sequence_double = ToSequence<double>(42);
  stats_defined_members->m_sequence_string = ToSequence<std::string>("42");
  stats_defined_members->m_map_string_uint64 = ToMap<uint64_t>("42", 42);
  stats_defined_members->m_map_string_double = ToMap<double>("42", 42.0);
  report->AddStats(
      std::unique_ptr<const webrtc::RTCStats>(stats_defined_members.release()));

  pc_handler_->native_peer_connection()->SetGetStatsReport(report.get());
  std::unique_ptr<RTCStatsReportPlatform> result;
  pc_handler_->GetStats(
      base::BindOnce(OnStatsDelivered, &result,
                     blink::scheduler::GetSingleThreadTaskRunnerForTesting()),
      {}, false);
  RunMessageLoopsUntilIdle();
  EXPECT_TRUE(result);

  int undefined_stats_count = 0;
  int defined_stats_count = 0;
  for (std::unique_ptr<RTCStatsWrapper> stats = result->Next(); stats;
       stats = result->Next()) {
    EXPECT_EQ(stats->GetType().Utf8(), webrtc::RTCTestStats::kType);
    if (stats->Id().Utf8() == "RTCUndefinedStats") {
      ++undefined_stats_count;
      EXPECT_EQ(stats->TimestampMs(), 1.0);
      for (size_t i = 0; i < stats->MembersCount(); ++i) {
        EXPECT_FALSE(stats->GetMember(i)->IsDefined());
      }
    } else if (stats->Id().Utf8() == "RTCDefinedStats") {
      ++defined_stats_count;
      EXPECT_EQ(stats->TimestampMs(), 2.0);
      std::set<webrtc::RTCStatsMemberInterface::Type> members;
      for (size_t i = 0; i < stats->MembersCount(); ++i) {
        std::unique_ptr<RTCStatsMember> member = stats->GetMember(i);
        // TODO(hbos): A WebRTC-change is adding new members, this would cause
        // not all members to be defined. This if-statement saves Chromium from
        // crashing. As soon as the change has been rolled in, I will update
        // this test. crbug.com/627816
        if (!member->IsDefined())
          continue;
        EXPECT_TRUE(member->IsDefined());
        members.insert(member->GetType());
        switch (member->GetType()) {
          case webrtc::RTCStatsMemberInterface::kBool:
            EXPECT_EQ(member->ValueBool(), true);
            break;
          case webrtc::RTCStatsMemberInterface::kInt32:
            EXPECT_EQ(member->ValueInt32(), static_cast<int32_t>(42));
            break;
          case webrtc::RTCStatsMemberInterface::kUint32:
            EXPECT_EQ(member->ValueUint32(), static_cast<uint32_t>(42));
            break;
          case webrtc::RTCStatsMemberInterface::kInt64:
            EXPECT_EQ(member->ValueInt64(), static_cast<int64_t>(42));
            break;
          case webrtc::RTCStatsMemberInterface::kUint64:
            EXPECT_EQ(member->ValueUint64(), static_cast<uint64_t>(42));
            break;
          case webrtc::RTCStatsMemberInterface::kDouble:
            EXPECT_EQ(member->ValueDouble(), 42.0);
            break;
          case webrtc::RTCStatsMemberInterface::kString:
            EXPECT_EQ(member->ValueString(), "42");
            break;
          case webrtc::RTCStatsMemberInterface::kSequenceBool:
            ExpectSequenceEquals(member->ValueSequenceBool(), true);
            break;
          case webrtc::RTCStatsMemberInterface::kSequenceInt32:
            ExpectSequenceEquals(member->ValueSequenceInt32(),
                                 static_cast<int32_t>(42));
            break;
          case webrtc::RTCStatsMemberInterface::kSequenceUint32:
            ExpectSequenceEquals(member->ValueSequenceUint32(),
                                 static_cast<uint32_t>(42));
            break;
          case webrtc::RTCStatsMemberInterface::kSequenceInt64:
            ExpectSequenceEquals(member->ValueSequenceInt64(),
                                 static_cast<int64_t>(42));
            break;
          case webrtc::RTCStatsMemberInterface::kSequenceUint64:
            ExpectSequenceEquals(member->ValueSequenceUint64(),
                                 static_cast<uint64_t>(42));
            break;
          case webrtc::RTCStatsMemberInterface::kSequenceDouble:
            ExpectSequenceEquals(member->ValueSequenceDouble(), 42.0);
            break;
          case webrtc::RTCStatsMemberInterface::kSequenceString:
            ExpectSequenceEquals(member->ValueSequenceString(),
                                 String::FromUTF8("42"));
            break;
          case webrtc::RTCStatsMemberInterface::kMapStringUint64:
            ExpectMapEquals(member->ValueMapStringUint64(),
                            String::FromUTF8("42"), static_cast<uint64_t>(42));
            break;
          case webrtc::RTCStatsMemberInterface::kMapStringDouble:
            ExpectMapEquals(member->ValueMapStringDouble(),
                            String::FromUTF8("42"), 42.0);
            break;
          default:
            NOTREACHED();
        }
      }
      EXPECT_EQ(members.size(), static_cast<size_t>(16));
    } else {
      NOTREACHED();
    }
  }
  EXPECT_EQ(undefined_stats_count, 1);
  EXPECT_EQ(defined_stats_count, 1);
}

TEST_F(RTCPeerConnectionHandlerTest, OnConnectionChange) {
  testing::InSequence sequence;

  webrtc::PeerConnectionInterface::PeerConnectionState new_state =
      webrtc::PeerConnectionInterface::PeerConnectionState::kNew;
  EXPECT_CALL(*mock_tracker_.Get(),
              TrackConnectionStateChange(
                  pc_handler_.get(),
                  webrtc::PeerConnectionInterface::PeerConnectionState::kNew));
  EXPECT_CALL(*mock_client_.Get(),
              DidChangePeerConnectionState(
                  webrtc::PeerConnectionInterface::PeerConnectionState::kNew));
  pc_handler_->observer()->OnConnectionChange(new_state);

  new_state = webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting;
  EXPECT_CALL(
      *mock_tracker_.Get(),
      TrackConnectionStateChange(
          pc_handler_.get(),
          webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting));
  EXPECT_CALL(
      *mock_client_.Get(),
      DidChangePeerConnectionState(
          webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting));
  pc_handler_->observer()->OnConnectionChange(new_state);

  new_state = webrtc::PeerConnectionInterface::PeerConnectionState::kConnected;
  EXPECT_CALL(
      *mock_tracker_.Get(),
      TrackConnectionStateChange(
          pc_handler_.get(),
          webrtc::PeerConnectionInterface::PeerConnectionState::kConnected));
  EXPECT_CALL(
      *mock_client_.Get(),
      DidChangePeerConnectionState(
          webrtc::PeerConnectionInterface::PeerConnectionState::kConnected));
  pc_handler_->observer()->OnConnectionChange(new_state);

  new_state =
      webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected;
  EXPECT_CALL(
      *mock_tracker_.Get(),
      TrackConnectionStateChange(
          pc_handler_.get(),
          webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected));
  EXPECT_CALL(
      *mock_client_.Get(),
      DidChangePeerConnectionState(
          webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected));
  pc_handler_->observer()->OnConnectionChange(new_state);

  new_state = webrtc::PeerConnectionInterface::PeerConnectionState::kFailed;
  EXPECT_CALL(
      *mock_tracker_.Get(),
      TrackConnectionStateChange(
          pc_handler_.get(),
          webrtc::PeerConnectionInterface::PeerConnectionState::kFailed));
  EXPECT_CALL(
      *mock_client_.Get(),
      DidChangePeerConnectionState(
          webrtc::PeerConnectionInterface::PeerConnectionState::kFailed));
  pc_handler_->observer()->OnConnectionChange(new_state);

  new_state = webrtc::PeerConnectionInterface::PeerConnectionState::kClosed;
  EXPECT_CALL(
      *mock_tracker_.Get(),
      TrackConnectionStateChange(
          pc_handler_.get(),
          webrtc::PeerConnectionInterface::PeerConnectionState::kClosed));
  EXPECT_CALL(
      *mock_client_.Get(),
      DidChangePeerConnectionState(
          webrtc::PeerConnectionInterface::PeerConnectionState::kClosed));
  pc_handler_->observer()->OnConnectionChange(new_state);
}

TEST_F(RTCPeerConnectionHandlerTest, OnIceGatheringChange) {
  testing::InSequence sequence;
  EXPECT_CALL(*mock_tracker_.Get(),
              TrackIceGatheringStateChange(
                  pc_handler_.get(),
                  webrtc::PeerConnectionInterface::kIceGatheringNew));
  EXPECT_CALL(*mock_client_.Get(),
              DidChangeIceGatheringState(
                  webrtc::PeerConnectionInterface::kIceGatheringNew));
  EXPECT_CALL(*mock_tracker_.Get(),
              TrackIceGatheringStateChange(
                  pc_handler_.get(),
                  webrtc::PeerConnectionInterface::kIceGatheringGathering));
  EXPECT_CALL(*mock_client_.Get(),
              DidChangeIceGatheringState(
                  webrtc::PeerConnectionInterface::kIceGatheringGathering));
  EXPECT_CALL(*mock_tracker_.Get(),
              TrackIceGatheringStateChange(
                  pc_handler_.get(),
                  webrtc::PeerConnectionInterface::kIceGatheringComplete));
  EXPECT_CALL(*mock_client_.Get(),
              DidChangeIceGatheringState(
                  webrtc::PeerConnectionInterface::kIceGatheringComplete));

  webrtc::PeerConnectionInterface::IceGatheringState new_state =
      webrtc::PeerConnectionInterface::kIceGatheringNew;
  pc_handler_->observer()->OnIceGatheringChange(new_state);

  new_state = webrtc::PeerConnectionInterface::kIceGatheringGathering;
  pc_handler_->observer()->OnIceGatheringChange(new_state);

  new_state = webrtc::PeerConnectionInterface::kIceGatheringComplete;
  pc_handler_->observer()->OnIceGatheringChange(new_state);

  // Check NULL candidate after ice gathering is completed.
  EXPECT_EQ("", mock_client_->candidate_mid());
  EXPECT_FALSE(mock_client_->candidate_mlineindex().has_value());
  EXPECT_EQ("", mock_client_->candidate_sdp());
}

TEST_F(RTCPeerConnectionHandlerTest, OnIceCandidate) {
  testing::InSequence sequence;
  EXPECT_CALL(*mock_tracker_.Get(),
              TrackAddIceCandidate(pc_handler_.get(), _,
                                   PeerConnectionTracker::kSourceLocal, true));
  EXPECT_CALL(*mock_client_.Get(), DidGenerateICECandidate(_));

  std::unique_ptr<webrtc::IceCandidateInterface> native_candidate(
      mock_dependency_factory_->CreateIceCandidate("sdpMid", 1, kDummySdp));
  pc_handler_->observer()->OnIceCandidate(native_candidate.get());
  RunMessageLoopsUntilIdle();
  EXPECT_EQ("sdpMid", mock_client_->candidate_mid());
  EXPECT_EQ(1, mock_client_->candidate_mlineindex());
  EXPECT_EQ(kDummySdp, mock_client_->candidate_sdp());
}

TEST_F(RTCPeerConnectionHandlerTest, OnRenegotiationNeeded) {
  testing::InSequence sequence;
  EXPECT_CALL(*mock_tracker_.Get(),
              TrackOnRenegotiationNeeded(pc_handler_.get()));
  EXPECT_CALL(*mock_client_.Get(), NegotiationNeeded());
  pc_handler_->observer()->OnNegotiationNeededEvent(42);
}

TEST_F(RTCPeerConnectionHandlerTest, CreateDataChannel) {
  blink::WebString label = "d1";
  EXPECT_CALL(*mock_tracker_.Get(),
              TrackCreateDataChannel(pc_handler_.get(), testing::NotNull(),
                                     PeerConnectionTracker::kSourceLocal));
  rtc::scoped_refptr<webrtc::DataChannelInterface> channel =
      pc_handler_->CreateDataChannel("d1", webrtc::DataChannelInit());
  EXPECT_TRUE(channel.get());
  EXPECT_EQ(label.Utf8(), channel->label());
}

TEST_F(RTCPeerConnectionHandlerTest, CheckInsertableStreamsConfig) {
  for (bool encoded_insertable_streams : {true, false}) {
    auto handler = std::make_unique<RTCPeerConnectionHandlerUnderTest>(
        mock_client_.Get(), mock_dependency_factory_.Get(),
        encoded_insertable_streams);
    EXPECT_EQ(handler->encoded_insertable_streams(),
              encoded_insertable_streams);
  }
}

TEST_F(RTCPeerConnectionHandlerTest, ThermalResourceDefaultValue) {
  EXPECT_TRUE(mock_peer_connection_->adaptation_resources().empty());
  pc_handler_->OnThermalStateChange(
      mojom::blink::DeviceThermalState::kCritical);
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_CHROMEOS)
  bool expect_disabled = false;
#else
  bool expect_disabled = true;
#endif
  // A ThermalResource is created in response to the thermal signal.
  EXPECT_EQ(mock_peer_connection_->adaptation_resources().empty(),
            expect_disabled);
}

TEST_F(RTCPeerConnectionHandlerTest,
       ThermalStateChangeDoesNothingIfThermalResourceIsDisabled) {
  // Overwrite base::Feature kWebRtcThermalResource's default to DISABLED.
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndDisableFeature(kWebRtcThermalResource);

  EXPECT_TRUE(mock_peer_connection_->adaptation_resources().empty());
  pc_handler_->OnThermalStateChange(
      mojom::blink::DeviceThermalState::kCritical);
  // A ThermalResource is created in response to the thermal signal.
  EXPECT_TRUE(mock_peer_connection_->adaptation_resources().empty());
}

TEST_F(RTCPeerConnectionHandlerTest,
       ThermalStateChangeTriggersThermalResourceIfEnabled) {
  // Overwrite base::Feature kWebRtcThermalResource's default to ENABLED.
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(kWebRtcThermalResource);

  EXPECT_TRUE(mock_peer_connection_->adaptation_resources().empty());
  // ThermalResource is created and injected on the fly.
  pc_handler_->OnThermalStateChange(
      mojom::blink::DeviceThermalState::kCritical);
  auto resources = mock_peer_connection_->adaptation_resources();
  ASSERT_EQ(1u, resources.size());
  auto thermal_resource = resources[0];
  EXPECT_EQ("ThermalResource", thermal_resource->Name());
  // The initial kOveruse is observed.
  FakeResourceListener resource_listener;
  thermal_resource->SetResourceListener(&resource_listener);
  EXPECT_EQ(1u, resource_listener.measurement_count());
  EXPECT_EQ(webrtc::ResourceUsageState::kOveruse,
            resource_listener.latest_measurement());
  // ThermalResource responds to new measurements.
  pc_handler_->OnThermalStateChange(mojom::blink::DeviceThermalState::kNominal);
  EXPECT_EQ(2u, resource_listener.measurement_count());
  EXPECT_EQ(webrtc::ResourceUsageState::kUnderuse,
            resource_listener.latest_measurement());
  thermal_resource->SetResourceListener(nullptr);
}

TEST_F(RTCPeerConnectionHandlerTest,
       ThermalStateUmaListenerCreatedWhenVideoStreamAdded) {
  base::HistogramTester histogram;
  EXPECT_FALSE(pc_handler_->HasThermalUmaListener());
  MediaStreamDescriptor* local_stream = CreateLocalMediaStream("local_stream");
  EXPECT_TRUE(AddStream(local_stream));
  EXPECT_TRUE(pc_handler_->HasThermalUmaListener());
}

TEST_F(RTCPeerConnectionHandlerTest,
       SpeedLimitUmaListenerCreatedWhenStreamAdded) {
  base::HistogramTester histogram;
  EXPECT_FALSE(pc_handler_->HasSpeedLimitUmaListener());
  MediaStreamDescriptor* local_stream = CreateLocalMediaStream("local_stream");
  EXPECT_TRUE(AddStream(local_stream));
  EXPECT_TRUE(pc_handler_->HasSpeedLimitUmaListener());
}

TEST_F(RTCPeerConnectionHandlerTest, CandidatesIgnoredWheHandlerDeleted) {
  auto* observer = pc_handler_->observer();
  std::unique_ptr<webrtc::IceCandidateInterface> native_candidate(
      mock_dependency_factory_->CreateIceCandidate("sdpMid", 1, kDummySdp));
  pc_handler_.reset();
  observer->OnIceCandidate(native_candidate.get());
}

TEST_F(RTCPeerConnectionHandlerTest,
       CandidatesIgnoredWheHandlerDeletedFromEvent) {
  auto* observer = pc_handler_->observer();
  std::unique_ptr<webrtc::IceCandidateInterface> native_candidate(
      mock_dependency_factory_->CreateIceCandidate("sdpMid", 1, kDummySdp));
  EXPECT_CALL(*mock_client_, DidChangeSessionDescriptions(_, _, _, _))
      .WillOnce(testing::Invoke([&] { pc_handler_.reset(); }));
  observer->OnIceCandidate(native_candidate.get());
}

TEST_F(RTCPeerConnectionHandlerTest,
       OnIceCandidateAfterClientGarbageCollectionDoesNothing) {
  testing::InSequence sequence;
  EXPECT_CALL(*mock_tracker_.Get(),
              TrackAddIceCandidate(pc_handler_.get(), _,
                                   PeerConnectionTracker::kSourceLocal, true))
      .Times(0);

  std::unique_ptr<webrtc::IceCandidateInterface> native_candidate(
      mock_dependency_factory_->CreateIceCandidate("sdpMid", 1, kDummySdp));
  mock_client_ = nullptr;
  WebHeap::CollectAllGarbageForTesting();
  pc_handler_->observer()->OnIceCandidate(native_candidate.get());
  RunMessageLoopsUntilIdle();
}

TEST_F(RTCPeerConnectionHandlerTest,
       OnIceCandidateAfterClientGarbageCollectionFails) {
  DummyExceptionStateForTesting exception_state;
  auto pc_handler = CreateRTCPeerConnectionHandlerUnderTest();
  mock_client_ = nullptr;
  WebHeap::CollectAllGarbageForTesting();
  EXPECT_FALSE(pc_handler->Initialize(
      /*context=*/nullptr, webrtc::PeerConnectionInterface::RTCConfiguration(),
      /*media_constraints=*/nullptr,
      /*frame=*/nullptr, exception_state));
}

}  // namespace blink
