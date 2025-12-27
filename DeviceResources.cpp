//
// DeviceResources.cpp - Direct3D 12 デバイスとスワップチェーンのラッパー
//

#include "pch.h"
#include "DeviceResources.h"

using namespace DirectX;
using namespace DX;

using Microsoft::WRL::ComPtr;

#ifdef __clang__
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif

#pragma warning(disable : 4061)

namespace
{
	inline DXGI_FORMAT NoSRGB(DXGI_FORMAT fmt) noexcept
	{
		switch (fmt)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:   return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:   return DXGI_FORMAT_B8G8R8X8_UNORM;
		default:                                return fmt;
		}
	}

	inline long ComputeIntersectionArea(
		long ax1, long ay1, long ax2, long ay2,
		long bx1, long by1, long bx2, long by2) noexcept
	{
		return std::max(0l, std::min(ax2, bx2) - std::max(ax1, bx1)) * std::max(0l, std::min(ay2, by2) - std::max(ay1, by1));
	}
}

// DeviceResources のコンストラクタ。
DeviceResources::DeviceResources(
	DXGI_FORMAT backBufferFormat,
	DXGI_FORMAT depthBufferFormat,
	UINT backBufferCount,
	D3D_FEATURE_LEVEL minFeatureLevel,
	unsigned int flags) noexcept(false) :
	m_backBufferIndex(0),
	m_fenceValues{},
	m_rtvDescriptorSize(0),
	m_screenViewport{},
	m_scissorRect{},
	m_backBufferFormat(backBufferFormat),
	m_depthBufferFormat(depthBufferFormat),
	m_backBufferCount(backBufferCount),
	m_d3dMinFeatureLevel(minFeatureLevel),
	m_window(nullptr),
	m_d3dFeatureLevel(D3D_FEATURE_LEVEL_11_0),
	m_dxgiFactoryFlags(0),
	m_outputSize{ 0, 0, 1, 1 },
	m_colorSpace(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709),
	m_options(flags),
	m_deviceNotify(nullptr)
{
	if (backBufferCount < 2 || backBufferCount > MAX_BACK_BUFFER_COUNT)
	{
		throw std::out_of_range("invalid backBufferCount");
	}

	if (minFeatureLevel < D3D_FEATURE_LEVEL_11_0)
	{
		throw std::out_of_range("minFeatureLevel too low");
	}
}

// DeviceResources のデストラクタ。
DeviceResources::~DeviceResources()
{
	// 破棄しようとしているリソースを GPU が参照していないことを保証します。
	WaitForGpu();
}

// Direct3D デバイスを構成し、デバイスとデバイスコンテキストへのハンドルを保持します。
void DeviceResources::CreateDeviceResources()
{
#if defined(_DEBUG)
	// デバッグレイヤーを有効にします（Graphics Tools の "optional feature" が必要）。
	//
	// 注意: デバイス作成後にデバッグレイヤーを有効にすると、アクティブなデバイスは無効になります。
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf()))))
		{
			debugController->EnableDebugLayer();
		}
		else
		{
			OutputDebugStringA("WARNING: Direct3D Debug Device is not available\n");
		}

#ifndef __MINGW32__
		ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgiInfoQueue.GetAddressOf()))))
		{
			m_dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;

			dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
			dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);

			DXGI_INFO_QUEUE_MESSAGE_ID hide[] =
			{
				80 /* IDXGISwapChain::GetContainingOutput: スワップチェーンのアダプタがスワップチェーンのウィンドウが存在する出力を制御していない。 */,
			};
			DXGI_INFO_QUEUE_FILTER filter = {};
			filter.DenyList.NumIDs = static_cast<UINT>(std::size(hide));
			filter.DenyList.pIDList = hide;
			dxgiInfoQueue->AddStorageFilterEntries(DXGI_DEBUG_DXGI, &filter);
		}
#endif // __MINGW32__
	}
#endif

ThrowIfFailed(CreateDXGIFactory2(m_dxgiFactoryFlags, IID_PPV_ARGS(m_dxgiFactory.ReleaseAndGetAddressOf())));

// フルスクリーンボーダレスウィンドウでティアリングがサポートされているかを判定します。
if (m_options & c_AllowTearing)
{
	BOOL allowTearing = FALSE;

	HRESULT hr = m_dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
	if (FAILED(hr) || !allowTearing)
	{
		m_options &= ~c_AllowTearing;
#ifdef _DEBUG
		OutputDebugStringA("WARNING: Variable refresh rate displays not supported");
#endif
	}
}

