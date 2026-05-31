#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if defined(PCFX_WIN32_HAVE_D3D11)
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcommon.h>
#ifndef DXGI_MWA_NO_WINDOW_CHANGES
#define DXGI_MWA_NO_WINDOW_CHANGES 0x1
#endif
#ifndef DXGI_MWA_NO_ALT_ENTER
#define DXGI_MWA_NO_ALT_ENTER 0x2
#endif
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "video_blit.h"

#if defined(PCFX_WIN32_HAVE_D3D11)
#ifndef DXGI_FORMAT_B5G6R5_UNORM
#define DXGI_FORMAT_B5G6R5_UNORM ((DXGI_FORMAT)85)
#endif
#define PCFX_WIN32_D3D_UPLOAD_TEXTURES 3
#define PCFX_WIN32_D3D_SWAP_BUFFERS 2
#endif

static MDFN_Pixel g_framebuffer[PCFX_WIN32_FB_WIDTH * PCFX_WIN32_FB_HEIGHT];
#if defined(WANT_32BPP)
uint32_t* __restrict__ internal_pix = (uint32_t*)g_framebuffer;
#elif defined(WANT_16BPP)
uint16_t* __restrict__ internal_pix = (uint16_t*)g_framebuffer;
#endif
const uint32_t internal_pitch = PCFX_WIN32_FB_WIDTH;

static HWND g_hwnd;
static int g_active_w = 256;
static int g_active_h = PCFX_WIN32_FB_HEIGHT;
static int g_full_width = 0;
static int g_window_scale = 2;
static int g_scale_mode = PCFX_WIN32_SCALE_ASPECT;
static int g_smooth = 0;
#if defined(PCFX_WIN32_HAVE_D3D11)
static int g_video_backend = PCFX_WIN32_VIDEO_D3D11;
#else
static int g_video_backend = PCFX_WIN32_VIDEO_GDI;
#endif
static int g_presentation_fullscreen = 0;
static int g_fullscreen_mode = PCFX_WIN32_FULLSCREEN_EXCLUSIVE;
static int g_need_margin_clear = 1;
static int g_force_full_clear = 1;
static int g_have_last_geometry = 0;
static RECT g_last_client;
static RECT g_last_dst;
static HDC g_backbuffer_dc;
static HBITMAP g_backbuffer_bitmap;
static HBITMAP g_backbuffer_old_bitmap;
static int g_backbuffer_w;
static int g_backbuffer_h;
static char g_video_last_error[512];

typedef struct PCFXWin32DIBInfo
{
    BITMAPINFOHEADER bmiHeader;
    DWORD masks[3];
} PCFXWin32DIBInfo;

static PCFXWin32DIBInfo g_bmi;

static void video_set_error(const char* text)
{
    if(!text) text = "Unknown video backend error.";
    snprintf(g_video_last_error, sizeof(g_video_last_error), "%s", text);
}

static void video_set_hresult_error(const char* what, HRESULT hr)
{
    snprintf(g_video_last_error, sizeof(g_video_last_error), "%s failed: HRESULT 0x%08lx", what, (unsigned long)hr);
}

static void setup_bmi_struct(PCFXWin32DIBInfo* bmi, int width, int height)
{
    memset(bmi, 0, sizeof(*bmi));
    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = width;
    bmi->bmiHeader.biHeight = -height;
    bmi->bmiHeader.biPlanes = 1;
#if defined(WANT_16BPP)
    bmi->bmiHeader.biBitCount = 16;
    bmi->bmiHeader.biCompression = BI_BITFIELDS;
    bmi->masks[0] = 0xF800;
    bmi->masks[1] = 0x07E0;
    bmi->masks[2] = 0x001F;
#elif defined(WANT_32BPP)
    bmi->bmiHeader.biBitCount = 32;
    bmi->bmiHeader.biCompression = BI_RGB;
#else
#error "The Win32 backend supports WANT_16BPP or WANT_32BPP only."
#endif
}

static void video_setup_bmi(void)
{
    setup_bmi_struct(&g_bmi, PCFX_WIN32_FB_WIDTH, PCFX_WIN32_FB_HEIGHT);
}

static int rect_same(const RECT* a, const RECT* b)
{
    return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}

static int current_source_width(void)
{
    int src_w = g_active_w > 0 ? g_active_w : 256;
    if(src_w > PCFX_WIN32_FB_WIDTH) src_w = PCFX_WIN32_FB_WIDTH;
    return src_w;
}

static int current_source_height(void)
{
    int src_h = g_active_h > 0 ? g_active_h : PCFX_WIN32_FB_HEIGHT;
    if(src_h > PCFX_WIN32_FB_HEIGHT) src_h = PCFX_WIN32_FB_HEIGHT;
    return src_h;
}

static int presentation_source_width(void)
{
    return current_source_width();
}

static int presentation_source_height(void)
{
    return current_source_height();
}

static RECT video_calc_dest_rect(const RECT* client)
{
    RECT rc = *client;
    const int src_w = presentation_source_width();
    const int src_h = presentation_source_height();
    const int cw = rc.right - rc.left;
    const int ch = rc.bottom - rc.top;

    if(cw <= 0 || ch <= 0)
        return rc;

    switch(g_scale_mode)
    {
        case PCFX_WIN32_SCALE_STRETCH:
            rc.left = 0;
            rc.top = 0;
            rc.right = cw;
            rc.bottom = ch;
            return rc;

        case PCFX_WIN32_SCALE_ASPECT:
        {
            const int aspect_num = 4;
            const int aspect_den = 3;
            int dw = cw;
            int dh = (int)((int64_t)dw * aspect_den / aspect_num);
            if(dh > ch)
            {
                dh = ch;
                dw = (int)((int64_t)dh * aspect_num / aspect_den);
            }
            if(dw < 1) dw = 1;
            if(dh < 1) dh = 1;
            rc.left = (cw - dw) / 2;
            rc.top = (ch - dh) / 2;
            rc.right = rc.left + dw;
            rc.bottom = rc.top + dh;
            return rc;
        }

        case PCFX_WIN32_SCALE_INTEGER:
        {
            int sx = cw / src_w;
            int sy = ch / src_h;
            int scale = sx < sy ? sx : sy;
            if(scale < 1) scale = 1;
            int dw = src_w * scale;
            int dh = src_h * scale;
            rc.left = (cw - dw) / 2;
            rc.top = (ch - dh) / 2;
            rc.right = rc.left + dw;
            rc.bottom = rc.top + dh;
            return rc;
        }

        case PCFX_WIN32_SCALE_FIXED:
        default:
        {
            int scale = g_window_scale > 0 ? g_window_scale : 1;
            int dw = src_w * scale;
            int dh = src_h * scale;
            rc.left = (cw - dw) / 2;
            rc.top = (ch - dh) / 2;
            rc.right = rc.left + dw;
            rc.bottom = rc.top + dh;
            return rc;
        }
    }
}

