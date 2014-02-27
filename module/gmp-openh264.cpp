// Written by Josh Aas and Eric Rescorla
// TODO(ekr@rtfm.com): Need license.

#include <stdint.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>

#include "gmp-general.h"
#include "gmp-video-host.h"
#include "gmp-video-encode.h"
#include "gmp-video-decode.h"
#include "gmp-video-frame-i420.h"
#include "gmp-video-frame-encoded.h"

#include "codec_def.h"
#include "codec_app_def.h"
#include "codec_api.h"
#include "param_svc.h"

#if defined(_MSC_VER)
#define PUBLIC_FUNC __declspec(dllexport)
#else
#define PUBLIC_FUNC
#endif


#define GMPLOG(c, x) std::cerr << c << ": " << x << std::endl;
#define GL_ERROR "Error"
#define GL_INFO  "Info"
#define GL_DEBUG  "Debug"

class OpenH264VideoEncoder;

class GMPEncodeFrameTask : public GMPTask {
 public:
  GMPEncodeFrameTask(OpenH264VideoEncoder* encoder,
                     GMPVideoi420Frame* frame,
                     GMPVideoFrameType type) :
      encoder_(encoder),
      frame_(frame),
      type_(type) {}

  virtual ~GMPEncodeFrameTask() {
  }

  virtual void Run();

 private:
  OpenH264VideoEncoder* encoder_;
  GMPVideoi420Frame* frame_;
  GMPVideoFrameType type_;
};

class GMPDestroyFrameTask : public GMPTask {
 public:
  GMPDestroyFrameTask(GMPVideoi420Frame* frame): 
      frame_(frame) {}

  virtual ~GMPDestroyFrameTask() {
    frame_->Destroy();
  }

  virtual void Run() {}

 private:
  GMPVideoi420Frame* frame_;
};


class OpenH264VideoEncoder : public GMPVideoEncoder
{
 public:
  OpenH264VideoEncoder(GMPVideoHost *hostAPI) :
      host_(hostAPI),
      encoder_(nullptr),
      max_payload_size_(0),
      callback_(nullptr) {}

  virtual ~OpenH264VideoEncoder() {
    worker_thread_->Join();
    // TODO(ekr@rtfm.com)
  }

  virtual GMPVideoErr InitEncode(const GMPVideoCodec& codecSettings,
                                 GMPEncoderCallback* callback,
                                 int32_t numberOfCores,
                                 uint32_t maxPayloadSize) override {
    GMPLOG(GL_INFO, "PID " << getpid());

   GMPVideoErr err = host_->GetThread(&main_thread_);
    if (err != GMPVideoNoErr) {
      GMPLOG(GL_ERROR, "Couldn't get main thread");
      return err;
    }

    err = host_->CreateThread(&worker_thread_);
    if (err != GMPVideoNoErr) {
      GMPLOG(GL_ERROR, "Couldn't create new thread");
      return err;
    }

    err = host_->CreateMutex(&mutex_);
    if (err != GMPVideoNoErr) {
      GMPLOG(GL_ERROR, "Couldn't create mutex");
      return err;
    }

    int rv = CreateSVCEncoder(&encoder_);
    if (rv) {
      return GMPVideoGenericErr;
    }

    SEncParamExt param;
    memset(&param, 0, sizeof(param));

    GMPLOG(GL_INFO, "Initializing encoder at "
            << codecSettings.mWidth
            << "x"
            << codecSettings.mHeight
            << "@"
            << static_cast<int>(codecSettings.mMaxFramerate));

    // Translate parameters.
    param.iPicWidth = codecSettings.mWidth;
    param.iPicHeight = codecSettings.mHeight;
    param.iTargetBitrate = codecSettings.mStartBitrate * 1000;
    param.iTemporalLayerNum = 1;
    param.iSpatialLayerNum = 1;

    // TODO(ekr@rtfm.com). Scary conversion from unsigned char to float below.
    param.fMaxFrameRate = codecSettings.mMaxFramerate;
    param.iInputCsp = videoFormatI420;

    // Set up layers. Currently we have one layer.
    auto layer = &param.sSpatialLayers[0];

    layer->iVideoWidth = codecSettings.mWidth;
    layer->iVideoHeight = codecSettings.mHeight;
    layer->iSpatialBitrate = param.iTargetBitrate;
    layer->fFrameRate = param.fMaxFrameRate;

    // Based on guidance from Cisco.
    layer->sSliceCfg.sSliceArgument.uiSliceMbNum[0] = 1000;
    layer->sSliceCfg.sSliceArgument.uiSliceNum = 1;
    layer->sSliceCfg.sSliceArgument.uiSliceSizeConstraint = 1000;

    rv = encoder_->InitializeExt(&param);
    if (rv) {
      GMPLOG(GL_ERROR, "Couldn't initialize encoder");
      return GMPVideoGenericErr;
    }

    max_payload_size_ = maxPayloadSize;
    callback_ = callback;

    GMPLOG(GL_INFO, "Initialized encoder");

    return GMPVideoNoErr;
  }