ComPtr<IDXGIAdapter1> adapter;
GetAdapter(adapter.GetAddressOf());

// DX12 API デバイスオブジェクトを作成します。
HRESULT hr = D3D12CreateDevice(
	adapter.Get(),
	m_d3dMinFeatureLevel,
	IID_PPV_ARGS(m_d3dDevice.ReleaseAndGetAddressOf())
);
ThrowIfFailed(hr);

m_d3dDevice->SetName(L"DeviceResources");

#ifndef NDEBUG
// デバッグデバイスを構成します（有効な場合）。
ComPtr<ID3D12InfoQueue> d3dInfoQueue;
if (SUCCEEDED(m_d3dDevice.As(&d3dInfoQueue)))
{
#ifdef _DEBUG
	d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
	d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
#endif
	D3D12_MESSAGE_ID hide[] =
	{
		D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
		D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
		// ハイブリッドグラフィックス環境でのデバッグレイヤー問題への回避策
		D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_WRONGSWAPCHAINBUFFERREFERENCE,
		D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
	};
	D3D12_INFO_QUEUE_FILTER filter = {};
	filter.DenyList.NumIDs = static_cast<UINT>(std::size(hide));
	filter.DenyList.pIDList = hide;
	d3dInfoQueue->AddStorageFilterEntries(&filter);
}
#endif

// このデバイスでサポートされる最大の機能レベルを決定します
static const D3D_FEATURE_LEVEL s_featureLevels[] =
{
#if defined(NTDDI_WIN10_FE) || defined(USING_D3D12_AGILITY_SDK)
		D3D_FEATURE_LEVEL_12_2,
#endif
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
};

D3D12_FEATURE_DATA_FEATURE_LEVELS featLevels =
{
	static_cast<UINT>(std::size(s_featureLevels)), s_featureLevels, D3D_FEATURE_LEVEL_11_0
};

hr = m_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featLevels, sizeof(featLevels));
if (SUCCEEDED(hr))
{
	m_d3dFeatureLevel = featLevels.MaxSupportedFeatureLevel;
}
else
{
	m_d3dFeatureLevel = m_d3dMinFeatureLevel;
}

// コマンドキューを作成します。
D3D12_COMMAND_QUEUE_DESC queueDesc = {};
queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

ThrowIfFailed(m_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(m_commandQueue.ReleaseAndGetAddressOf())));

m_commandQueue->SetName(L"DeviceResources");

// レンダーターゲットビューおよびデプスステンシルビュー用の記述子ヒープを作成します。
D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {};
rtvDescriptorHeapDesc.NumDescriptors = m_backBufferCount;
rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(m_rtvDescriptorHeap.ReleaseAndGetAddressOf())));

m_rtvDescriptorHeap->SetName(L"DeviceResources");

m_rtvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

if (m_depthBufferFormat != DXGI_FORMAT_UNKNOWN)
{
	D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc = {};
	dsvDescriptorHeapDesc.NumDescriptors = 1;
	dsvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

	ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(&dsvDescriptorHeapDesc, IID_PPV_ARGS(m_dsvDescriptorHeap.ReleaseAndGetAddressOf())));

	m_dsvDescriptorHeap->SetName(L"DeviceResources");
}

// 描画対象となる各バックバッファ用のコマンドアロケータを作成します。
for (UINT n = 0; n < m_backBufferCount; n++)
{
	ThrowIfFailed(m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_commandAllocators[n].ReleaseAndGetAddressOf())));

	wchar_t name[25] = {};
	swprintf_s(name, L"Render target %u", n);
	m_commandAllocators[n]->SetName(name);
}

// グラフィックスコマンドを記録するためのコマンドリストを作成します。
ThrowIfFailed(m_d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[0].Get(), nullptr, IID_PPV_ARGS(m_commandList.ReleaseAndGetAddressOf())));
ThrowIfFailed(m_commandList->Close());

m_commandList->SetName(L"DeviceResources");

// GPU 実行進行状況を追跡するためのフェンスを作成します。
ThrowIfFailed(m_d3dDevice->CreateFence(m_fenceValues[m_backBufferIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.ReleaseAndGetAddressOf())));
m_fenceValues[m_backBufferIndex]++;

m_fence->SetName(L"DeviceResources");

m_fenceEvent.Attach(CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE));
if (!m_fenceEvent.IsValid())
{
	throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()), "CreateEventEx");
}
}

