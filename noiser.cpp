#pragma warning (disable: 6387)

#define M_NOISER_VER L"v1.2.1"
#include "resource.h"
#include <combaseapi.h>
#include <ctime>
#include <d3d11.h>
#include <d3dcommon.h>
#include <dxgiformat.h>
#ifndef _DEBUG
#include <debugapi.h>
#endif
#include <fileapi.h>
#include <libloaderapi.h>
#include <objbase.h>
#include <OCIdl.h>
#include <processthreadsapi.h>
#include <random>
#ifdef _M_TEST
#include <string>
#endif
#include <vector>
#include <wincodec.h>
#include <Windows.h>
#include <WTypesbase.h>
#include <Shlwapi.h>
#include <cstdio>
#include <cstring>

// このIncludeには推移的に必要なヘッダーも含めて全て含まれています。

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "Shlwapi.lib")

using namespace std;

typedef struct tagParams {
	UINT width;
	UINT height;
	UINT seed;
	UINT _pad; // メモリサイズ調整のために必須、実際は不使用
} Params, *pParams;

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;

void InitD3D11() {
	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		createDeviceFlags, nullptr, 0, D3D11_SDK_VERSION, &g_device, nullptr, &g_context);
	if (FAILED(hr)) {
		wprintf_s(L"Failed to D3D11CreateDevice\nat InitD3D11\n");
		TerminateProcess(GetCurrentProcess(), 1);
		return;
	}
}

ID3D11ComputeShader* LoadComputeShaderFromResource() {
	HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCE(IDR_SHADER1), MAKEINTRESOURCE(10));
	if (!hRes) {
		wprintf_s(L"Failed to FindResource\nat LoadComputeShderFromResource\n");
		TerminateProcess(GetCurrentProcess(), 1);
		return nullptr;
	}
	HGLOBAL hData = LoadResource(nullptr, hRes);
	if (!hData) {
		wprintf_s(L"Failed to LoadResource\nat LoadComputeShaderFromResource\n");
		TerminateProcess(GetCurrentProcess(), 1);
		return nullptr;
	}
	void* pData = LockResource(hData);
	DWORD size = SizeofResource(nullptr, hRes);
	if (!pData || size == 0) {
		wprintf_s(L"Failed to LockResource or SizeofResource or both\nat LoadComputeShaderFromResource\n");
		TerminateProcess(GetCurrentProcess(), 1);
		return nullptr;
	}
	ID3D11ComputeShader* shader = nullptr;
	HRESULT hr = g_device->CreateComputeShader(pData, size, nullptr, &shader);
	if (FAILED(hr)) {
		wprintf_s(L"Failed to CreateComputeShader\nat LoadComputeShaderFromResource\n");
		TerminateProcess(GetCurrentProcess(), 1);
		return nullptr;
	}
	return shader;
}

IWICBitmap* LoadImageWIC(LPCWSTR path, UINT& w, UINT& h) {
	IWICImagingFactory* factory = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
	if (FAILED(hr)) {
		wprintf_s(L"Failed to CoCreateInstance\nat LoadImageWIC\n");
		TerminateProcess(GetCurrentProcess(), 1);
		return nullptr;
	}
	wchar_t full[MAX_PATH];
	GetFullPathNameW(path, MAX_PATH, full, nullptr);
	IWICBitmapDecoder* decoder = nullptr;
	hr = factory->CreateDecoderFromFilename(full, nullptr, GENERIC_READ,
		WICDecodeMetadataCacheOnDemand, &decoder);
	if (FAILED(hr)) {
		wprintf_s(L"Failed to CreateDecoderFromFilename\nat LoadImageWIC\nThe reason this happened appears to be due to an invalid file name.\n");
		if (factory) factory->Release();
		TerminateProcess(GetCurrentProcess(), 1);
		return nullptr;
	}
	GUID containerFormat = {};
	if (FAILED(decoder->GetContainerFormat(&containerFormat)) || containerFormat != GUID_ContainerFormatPng) {
		wprintf_s(L"Failed to GetContainerFormat\nat LoadImageWIC\n");
		if (decoder) decoder->Release(); if (factory) factory->Release();
		TerminateProcess(GetCurrentProcess(), 1);
		return nullptr;
	}
	IWICBitmapFrameDecode* frame = nullptr;
	decoder->GetFrame(0, &frame);
	frame->GetSize(&w, &h);
	IWICFormatConverter* conv = nullptr;
	factory->CreateFormatConverter(&conv);
	conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
		WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
	IWICBitmap* bmp = nullptr;
	factory->CreateBitmapFromSource(conv, WICBitmapCacheOnLoad, &bmp);
	conv->Release(); frame->Release(); decoder->Release(); factory->Release();
	return bmp;
}

