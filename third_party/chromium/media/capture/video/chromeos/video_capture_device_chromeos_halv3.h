// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_CHROMIUM_MEDIA_CAPTURE_VIDEO_CHROMEOS_VIDEO_CAPTURE_DEVICE_CHROMEOS_HALV3_H_
#define THIRD_PARTY_CHROMIUM_MEDIA_CAPTURE_VIDEO_CHROMEOS_VIDEO_CAPTURE_DEVICE_CHROMEOS_HALV3_H_

#include <memory>

#include "third_party/chromium/media/capture/video/chromeos/camera_device_context.h"
#include "third_party/chromium/media/capture/video/video_capture_device.h"
#include "third_party/chromium/media/capture/video/video_capture_device_descriptor.h"

namespace media_m96 {

class VideoCaptureDeviceChromeOSDelegate;

// Implementation of VideoCaptureDevice for ChromeOS with CrOS camera HALv3.
class CAPTURE_EXPORT VideoCaptureDeviceChromeOSHalv3 final
    : public VideoCaptureDevice {
 public:
  VideoCaptureDeviceChromeOSHalv3(
      VideoCaptureDeviceChromeOSDelegate* delegate,
      const VideoCaptureDeviceDescriptor& vcd_descriptor);

  ~VideoCaptureDeviceChromeOSHalv3() final;

  // VideoCaptureDevice implementation.
  void AllocateAndStart(const VideoCaptureParams& params,
                        std::unique_ptr<Client> client) final;
  void StopAndDeAllocate() final;
  void TakePhoto(TakePhotoCallback callback) final;
  void GetPhotoState(GetPhotoStateCallback callback) final;
  void SetPhotoOptions(mojom::PhotoSettingsPtr settings,
                       SetPhotoOptionsCallback callback) final;

 private:
  VideoCaptureDeviceChromeOSDelegate* vcd_delegate_;

  ClientType client_type_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(VideoCaptureDeviceChromeOSHalv3);
};

}  // namespace media_m96

#endif  // THIRD_PARTY_CHROMIUM_MEDIA_CAPTURE_VIDEO_CHROMEOS_VIDEO_CAPTURE_DEVICE_CHROMEOS_HALV3_H_