// ウィンドウサイズが変更されるたびに再作成する必要があるリソース。
void DeviceResources::CreateWindowSizeDependentResources()
{
	if (!m_window)
	{
		throw std::logic_error("Call SetWindow with a valid Win32 window handle");
	}

	// 以前の GPU 作業がすべて完了するまで待ちます。
	WaitForGpu();

	// スワップチェーンに結びついたリソースを解放し、フェンス値を更新します。
	for (UINT n = 0; n < m_backBufferCount; n++)
	{
		m_renderTargets[n].Reset();
		m_fenceValues[n] = m_fenceValues[m_backBufferIndex];
	}

	// ピクセル単位のレンダーターゲットサイズを決定します。
	const UINT backBufferWidth = std::max<UINT>(static_cast<UINT>(m_outputSize.right - m_outputSize.left), 1u);
	const UINT backBufferHeight = std::max<UINT>(static_cast<UINT>(m_outputSize.bottom - m_outputSize.top), 1u);
	const DXGI_FORMAT backBufferFormat = NoSRGB(m_backBufferFormat);

	// スワップチェーンが既に存在する場合はリサイズし、存在しない場合は新規作成します。
	if (m_swapChain)
	{
		// スワップチェーンが既に存在する場合はリサイズします。
		HRESULT hr = m_swapChain->ResizeBuffers(
			m_backBufferCount,
			backBufferWidth,
			backBufferHeight,
			backBufferFormat,
			(m_options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u
		);

		if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
		{
#ifdef _DEBUG
			char buff[64] = {};
			sprintf_s(buff, "Device Lost on ResizeBuffers: Reason code 0x%08X\n",
				static_cast<unsigned int>((hr == DXGI_ERROR_DEVICE_REMOVED) ? m_d3dDevice->GetDeviceRemovedReason() : hr));
			OutputDebugStringA(buff);
#endif
			// 何らかの理由でデバイスが削除された場合、新しいデバイスとスワップチェーンを作成する必要があります。
			HandleDeviceLost();

			// ここでの処理を続行しないでください。HandleDeviceLost はこのメソッドを再度呼び出して
			// 正しく新しいデバイスをセットアップします。
			return;
		}
		else
		{
			ThrowIfFailed(hr);
		}
	}
	else
	{
		// スワップチェーンの記述子を作成します。
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = backBufferWidth;
		swapChainDesc.Height = backBufferHeight;
		swapChainDesc.Format = backBufferFormat;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = m_backBufferCount;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		swapChainDesc.Flags = (m_options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

		DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
		fsSwapChainDesc.Windowed = TRUE;

		// ウィンドウ用のスワップチェーンを作成します。
		ComPtr<IDXGISwapChain1> swapChain;
		ThrowIfFailed(m_dxgiFactory->CreateSwapChainForHwnd(
			m_commandQueue.Get(),
			m_window,
			&swapChainDesc,
			&fsSwapChainDesc,
			nullptr,
			swapChain.GetAddressOf()
		));

		ThrowIfFailed(swapChain.As(&m_swapChain));

		// このクラスは排他的フルスクリーンモードをサポートせず、DXGI が ALT+ENTER に応答するのを防ぎます
		ThrowIfFailed(m_dxgiFactory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER));
	}

	// HDR のカラースペース設定を処理します
	UpdateColorSpace();

	// このウィンドウのバックバッファ（最終レンダーターゲット）を取得し、
	// 各バッファに対してレンダーターゲットビューを作成します。
	for (UINT n = 0; n < m_backBufferCount; n++)
	{
		ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(m_renderTargets[n].GetAddressOf())));

		wchar_t name[25] = {};
		swprintf_s(name, L"Render target %u", n);
		m_renderTargets[n]->SetName(name);

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = m_backBufferFormat;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

#ifdef __MINGW32__
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
		std::ignore = m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(&cpuHandle);
#else
		const auto cpuHandle = m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
#endif

		const CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor(cpuHandle, static_cast<INT>(n), m_rtvDescriptorSize);
		m_d3dDevice->CreateRenderTargetView(m_renderTargets[n].Get(), &rtvDesc, rtvDescriptor);
	}

	// 現在のバックバッファのインデックスをリセットします。
	m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

	if (m_depthBufferFormat != DXGI_FORMAT_UNKNOWN)
	{
		// 深度/ステンシルバッファとして 2D サーフェスを割り当て、このサーフェス上に深度/ステンシルビューを作成します。
		const CD3DX12_HEAP_PROPERTIES depthHeapProperties(D3D12_HEAP_TYPE_DEFAULT);

		D3D12_RESOURCE_DESC depthStencilDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			m_depthBufferFormat,
			backBufferWidth,
			backBufferHeight,
			1, // 単一の配列要素を使用します。
			1  // 単一のミップマップレベルを使用します。
		);
		depthStencilDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		const CD3DX12_CLEAR_VALUE depthOptimizedClearValue(m_depthBufferFormat, (m_options & c_ReverseDepth) ? 0.0f : 1.0f, 0u);

		ThrowIfFailed(m_d3dDevice->CreateCommittedResource(
			&depthHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&depthStencilDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(m_depthStencil.ReleaseAndGetAddressOf())
		));

		m_depthStencil->SetName(L"Depth stencil");

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = m_depthBufferFormat;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