void SaveImageWIC(const vector<BYTE>& data, UINT w, UINT h, LPCWSTR path) {
	IWICImagingFactory* factory = nullptr;
	HRESULT res = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
	if (FAILED(res)) {
		wprintf_s(L"Failed to CoCreateInstance\nat SaveImageWIC\n");
		TerminateProcess(GetCurrentProcess(), 1);
		return;
	}
	IWICBitmapEncoder* enc = nullptr;
	factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
	IWICStream* stream = nullptr;
	factory->CreateStream(&stream);
	stream->InitializeFromFilename(path, GENERIC_WRITE);
	enc->Initialize(stream, WICBitmapEncoderNoCache);
	IWICBitmapFrameEncode* frame = nullptr; IPropertyBag2* props = nullptr;
	enc->CreateNewFrame(&frame, &props);
	frame->Initialize(props);
	frame->SetSize(w, h);
	WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
	frame->SetPixelFormat(&fmt);
	frame->WritePixels(h, w * 4, static_cast<UINT>(data.size()), const_cast<BYTE*>(data.data()));
	frame->Commit(); enc->Commit();
	if (props) props->Release();
	if (frame) frame->Release();
	if (stream) stream->Release();
	if (enc) enc->Release();
	if (factory) factory->Release();
}

#ifdef _M_TEST
wstring MakeVHSFilename(const wstring& inputPath) {
	wstring output = inputPath;
	size_t dot = output.find_last_of(L'.');
	if (dot == wstring::npos) return output + L"_vhs";
	return output.substr(0, dot) + L"_vhs" + output.substr(dot);
}
#endif

ID3D11ShaderResourceView* CreateTextureSRV(const vector<BYTE>& pixels, UINT w, UINT h) {
	D3D11_TEXTURE2D_DESC td{};
	td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA init{ pixels.data(), w * 4, 0 };
	ID3D11Texture2D* tex = nullptr;
	HRESULT hr = g_device->CreateTexture2D(&td, &init, &tex);
	if (FAILED(hr)) {
		wprintf_s(L"Failed to create texture\nat CreateTextureSRV\n");
		return nullptr;
	}
	D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
	srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvd.Format = td.Format;
	srvd.Texture2D.MipLevels = 1;
	ID3D11ShaderResourceView* srv = nullptr;
	hr = g_device->CreateShaderResourceView(tex, &srvd, &srv);
	if (tex) tex->Release();
	if (FAILED(hr)) {
		wprintf_s(L"Failed to create shader resource view\nat CreateTextureSRV\n");
		return nullptr;
	}
	return srv;
}

ID3D11UnorderedAccessView* CreateOutputUAV(const vector<BYTE>& pixels, UINT w, UINT h) {
	D3D11_TEXTURE2D_DESC td{};
	td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA init{ pixels.data(), w * 4, 0 };
	ID3D11Texture2D* tex = nullptr;
	HRESULT hr = g_device->CreateTexture2D(&td, &init, &tex);
	if (FAILED(hr)) {
		wprintf_s(L"Failed to create texture\nat CreateOutputUAV\n");
		return nullptr;
	}
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavd{};
	uavd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	uavd.Format = td.Format;
	ID3D11UnorderedAccessView* uav = nullptr;
	hr = g_device->CreateUnorderedAccessView(tex, &uavd, &uav);
	if (tex) tex->Release();
	if (FAILED(hr)) {
		wprintf_s(L"Failed to create unordered access view\nat CreateOutputUAV\n");
		return nullptr;
	}
	return uav;
}

