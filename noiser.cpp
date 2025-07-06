#pragma warning (disable: 6387)

#include "resource.h"
#include <combaseapi.h>
#include <ctime>
#include <d3d11.h>
#include <d3dcommon.h>
#include <dxgiformat.h>
#include <fileapi.h>
#include <libloaderapi.h>
#include <objbase.h>
#include <OCIdl.h>
#include <processthreadsapi.h>
#include <random>
#include <string>
#include <vector>
#include <wincodec.h>
#include <Windows.h>
#include <WTypesbase.h>
#include <cstring>
#include <cstring>
#include <cmath>

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

using namespace std;

typedef struct tagNoiseRect {
	UINT pos[2];
	UINT size;
	UINT color[4];
} NoiseRect, *pNoiseRect;

typedef struct tagParams {
	UINT width;
	UINT height;
	UINT numRects;
	UINT seed;
} Params, *pParams;

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;

void InitD3D() {
	HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		0, nullptr, 0, D3D11_SDK_VERSION, &g_device, nullptr, &g_context);
	if (FAILED(hr)) {
		TerminateProcess(GetCurrentProcess(), -1);
		return;
	}
}

ID3D11ComputeShader* LoadComputeShaderFromResource() {
	HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCE(IDR_SHADER1), MAKEINTRESOURCE(10));
	if (!hRes) {
		TerminateProcess(GetCurrentProcess(), -1);
		return nullptr;
	}
	HGLOBAL hData = LoadResource(nullptr, hRes);
	if (!hData) {
		TerminateProcess(GetCurrentProcess(), -1);
		return nullptr;
	}
	void* pData = LockResource(hData);
	DWORD size = SizeofResource(nullptr, hRes);
	if (!pData || size == 0) {
		TerminateProcess(GetCurrentProcess(), -1);
		return nullptr;
	}
	ID3D11ComputeShader* shader = nullptr;
	HRESULT hr = g_device->CreateComputeShader(pData, size, nullptr, &shader);
	if (FAILED(hr)) {
		TerminateProcess(GetCurrentProcess(), -1);
		return nullptr;
	}
	return shader;
}