#ifdef __MINGW32__
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
		std::ignore = m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(&cpuHandle);
#else
		const auto cpuHandle = m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
#endif

		m_d3dDevice->CreateDepthStencilView(m_depthStencil.Get(), &dsvDesc, cpuHandle);
	}

	// 3D レンダリング用のビューポートとシザー矩形をウィンドウ全体に設定します。
	m_screenViewport.TopLeftX = m_screenViewport.TopLeftY = 0.f;
	m_screenViewport.Width = static_cast<float>(backBufferWidth);
	m_screenViewport.Height = static_cast<float>(backBufferHeight);
	m_screenViewport.MinDepth = D3D12_MIN_DEPTH;
	m_screenViewport.MaxDepth = D3D12_MAX_DEPTH;

	m_scissorRect.left = m_scissorRect.top = 0;
	m_scissorRect.right = static_cast<LONG>(backBufferWidth);
	m_scissorRect.bottom = static_cast<LONG>(backBufferHeight);
}

// このメソッドは Win32 ウィンドウが作成（または再作成）されたときに呼ばれます。
void DeviceResources::SetWindow(HWND window, int width, int height) noexcept
{
	m_window = window;

	m_outputSize.left = m_outputSize.top = 0;
	m_outputSize.right = static_cast<long>(width);
	m_outputSize.bottom = static_cast<long>(height);
}

// このメソッドは Win32 ウィンドウのサイズが変わったときに呼ばれます。
bool DeviceResources::WindowSizeChanged(int width, int height)
{
	if (!m_window)
		return false;

	RECT newRc;
	newRc.left = newRc.top = 0;
	newRc.right = static_cast<long>(width);
	newRc.bottom = static_cast<long>(height);
	if (newRc.right == m_outputSize.right && newRc.bottom == m_outputSize.bottom)
	{
		// HDR のカラースペース設定を処理します
		UpdateColorSpace();

		return false;
	}

	m_outputSize = newRc;
	CreateWindowSizeDependentResources();
	return true;
}

// すべてのデバイスリソースを再作成し、現在の状態に戻します。
void DeviceResources::HandleDeviceLost()
{
	if (m_deviceNotify)
	{
		m_deviceNotify->OnDeviceLost();
	}

	for (UINT n = 0; n < m_backBufferCount; n++)
	{
		m_commandAllocators[n].Reset();
		m_renderTargets[n].Reset();
	}

	m_depthStencil.Reset();
	m_commandQueue.Reset();
	m_commandList.Reset();
	m_fence.Reset();
	m_rtvDescriptorHeap.Reset();
	m_dsvDescriptorHeap.Reset();
	m_swapChain.Reset();
	m_d3dDevice.Reset();
	m_dxgiFactory.Reset();

#if defined(_DEBUG) && !defined(__MINGW32__)
	{
		ComPtr<IDXGIDebug1> dxgiDebug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
		{
			dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
		}
	}
#endif

	CreateDeviceResources();
	CreateWindowSizeDependentResources();

	if (m_deviceNotify)
	{
		m_deviceNotify->OnDeviceRestored();
	}
}

// レンダリングのためにコマンドリストとレンダーターゲットを準備します。
void DeviceResources::Prepare(D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
	// コマンドリストとアロケータをリセットします。
	ThrowIfFailed(m_commandAllocators[m_backBufferIndex]->Reset());
	ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_backBufferIndex].Get(), nullptr));

	if (beforeState != afterState)
	{
		// 描画可能にするためにレンダーターゲットを正しい状態に遷移させます。
		const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_backBufferIndex].Get(),
			beforeState, afterState);
		m_commandList->ResourceBarrier(1, &barrier);
	}
}