void ReadbackAndSave(ID3D11UnorderedAccessView* uav, UINT w, UINT h, LPCWSTR inPath) {
	ID3D11Resource* res = nullptr;
	uav->GetResource(&res);
	if (!res) return;

	ID3D11Texture2D* tex = nullptr;
	HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
	if (FAILED(hr) || !tex) {
		wprintf_s(L"Failed to query interface\nat ReadbackAndSave\n");
		res->Release();
		return;
	}

	D3D11_TEXTURE2D_DESC sd;
	tex->GetDesc(&sd);
	sd.Usage = D3D11_USAGE_STAGING;
	sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	sd.BindFlags = 0;
	sd.MiscFlags = 0;

	ID3D11Texture2D* staging = nullptr;
	hr = g_device->CreateTexture2D(&sd, nullptr, &staging);
	if (FAILED(hr) || !staging) {
		wprintf_s(L"Failed to create texture\nat ReadbackAndSave\n");
		tex->Release();
		res->Release();
		return;
	}

	g_context->CopyResource(staging, tex);

	D3D11_MAPPED_SUBRESOURCE mr = {};
	hr = g_context->Map(staging, 0, D3D11_MAP_READ, 0, &mr);
	if (FAILED(hr)) {
		wprintf_s(L"Failed to read map\nat ReadbackAndSave\n");
		staging->Release();
		tex->Release();
		res->Release();
		return;
	}

	std::vector<BYTE> outp(w * h * 4);
	BYTE* dst = outp.data();
	BYTE* src = reinterpret_cast<BYTE*>(mr.pData);
	for (UINT y = 0; y < h; ++y) {
		memcpy(dst + y * w * 4, src + y * mr.RowPitch, static_cast<size_t>(w) * 4);
	}
	g_context->Unmap(staging, 0);

	WCHAR out[MAX_PATH];
	wcscpy_s(out, inPath);
	PathRemoveExtensionW(out);
	wcscat_s(out, L"_vhs.png");
	SaveImageWIC(outp, w, h, out);

	staging->Release();
	tex->Release();
	res->Release();
}

int wmain(int argc, wchar_t* argv[]) {
#ifndef _DEBUG
	if (IsDebuggerPresent()) return 0;
#endif
#ifdef _M_TEST
	if (argc < 2 || argc > 4) {
		wprintf_s(L"noiser %ls (Debug build)\nInvalid Parameter(s).\n", M_NOISER_VER);
		return 1;
	}
#else
	if (argc < 2 || argc > 3) {
		wprintf_s(L"noiser %ls\nInvalid Parameter(s).\n", M_NOISER_VER);
		return 1;
	}
#endif

#ifdef _M_TEST
	bool testMode = (argc == 3 && wcscmp(argv[2], L"-test") == 0);
#endif
	wchar_t full[MAX_PATH];
	DWORD dw = GetFullPathNameW(argv[1], MAX_PATH, full, nullptr);
	if (dw == 0) {
		wprintf_s(L"Failed to GetFullPathNameW\nat wmain\n");
		return 1;
	}
	UINT width, height;
	HRESULT res = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(res)) {
		wprintf_s(L"Failed to CoInitializeEx\nat wmain\n");
		return 1;
	}
	InitD3D11();
	IWICBitmap* bmp = LoadImageWIC(full, width, height);
	vector<BYTE> pixels(width * height * 4);
	WICRect rect = { 0, 0, static_cast<INT>(width), static_cast<INT>(height) };
	bmp->CopyPixels(&rect, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
	bmp->Release();

	auto srv = CreateTextureSRV(pixels, width, height);
	if (!srv) {
		wprintf_s(L"Failed to CreateTextureSRV\nat wmain\n");
		return 1;
	}

	auto uav = CreateOutputUAV(pixels, width, height);
	if (!uav) {
		wprintf_s(L"Failed to CreateOutputUAV\nat wmain\n");
		return 1;
	}


#ifdef _M_TEST
	if (testMode) {
		// ノイズなしテスト：そのまま出力して終了
		wstring outPath = MakeVHSFilename(full);  // suffix は "_vhs" のまま
		SaveImageWIC(pixels, width, height, outPath.c_str());
		CoUninitialize();
		wprintf_s(L"Test Generated.\n");
		return 0;
	}
#endif

	mt19937 rng((UINT)time(nullptr));
	Params p = { width, height, (UINT)rng() };
	D3D11_BUFFER_DESC cbd{};
	cbd.ByteWidth = sizeof(Params);
	cbd.Usage = D3D11_USAGE_DEFAULT;
	cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	D3D11_SUBRESOURCE_DATA init{ &p, 0, 0 };
	ID3D11Buffer* pcb;
	g_device->CreateBuffer(&cbd, &init, &pcb);

	auto shader = LoadComputeShaderFromResource();
	g_context->CSSetShader(shader, nullptr, 0);
	g_context->CSSetConstantBuffers(0, 1, &pcb);
	g_context->CSSetShaderResources(1, 1, &srv);
	g_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	g_context->Dispatch((width + 15) / 16, (height + 15) / 16, 1);
	g_context->Flush();

	ReadbackAndSave(uav, width, height, argv[1]);

	if (shader) shader->Release();
	if (pcb) pcb->Release();
	if (srv) srv->Release();
	if (uav) uav->Release();
	if (g_context) g_context->Release();
	if (g_device) g_device->Release();

	return 0;
}