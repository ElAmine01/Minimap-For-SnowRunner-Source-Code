#define NOMINMAX
#include "TextureLoader.h"
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

#include <vector>

namespace snowmap::render {

void LoadedTexture::Release() {
    if (srv) { srv->Release(); srv = nullptr; }
    width = height = 0;
}


LoadedTexture LoadImageFromDisk(ID3D11Device* dev, const std::string& path) {
    LoadedTexture out;
    if (!dev || path.empty()) return out;

    // This function may run on an async thread — ensure COM is ready.
    const HRESULT hrCom   = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool    uninitCom = SUCCEEDED(hrCom); // balance S_OK and S_FALSE

    int n = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
    std::wstring wp(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), wp.data(), n);

    // ── Decode PNG → RGBA8 via WIC ────────────────────────────────────────────
    HRESULT hr = S_OK;
    IWICImagingFactory*    wic  = nullptr;
    IWICBitmapDecoder*     dec  = nullptr;
    IWICBitmapFrameDecode* frm  = nullptr;
    IWICFormatConverter*   conv = nullptr;

    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                             CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (SUCCEEDED(hr)) hr = wic->CreateDecoderFromFilename(wp.c_str(), nullptr,
                                                           GENERIC_READ,
                                                           WICDecodeMetadataCacheOnLoad,
                                                           &dec);
    if (SUCCEEDED(hr)) hr = dec->GetFrame(0, &frm);
    if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr)) hr = conv->Initialize(frm, GUID_WICPixelFormat32bppRGBA,
                                             WICBitmapDitherTypeNone, nullptr,
                                             0.0, WICBitmapPaletteTypeCustom);

    UINT w = 0, h = 0;
    std::vector<uint8_t> px;
    if (SUCCEEDED(hr)) {
        conv->GetSize(&w, &h);
        const UINT stride = w * 4;
        px.resize(static_cast<size_t>(stride) * h);
        hr = conv->CopyPixels(nullptr, stride, static_cast<UINT>(px.size()), px.data());
    }

    if (conv) conv->Release();
    if (frm)  frm->Release();
    if (dec)  dec->Release();
    if (wic)  wic->Release();
    if (uninitCom) CoUninitialize();

    if (FAILED(hr) || w == 0 || h == 0) return out;

    const UINT stride = w * 4;

    // ── Vertical flip: North moves to row 0 in memory ────────────────────────
    {
        std::vector<uint8_t> row(stride);
        for (UINT y = 0; y < h / 2; ++y) {
            uint8_t* top = px.data() + y * stride;
            uint8_t* bot = px.data() + (h - 1 - y) * stride;
            memcpy(row.data(), top, stride);
            memcpy(top, bot, stride);
            memcpy(bot, row.data(), stride);
        }
    }

    // ── Upload to GPU ─────────────────────────────────────────────────────────
    D3D11_TEXTURE2D_DESC td{};
    td.Width            = w;
    td.Height           = h;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_IMMUTABLE;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem     = px.data();
    init.SysMemPitch = stride;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, &init, &tex))) return out;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels       = 1;
    dev->CreateShaderResourceView(tex, &sd, &out.srv);
    tex->Release();

    if (out.srv) {
        out.width  = static_cast<int>(w);
        out.height = static_cast<int>(h);
    }
    return out;
}

} // namespace snowmap::render