// スワップチェーンの内容を画面に表示します。
void DeviceResources::Present(D3D12_RESOURCE_STATES beforeState)
{
	if (beforeState != D3D12_RESOURCE_STATE_PRESENT)
	{
		// 表示可能な状態にするためにレンダーターゲットを遷移させます。
		const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_backBufferIndex].Get(),
			beforeState, D3D12_RESOURCE_STATE_PRESENT);
		m_commandList->ResourceBarrier(1, &barrier);
	}

	// コマンドリストを GPU に送って処理させます。
	ThrowIfFailed(m_commandList->Close());
	m_commandQueue->ExecuteCommandLists(1, CommandListCast(m_commandList.GetAddressOf()));

	HRESULT hr;
	if (m_options & c_AllowTearing)
	{
		// 同期間隔を 0 にする場合、サポートされていれば常にティアリングを使用することが推奨されます。
		// ただし、真のフルスクリーンモードでは失敗します。
		hr = m_swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
	}
	else
	{
		// 第1引数は DXGI に VSync までブロックするよう指示します。これにより、表示されないフレームを
		// 描画して無駄なサイクルを消費することを防げます。
		hr = m_swapChain->Present(1, 0);
	}

	// デバイスがリセットされた場合、レンダラーを完全に再初期化する必要があります。
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
#ifdef _DEBUG
		char buff[64] = {};
		sprintf_s(buff, "Device Lost on Present: Reason code 0x%08X\n",
			static_cast<unsigned int>((hr == DXGI_ERROR_DEVICE_REMOVED) ? m_d3dDevice->GetDeviceRemovedReason() : hr));
		OutputDebugStringA(buff);
#endif
		HandleDeviceLost();
	}
	else
	{
		ThrowIfFailed(hr);

		MoveToNextFrame();

		if (!m_dxgiFactory->IsCurrent())
		{
			UpdateColorSpace();
		}
	}
}

// 保留中の GPU 作業が完了するのを待ちます。
void DeviceResources::WaitForGpu() noexcept
{
	if (m_commandQueue && m_fence && m_fenceEvent.IsValid())
	{
		// GPU キューに Signal コマンドをスケジュールします。
		const UINT64 fenceValue = m_fenceValues[m_backBufferIndex];
		if (SUCCEEDED(m_commandQueue->Signal(m_fence.Get(), fenceValue)))
		{
			// Signal が処理されるまで待ちます。
			if (SUCCEEDED(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent.Get())))
			{
				std::ignore = WaitForSingleObjectEx(m_fenceEvent.Get(), INFINITE, FALSE);

				// 現在のフレームのフェンス値をインクリメントします。
				m_fenceValues[m_backBufferIndex]++;
			}
		}
	}
}

// 次のフレームのレンダリング準備を行います。
void DeviceResources::MoveToNextFrame()
{
	// キューに Signal コマンドをスケジュールします。
	const UINT64 currentFenceValue = m_fenceValues[m_backBufferIndex];
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

	// バックバッファのインデックスを更新します。
	m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

	// 次のフレームがレンダリング可能でない場合は、準備ができるまで待ちます。
	if (m_fence->GetCompletedValue() < m_fenceValues[m_backBufferIndex])
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_backBufferIndex], m_fenceEvent.Get()));
		std::ignore = WaitForSingleObjectEx(m_fenceEvent.Get(), INFINITE, FALSE);
	}

	// 次のフレームのフェンス値を設定します。
	m_fenceValues[m_backBufferIndex] = currentFenceValue + 1;
}

// このメソッドは Direct3D 12 をサポートする最初に見つかったハードウェアアダプタを取得します。
// 見つからない場合は WARP を試し、それでもなければ例外を投げます。
void DeviceResources::GetAdapter(IDXGIAdapter1** ppAdapter)
{
	*ppAdapter = nullptr;

	ComPtr<IDXGIAdapter1> adapter;

	for (UINT adapterIndex = 0;
		SUCCEEDED(m_dxgiFactory->EnumAdapterByGpuPreference(
			adapterIndex,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())));
		adapterIndex++)
	{
		DXGI_ADAPTER_DESC1 desc;
		ThrowIfFailed(adapter->GetDesc1(&desc));

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// Basic Render Driver アダプタは選択しないでください。
			continue;
		}

		// アダプタが Direct3D 12 をサポートするか確認します（実際のデバイスはまだ作成しません）。
		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), m_d3dMinFeatureLevel, __uuidof(ID3D12Device), nullptr)))
		{
#ifdef _DEBUG
			wchar_t buff[256] = {};
			swprintf_s(buff, L"Direct3D Adapter (%u): VID:%04X, PID:%04X - %ls\n", adapterIndex, desc.VendorId, desc.DeviceId, desc.Description);
			OutputDebugStringW(buff);
#endif
			break;
		}
	}

