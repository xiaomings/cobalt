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

#ifndef MEDIA_MOJO_CLIENTS_STARBOARD_STARBOARD_RENDERER_CLIENT_H_
#define MEDIA_MOJO_CLIENTS_STARBOARD_STARBOARD_RENDERER_CLIENT_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "cobalt/media/service/mojom/video_geometry_setter.mojom.h"
#include "cobalt/media/service/video_geometry_setter_service.h"
#include "media/base/pipeline_status.h"
#include "media/base/starboard/starboard_rendering_mode.h"
#include "media/mojo/clients/mojo_renderer_wrapper.h"
#include "media/mojo/mojom/renderer_extensions.mojom.h"
#include "media/starboard/bind_host_receiver_callback.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace media {
class GpuVideoAcceleratorFactories;
class RendererClient;
class MediaLog;
class MediaResource;
class MojoRenderer;
class VideoRendererSink;
class VideoOverlayFactory;

// StarboardRendererClient lives in Chrome_InProcRendererThread
// (Media thread) talks to the StarboardRenderer living in the
// Chrome_InProcGpuThread, using `mojo_renderer_` and `renderer_extension_`.
class MEDIA_EXPORT StarboardRendererClient
    : public MojoRendererWrapper,
      public mojom::StarboardRendererClientExtension,
      public cobalt::media::mojom::VideoGeometryChangeClient {
 public:
  using RendererExtension = mojom::StarboardRendererExtension;
  using ClientExtension = media::mojom::StarboardRendererClientExtension;

  StarboardRendererClient(
      const scoped_refptr<base::SequencedTaskRunner>& media_task_runner,
      std::unique_ptr<MediaLog> media_log,
      std::unique_ptr<MojoRenderer> mojo_renderer,
      std::unique_ptr<VideoOverlayFactory> video_overlay_factory,
      VideoRendererSink* video_renderer_sink,
      mojo::PendingRemote<RendererExtension> pending_renderer_extension,
      mojo::PendingReceiver<ClientExtension> client_extension_receiver,
      BindHostReceiverCallback bind_host_receiver_callback,
      GpuVideoAcceleratorFactories* gpu_factories);

  StarboardRendererClient(const StarboardRendererClient&) = delete;
  StarboardRendererClient& operator=(const StarboardRendererClient&) = delete;

  ~StarboardRendererClient() override;

  // MojoRendererWrapper overrides.
  void Initialize(MediaResource* media_resource,
                  RendererClient* client,
                  PipelineStatusCallback init_cb) override;
  RendererType GetRendererType() override;

  // mojom::StarboardRendererClientExtension implementation
  void PaintVideoHoleFrame(const gfx::Size& size) override;
  void UpdateStarboardRenderingMode(const StarboardRenderingMode mode) override;

  // cobalt::media::mojom::VideoGeometryChangeClient implementation.
  void OnVideoGeometryChange(const gfx::RectF& rect_f,
                             gfx::OverlayTransform transform) override;

 private:
  void OnConnectionError();
  void OnSubscribeToVideoGeometryChange(MediaResource* media_resource,
                                        RendererClient* client);
  void InitAndBindMojoRenderer(base::OnceClosure complete_cb);
  void OnGpuChannelTokenReady(mojom::CommandBufferIdPtr command_buffer_id,
                              base::OnceClosure complete_cb,
                              const base::UnguessableToken& channel_token);
  void InitializeMojoRenderer(MediaResource* media_resource,
                              RendererClient* client,
                              PipelineStatusCallback init_cb);
  void InitAndConstructMojoRenderer(mojom::CommandBufferIdPtr command_buffer_id,
                                    base::OnceClosure complete_cb);
  bool AreMojoPipesConnected() const {
    return client_extension_receiver_.is_bound() &&
           renderer_extension_.is_bound();
  }

  scoped_refptr<base::SequencedTaskRunner> media_task_runner_;
  std::unique_ptr<MediaLog> media_log_;
  std::unique_ptr<VideoOverlayFactory> video_overlay_factory_;
  raw_ptr<VideoRendererSink> video_renderer_sink_ = nullptr;
  mojo::PendingRemote<RendererExtension> pending_renderer_extension_;
  mojo::PendingReceiver<ClientExtension> pending_client_extension_receiver_;
  mojo::Receiver<ClientExtension> client_extension_receiver_;
  const BindHostReceiverCallback bind_host_receiver_callback_;
  raw_ptr<GpuVideoAcceleratorFactories> gpu_factories_ = nullptr;

  mojo::Remote<RendererExtension> renderer_extension_;

  raw_ptr<RendererClient> client_ = nullptr;

  // Rendering mode the Starboard Renderer will use.
  // Default is punch-out mode.
  StarboardRenderingMode rendering_mode_ = StarboardRenderingMode::kPunchOut;

  mojo::Remote<cobalt::media::mojom::VideoGeometryChangeSubscriber>
      video_geometry_change_subcriber_remote_;
  mojo::Receiver<cobalt::media::mojom::VideoGeometryChangeClient>
      video_geometry_change_client_receiver_{this};

  base::WeakPtrFactory<StarboardRendererClient> weak_factory_{this};
};

}  // namespace media

#endif  // MEDIA_MOJO_CLIENTS_STARBOARD_STARBOARD_RENDERER_CLIENT_H_