  virtual GMPVideoErr Encode(GMPVideoi420Frame& inputImage,
                             const GMPCodecSpecificInfo& codecSpecificInfo,
                             const std::vector<GMPVideoFrameType>* frameTypes) override {
    GMPLOG(GL_DEBUG,"Encoding frame");

#if 0
    // TODO(josh): this is empty.

    //    assert(!frameTypes->empty());
    if (frameTypes->empty()) {
      GMPLOG(GL_ERROR, "No frame types provided");
      return GMPVideoGenericErr;
    }
#endif

    GMPVideoFrame* frameCopyGen;
    GMPVideoErr err = host_->CreateFrame(kGMPI420VideoFrame, &frameCopyGen);
    if (err != GMPVideoNoErr)
      return err;

    GMPVideoi420Frame* frameCopy = static_cast<GMPVideoi420Frame*>(frameCopyGen);
    inputImage.SwapFrame(frameCopy);

    GMPLOG(GL_DEBUG,"Posting");

    worker_thread_->Post(new GMPEncodeFrameTask(this, frameCopy,
#if 0
                                         (*frameTypes)[0]));
#else
    kGMPKeyFrame));
#endif

    GMPLOG(GL_DEBUG,"Frame posted");

    return GMPVideoGenericErr;
  }

  void Encode_w(GMPVideoi420Frame* inputImage,
                GMPVideoFrameType frame_type) {
    GMPLOG(GL_DEBUG, "Encoding frame on worker thread");
    SFrameBSInfo encoded;

    SSourcePicture src;

    src.iColorFormat = videoFormatI420;
    src.iStride[0] = inputImage->Stride(kGMPYPlane);
    src.pData[0] = reinterpret_cast<unsigned char*>(
        const_cast<uint8_t *>(inputImage->Buffer(kGMPYPlane)));
    src.iStride[1] = inputImage->Stride(kGMPUPlane);
    src.pData[1] = reinterpret_cast<unsigned char*>(
        const_cast<uint8_t *>(inputImage->Buffer(kGMPUPlane)));
    src.iStride[2] = inputImage->Stride(kGMPVPlane);
    src.pData[2] = reinterpret_cast<unsigned char*>(
        const_cast<uint8_t *>(inputImage->Buffer(kGMPVPlane)));
    src.iStride[3] = 0;
    src.pData[3] = nullptr;
    src.iPicWidth = inputImage->Width();
    src.iPicHeight = inputImage->Height();

    const SSourcePicture* pics = &src;

    int type = encoder_->EncodeFrame(pics, &encoded);

    GMPLOG(GL_DEBUG, "Encoding complete");

    // Translate int to enum
    switch (type) {
      case videoFrameTypeIDR:
      case videoFrameTypeI:
      case videoFrameTypeP:
        {
#if 0
          ScopedDeletePtr<EncodedFrame> encoded_frame(
              EncodedFrame::Create(encoded,
                                   inputImage->width(),
                                   inputImage->height(),
                                   inputImage->timestamp(),
                                   frame_type));
          callback_->Encoded(encoded_frame->image(), NULL, NULL);
#endif
        }
        break;
      case videoFrameTypeSkip:
        //can skip the call back since not actual bit stream will be generated
        break;
      case videoFrameTypeIPMixed://this type is currently not suppported
      case videoFrameTypeInvalid:
        GMPLOG(GL_ERROR, "Couldn't encode frame. Error = " << type);
        break;
      default:
        // The API is defined as returning a type.
        assert(false);
        break;
    }

    // Post back to main thread for destruction.
    main_thread_->Post(new GMPDestroyFrameTask(inputImage));

    return;
  }
  virtual GMPVideoErr SetChannelParameters(uint32_t aPacketLoss, uint32_t aRTT) override {
    printf("%s\n", __PRETTY_FUNCTION__);
    return GMPVideoNoErr;
  }

  virtual GMPVideoErr SetRates(uint32_t aNewBitRate, uint32_t aFrameRate) override {
    printf("%s\n", __PRETTY_FUNCTION__);
    return GMPVideoNoErr;
  }

  virtual GMPVideoErr SetPeriodicKeyFrames(bool aEnable) override {
    printf("%s\n", __PRETTY_FUNCTION__);
    return GMPVideoNoErr;
  }

  virtual void EncodingComplete() override {
    printf("%s\n", __PRETTY_FUNCTION__);
    delete this;
  }