#if !defined(NDEBUG)
	if (!adapter)
	{
		// 代わりに WARP12 を試します
		if (FAILED(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()))))
		{
			throw std::runtime_error("WARP12 not available. Enable the 'Graphics Tools' optional feature");
		}

		OutputDebugStringA("Direct3D Adapter - WARP12\n");
	}
#endif

	if (!adapter)
	{
		throw std::runtime_error("No Direct3D 12 device found");
	}

	*ppAdapter = adapter.Detach();
}

// HDR 出力を処理するためにスワップチェーンのカラースペースを設定します。
void DeviceResources::UpdateColorSpace()
{
	if (!m_dxgiFactory)
		return;

	if (!m_dxgiFactory->IsCurrent())
	{
		// 出力情報は DXGI ファクトリにキャッシュされます。古くなっている場合は新しいファクトリを作成する必要があります。
		ThrowIfFailed(CreateDXGIFactory2(m_dxgiFactoryFlags, IID_PPV_ARGS(m_dxgiFactory.ReleaseAndGetAddressOf())));
	}

	DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

	bool isDisplayHDR10 = false;

	if (m_swapChain)
	{
		// HDR サポートを検出するために、現在アプリに関連する主要な DXGI 出力のカラースペースを
		// ウィンドウ/ディスプレイの交差を用いて確認する必要があります。

		// アプリウィンドウの矩形境界を取得します。
		RECT windowBounds;
		if (!GetWindowRect(m_window, &windowBounds))
			throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()), "GetWindowRect");

		const long ax1 = windowBounds.left;
		const long ay1 = windowBounds.top;
		const long ax2 = windowBounds.right;
		const long ay2 = windowBounds.bottom;

		ComPtr<IDXGIOutput> bestOutput;
		long bestIntersectArea = -1;

		ComPtr<IDXGIAdapter> adapter;
		for (UINT adapterIndex = 0;
			SUCCEEDED(m_dxgiFactory->EnumAdapters(adapterIndex, adapter.ReleaseAndGetAddressOf()));
			++adapterIndex)
		{
			ComPtr<IDXGIOutput> output;
			for (UINT outputIndex = 0;
				SUCCEEDED(adapter->EnumOutputs(outputIndex, output.ReleaseAndGetAddressOf()));
				++outputIndex)
			{
				// 現在の出力の矩形境界を取得します。
				DXGI_OUTPUT_DESC desc;
				ThrowIfFailed(output->GetDesc(&desc));
				const auto& r = desc.DesktopCoordinates;

				// 交差領域を計算します
				const long intersectArea = ComputeIntersectionArea(ax1, ay1, ax2, ay2, r.left, r.top, r.right, r.bottom);
				if (intersectArea > bestIntersectArea)
				{
					bestOutput.Swap(output);
					bestIntersectArea = intersectArea;
				}
			}
		}

		if (bestOutput)
		{
			ComPtr<IDXGIOutput6> output6;
			if (SUCCEEDED(bestOutput.As(&output6)))
			{
				DXGI_OUTPUT_DESC1 desc;
				ThrowIfFailed(output6->GetDesc1(&desc));

				if (desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
				{
					// 表示出力は HDR10 です。
					isDisplayHDR10 = true;
				}
			}
		}
	}

	if ((m_options & c_EnableHDR) && isDisplayHDR10)
	{
		switch (m_backBufferFormat)
		{
		case DXGI_FORMAT_R10G10B10A2_UNORM:
			// アプリケーションが HDR10 信号を生成します。
			colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
			break;

		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			// システムが HDR10 信号を生成します。アプリケーションは線形値を使用します。
			colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
			break;

		default:
			break;
		}
	}

	m_colorSpace = colorSpace;

	UINT colorSpaceSupport = 0;
	if (m_swapChain
		&& SUCCEEDED(m_swapChain->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport))
		&& (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
	{
		ThrowIfFailed(m_swapChain->SetColorSpace1(colorSpace));
	}
}