static void mark_geometry_dirty(void)
{
    g_need_margin_clear = 1;
    g_force_full_clear = 1;
    g_have_last_geometry = 0;
}

static void request_paint(int clear_margins)
{
    if(clear_margins)
        mark_geometry_dirty();
    if(g_hwnd)
        InvalidateRect(g_hwnd, NULL, FALSE);
}

static void fill_rect_if_nonempty(HDC hdc, const RECT* rc, HBRUSH brush)
{
    if(rc->right > rc->left && rc->bottom > rc->top)
        FillRect(hdc, rc, brush);
}

static void clear_full_client(HDC hdc, const RECT* client)
{
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    fill_rect_if_nonempty(hdc, client, black);
}

static void clear_margins(HDC hdc, const RECT* client, const RECT* dst)
{
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RECT r;

    r.left = client->left;
    r.top = client->top;
    r.right = client->right;
    r.bottom = dst->top;
    fill_rect_if_nonempty(hdc, &r, black);

    r.left = client->left;
    r.top = dst->bottom;
    r.right = client->right;
    r.bottom = client->bottom;
    fill_rect_if_nonempty(hdc, &r, black);

    r.left = client->left;
    r.top = dst->top;
    r.right = dst->left;
    r.bottom = dst->bottom;
    fill_rect_if_nonempty(hdc, &r, black);

    r.left = dst->right;
    r.top = dst->top;
    r.right = client->right;
    r.bottom = dst->bottom;
    fill_rect_if_nonempty(hdc, &r, black);
}

static void destroy_backbuffer(void)
{
    if(g_backbuffer_dc)
    {
        if(g_backbuffer_old_bitmap)
        {
            SelectObject(g_backbuffer_dc, g_backbuffer_old_bitmap);
            g_backbuffer_old_bitmap = NULL;
        }
        DeleteDC(g_backbuffer_dc);
        g_backbuffer_dc = NULL;
    }
    if(g_backbuffer_bitmap)
    {
        DeleteObject(g_backbuffer_bitmap);
        g_backbuffer_bitmap = NULL;
    }
    g_backbuffer_w = 0;
    g_backbuffer_h = 0;
}

static HDC ensure_backbuffer(HDC screen_hdc, int width, int height)
{
    if(width <= 0 || height <= 0)
        return NULL;

    if(g_backbuffer_dc && g_backbuffer_bitmap && g_backbuffer_w == width && g_backbuffer_h == height)
        return g_backbuffer_dc;

    destroy_backbuffer();

    g_backbuffer_dc = CreateCompatibleDC(screen_hdc);
    if(!g_backbuffer_dc)
        return NULL;

    g_backbuffer_bitmap = CreateCompatibleBitmap(screen_hdc, width, height);
    if(!g_backbuffer_bitmap)
    {
        destroy_backbuffer();
        return NULL;
    }

    g_backbuffer_old_bitmap = (HBITMAP)SelectObject(g_backbuffer_dc, g_backbuffer_bitmap);
    g_backbuffer_w = width;
    g_backbuffer_h = height;
    return g_backbuffer_dc;
}

#if defined(PCFX_WIN32_HAVE_D3D11)
typedef HRESULT (WINAPI *PFN_D3DCompile_Fn)(LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName,
                                            const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude,
                                            LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2,
                                            ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs);

typedef struct D3DVertex
{
    float x, y;
    float u, v;
} D3DVertex;

static HMODULE g_d3dcompiler;
static PFN_D3DCompile_Fn g_D3DCompile;
static ID3D11Device* g_d3d_device;
static ID3D11DeviceContext* g_d3d_context;
static IDXGISwapChain* g_d3d_swap;
static ID3D11RenderTargetView* g_d3d_rtv;
static ID3D11Texture2D* g_d3d_textures[PCFX_WIN32_D3D_UPLOAD_TEXTURES];
static ID3D11ShaderResourceView* g_d3d_srvs[PCFX_WIN32_D3D_UPLOAD_TEXTURES];
static ID3D11SamplerState* g_d3d_sampler_point;
static ID3D11SamplerState* g_d3d_sampler_linear;
static ID3D11VertexShader* g_d3d_vs;
static ID3D11PixelShader* g_d3d_ps;
static ID3D11InputLayout* g_d3d_layout;
static ID3D11Buffer* g_d3d_vb;
static ID3D11Buffer* g_d3d_cb;
static int g_d3d_client_w;
static int g_d3d_client_h;
static int g_d3d_ready;
static int g_d3d_failed;
static int g_d3d_upload_index;
static int g_d3d_vertex_valid;
static int g_d3d_vertex_src_w;
static int g_d3d_vertex_src_h;
static int g_d3d_vertex_client_w;
static int g_d3d_vertex_client_h;
static RECT g_d3d_vertex_dst;
static int g_d3d_cb_src_w;
static int g_d3d_cb_src_h;
static int g_d3d_exclusive_active;
static int g_d3d_exclusive_failed;


static void safe_release_iunknown(void** pp)
{
    if(pp && *pp)
    {
        IUnknown_Release((IUnknown*)*pp);
        *pp = NULL;
    }
}

static void d3d_release_rtv(void)
{
    safe_release_iunknown((void**)&g_d3d_rtv);
}

static void d3d_force_windowed_state(void)
{
    if(g_d3d_swap)
        IDXGISwapChain_SetFullscreenState(g_d3d_swap, FALSE, NULL);
    g_d3d_exclusive_active = 0;
}

