
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <format>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>
#include <wingdi.h>
#include <minwindef.h>
#include <windef.h>
#include <winerror.h>
#include <winnt.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <heapapi.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <winscard.h>

#ifndef WIN32
#error Sorry, this is only implemented on Windows.
#endif

// D2D1 example:
// https://docs.microsoft.com/en-us/windows/win32/direct2d/direct2d-quickstart

#define Assert(exp) \
  do { \
    if (! (exp) ) { Error("assertion failed: " #exp); exit(0); } \
  } while(0)

// ??
extern "C" IMAGE_DOS_HEADER __ImageBase;
static HINSTANCE GetInstance() {
  return (HINSTANCE)&__ImageBase;
}

static void Error(const std::string &msg) {
  MessageBoxA(nullptr, msg.c_str(), "ERROR!", 0);
}

struct App {
  // One window at a time.
  HWND window = nullptr;

  bool initialized = false;

  // The scRGB color space strictly defines 1.0f as 80 nits.
  // To match standard SDR apps, scale this by your SDR brightness slider.
  // (Normally queried via DisplayConfigGetDeviceInfo at runtime)
  float sdr_scale = 3.0f;

  bool UpdateSdrScale() {
    if (!window) return false;
    HMONITOR h_monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW minfo = {};
    minfo.cbSize = sizeof(minfo);
    if (!GetMonitorInfoW(h_monitor, (LPMONITORINFO)&minfo)) return false;

    UINT32 num_path_elements = 0;
    UINT32 num_mode_elements = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &num_path_elements,
                                    &num_mode_elements) != ERROR_SUCCESS) {
      return false;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> path_array(num_path_elements);
    std::vector<DISPLAYCONFIG_MODE_INFO> mode_array(num_mode_elements);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &num_path_elements,
                           path_array.data(), &num_mode_elements,
                           mode_array.data(), nullptr) != ERROR_SUCCESS) {
      return false;
    }

    for (const auto &path : path_array) {
      DISPLAYCONFIG_SOURCE_DEVICE_NAME source_name = {};
      source_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
      source_name.header.size = sizeof(source_name);
      source_name.header.adapterId = path.sourceInfo.adapterId;
      source_name.header.id = path.sourceInfo.id;
      if (DisplayConfigGetDeviceInfo(&source_name.header) == ERROR_SUCCESS) {
        if (wcscmp(source_name.viewGdiDeviceName, minfo.szDevice) == 0) {
          DISPLAYCONFIG_SDR_WHITE_LEVEL white_level = {};
          white_level.header.type =
              DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
          white_level.header.size = sizeof(white_level);
          white_level.header.adapterId = path.targetInfo.adapterId;
          white_level.header.id = path.targetInfo.id;
          if (DisplayConfigGetDeviceInfo(&white_level.header) ==
              ERROR_SUCCESS) {
            float new_scale = white_level.SDRWhiteLevel / 1000.0f;
            if (new_scale != sdr_scale) {
              sdr_scale = new_scale;
              return true;
            }
          }
          break;
        }
      }
    }
    return false;
  }

  // XXX?
  ID2D1Factory1 *d2d_factory = nullptr;
  ID3D11Device *d3d_device = nullptr;
  ID3D11DeviceContext *d3d_context = nullptr;
  IDXGISwapChain3 *swap_chain = nullptr;
  ID2D1Device *d2d_device = nullptr;
  ID2D1DeviceContext *d2d_context = nullptr;
  ID2D1Bitmap1 *d2d_target_bitmap = nullptr;

  bool Init() {

    const int initial_width = 800;
    const int initial_height = 800;

    // This is a "device independent" resource that can live the
    // length of the app.
    // In the example app this is all that CreateDeviceIndependentResources
    // does.
    //
    // XXX single threaded? no
    if (S_OK !=
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory)) {
      Error("d2d1");
      return false;
    }

    // Register the window class.
    // (XXX this is a weird initialization style. can we use .size = ?)
    WNDCLASSEX wcex = { sizeof(WNDCLASSEX) };
    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = &WndProc;
    wcex.cbClsExtra    = 0;
    wcex.cbWndExtra    = sizeof(LONG_PTR);
    wcex.hInstance     = GetInstance();
    wcex.hbrBackground = nullptr;
    wcex.lpszMenuName  = nullptr;
    wcex.hCursor       = LoadCursor(nullptr, IDI_APPLICATION);
    wcex.lpszClassName = "FastView";

    RegisterClassExA(&wcex);

    // Because the CreateWindow function takes its size in pixels,
    // obtain the system DPI and use it to scale the window size.
    float dpiX, dpiY;

    // The factory returns the current system DPI. This is also the
    // value it will use to create its own windows.
    d2d_factory->GetDesktopDpi(&dpiX, &dpiY);

    // Create the window.
    window = CreateWindowA(
        "Fastview", "Fastview test window", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT,
        // XXX Maybe we should not actually use the dpi. We would like the
        // window to be 1:1 with pixels in the image to start (or at least
        // an integer multiple!)
        static_cast<UINT>(ceil((float)initial_width * dpiX / 96.0f)),
        static_cast<UINT>(ceil((float)initial_height * dpiY / 96.0f)),
        nullptr, nullptr, GetInstance(), this);
    if (window == nullptr) {
      Error("CreateWindow");
      return false;
    }

    ShowWindow(window, SW_SHOWNORMAL);

    // Move it to the foreground when we open. mintty likes to open
    // it behind the terminal, which is not good.
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(window);

    UpdateWindow(window);

    UpdateSdrScale();
    UpdateRenderTarget();

    // We may have gotten WM_PAINT before we finished initializing,
    // so force a redraw now that everything's loaded.
    Redraw();
    initialized = true;
    return true;
  }

  ~App() {
    if (d2d_target_bitmap) d2d_target_bitmap->Release();
    if (d2d_context) d2d_context->Release();
    if (d2d_device) d2d_device->Release();
    if (swap_chain) swap_chain->Release();
    if (d3d_context) d3d_context->Release();
    if (d3d_device) d3d_device->Release();
    if (d2d_factory) d2d_factory->Release();
  }

  void UpdateRenderTarget() {
    Assert(window != nullptr);
    if (d2d_context) return;

    RECT rc;
    GetClientRect(window, &rc);
    UINT width = rc.right - rc.left;
    UINT height = rc.bottom - rc.top;

    if (!d3d_device) {
      UINT creation_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
      D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0
      };
      D3D_FEATURE_LEVEL feature_level;
      if (S_OK != D3D11CreateDevice(
          nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creation_flags,
          feature_levels, 2, D3D11_SDK_VERSION, &d3d_device, &feature_level,
          &d3d_context)) {
        Error("D3D11CreateDevice");
        return;
      }
    }

    IDXGIDevice *dxgi_device = nullptr;
    if (S_OK != d3d_device->QueryInterface(IID_PPV_ARGS(&dxgi_device))) {
      Error("QueryInterface IDXGIDevice");
      return;
    }

    if (!d2d_device) {
      if (S_OK != d2d_factory->CreateDevice(dxgi_device, &d2d_device)) {
        Error("CreateDevice");
        dxgi_device->Release();
        return;
      }
      if (S_OK != d2d_device->CreateDeviceContext(
          D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2d_context)) {
        Error("CreateDeviceContext");
        dxgi_device->Release();
        return;
      }
    }

    IDXGIAdapter *dxgi_adapter = nullptr;
    dxgi_device->GetAdapter(&dxgi_adapter);
    IDXGIFactory2 *dxgi_factory = nullptr;
    dxgi_adapter->GetParent(IID_PPV_ARGS(&dxgi_factory));

    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
    swap_chain_desc.Width = width;
    swap_chain_desc.Height = height;
    swap_chain_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    swap_chain_desc.Stereo = FALSE;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.SampleDesc.Quality = 0;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.BufferCount = 2;
    swap_chain_desc.Scaling = DXGI_SCALING_NONE;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swap_chain_desc.Flags = 0;

    IDXGISwapChain1 *swap_chain1 = nullptr;
    if (S_OK != dxgi_factory->CreateSwapChainForHwnd(
        d3d_device, window, &swap_chain_desc, nullptr, nullptr,
        &swap_chain1)) {
      Error("CreateSwapChainForHwnd");
    } else {
      swap_chain1->QueryInterface(IID_PPV_ARGS(&swap_chain));
      swap_chain1->Release();

      if (swap_chain) {
        swap_chain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
      }

      IDXGISurface *dxgi_back_buffer = nullptr;
      if (S_OK == swap_chain->GetBuffer(0, IID_PPV_ARGS(&dxgi_back_buffer))) {
        D2D1_BITMAP_PROPERTIES1 bitmap_props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_R16G16B16A16_FLOAT,
                              D2D1_ALPHA_MODE_IGNORE));

        if (S_OK != d2d_context->CreateBitmapFromDxgiSurface(
            dxgi_back_buffer, &bitmap_props, &d2d_target_bitmap)) {
          Error("CreateBitmapFromDxgiSurface");
        } else {
          d2d_context->SetTarget(d2d_target_bitmap);
        }
        dxgi_back_buffer->Release();
      }
    }

    dxgi_factory->Release();
    dxgi_adapter->Release();
    dxgi_device->Release();
  }

  void MessageLoop() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
      // XXX? I guess these are builtins...
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  static LRESULT CALLBACK WndProc(
      HWND hWnd,
      UINT message,
      WPARAM wParam,
      LPARAM lParam) {

    // Error(StringPrintf("Wndproc message %d", message));

    if (message == WM_CREATE) {
      // Note that this is called before CreateWindow returns,
      // so the App hasn't even set its hwnd member yet.
      LPCREATESTRUCT pcs = (LPCREATESTRUCT)lParam;
      [[maybe_unused]] App *app = (App *)pcs->lpCreateParams;

      // Use SetWindowLongPtrW to attach the app to the window
      // (Could just do this after the CreateWindow call??)
      ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));

      return 1;
    } else {
      // Otherwise, get the app instance and dispatch
      // there.
      App *app = reinterpret_cast<App *>(
          static_cast<LONG_PTR>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA)));

      // If we haven't even gotten the CREATE message yet
      // (e.g. WM_NCCREATE) then just do the default.
      if (app == nullptr)
        return DefWindowProc(hWnd, message, wParam, lParam);

      // So too if Init hasn't finished. Note that it would be
      // possible to set up this member in WM_CREATE if we wanted.
      if (!app->initialized)
        return DefWindowProc(hWnd, message, wParam, lParam);

      if (hWnd != app->window) {
        Error(std::format("I thought the hwnd passed to WndProc would "
                          "always be the same as the one we saved as "
                          "a member variable: {} vs {}",
                          (intptr_t)hWnd, (intptr_t)app->window));
        exit(0);
      }

      return app->Proc(message, wParam, lParam);
    }
  }

  LRESULT Proc(
      UINT message,
      WPARAM wParam,
      LPARAM lParam) {
    // (Note: not called for WM_CREATE).
    Assert(initialized);

    switch (message) {
    case WM_DESTROY:
      PostQuitMessage(0);
      return 1;

    case WM_PAINT: {
      Redraw();
      // this is what demo app does.. why?
      return 0;
    }
    case WM_DISPLAYCHANGE:
      UpdateSdrScale();
      InvalidateRect(window, nullptr, false);
      return 0;

    case WM_MOVE:
      if (UpdateSdrScale()) {
        InvalidateRect(window, nullptr, false);
      }
      return 0;

      // XXX implement!
    case WM_SIZE:

    default:
      return DefWindowProc(window, message, wParam, lParam);
    }
  }

  void Redraw() {
    // (Note BeginPaint already validates it...?)
    d2d_context->BeginDraw();
    d2d_context->SetTransform(D2D1::Matrix3x2F::Identity());
    // Draw a dark blue background to confirm it's working
    d2d_context->Clear(D2D1::ColorF(0.1f, 0.2f, 0.3f, 1.0f));

    ID2D1SolidColorBrush *brush = nullptr;
    if (SUCCEEDED(d2d_context->CreateSolidColorBrush(
            D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &brush))) {

      #if 0
      // HDR Red (Brightness > 1)
      brush->SetColor(D2D1::ColorF(24.0f, 0.0f, 0.0f, 1.0f));
      d2d_context->FillRectangle(D2D1::RectF(50.0f, 50.0f, 250.0f, 250.0f),
                                 brush);
      #endif

      brush->SetColor(D2D1::ColorF(std::numeric_limits<float>::infinity(),
                                   0.0f, 0.0f, 1.0f));
      d2d_context->FillRectangle(D2D1::RectF(50.0f, 50.0f, 250.0f, 250.0f),
                                 brush);

      // Standard Red (For comparison)
      brush->SetColor(D2D1::ColorF(1.0f * sdr_scale, 0.0f, 0.0f, 1.0f));
      d2d_context->FillRectangle(D2D1::RectF(50.0f, 300.0f, 250.0f, 500.0f),
                                 brush);


      #if 0
      // HDR Green (Brightness > 1)
      brush->SetColor(D2D1::ColorF(0.0f, 24.0f, 0.0f, 1.0f));
      d2d_context->FillRectangle(D2D1::RectF(300.0f, 50.0f, 500.0f, 250.0f),
                                 brush);
      #endif

      brush->SetColor(D2D1::ColorF(0.0f, NAN, 0.0f, 1.0f));
      d2d_context->FillRectangle(D2D1::RectF(300.0f, 50.0f, 500.0f, 250.0f),
                                 brush);


      // Standard Green
      brush->SetColor(D2D1::ColorF(0.0f, 1.0f * sdr_scale, 0.0f, 1.0f));
      d2d_context->FillRectangle(D2D1::RectF(300.0f, 300.0f, 500.0f, 500.0f),
                                 brush);

      #if 0
      // Wide Gamut / HDR Blue (Negative R/G to push outside sRGB gamut)
      brush->SetColor(D2D1::ColorF(-1.0f, -1.0f, 20.0f, 1.0f));
      d2d_context->FillRectangle(D2D1::RectF(550.0f, 50.0f, 750.0f, 250.0f),
                                 brush);
      #endif

      // Wide Gamut / HDR Blue (Negative R/G to push outside sRGB gamut)
      brush->SetColor(D2D1::ColorF(1.0e39,
                                   1.0e39,
                                   -0.0, 1.0f));
      d2d_context->FillRectangle(D2D1::RectF(550.0f, 50.0f, 750.0f, 250.0f),
                                 brush);


      // Standard Blue
      brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 1.0f * sdr_scale, 1.0f));
      d2d_context->FillRectangle(D2D1::RectF(550.0f, 300.0f, 750.0f, 500.0f),
                                 brush);

      // Negative Infinity
      brush->SetColor(D2D1::ColorF(-std::numeric_limits<float>::infinity(),
                                   0.0f, 0.0f, 1.0f));
      d2d_context->FillRectangle(D2D1::RectF(50.0f, 550.0f, 250.0f, 750.0f),
                                 brush);

      // Subnormal
      brush->SetColor(D2D1::ColorF(0.0f,
                                   std::numeric_limits<float>::denorm_min(),
                                   0.0f, 1.0f));
      d2d_context->FillRectangle(D2D1::RectF(300.0f, 550.0f, 500.0f, 750.0f),
                                 brush);

      // Negative value
      brush->SetColor(D2D1::ColorF(-1e39, 0, -1e39, 1.0f));
      d2d_context->FillRectangle(D2D1::RectF(550.0f, 550.0f, 750.0f, 750.0f),
                                 brush);

      brush->Release();
    }

    // XXX supposedly this can fail, and we need to recreate
    // resources in that case
    d2d_context->EndDraw();

    if (swap_chain) {
      swap_chain->Present(1, 0);
    }

    ValidateRect(window, nullptr);
  }
};

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine, int nCmdShow) {
  // If we aren't even able to get a message box here, it's probably
  // because some necessary DLL isn't linked in. We get no feedback in
  // this case!
  // See dependent DLLs with: objdump -x window.exe

  // Is in demo app, seems like a good idea for debugging at least?
  // Apparently this is enabled for all 64-bit processes anyway?
  (void)HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption,
         nullptr, 0);

  // I think just linking in the app stuff causes this to fail to run
  // because it's failing to find some DLL. can we get a better error
  // message from the system somehow?
  {
    App app;
    if (!app.Init()) {
      MessageBoxA(nullptr, "init failed", "init failed", 0);
      return 1;
    }
    app.MessageLoop();
  }

  // MessageBoxA(nullptr, "done", "exit", 0);

  return 0;
}