IWICBitmap* LoadImageWIC(LPCWSTR path, UINT& w, UINT& h) {
	IWICImagingFactory* factory = nullptr;
	HRESULT res = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
	if (FAILED(res)) {
		TerminateProcess(GetCurrentProcess(), -1);
		return nullptr;
	}
	wchar_t full[MAX_PATH];
	GetFullPathNameW(path, MAX_PATH, full, nullptr);
	IWICBitmapDecoder* decoder = nullptr;
	factory->CreateDecoderFromFilename(full, nullptr, GENERIC_READ,
		WICDecodeMetadataCacheOnDemand, &decoder);
	GUID containerFormat = {};
	if (FAILED(decoder->GetContainerFormat(&containerFormat)) || containerFormat != GUID_ContainerFormatPng) {
		decoder->Release(); factory->Release();
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
		TerminateProcess(GetCurrentProcess(), -1);
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
	WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA;
	frame->SetPixelFormat(&fmt);
	frame->WritePixels(h, w * 4, static_cast<UINT>(data.size()), const_cast<BYTE*>(data.data()));
	frame->Commit(); enc->Commit();
	props->Release(); frame->Release(); stream->Release(); enc->Release(); factory->Release();
}

wstring MakeNoisedFilename(const wstring& inputPath) {
	wstring output = inputPath;
	size_t dot = output.find_last_of(L'.');
	if (dot == wstring::npos) return output + L"_noised";
	return output.substr(0, dot) + L"_noised" + output.substr(dot);
}

int wmain(int argc, wchar_t* argv[]) {
#ifdef _M_TEST
	if (argc < 2 || argc > 4) return 1;
#else
	if (argc < 2 || argc > 3) return 1;
#endif

#ifdef _M_TEST
	bool testMode = (argc == 3 && wcscmp(argv[2], L"-test") == 0);
#endif
	wchar_t full[MAX_PATH]; GetFullPathNameW(argv[1], MAX_PATH, full, nullptr);
	UINT width, height;
	HRESULT res = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(res)) {
		return 1;
	}
	InitD3D();
	IWICBitmap* bmp = LoadImageWIC(full, width, height);
	vector<BYTE> pixels(width * height * 4);
	WICRect rect = { 0, 0, static_cast<INT>(width), static_cast<INT>(height) };
	bmp->CopyPixels(&rect, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
	bmp->Release();

	ID3D11ShaderResourceView* inputTexSRV = nullptr;
	{
		D3D11_TEXTURE2D_DESC tdSRV{};
		tdSRV.Width = width;
		tdSRV.Height = height;
		tdSRV.MipLevels = 1;
		tdSRV.ArraySize = 1;
		tdSRV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		tdSRV.SampleDesc.Count = 1;
		tdSRV.Usage = D3D11_USAGE_DEFAULT;
		tdSRV.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initSRV{ pixels.data(), width * 4, 0 };
		ID3D11Texture2D* texCopy = nullptr;
		g_device->CreateTexture2D(&tdSRV, &initSRV, &texCopy);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Format = tdSRV.Format;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;
		g_device->CreateShaderResourceView(texCopy, &srvDesc, &inputTexSRV);
		texCopy->Release();
	}

#ifdef _M_TEST
	if (testMode) {
		// ノイズなしテスト：そのまま出力して終了
		wstring outPath = MakeNoisedFilename(full);  // suffix は "_noised" のまま
		SaveImageWIC(pixels, width, height, outPath.c_str());
		CoUninitialize();
		return 0;
	}
#endif

	// ノイズ矩形生成
	const UINT numRects = width * height / 10000;
	mt19937 rng((UINT)time(nullptr));
	uniform_int_distribution<UINT> drgb(0, 255), da(10, 40), pnoise(0, 10000);
	vector<NoiseRect> rects(numRects);
	for (ULONG_PTR y = 0; y < height; ++y) {
		for (ULONG_PTR x = 0; x < width; ++x) {
			// 0.01% の確率でノイズを付与（ごま塩レベル）
			if (pnoise(rng) == 0) {
				size_t idx = (static_cast<size_t>(y) * width + x) * 4;
				// BGRA のそれぞれを lerp で軽くブレンド
				float  a = da(rng) / 255.0f;
				BYTE nr = static_cast<BYTE>(lerp(pixels[idx + 2], drgb(rng), a));
				BYTE ng = static_cast<BYTE>(lerp(pixels[idx + 1], drgb(rng), a));
				BYTE nb = static_cast<BYTE>(lerp(pixels[idx + 0], drgb(rng), a));
				pixels[idx + 2] = nr;
				pixels[idx + 1] = ng;
				pixels[idx + 0] = nb;
			}
		}
	}

	// StructuredBuffer
	D3D11_BUFFER_DESC bd{};
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.ByteWidth = sizeof(NoiseRect) * numRects;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.StructureByteStride = sizeof(NoiseRect);
	bd.Usage = D3D11_USAGE_DEFAULT;
	D3D11_SUBRESOURCE_DATA init{ rects.data(),0,0 };
	ID3D11Buffer* sb = nullptr;
	g_device->CreateBuffer(&bd, &init, &sb);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
	srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvd.Format = DXGI_FORMAT_UNKNOWN;
	srvd.Buffer.NumElements = numRects;
	ID3D11ShaderResourceView* rectSRV = nullptr;
	g_device->CreateShaderResourceView(sb, &srvd, &rectSRV);

	// 出力テクスチャ
	D3D11_TEXTURE2D_DESC td{};
	td.Width = width; td.Height = height; td.MipLevels = 1; td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	td.SampleDesc.Count = 1;
	D3D11_SUBRESOURCE_DATA tdInit{};
	tdInit.pSysMem = pixels.data();
	tdInit.SysMemPitch = width * 4;
	ID3D11Texture2D* tex = nullptr;
	g_device->CreateTexture2D(&td, &tdInit, &tex);
	ID3D11UnorderedAccessView* uav = nullptr;
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavd{};
	uavd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	uavd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	HRESULT hr = g_device->CreateUnorderedAccessView(tex, &uavd, &uav);
	if (FAILED(hr)) {
		TerminateProcess(GetCurrentProcess(), -1);
		return 1;
	}

	// 定数バッファ
	Params p{ width,height,numRects,(UINT)rng() };
	D3D11_BUFFER_DESC cbd{ sizeof(Params),D3D11_USAGE_DEFAULT,D3D11_BIND_CONSTANT_BUFFER };
	D3D11_SUBRESOURCE_DATA cbdInit{ &p,0,0 };
	ID3D11Buffer* pcb = nullptr;
	g_device->CreateBuffer(&cbd, &cbdInit, &pcb);

	auto shader = LoadComputeShaderFromResource();

	ID3D11Texture2D* inputTex = nullptr;
	{
		D3D11_TEXTURE2D_DESC tdCopy = td;              // td はもともとのテクスチャ定義
		tdCopy.BindFlags = D3D11_BIND_SHADER_RESOURCE; // SRV 用バインドを追加
		tdCopy.Usage = D3D11_USAGE_DEFAULT;
		ID3D11Texture2D* texCopy = nullptr;
		g_device->CreateTexture2D(&tdCopy, &tdInit, &texCopy);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvd2{};
		srvd2.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvd2.Format = tdCopy.Format;
		srvd2.Texture2D.MipLevels = 1;
		g_device->CreateShaderResourceView(texCopy, &srvd2, &inputTexSRV);
		texCopy->Release();
	}

	ID3D11ShaderResourceView* srvs[2] = { rectSRV, inputTexSRV };

	g_context->CSSetShader(shader, nullptr, 0);
	g_context->CSSetShaderResources(0, 1, &rectSRV);
	g_context->CSSetShaderResources(1, 1, &inputTexSRV);
	g_context->CSSetShaderResources(0, 2, srvs);
	g_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	g_context->CSSetConstantBuffers(0, 1, &pcb);
	g_context->Dispatch((width + 15) / 16, (height + 15) / 16, 1);
	g_context->Flush();

	// 結果取得
	D3D11_TEXTURE2D_DESC rd = td;
	rd.Usage = D3D11_USAGE_STAGING; rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; rd.BindFlags = 0;
	ID3D11Texture2D* rTex = nullptr;
	g_device->CreateTexture2D(&rd, nullptr, &rTex);
	g_context->CopyResource(rTex, tex);
	D3D11_MAPPED_SUBRESOURCE mr;
	g_context->Map(rTex, 0, D3D11_MAP_READ, 0, &mr);
	vector<BYTE> outpx(width * height * 4);
	const size_t imageSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
	memcpy(outpx.data(), mr.pData, imageSize);
	g_context->Unmap(rTex, 0);

	wstring noisedPath = MakeNoisedFilename(full);
	SaveImageWIC(outpx, width, height, noisedPath.c_str());

	// 解放
	if (inputTexSRV) inputTexSRV->Release();
	if (rectSRV) rectSRV->Release();
	if (sb) sb->Release();
	if (pcb) pcb->Release();
	if (shader) shader->Release();
	if (uav) uav->Release();
	if (tex) tex->Release();
	if (rTex) rTex->Release();
	if (g_context) g_context->Release();
	if (g_device) g_device->Release();
	CoUninitialize();
	return 0;
}