static void d3d_destroy(void)
{
    d3d_force_windowed_state();
    d3d_release_rtv();
    safe_release_iunknown((void**)&g_d3d_cb);
    safe_release_iunknown((void**)&g_d3d_vb);
    safe_release_iunknown((void**)&g_d3d_layout);
    safe_release_iunknown((void**)&g_d3d_ps);
    safe_release_iunknown((void**)&g_d3d_vs);
    safe_release_iunknown((void**)&g_d3d_sampler_linear);
    safe_release_iunknown((void**)&g_d3d_sampler_point);
    for(int i = 0; i < PCFX_WIN32_D3D_UPLOAD_TEXTURES; i++)
    {
        safe_release_iunknown((void**)&g_d3d_srvs[i]);
        safe_release_iunknown((void**)&g_d3d_textures[i]);
    }
    if(g_d3d_context)
    {
        ID3D11DeviceContext_ClearState(g_d3d_context);
        ID3D11DeviceContext_Flush(g_d3d_context);
    }
    safe_release_iunknown((void**)&g_d3d_swap);
    safe_release_iunknown((void**)&g_d3d_context);
    safe_release_iunknown((void**)&g_d3d_device);
    if(g_d3dcompiler)
    {
        FreeLibrary(g_d3dcompiler);
        g_d3dcompiler = NULL;
    }
    g_D3DCompile = NULL;
    g_d3d_client_w = 0;
    g_d3d_client_h = 0;
    g_d3d_ready = 0;
    g_d3d_upload_index = 0;
    g_d3d_vertex_valid = 0;
    g_d3d_vertex_src_w = 0;
    g_d3d_vertex_src_h = 0;
    g_d3d_vertex_client_w = 0;
    g_d3d_vertex_client_h = 0;
    memset(&g_d3d_vertex_dst, 0, sizeof(g_d3d_vertex_dst));
    g_d3d_cb_src_w = 0;
    g_d3d_cb_src_h = 0;
    g_d3d_exclusive_active = 0;
    g_d3d_exclusive_failed = 0;
}

static int d3d_load_compiler(void)
{
    static const char* const dlls[] = {
        "d3dcompiler_47.dll",
        "d3dcompiler_46.dll",
        "d3dcompiler_43.dll",
        "d3dcompiler_42.dll",
        "d3dcompiler_41.dll"
    };
    for(size_t i = 0; i < sizeof(dlls) / sizeof(dlls[0]); i++)
    {
        g_d3dcompiler = LoadLibraryA(dlls[i]);
        if(g_d3dcompiler)
        {
            g_D3DCompile = (PFN_D3DCompile_Fn)GetProcAddress(g_d3dcompiler, "D3DCompile");
            if(g_D3DCompile)
                return 1;
            FreeLibrary(g_d3dcompiler);
            g_d3dcompiler = NULL;
        }
    }
    video_set_error("D3D11 selected, but no usable d3dcompiler_*.dll was found. Select Video > Backend > GDI or install the DirectX shader compiler runtime.");
    g_d3d_failed = 1;
    return 0;
}

static void d3d_set_frame_latency(void)
{
    IDXGIDevice1* dxgi_device = NULL;

    if(!g_d3d_device)
        return;

    if(SUCCEEDED(ID3D11Device_QueryInterface(g_d3d_device, &IID_IDXGIDevice1, (void**)&dxgi_device)))
    {
        IDXGIDevice1_SetMaximumFrameLatency(dxgi_device, 1);
        IDXGIDevice1_Release(dxgi_device);
    }
}

