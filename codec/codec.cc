
// (Stub windows media foundations DLL. Doesn't work, and wouldn't
// do anything if it did!)

#include <cstdint>
#include <mutex>

#include <mfapi.h>
#include <mferror.h>
#include <mfobjects.h>
#include <mftransform.h>
#include <minwindef.h>
#include <windows.h>
#include <winerror.h>
#include <winnt.h>
#include <initguid.h>


static constexpr DWORD CODEC_FOURCC = MAKEFOURCC('H','7','7','7');
DEFINE_MEDIATYPE_GUID(MFVideoFormat_H777, CODEC_FOURCC);

template <class T> void SafeRelease(T **ppT) {
  if (*ppT) {
    (*ppT)->Release();
    *ppT = nullptr;
  }
}

// Define a class for the decoder
class Codec : public IMFTransform {
 public:

  std::mutex mu;

  IMFMediaType *input_type = nullptr;

  Codec() {
    // Initialize media types used for input and output.
  }

  ~Codec() {

  }

  bool Init() {
    MFCreateMediaType(&input_type);
  }

  // MFTGetInfo?

  HRESULT GetStreamLimits(DWORD *in_min,
                          DWORD *in_max,
                          DWORD *out_min,
                          DWORD *out_max) override {
    *in_min = *in_max = 1;
    *out_min = *out_max = 1;
    return S_OK;
  }

  HRESULT GetStreamCount(DWORD *in_streams, DWORD *out_streams) override {
    *in_streams = 1;
    *out_streams = 1;
    return S_OK;
  }

  // Not implemented means that the two streams are numbered 0, 1, ...
  // So in this case, both the single input and output stream have id 0.
  HRESULT GetStreamIDs(
      DWORD num_in_ids,
      DWORD *in_ids,
      DWORD num_out_ids,
      DWORD *out_ids) override {
    return E_NOTIMPL;
  }

  HRESULT GetInputAvailableType(
      DWORD stream_id,
      DWORD type_index,
      IMFMediaType **type) override {
    if (stream_id != 0) return MF_E_INVALIDSTREAMNUMBER;

    switch (type_index) {
    case 0: {
      std::unique_lock<std::mutex> ml(mu);

      IMFMediaType *out = nullptr;
      HRESULT ret = MFCreateMediaType(&out);
      if (SUCCEEDED(ret)) {
        ret = out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
      }
      if (SUCCEEDED(ret)) {
        ret = out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H777);
      }
      if (SUCCEEDED(ret)) {
        *type = out;
        (*type)->AddRef();
      }

      SafeRelease(&out);
      return S_OK;
    }
    default:
      return MF_E_NO_MORE_TYPES;
    }
  }

  HRESULT STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void** ppvObject) override {
    return E_NOTIMPL;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return 0;
  }

  ULONG STDMETHODCALLTYPE Release() override {
    return 0;
  }

  /*
  HRESULT STDMETHODCALLTYPE GetSupportedMediaTypes(DWORD dwInputCount, IMFMediaType** ppInputTypes, DWORD dwOutputCount, IMFMediaType** ppOutputTypes) override;
  */

  HRESULT STDMETHODCALLTYPE ProcessInput(DWORD input_stream_id,
                                         IMFSample *sample,
                                         DWORD flags) override {

  }

  HRESULT STDMETHODCALLTYPE ProcessOutput(DWORD flags,
                                          IMF2DBuffer** ppOutputBuffer,
                                          DWORD *pdwStatus) override;

  // ... (other methods as needed)
};

// Function to create an instance of the decoder (stubbed in this example)
HRESULT CreateDecoder(IMFTransform** ppDecoder) {
  *ppDecoder = new Codec();
  if (!*ppDecoder)
  {
    return E_OUTOFMEMORY;
  }

  (*ppDecoder)->AddRef();
  return S_OK;
}

// Dll loading and unloading (simplified for demonstration)
BOOL APIENTRY DllMain(HMODULE hModule, uint32_t dwReason, LPVOID lpReserved) {
  return TRUE;
}
