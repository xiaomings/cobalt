// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_CHROMIUM_MEDIA_RENDERERS_DECRYPTING_RENDERER_FACTORY_H_
#define THIRD_PARTY_CHROMIUM_MEDIA_RENDERERS_DECRYPTING_RENDERER_FACTORY_H_

#include "base/callback.h"
#include "base/macros.h"
#include "third_party/chromium/media/base/media_export.h"
#include "third_party/chromium/media/base/renderer_factory.h"

namespace media_m96 {

class MediaLog;

// Simple RendererFactory wrapper class. It wraps any Renderer created by the
// underlying factory, and returns it as a DecryptingRenderer.
//
// See DecryptingRenderer for more information.
//
// The caller must guarantee that the returned DecryptingRenderer will never
// be initialized with a |media_resource| of type MediaResource::Type::URL.
class MEDIA_EXPORT DecryptingRendererFactory final : public RendererFactory {
 public:
  DecryptingRendererFactory(
      MediaLog* media_log,
      std::unique_ptr<media_m96::RendererFactory> renderer_factory);

  DecryptingRendererFactory(const DecryptingRendererFactory&) = delete;
  DecryptingRendererFactory& operator=(const DecryptingRendererFactory&) =
      delete;

  ~DecryptingRendererFactory() final;

  // RendererFactory implementation.
  std::unique_ptr<Renderer> CreateRenderer(
      const scoped_refptr<base::SingleThreadTaskRunner>& media_task_runner,
      const scoped_refptr<base::TaskRunner>& worker_task_runner,
      AudioRendererSink* audio_renderer_sink,
      VideoRendererSink* video_renderer_sink,
      RequestOverlayInfoCB request_overlay_info_cb,
      const gfx::ColorSpace& target_color_space) final;

 private:
  MediaLog* media_log_;

  std::unique_ptr<media_m96::RendererFactory> renderer_factory_;
};

}  // namespace media_m96

#endif  // THIRD_PARTY_CHROMIUM_MEDIA_RENDERERS_DECRYPTING_RENDERER_FACTORY_H_