static void d3d_disable_dxgi_automatic_window_changes(void)
{
    IDXGIFactory* factory = NULL;

    if(!g_d3d_swap || !g_hwnd)
        return;

    if(SUCCEEDED(IDXGISwapChain_GetParent(g_d3d_swap, &IID_IDXGIFactory, (void**)&factory)))
    {
        /* The frontend owns fullscreen/windowed transitions.  Disable DXGI's
         * built-in Alt+Enter/window mutation path so it cannot race with the
         * emulator's menu state, saved placement, or exclusive/borderless mode
         * selection. */
        IDXGIFactory_MakeWindowAssociation(factory, g_hwnd,
            DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
        IDXGIFactory_Release(factory);
    }
}

static HRESULT d3d_compile_shader(const char* source, const char* entry, const char* target, ID3DBlob** blob)
{
    ID3DBlob* err = NULL;
    HRESULT hr;
    UINT flags = 0;
    if(!g_D3DCompile && !d3d_load_compiler())
        return E_FAIL;
    hr = g_D3DCompile(source, strlen(source), "pcfx_win32_video.hlsl", NULL, NULL, entry, target, flags, 0, blob, &err);
    if(FAILED(hr))
    {
        if(err)
        {
            const char* msg = (const char*)ID3D10Blob_GetBufferPointer(err);
            snprintf(g_video_last_error, sizeof(g_video_last_error), "D3D shader compilation failed: %s", msg ? msg : "unknown compiler error");
            ID3D10Blob_Release(err);
        }
        else
        {
            video_set_hresult_error("D3D shader compilation", hr);
        }
    }
    if(err)
        ID3D10Blob_Release(err);
    return hr;
}

static int d3d_create_render_target(int width, int height)
{
    ID3D11Texture2D* backbuffer = NULL;
    HRESULT hr;

    if(width <= 0 || height <= 0)
        return 0;

    d3d_release_rtv();
    hr = IDXGISwapChain_GetBuffer(g_d3d_swap, 0, &IID_ID3D11Texture2D, (void**)&backbuffer);
    if(FAILED(hr))
    {
        video_set_hresult_error("IDXGISwapChain::GetBuffer", hr);
        return 0;
    }
    hr = ID3D11Device_CreateRenderTargetView(g_d3d_device, (ID3D11Resource*)backbuffer, NULL, &g_d3d_rtv);
    ID3D11Texture2D_Release(backbuffer);
    if(FAILED(hr))
    {
        video_set_hresult_error("ID3D11Device::CreateRenderTargetView", hr);
        return 0;
    }
    g_d3d_client_w = width;
    g_d3d_client_h = height;
    return 1;
}

static void d3d_monitor_size_for_window(int* out_w, int* out_h)
{
    MONITORINFO mi;
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if(g_hwnd && GetMonitorInfoA(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi))
    {
        w = mi.rcMonitor.right - mi.rcMonitor.left;
        h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }

    if(w < 640) w = 640;
    if(h < 480) h = 480;
    if(out_w) *out_w = w;
    if(out_h) *out_h = h;
}

static IDXGIOutput* d3d_get_containing_output_for_window(void)
{
    IDXGIOutput* output = NULL;
    if(g_d3d_swap && SUCCEEDED(IDXGISwapChain_GetContainingOutput(g_d3d_swap, &output)) && output)
        return output;
    return NULL;
}

static void d3d_exclusive_target_size(UINT* out_w, UINT* out_h, IDXGIOutput** out_output)
{
    IDXGIOutput* output = d3d_get_containing_output_for_window();
    int w = 0;
    int h = 0;

    d3d_monitor_size_for_window(&w, &h);

    if(output)
    {
        DXGI_OUTPUT_DESC od;
        memset(&od, 0, sizeof(od));
        if(SUCCEEDED(IDXGIOutput_GetDesc(output, &od)))
        {
            const int ow = od.DesktopCoordinates.right - od.DesktopCoordinates.left;
            const int oh = od.DesktopCoordinates.bottom - od.DesktopCoordinates.top;
            if(ow > 0 && oh > 0)
            {
                w = ow;
                h = oh;
            }
        }
    }

    /* Do not pick a lower display mode for exclusive fullscreen.  The previous
     * implementation capped exclusive mode to common <=1080p modes to reduce
     * backbuffer work, but that makes DXGI/driver/monitor scaling part of the
     * presentation path.  On some systems the result is the emulator image
     * being drawn into an apparently offset or partial region.  Use the current
     * desktop/output size instead, so the fullscreen-exclusive backbuffer,
     * D3D viewport, and monitor scanout agree exactly. */
    if(w < 640) w = 640;
    if(h < 480) h = 480;

    if(out_w) *out_w = (UINT)w;
    if(out_h) *out_h = (UINT)h;
    if(out_output)
        *out_output = output;
    else if(output)
        IDXGIOutput_Release(output);
}

static void d3d_get_swapchain_size(UINT fallback_w, UINT fallback_h, int* out_w, int* out_h)
{
    DXGI_SWAP_CHAIN_DESC desc;
    UINT w = fallback_w;
    UINT h = fallback_h;

    memset(&desc, 0, sizeof(desc));
    if(g_d3d_swap && SUCCEEDED(IDXGISwapChain_GetDesc(g_d3d_swap, &desc)))
    {
        if(desc.BufferDesc.Width > 0) w = desc.BufferDesc.Width;
        if(desc.BufferDesc.Height > 0) h = desc.BufferDesc.Height;
    }

    if(out_w) *out_w = (int)w;
    if(out_h) *out_h = (int)h;
}

static int d3d_leave_exclusive(void)
{
    HRESULT hr;
    if(!g_d3d_swap || !g_d3d_exclusive_active)
        return 1;

    ID3D11DeviceContext_OMSetRenderTargets(g_d3d_context, 0, NULL, NULL);
    d3d_release_rtv();
    hr = IDXGISwapChain_SetFullscreenState(g_d3d_swap, FALSE, NULL);
    g_d3d_exclusive_active = 0;
    g_d3d_exclusive_failed = 0;
    g_d3d_vertex_valid = 0;
    g_d3d_client_w = 0;
    g_d3d_client_h = 0;
    if(FAILED(hr))
    {
        video_set_hresult_error("IDXGISwapChain::SetFullscreenState(FALSE)", hr);
        return 0;
    }
    return 1;
}

static int d3d_enter_exclusive(void)
{
    IDXGIOutput* output = NULL;
    UINT target_w = 0;
    UINT target_h = 0;
    int actual_w = 0;
    int actual_h = 0;
    HRESULT hr;

    if(!g_d3d_swap || g_d3d_exclusive_active)
        return 1;
    if(g_d3d_exclusive_failed)
        return 0;

    d3d_exclusive_target_size(&target_w, &target_h, &output);

    ID3D11DeviceContext_OMSetRenderTargets(g_d3d_context, 0, NULL, NULL);
    d3d_release_rtv();

    /* Enter exclusive fullscreen at the current output desktop size.  Do not
     * force a lower DXGI display mode: the emulator already scales a tiny
     * framebuffer, and display-mode scaling can produce an offset/partial
     * image on some driver stacks. */
    hr = IDXGISwapChain_SetFullscreenState(g_d3d_swap, TRUE, output);
    if(output)
    {
        IDXGIOutput_Release(output);
        output = NULL;
    }
    if(FAILED(hr))
    {
        video_set_hresult_error("IDXGISwapChain::SetFullscreenState(TRUE)", hr);
        g_d3d_exclusive_failed = 1;
        return 0;
    }

    hr = IDXGISwapChain_ResizeBuffers(g_d3d_swap, PCFX_WIN32_D3D_SWAP_BUFFERS,
                                      target_w, target_h,
                                      DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    if(FAILED(hr))
    {
        IDXGISwapChain_SetFullscreenState(g_d3d_swap, FALSE, NULL);
        video_set_hresult_error("IDXGISwapChain::ResizeBuffers(exclusive desktop-size)", hr);
        g_d3d_exclusive_failed = 1;
        return 0;
    }

    d3d_get_swapchain_size(target_w, target_h, &actual_w, &actual_h);

    g_d3d_exclusive_active = 1;
    g_d3d_exclusive_failed = 0;
    g_d3d_vertex_valid = 0;
    g_d3d_cb_src_w = 0;
    g_d3d_cb_src_h = 0;
    if(!d3d_create_render_target(actual_w, actual_h))
    {
        IDXGISwapChain_SetFullscreenState(g_d3d_swap, FALSE, NULL);
        g_d3d_exclusive_active = 0;
        g_d3d_exclusive_failed = 1;
        return 0;
    }
    return 1;
}

static void d3d_apply_fullscreen_request(void)
{
    if(!g_d3d_swap)
        return;

    if(g_presentation_fullscreen && g_fullscreen_mode == PCFX_WIN32_FULLSCREEN_EXCLUSIVE)
    {
        if(!g_d3d_exclusive_active && !g_d3d_exclusive_failed)
            d3d_enter_exclusive();
    }
    else
    {
        d3d_leave_exclusive();
    }
}

static int d3d_resize_if_needed(int width, int height)
{
    HRESULT hr;
    if(!g_d3d_swap)
        return 0;
    if(width <= 0 || height <= 0)
        return 0;
    if(g_d3d_rtv && g_d3d_client_w == width && g_d3d_client_h == height)
        return 1;

    d3d_release_rtv();
    ID3D11DeviceContext_OMSetRenderTargets(g_d3d_context, 0, NULL, NULL);
    hr = IDXGISwapChain_ResizeBuffers(g_d3d_swap, 0, (UINT)width, (UINT)height, DXGI_FORMAT_UNKNOWN, 0);
    if(FAILED(hr))
    {
        video_set_hresult_error("IDXGISwapChain::ResizeBuffers", hr);
        return 0;
    }
    return d3d_create_render_target(width, height);
}

static int d3d_init(void)
{
    RECT rc;
    int client_w, client_h;
    HRESULT hr;
    DXGI_SWAP_CHAIN_DESC scd;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got_level;
    ID3DBlob* vs_blob = NULL;
    ID3DBlob* ps_blob = NULL;
    D3D11_TEXTURE2D_DESC tdesc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D11_SUBRESOURCE_DATA initial_data;
    D3D11_SAMPLER_DESC sdesc;
    D3D11_BUFFER_DESC bdesc;
    D3D11_INPUT_ELEMENT_DESC elems[2];

    static const char vs_src[] =
        "struct VSIn { float2 pos : POSITION; float2 tex : TEXCOORD0; };\n"
        "struct VSOut { float4 pos : SV_Position; float2 tex : TEXCOORD0; };\n"
        "VSOut main(VSIn input) { VSOut o; o.pos = float4(input.pos, 0.0, 1.0); o.tex = input.tex; return o; }\n";
    static const char ps_src[] =
        "Texture2D tex0 : register(t0);\n"
        "SamplerState samp0 : register(s0);\n"
        "cbuffer Params : register(b0) { float4 valid_uv; };\n"
        "struct VSOut { float4 pos : SV_Position; float2 tex : TEXCOORD0; };\n"
        "float4 main(VSOut input) : SV_Target {\n"
        "  if(input.tex.x < 0.0 || input.tex.y < 0.0 || input.tex.x > valid_uv.x || input.tex.y > valid_uv.y) return float4(0.0, 0.0, 0.0, 1.0);\n"
        "  return tex0.Sample(samp0, input.tex);\n"
        "}\n";

    if(g_d3d_ready)
        return 1;
    if(g_d3d_failed)
        return 0;
    if(!g_hwnd)
    {
        video_set_error("D3D11 cannot initialize before the Win32 window exists.");
        return 0;
    }
    if(!GetClientRect(g_hwnd, &rc))
    {
        video_set_error("GetClientRect failed during D3D11 initialization.");
        return 0;
    }
    client_w = rc.right - rc.left;
    client_h = rc.bottom - rc.top;
    if(client_w <= 0 || client_h <= 0)
        return 0;

    if(!d3d_load_compiler())
        return 0;

    memset(&scd, 0, sizeof(scd));
    scd.BufferDesc.Width = (UINT)client_w;
    scd.BufferDesc.Height = (UINT)client_h;
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 0;
    scd.BufferDesc.RefreshRate.Denominator = 0;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = PCFX_WIN32_D3D_SWAP_BUFFERS;
    scd.OutputWindow = g_hwnd;
    scd.Windowed = TRUE;
    /* Use the old blt-model DISCARD swap effect for Windows 7 compatibility.
     * Sequential swap chains preserve backbuffer contents and can force a large
     * full-window copy during Present().  With DISCARD we make no preservation
     * assumption: one full-client draw covers every pixel, and the shader
     * outputs black outside the active emulated image. */
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                       D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED,
                                       levels, (UINT)(sizeof(levels) / sizeof(levels[0])),
                                       D3D11_SDK_VERSION, &scd, &g_d3d_swap,
                                       &g_d3d_device, &got_level, &g_d3d_context);
    if(FAILED(hr))
    {
        video_set_hresult_error("D3D11 hardware device creation", hr);
        d3d_destroy();
        g_d3d_failed = 1;
        return 0;
    }

    d3d_set_frame_latency();
    d3d_disable_dxgi_automatic_window_changes();

    if(!d3d_create_render_target(client_w, client_h))
    {
        d3d_destroy();
        g_d3d_failed = 1;
        return 0;
    }

    hr = d3d_compile_shader(vs_src, "main", "vs_4_0", &vs_blob);
    if(FAILED(hr)) { d3d_destroy(); g_d3d_failed = 1; return 0; }
    hr = d3d_compile_shader(ps_src, "main", "ps_4_0", &ps_blob);
    if(FAILED(hr)) { ID3D10Blob_Release(vs_blob); d3d_destroy(); g_d3d_failed = 1; return 0; }

    hr = ID3D11Device_CreateVertexShader(g_d3d_device, ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob), NULL, &g_d3d_vs);
    if(FAILED(hr))
    {
        video_set_hresult_error("ID3D11Device::CreateVertexShader", hr);
        ID3D10Blob_Release(vs_blob);
        ID3D10Blob_Release(ps_blob);
        d3d_destroy();
        g_d3d_failed = 1;
        return 0;
    }
    hr = ID3D11Device_CreatePixelShader(g_d3d_device, ID3D10Blob_GetBufferPointer(ps_blob), ID3D10Blob_GetBufferSize(ps_blob), NULL, &g_d3d_ps);
    if(FAILED(hr))
    {
        video_set_hresult_error("ID3D11Device::CreatePixelShader(video)", hr);
        ID3D10Blob_Release(vs_blob);
        ID3D10Blob_Release(ps_blob);
        d3d_destroy();
        g_d3d_failed = 1;
        return 0;
    }
    ID3D10Blob_Release(ps_blob);
    ps_blob = NULL;

    memset(elems, 0, sizeof(elems));
    elems[0].SemanticName = "POSITION";
    elems[0].SemanticIndex = 0;
    elems[0].Format = DXGI_FORMAT_R32G32_FLOAT;
    elems[0].InputSlot = 0;
    elems[0].AlignedByteOffset = 0;
    elems[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    elems[0].InstanceDataStepRate = 0;
    elems[1].SemanticName = "TEXCOORD";
    elems[1].SemanticIndex = 0;
    elems[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    elems[1].InputSlot = 0;
    elems[1].AlignedByteOffset = 8;
    elems[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    elems[1].InstanceDataStepRate = 0;
    hr = ID3D11Device_CreateInputLayout(g_d3d_device, elems, 2, ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob), &g_d3d_layout);
    ID3D10Blob_Release(vs_blob);
    if(FAILED(hr))
    {
        video_set_hresult_error("ID3D11Device::CreateInputLayout", hr);
        d3d_destroy();
        g_d3d_failed = 1;
        return 0;
    }

    memset(&bdesc, 0, sizeof(bdesc));
    bdesc.ByteWidth = sizeof(D3DVertex) * 4;
    bdesc.Usage = D3D11_USAGE_DYNAMIC;
    bdesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateBuffer(g_d3d_device, &bdesc, NULL, &g_d3d_vb);
    if(FAILED(hr))
    {
        video_set_hresult_error("ID3D11Device::CreateBuffer(vertex)", hr);
        d3d_destroy();
        g_d3d_failed = 1;
        return 0;
    }

    memset(&bdesc, 0, sizeof(bdesc));
    bdesc.ByteWidth = 16;
    bdesc.Usage = D3D11_USAGE_DEFAULT;
    bdesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = ID3D11Device_CreateBuffer(g_d3d_device, &bdesc, NULL, &g_d3d_cb);
    if(FAILED(hr))
    {
        video_set_hresult_error("ID3D11Device::CreateBuffer(constants)", hr);
        d3d_destroy();
        g_d3d_failed = 1;
        return 0;
    }

    memset(&tdesc, 0, sizeof(tdesc));
    tdesc.Width = PCFX_WIN32_FB_WIDTH;
    tdesc.Height = PCFX_WIN32_FB_HEIGHT;
    tdesc.MipLevels = 1;
    tdesc.ArraySize = 1;
#if defined(WANT_16BPP)
    tdesc.Format = DXGI_FORMAT_B5G6R5_UNORM;
#else
    tdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
#endif
    tdesc.SampleDesc.Count = 1;
    tdesc.Usage = D3D11_USAGE_DYNAMIC;
    tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    tdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = tdesc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;

    memset(&initial_data, 0, sizeof(initial_data));
    initial_data.pSysMem = g_framebuffer;
    initial_data.SysMemPitch = PCFX_WIN32_FB_WIDTH * (UINT)sizeof(MDFN_Pixel);
    initial_data.SysMemSlicePitch = PCFX_WIN32_FB_WIDTH * PCFX_WIN32_FB_HEIGHT * (UINT)sizeof(MDFN_Pixel);

    for(int i = 0; i < PCFX_WIN32_D3D_UPLOAD_TEXTURES; i++)
    {
        hr = ID3D11Device_CreateTexture2D(g_d3d_device, &tdesc, &initial_data, &g_d3d_textures[i]);
        if(FAILED(hr))
        {
            video_set_hresult_error("ID3D11Device::CreateTexture2D(dynamic video)", hr);
            d3d_destroy();
            g_d3d_failed = 1;
            return 0;
        }

        hr = ID3D11Device_CreateShaderResourceView(g_d3d_device, (ID3D11Resource*)g_d3d_textures[i], &srv_desc, &g_d3d_srvs[i]);
        if(FAILED(hr))
        {
            video_set_hresult_error("ID3D11Device::CreateShaderResourceView", hr);
            d3d_destroy();
            g_d3d_failed = 1;
            return 0;
        }
    }

    memset(&sdesc, 0, sizeof(sdesc));
    sdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sdesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = ID3D11Device_CreateSamplerState(g_d3d_device, &sdesc, &g_d3d_sampler_point);
    if(FAILED(hr))
    {
        video_set_hresult_error("ID3D11Device::CreateSamplerState(point)", hr);
        d3d_destroy();
        g_d3d_failed = 1;
        return 0;
    }
    sdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    hr = ID3D11Device_CreateSamplerState(g_d3d_device, &sdesc, &g_d3d_sampler_linear);
    if(FAILED(hr))
    {
        video_set_hresult_error("ID3D11Device::CreateSamplerState(linear)", hr);
        d3d_destroy();
        g_d3d_failed = 1;
        return 0;
    }

    g_d3d_ready = 1;
    video_set_error("No video backend error.");
    return 1;
}

static int d3d_update_vertices(const RECT* client, const RECT* dst, float valid_u, float valid_v)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    D3DVertex* v;
    HRESULT hr;
    const int client_w = client->right - client->left;
    const int client_h = client->bottom - client->top;
    const int dst_w = dst->right - dst->left;
    const int dst_h = dst->bottom - dst->top;
    float u0, u1, v0, v1;

    if(client_w <= 0 || client_h <= 0 || dst_w <= 0 || dst_h <= 0)
        return 0;

    /* Draw one full-client quad and let the pixel shader output black for
     * texture coordinates outside the active emulated image.  This avoids the
     * old fullscreen aspect path's separate per-frame black margin draws,
     * which are disproportionately expensive on some DXGI/D3D11 stacks. */
    u0 = ((float)(client->left - dst->left) / (float)dst_w) * valid_u;
    u1 = ((float)(client->right - dst->left) / (float)dst_w) * valid_u;
    v0 = ((float)(client->top - dst->top) / (float)dst_h) * valid_v;
    v1 = ((float)(client->bottom - dst->top) / (float)dst_h) * valid_v;

    hr = ID3D11DeviceContext_Map(g_d3d_context, (ID3D11Resource*)g_d3d_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if(FAILED(hr))
    {
        video_set_hresult_error("ID3D11DeviceContext::Map(vertex buffer)", hr);
        return 0;
    }

    v = (D3DVertex*)mapped.pData;
    v[0].x = -1.0f; v[0].y =  1.0f; v[0].u = u0; v[0].v = v0;
    v[1].x =  1.0f; v[1].y =  1.0f; v[1].u = u1; v[1].v = v0;
    v[2].x = -1.0f; v[2].y = -1.0f; v[2].u = u0; v[2].v = v1;
    v[3].x =  1.0f; v[3].y = -1.0f; v[3].u = u1; v[3].v = v1;

    ID3D11DeviceContext_Unmap(g_d3d_context, (ID3D11Resource*)g_d3d_vb, 0);
    return 1;
}

static int d3d_upload_frame(int src_w, int src_h, ID3D11ShaderResourceView** out_srv)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;
    int index;
    const uint8_t* src;
    uint8_t* dst;
    const UINT src_pitch = PCFX_WIN32_FB_WIDTH * (UINT)sizeof(MDFN_Pixel);
    const UINT copy_bytes = (UINT)src_w * (UINT)sizeof(MDFN_Pixel);

    if(!out_srv || src_w <= 0 || src_h <= 0)
        return 0;

    index = (g_d3d_upload_index + 1) % PCFX_WIN32_D3D_UPLOAD_TEXTURES;
    hr = ID3D11DeviceContext_Map(g_d3d_context, (ID3D11Resource*)g_d3d_textures[index], 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if(FAILED(hr))
    {
        video_set_hresult_error("ID3D11DeviceContext::Map(dynamic video texture)", hr);
        return 0;
    }

    src = (const uint8_t*)g_framebuffer;
    dst = (uint8_t*)mapped.pData;
    for(int y = 0; y < src_h; y++)
        memcpy(dst + (size_t)y * mapped.RowPitch, src + (size_t)y * src_pitch, copy_bytes);

    ID3D11DeviceContext_Unmap(g_d3d_context, (ID3D11Resource*)g_d3d_textures[index], 0);

    g_d3d_upload_index = index;
    *out_srv = g_d3d_srvs[index];
    return 1;
}



static int d3d_present_frame(void)
{
    RECT client;
    RECT dst;
    int client_w, client_h;
    int dst_w, dst_h;
    int src_w, src_h;
    D3D11_VIEWPORT vp;
    UINT stride = sizeof(D3DVertex);
    UINT offset = 0;
    ID3D11ShaderResourceView* srv = NULL;
    ID3D11SamplerState* sampler = g_smooth ? g_d3d_sampler_linear : g_d3d_sampler_point;
    HRESULT hr;

    if(!g_hwnd)
        return 0;
    if(!d3d_init())
        return 0;

    GetClientRect(g_hwnd, &client);
    client_w = client.right - client.left;
    client_h = client.bottom - client.top;
    if(client_w <= 0 || client_h <= 0)
        return 1;

    d3d_apply_fullscreen_request();

    if(g_d3d_exclusive_active)
    {
        client.left = 0;
        client.top = 0;
        client.right = g_d3d_client_w;
        client.bottom = g_d3d_client_h;
        client_w = g_d3d_client_w;
        client_h = g_d3d_client_h;
    }
    else if(!d3d_resize_if_needed(client_w, client_h))
    {
        return 0;
    }

    dst = video_calc_dest_rect(&client);
    dst_w = dst.right - dst.left;
    dst_h = dst.bottom - dst.top;
    if(dst_w <= 0 || dst_h <= 0)
        return 1;

    src_w = current_source_width();
    src_h = current_source_height();

    if(!d3d_upload_frame(src_w, src_h, &srv))
        return 0;

    {
        const float valid_u = (float)src_w / (float)PCFX_WIN32_FB_WIDTH;
        const float valid_v = (float)src_h / (float)PCFX_WIN32_FB_HEIGHT;

        if(!g_d3d_vertex_valid ||
           g_d3d_vertex_src_w != src_w || g_d3d_vertex_src_h != src_h ||
           g_d3d_vertex_client_w != client_w || g_d3d_vertex_client_h != client_h ||
           !rect_same(&g_d3d_vertex_dst, &dst))
        {
            if(!d3d_update_vertices(&client, &dst, valid_u, valid_v))
                return 0;
            g_d3d_vertex_valid = 1;
            g_d3d_vertex_src_w = src_w;
            g_d3d_vertex_src_h = src_h;
            g_d3d_vertex_client_w = client_w;
            g_d3d_vertex_client_h = client_h;
            g_d3d_vertex_dst = dst;
        }

        if(g_d3d_cb_src_w != src_w || g_d3d_cb_src_h != src_h)
        {
            float constants[4] = { valid_u, valid_v, 0.0f, 0.0f };
            ID3D11DeviceContext_UpdateSubresource(g_d3d_context, (ID3D11Resource*)g_d3d_cb, 0, NULL, constants, 0, 0);
            g_d3d_cb_src_w = src_w;
            g_d3d_cb_src_h = src_h;
        }
    }

    memset(&vp, 0, sizeof(vp));
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (FLOAT)client_w;
    vp.Height = (FLOAT)client_h;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    ID3D11DeviceContext_OMSetRenderTargets(g_d3d_context, 1, &g_d3d_rtv, NULL);
    ID3D11DeviceContext_IASetInputLayout(g_d3d_context, g_d3d_layout);
    ID3D11DeviceContext_IASetVertexBuffers(g_d3d_context, 0, 1, &g_d3d_vb, &stride, &offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(g_d3d_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11DeviceContext_VSSetShader(g_d3d_context, g_d3d_vs, NULL, 0);
    ID3D11DeviceContext_RSSetViewports(g_d3d_context, 1, &vp);
    ID3D11DeviceContext_PSSetShader(g_d3d_context, g_d3d_ps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(g_d3d_context, 0, 1, &g_d3d_cb);
    ID3D11DeviceContext_PSSetShaderResources(g_d3d_context, 0, 1, &srv);
    ID3D11DeviceContext_PSSetSamplers(g_d3d_context, 0, 1, &sampler);
    ID3D11DeviceContext_Draw(g_d3d_context, 4, 0);

    hr = IDXGISwapChain_Present(g_d3d_swap, 0, 0);
    if(FAILED(hr))
    {
        video_set_hresult_error("IDXGISwapChain::Present", hr);
        return 0;
    }

    g_last_client = client;
    g_last_dst = dst;
    g_have_last_geometry = 1;
    g_need_margin_clear = 0;
    g_force_full_clear = 0;
    return 1;
}

#else
static void d3d_destroy(void) { }
static int d3d_present_frame(void) { return 0; }
#endif /* PCFX_WIN32_HAVE_D3D11 */

static void draw_frame_gdi(HDC hdc, int force_margin_clear)
{
    RECT client;
    if(!g_hwnd || !hdc)
        return;

    GetClientRect(g_hwnd, &client);
    if(client.right <= client.left || client.bottom <= client.top)
        return;

    const int client_w = client.right - client.left;
    const int client_h = client.bottom - client.top;
    RECT dst = video_calc_dest_rect(&client);
    int dst_w = dst.right - dst.left;
    int dst_h = dst.bottom - dst.top;
    if(dst_w <= 0 || dst_h <= 0)
        return;

    int geometry_changed = !g_have_last_geometry ||
                           !rect_same(&client, &g_last_client) ||
                           !rect_same(&dst, &g_last_dst);

    HDC target = ensure_backbuffer(hdc, client_w, client_h);
    if(!target)
    {
        target = hdc;
        if(force_margin_clear || g_force_full_clear)
            clear_full_client(target, &client);
        else if(g_need_margin_clear || geometry_changed)
            clear_margins(target, &client, &dst);
    }
    else
    {
        RECT offscreen = {0, 0, client_w, client_h};
        clear_full_client(target, &offscreen);
    }

    if(g_smooth)
        SetStretchBltMode(target, HALFTONE);
    else
        SetStretchBltMode(target, COLORONCOLOR);
    SetBrushOrgEx(target, 0, 0, NULL);

    StretchDIBits(target,
                  dst.left, dst.top, dst_w, dst_h,
                  0, 0, current_source_width(), current_source_height(),
                  g_framebuffer,
                  (BITMAPINFO*)&g_bmi,
                  DIB_RGB_COLORS,
                  SRCCOPY);

    if(target != hdc)
        BitBlt(hdc, 0, 0, client_w, client_h, target, 0, 0, SRCCOPY);

    g_last_client = client;
    g_last_dst = dst;
    g_have_last_geometry = 1;
    g_need_margin_clear = 0;
    g_force_full_clear = 0;
    (void)force_margin_clear;
    (void)geometry_changed;
}

static void draw_frame(HDC hdc, int force_margin_clear)
{
#if defined(PCFX_WIN32_HAVE_D3D11)
    if(g_video_backend == PCFX_WIN32_VIDEO_D3D11)
    {
        if(d3d_present_frame())
            return;
        /* Keep the selected backend as D3D11, but provide a no-video-loss
         * fallback for systems without D3D11/compiler support. */
    }
#endif
    draw_frame_gdi(hdc, force_margin_clear);
}

void PCFX_Win32_SetWindow(HWND hwnd)
{
    if(g_hwnd != hwnd)
    {
        d3d_destroy();
#if defined(PCFX_WIN32_HAVE_D3D11)
        g_d3d_failed = 0;
#endif
    }
    g_hwnd = hwnd;
    mark_geometry_dirty();
}

void PCFX_Win32_SetScaleMode(int scale, int mode, int smooth)
{
    if(scale < 1) scale = 1;
    if(scale > 6) scale = 6;
    if(mode < PCFX_WIN32_SCALE_FIXED || mode > PCFX_WIN32_SCALE_INTEGER)
        mode = PCFX_WIN32_SCALE_FIXED;
    g_window_scale = scale;
    g_scale_mode = mode;
    g_smooth = smooth ? 1 : 0;
    request_paint(1);
}

void PCFX_Win32_SetVideoBackend(int backend)
{
#if defined(PCFX_WIN32_HAVE_D3D11)
    if(backend != PCFX_WIN32_VIDEO_GDI && backend != PCFX_WIN32_VIDEO_D3D11)
        backend = PCFX_WIN32_VIDEO_D3D11;
#else
    backend = PCFX_WIN32_VIDEO_GDI;
#endif
    if(g_video_backend == backend)
        return;
    g_video_backend = backend;
    mark_geometry_dirty();
#if defined(PCFX_WIN32_HAVE_D3D11)
    if(backend == PCFX_WIN32_VIDEO_GDI)
        d3d_destroy();
    else
        g_d3d_failed = 0;
#else
    d3d_destroy();
#endif
    request_paint(1);
}

int PCFX_Win32_GetVideoBackend(void)
{
    return g_video_backend;
}

const char* PCFX_Win32_VideoBackendName(int backend)
{
    switch(backend)
    {
        case PCFX_WIN32_VIDEO_GDI: return "GDI";
#if defined(PCFX_WIN32_HAVE_D3D11)
        case PCFX_WIN32_VIDEO_D3D11: return "D3D11";
#endif
        default: return "GDI";
    }
}

const char* PCFX_Win32_VideoLastError(void)
{
    return g_video_last_error[0] ? g_video_last_error : "No video backend error.";
}

void PCFX_Win32_SetPresentationFullscreen(int fullscreen)
{
    int new_fullscreen = fullscreen ? 1 : 0;
    if(g_presentation_fullscreen != new_fullscreen)
    {
        g_presentation_fullscreen = new_fullscreen;
#if defined(PCFX_WIN32_HAVE_D3D11)
        g_d3d_exclusive_failed = 0;
        if(g_d3d_ready)
            d3d_apply_fullscreen_request();
#endif
        mark_geometry_dirty();
    }
    request_paint(1);
}

void PCFX_Win32_SetFullscreenMode(int mode)
{
    if(mode != PCFX_WIN32_FULLSCREEN_EXCLUSIVE && mode != PCFX_WIN32_FULLSCREEN_BORDERLESS)
        mode = PCFX_WIN32_FULLSCREEN_EXCLUSIVE;
    if(g_fullscreen_mode == mode)
        return;
    g_fullscreen_mode = mode;
#if defined(PCFX_WIN32_HAVE_D3D11)
    g_d3d_exclusive_failed = 0;
    if(g_d3d_ready)
        d3d_apply_fullscreen_request();
#endif
    mark_geometry_dirty();
    request_paint(1);
}

int PCFX_Win32_GetFullscreenMode(void)
{
    return g_fullscreen_mode;
}

void PCFX_Win32_SetClientSize(int width, int height)
{
    (void)width;
    (void)height;
    request_paint(1);
}

void PCFX_Win32_GetScaleMode(int* scale, int* mode, int* smooth)
{
    if(scale) *scale = g_window_scale;
    if(mode) *mode = g_scale_mode;
    if(smooth) *smooth = g_smooth;
}

void PCFX_Win32_SetFullWidth(int fxga_active)
{
    g_full_width = fxga_active ? 1 : 0;
    (void)g_full_width;
}

void PCFX_Win32_SetDisplayWidth(int width)
{
    if(width < 1) width = 256;
    if(width > PCFX_WIN32_FB_WIDTH) width = PCFX_WIN32_FB_WIDTH;
    if(g_active_w != width)
    {
        g_active_w = width;
        request_paint(1);
    }
}

int PCFX_Win32_GetDisplayWidth(void) { return presentation_source_width(); }
int PCFX_Win32_GetDisplayHeight(void) { return presentation_source_height(); }
int PCFX_Win32_GetBytesPerPixel(void) { return (int)sizeof(MDFN_Pixel); }

const char* PCFX_Win32_GetPixelFormatName(void)
{
#if defined(WANT_16BPP)
    return "RGB565 16bpp";
#elif defined(WANT_32BPP)
    return "XRGB8888 32bpp";
#endif
}

void Clear_Video(void)
{
    memset(g_framebuffer, 0, sizeof(g_framebuffer));
    request_paint(1);
}

void Init_Video(void)
{
    video_setup_bmi();
    Clear_Video();
    Set_Video_InGame();
}

void Set_Video_Menu(void) { }
void Set_Video_InGame(void) { internal_pix = (void*)g_framebuffer; }
void Video_Close(void) { g_hwnd = NULL; destroy_backbuffer(); d3d_destroy(); }
void Update_Video_Menu(void) { request_paint(1); }

void Update_Video_Ingame(void)
{
    if(!g_hwnd)
        return;
#if defined(PCFX_WIN32_HAVE_D3D11)
    if(g_video_backend == PCFX_WIN32_VIDEO_D3D11 && d3d_present_frame())
        return;
#endif
    HDC hdc = GetDC(g_hwnd);
    if(hdc)
    {
        draw_frame_gdi(hdc, 0);
        ReleaseDC(g_hwnd, hdc);
    }
}

void PCFX_Win32_Paint(HDC hdc, const RECT* paint_rect)
{
    (void)paint_rect;
    draw_frame(hdc, 1);
}

void PCFX_Win32_ForceRedraw(void)
{
    if(!g_hwnd)
        return;
#if defined(PCFX_WIN32_HAVE_D3D11)
    if(g_video_backend == PCFX_WIN32_VIDEO_D3D11 && d3d_present_frame())
        return;
#endif
    HDC hdc = GetDC(g_hwnd);
    if(hdc)
    {
        draw_frame_gdi(hdc, 1);
        ReleaseDC(g_hwnd, hdc);
    }
}
