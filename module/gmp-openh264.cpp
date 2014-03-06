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

#include "task_utils.h"

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

class OpenH264VideoEncoder : public GMPVideoEncoder
{
 public:
  OpenH264VideoEncoder(GMPVideoHost *hostAPI) :
      host_(hostAPI),
      worker_thread_(nullptr),
      main_thread_(nullptr),
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
    GMPLOG(GL_DEBUG,
           __FUNCTION__
           << " size="
           << inputImage.Width() << "x" << inputImage.Height());

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

    worker_thread_->Post(WrapTask(
        this, &OpenH264VideoEncoder::Encode_w,
        frameCopy,
#if 0
        (*frameTypes)[0])));
#else
        kGMPKeyFrame));
#endif

    return GMPVideoGenericErr;
  }

  void Encode_w(GMPVideoi420Frame* inputImage,
                GMPVideoFrameType frame_type) {
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

    // Translate int to enum
    GMPVideoFrameType encoded_type;
    bool has_frame = false;

    switch (type) {
      case videoFrameTypeIDR:
        encoded_type = kGMPDeltaFrame;
        has_frame = true;
        break;
      case videoFrameTypeI:
        encoded_type = kGMPKeyFrame;
        has_frame = true;
        break;
      case videoFrameTypeP:
        encoded_type = kGMPDeltaFrame;
        has_frame = true;
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

    if (!has_frame)
      return;

    // Synchronously send this back to the main thread for delivery.
    main_thread_->Run(WrapTask(
        this,
        &OpenH264VideoEncoder::Encode_m,
        inputImage,
        &encoded,
        encoded_type));

    return;
  }

  void Encode_m(GMPVideoi420Frame* frame, SFrameBSInfo* encoded,
                GMPVideoFrameType frame_type) {
    // Now return the encoded data back to the parent.
    GMPVideoEncodedFrame* f;
    GMPVideoErr err = host_->CreateEncodedFrame(&f);

    if (err != GMPVideoNoErr) {
      GMPLOG(GL_ERROR, "Error creating encoded frame");
      return;
    }

    // Buffer up the data.
    uint32_t length = 0;
    std::vector<uint32_t> lengths;

    for (int i=0; i<encoded->iLayerNum; ++i) {
      lengths.push_back(0);
      for (int j=0; j<encoded->sLayerInfo[i].iNalCount; ++j) {
        lengths[i] += encoded->sLayerInfo[i].iNalLengthInByte[j];
        length += encoded->sLayerInfo[i].iNalLengthInByte[j];
      }
    }

    err = f->CreateEmptyFrame(length);
    if (err != GMPVideoNoErr) {
      GMPLOG(GL_ERROR, "Error allocating frame data");
      f->Destroy();
      return;
    }

    // Copy the data.
    uint8_t* tmp = f->Buffer();
    for (int i=0; i<encoded->iLayerNum; ++i) {
      // TODO(ekr@rtfm.com): This seems screwy, but I copied it from Cisco.
      memcpy(tmp, encoded->sLayerInfo[i].pBsBuf, lengths[i]);
      tmp += lengths[i];
    }

    f->SetEncodedWidth(frame->Width());
    f->SetEncodedHeight(frame->Height());
    f->SetTimeStamp(frame->Timestamp());
    f->SetFrameType(frame_type);
    f->SetCompleteFrame(true);

    GMPLOG(GL_DEBUG, "Encoding complete. type= "
           << f->FrameType()
           << " length="
           << f->Size());

    // Destroy the frame.
    frame->Destroy();

    // Return the encoded frame.
    GMPCodecSpecificInfo info;
    memset(&info, 0, sizeof(info));
    callback_->Encoded(*f, info);
    f->Destroy();
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
  GMPVideoHost* host_;
  GMPThread* worker_thread_;
  GMPThread* main_thread_;
  ISVCEncoder* encoder_;
  uint32_t max_payload_size_;
  GMPEncoderCallback* callback_;
};

class OpenH264VideoDecoder : public GMPVideoDecoder
{
public:
  OpenH264VideoDecoder(GMPVideoHost *hostAPI) :
      host_(hostAPI),
      worker_thread_(nullptr),
      main_thread_(nullptr),
      callback_(nullptr),
      decoder_(nullptr) {}

  virtual ~OpenH264VideoDecoder() {
  }

  virtual GMPVideoErr InitDecode(const GMPVideoCodec& codecSettings,
                                 GMPDecoderCallback* callback,
                                 int32_t coreCount) override {
    GMPLOG(GL_INFO, "InitDecode");
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

    if (CreateDecoder(&decoder_)) {
      GMPLOG(GL_ERROR, "Couldn't create decoder");
      return GMPVideoGenericErr;
    }

    if (!decoder_) {
      GMPLOG(GL_ERROR, "Couldn't create decoder");
      return GMPVideoGenericErr;
    }

    SDecodingParam param;
    memset(&param, 0, sizeof(param));
    param.iOutputColorFormat = videoFormatI420;
    param.uiTargetDqLayer = UCHAR_MAX;  // TODO(ekr@rtfm.com): correct?
    param.uiEcActiveFlag = 1; // Error concealment on.
    param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;

    if (decoder_->Initialize(&param)) {
      GMPLOG(GL_ERROR, "Couldn't initialize decoder");
      return GMPVideoGenericErr;
    }

    callback_ = callback;
    return GMPVideoNoErr;
  }

  virtual GMPVideoErr Decode(GMPVideoEncodedFrame& inputFrame,
                             bool missingFrames,
                             const GMPCodecSpecificInfo& codecSpecificInfo,
                             int64_t renderTimeMs = -1) override {
    GMPVideoEncodedFrame* frameCopy;
    GMPVideoErr err = host_->CreateEncodedFrame(&frameCopy);
    if (err != GMPVideoNoErr)
      return err;
    // TODO(ekr@rtfm.com): Can we do this without a copy? Maybe
    // Take ownership of the data...
    frameCopy->CopyFrame(inputFrame);

    GMPLOG(GL_DEBUG, __FUNCTION__
           << "frame size=" << frameCopy->Size());

    worker_thread_->Post(WrapTask(
        this, &OpenH264VideoDecoder::Decode_w,
        frameCopy,
        missingFrames,
        renderTimeMs));

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
  virtual GMPVideoErr Decode_w(GMPVideoEncodedFrame* inputFrame,
                               bool missingFrames,
                               int64_t renderTimeMs = -1) {
    GMPLOG(GL_DEBUG, "Frame decode on worker thread length = "
           << inputFrame->Size());

    SBufferInfo decoded;
    memset(&decoded, 0, sizeof(decoded));
    void *data[3] = {nullptr, nullptr, nullptr};

    int rv = decoder_->DecodeFrame2(inputFrame->Buffer(),
                                    inputFrame->Size(),
                                    data,
                                    &decoded);
    if (rv) {
      GMPLOG(GL_ERROR, "Decoding error rv=" << rv);
      return GMPVideoNoErr;
    }

    int width;
    int height;
    int ystride;
    int uvstride;

    if (decoded.iBufferStatus == 1) {
      GMPLOG(GL_DEBUG, "Video frame ready for display "
             << decoded.UsrData.sSystemBuffer.iWidth
             << "x"
             << decoded.UsrData.sSystemBuffer.iHeight);
    }

    return GMPVideoNoErr;
  }


  GMPVideoHost* host_;
  GMPThread* worker_thread_;
  GMPThread* main_thread_;
  GMPDecoderCallback* callback_;
  ISVCDecoder* decoder_;
};

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