private:
  static void ThreadMain(void *ctx) {
    GMPVideoEncoder* encoder = reinterpret_cast<GMPVideoEncoder*>(ctx);

    sleep(1000);
  }

  GMPVideoHost* host_;
  GMPThread* worker_thread_;
  GMPThread* main_thread_;
  GMPMutex* mutex_;
  ISVCEncoder* encoder_;
  uint32_t max_payload_size_;
  GMPEncoderCallback* callback_;
};

class OpenH264VideoDecoder : public GMPVideoDecoder
{
public:
  OpenH264VideoDecoder(GMPVideoHost *hostAPI) {
    printf("%s\n", __PRETTY_FUNCTION__);
    mHostAPI = hostAPI;
    mCallback = nullptr;
  }

  virtual ~OpenH264VideoDecoder() {
    printf("%s\n", __PRETTY_FUNCTION__);
  }

  virtual GMPVideoErr InitDecode(const GMPVideoCodec& codecSettings,
                                 GMPDecoderCallback* callback,
                                 int32_t coreCount) override {
    printf("%s\n", __PRETTY_FUNCTION__);
    mCallback = callback;
    return GMPVideoNoErr;
  }

  virtual GMPVideoErr Decode(GMPVideoEncodedFrame& inputFrame,
                             bool missingFrames,
                             const GMPCodecSpecificInfo& codecSpecificInfo,
                             int64_t renderTimeMs = -1) override {
    printf("%s\n", __PRETTY_FUNCTION__);

    // Print out the contents of the input buffer just so
    // we can see that the memory was sent properly.
    // Should be full of 0x2.
    const uint8_t* inBuffer = inputFrame.Buffer();
    if (!inBuffer) {
      printf("No buffer for encoded frame!\n");
      return GMPVideoGenericErr;
    }
    for (uint32_t i = 0; i < 1000; i++) {
      printf("%i", inBuffer[i]);
    }
    printf("\n");

    // Create an output frame full of 0x1.
    GMPVideoFrame* f;
    mHostAPI->CreateFrame(kGMPI420VideoFrame, &f);
    auto vf = static_cast<GMPVideoi420Frame*>(f);
    vf->CreateEmptyFrame(100, 100, 100, 100, 100);
    uint8_t* outBuffer = vf->Buffer(kGMPYPlane);
    if (!outBuffer) {
      printf("No buffer for i420 frame!\n");
      return GMPVideoGenericErr;
    }
    memset(outBuffer, 0x1, 1000);

    mCallback->Decoded(*vf);
    f->Destroy();
    return GMPVideoNoErr;
  }

  virtual GMPVideoErr Reset() override {
    printf("%s\n", __PRETTY_FUNCTION__);
    return GMPVideoNoErr;
  }

  virtual GMPVideoErr Drain() override {
    printf("%s\n", __PRETTY_FUNCTION__);
    return GMPVideoNoErr;
  }

  virtual void DecodingComplete() override {
    printf("%s\n", __PRETTY_FUNCTION__);
    delete this;
  }

private:
  //XXXJOSH ownership of all this stuff?
  GMPVideoHost *mHostAPI;
  GMPDecoderCallback* mCallback;
};


void GMPEncodeFrameTask::Run() {
  encoder_->Encode_w(frame_, type_);
}

extern "C" {

PUBLIC_FUNC GMPErr
GMPInit(void) {
  printf("GMP: Initialized.\n");
  return GMPNoErr;
}

PUBLIC_FUNC GMPErr
GMPGetAPI(const char* aApiName, void* aHostAPI, void** aPluginApi) {
  if (!strcmp(aApiName, "decode-video")) {
    *aPluginApi = new OpenH264VideoDecoder(static_cast<GMPVideoHost*>(aHostAPI));
    return GMPNoErr;
  } else if (!strcmp(aApiName, "encode-video")) {
    *aPluginApi = new OpenH264VideoEncoder(static_cast<GMPVideoHost*>(aHostAPI));
    return GMPNoErr;
  }
  return GMPGenericErr;
}

PUBLIC_FUNC void
GMPShutdown(void) {
  printf("GMP: Shutting down.\n");
}

} // extern "C"
