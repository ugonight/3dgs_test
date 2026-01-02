//
// Game.cpp
//

#include <D3Dcompiler.h>
#include <dxcapi.h>

#include "pch.h"
#include "Game.h"
#include "PlyLoader.h"

extern void ExitGame() noexcept;

using namespace DirectX;

using Microsoft::WRL::ComPtr;

static Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderWithDXC(
	const std::wstring& filename,
	const std::wstring& entryPoint,
	const std::wstring& target,
	bool enableDebug);

Game::Game() noexcept(false)
{
	m_deviceResources = std::make_unique<DX::DeviceResources>();
	// TODO: スワップチェーンのフォーマット、深度/ステンシルのフォーマット、バックバッファ数のパラメータを指定してください。
	//   DX::DeviceResources::c_AllowTearing を追加して可変レートディスプレイを有効にします。
	//   HDR10 表示のために DX::DeviceResources::c_EnableHDR を追加します。
	//   深度バッファのクリアを 1 ではなく 0 に最適化するために DX::DeviceResources::c_ReverseDepth を追加します。
	m_deviceResources->RegisterDeviceNotify(this);
}

Game::~Game()
{
	if (m_deviceResources)
	{
		m_deviceResources->WaitForGpu();
	}
}

// 実行に必要な Direct3D リソースを初期化します。
void Game::Initialize(HWND window, int width, int height)
{
	m_camera.Init({ 0, 1, 5 });
	m_camera.SetMoveSpeed(2.0f);

	m_deviceResources->SetWindow(window, width, height);

	m_deviceResources->CreateDeviceResources();
	CreateDeviceDependentResources();

	m_deviceResources->CreateWindowSizeDependentResources();
	CreateWindowSizeDependentResources();

	// TODO: デフォルトの可変タイムステップモード以外を使用する場合はタイマー設定を変更してください。
	// 例: 60 FPS の固定タイムステップ更新ロジックの場合:
	/*
	m_timer.SetFixedTimeStep(true);
	m_timer.SetTargetElapsedSeconds(1.0 / 60);
	*/
}

#pragma region Frame Update
// 基本的なゲームループを実行します。
void Game::Tick()
{
	m_timer.Tick([&]()
		{
			Update(m_timer);
		});

	Render();
}

// ワールドを更新します。
void Game::Update(DX::StepTimer const& timer)
{
	PIXBeginEvent(PIX_COLOR_DEFAULT, L"Update");

	float elapsedTime = float(timer.GetElapsedSeconds());

	// TODO: ここにゲームロジックを追加してください。
	m_camera.Update(elapsedTime);
	{
		auto aspectRatio = m_deviceResources->GetOutputSize().right / static_cast<float>(m_deviceResources->GetOutputSize().bottom);

		XMFLOAT4X4 mvp;
		DirectX::XMMATRIX model = XMMatrixScaling(1, 1, 1) *
			// 3DGS公式サンプルはなぜか30°傾いている
			XMMatrixRotationRollPitchYaw(XMConvertToRadians(30), XMConvertToRadians(0), XMConvertToRadians(0)) *
			XMMatrixTranslation(0, 0, 0);
		XMStoreFloat4x4(&mvp, (model * m_camera.GetViewMatrix() * m_camera.GetProjectionMatrix(0.8f, aspectRatio, 0.1f, 1000.f)));
		m_pConstantBuffers->mvp = mvp;
	}

	PIXEndEvent();
}
#pragma endregion

#pragma region Frame Render
// シーンを描画します。
void Game::Render()
{
	// 最初の Update の前に描画しようとしないでください。
	if (m_timer.GetFrameCount() == 0)
	{
		return;
	}

	// 新しいフレームを描画するためにコマンドリストを準備します。
	m_deviceResources->Prepare();
	Clear();

	auto commandList = m_deviceResources->GetCommandList();
	PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"Render");

	// TODO: ここにレンダリングコードを追加してください。
	PopulateCommandList();

	PIXEndEvent(commandList);

	// 新しいフレームを表示します。
	PIXBeginEvent(m_deviceResources->GetCommandQueue(), PIX_COLOR_DEFAULT, L"Present");
	m_deviceResources->Present();

	// DX12 の DirectX Tool Kit を使用している場合はこの行のコメントを外してください:
	// m_graphicsMemory->Commit(m_deviceResources->GetCommandQueue());

	PIXEndEvent(m_deviceResources->GetCommandQueue());
}

// バックバッファをクリアするヘルパー関数。
void Game::Clear()
{
	auto commandList = m_deviceResources->GetCommandList();
	PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"Clear");

	// ビューをクリアします。
	const auto rtvDescriptor = m_deviceResources->GetRenderTargetView();
	const auto dsvDescriptor = m_deviceResources->GetDepthStencilView();

	commandList->OMSetRenderTargets(1, &rtvDescriptor, FALSE, &dsvDescriptor);
	commandList->ClearRenderTargetView(rtvDescriptor, XMVECTORF32{ 0.1f,0.1f,0.1f,1.0f }, 0, nullptr);
	commandList->ClearDepthStencilView(dsvDescriptor, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// ビューポートとシザー矩形を設定します。
	const auto viewport = m_deviceResources->GetScreenViewport();
	const auto scissorRect = m_deviceResources->GetScissorRect();
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);

	PIXEndEvent(commandList);
}
#pragma endregion

#pragma region Message Handlers
// メッセージハンドラ
void Game::OnActivated()
{
	// TODO: ゲームがアクティブなウィンドウになります。
}

void Game::OnDeactivated()
{
	// TODO: ゲームがバックグラウンドウィンドウになります。
}

void Game::OnSuspending()
{
	// TODO: ゲームが電源サスペンドされる（または最小化される）際の処理。
}

void Game::OnResuming()
{
	m_timer.ResetElapsedTime();

	// TODO: ゲームが電源復帰（または最小化から復帰）する際の処理。
}

void Game::OnWindowMoved()
{
	const auto r = m_deviceResources->GetOutputSize();
	m_deviceResources->WindowSizeChanged(r.right, r.bottom);
}

void Game::OnDisplayChange()
{
	m_deviceResources->UpdateColorSpace();
}

void Game::OnWindowSizeChanged(int width, int height)
{
	if (!m_deviceResources->WindowSizeChanged(width, height))
		return;

	CreateWindowSizeDependentResources();

	// TODO: ゲームウィンドウがリサイズされています。
}

void Game::OnKeyDown(UINT8 key)
{
	m_camera.OnKeyDown(key);
}

void Game::OnKeyUp(UINT8 key)
{
	m_camera.OnKeyUp(key);
}

// Properties
void Game::GetDefaultSize(int& width, int& height) const noexcept
{
	// TODO: デフォルトのウィンドウサイズを変更してください（最小サイズは 320x200）。
	width = 800;
	height = 600;
}
#pragma endregion

#pragma region Direct3D Resources
// これらはデバイスに依存するリソースです。
void Game::CreateDeviceDependentResources()
{
	auto device = m_deviceResources->GetD3DDevice();

	// シェーダーモデル6のサポートを確認します
	D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_0 };
	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)))
		|| (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_0))
	{
#ifdef _DEBUG
		OutputDebugStringA("ERROR: Shader Model 6.0 is not supported!\n");
#endif
		throw std::runtime_error("Shader Model 6.0 is not supported!");
	}

	// DX12 の DirectX Tool Kit を使用している場合はこの行のコメントを外してください:
	// m_graphicsMemory = std::make_unique<GraphicsMemory>(device);

	// TODO: ここでウィンドウサイズに依存しないデバイス依存オブジェクトを初期化してください。
	LoadPipeline();
	LoadAssets();
}

// ウィンドウの SizeChanged イベントで変更されるすべてのメモリリソースを割り当てます。
void Game::CreateWindowSizeDependentResources()
{
	// TODO: ここでウィンドウサイズ依存のオブジェクトを初期化してください。
}

void Game::OnDeviceLost()
{
	// TODO: ここで Direct3D リソースのクリーンアップを行ってください。

	// DX12 の DirectX Tool Kit を使用している場合はこの行のコメントを外してください:
	// m_graphicsMemory.reset();
}

void Game::OnDeviceRestored()
{
	CreateDeviceDependentResources();

	CreateWindowSizeDependentResources();
}
#pragma endregion

void Game::LoadPipeline()
{
	auto device = m_deviceResources->GetD3DDevice();

	// ディスクリプタヒープを作成します。
	{
		// シェーダーリソースビュー (SRV) と定数バッファビュー (CBV) 記述子ヒープを記述して作成します。
		D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
		cbvSrvHeapDesc.NumDescriptors = 2;
		cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		DX::ThrowIfFailed(device->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&m_cbvSrvHeap)));

		m_cbvSrvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
}

void Game::LoadAssets()
{
	auto device = m_deviceResources->GetD3DDevice();
	auto commandList = m_deviceResources->GetCommandList();
	auto commandQueue = m_deviceResources->GetCommandQueue();

	// ルートシグネチャを作成します。
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
		// これはサンプルがサポートする最高バージョンです。CheckFeatureSupportが成功した場合、返されるHighestVersionはこの値より大きくなりません。
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
		if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		CD3DX12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		DX::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		DX::ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}

	// パイプラインステートを作成します（シェーダーのコンパイルと読み込みを含む）。
	{
		//ComPtr<ID3DBlob> vertexShader;
		//ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
		// グラフィックスデバッグツールでシェーダーデバッグを強化します。
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif

		// DX::ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "MSMain", "ms_5_0", compileFlags, 0, &meshShader, nullptr));
		// DX::ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

		ComPtr<ID3DBlob> meshShader = CompileShaderWithDXC(L"shaders.hlsl", L"MSMain", L"ms_6_5", true);
		ComPtr<ID3DBlob> pixelShader = CompileShaderWithDXC(L"shaders.hlsl", L"PSMain", L"ps_6_5", true);

		//// 頂点入力レイアウトを定義します。
		//D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		//{
		//	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		//	{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		//};

		CD3DX12_RASTERIZER_DESC rasterizerStateDesc(D3D12_DEFAULT);
		rasterizerStateDesc.CullMode = D3D12_CULL_MODE_NONE;

		// グラフィックスパイプラインステートオブジェクト（PSO）を記述して作成します。
		//D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		//psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		//psoDesc.pRootSignature = m_rootSignature.Get();
		//psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		//psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		//psoDesc.RasterizerState = rasterizerStateDesc;
		//psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		//psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		//psoDesc.SampleMask = UINT_MAX;
		//psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		//psoDesc.NumRenderTargets = 1;
		//psoDesc.RTVFormats[0] = m_deviceResources->GetBackBufferFormat();
		//psoDesc.DSVFormat = m_deviceResources->GetDepthBufferFormat();
		//psoDesc.SampleDesc.Count = 1;
		//DX::ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

		D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.MS = CD3DX12_SHADER_BYTECODE(meshShader.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = m_deviceResources->GetBackBufferFormat();
		psoDesc.DSVFormat = m_deviceResources->GetDepthBufferFormat();
		psoDesc.RasterizerState = rasterizerStateDesc;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);             // 不透明ブレンド
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // 深度テスト有効、ステンシル無効
		psoDesc.DepthStencilState.DepthEnable = false;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.SampleDesc = DefaultSampleDesc();
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		auto psoStream = CD3DX12_PIPELINE_MESH_STATE_STREAM(psoDesc);
		D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
		streamDesc.pPipelineStateSubobjectStream = &psoStream;
		streamDesc.SizeInBytes = sizeof(psoStream);

		ComPtr<ID3D12Device2> device2;
		device->QueryInterface(IID_PPV_ARGS(&device2));
		DX::ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_pipelineState)));
	}

	// 注: ComPtr は CPU オブジェクトですが、これらのリソースは、それらを参照するコマンドリストが GPU 上で実行を完了するまでスコープ内に留まる必要があります。
	// このメソッドの最後に GPU をフラッシュして、リソースが早期に破棄されないようにします。
	ComPtr<ID3D12Resource> vertexBufferUploadHeap;
	ComPtr<ID3D12Resource> indexBufferUploadHeap;

	// コマンドリストが Close 状態のままなので、記録前にアロケータとコマンドリストをリセットします。
	DX::ThrowIfFailed(m_deviceResources->GetCommandAllocator()->Reset());
	DX::ThrowIfFailed(commandList->Reset(m_deviceResources->GetCommandAllocator(), nullptr));

	auto cbvSrvHandle = m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart();
	{
		// 定数バッファ用のアップロード ヒープを作成します。
		auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SceneConstantBuffer));
		DX::ThrowIfFailed(device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_cbvUploadHeap)));

		// 定数バッファをマップします。
		// D3D11とは異なり、リソースはGPUで使用するためにアンマップする必要はありません。
		// このサンプルでは、​​各フレームのマッピング/アンマップによるオーバーヘッドを回避するため、リソースは「永続的に」マップされたままになります。
		CD3DX12_RANGE readRange(0, 0); // CPU 上でこのリソースから読み取るつもりはありません。
		DX::ThrowIfFailed(m_cbvUploadHeap->Map(0, &readRange, reinterpret_cast<void**>(&m_pConstantBuffers)));

		// 定数バッファビュー (CBV) を記述して作成します。
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = m_cbvUploadHeap->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = sizeof(SceneConstantBuffer);
		device->CreateConstantBufferView(&cbvDesc, cbvSrvHandle);
	}

	cbvSrvHandle.ptr += m_cbvSrvDescriptorSize;

	// 頂点バッファ（SRV）
	{
		std::vector<Vertex> vertices = PlyLoader::Load("D:/Downloads/models/garden/point_cloud/iteration_7000/point_cloud.ply");
		m_vertexCount = vertices.size();
		const UINT vertexBufferSize = static_cast<UINT>(sizeof(Vertex) * vertices.size());

		auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
		DX::ThrowIfFailed(device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer)));

		heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		DX::ThrowIfFailed(device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&vertexBufferUploadHeap)));

		// データを中間アップロードヒープにコピーし、アップロードヒープから頂点バッファへのコピーをスケジュールします。
		D3D12_SUBRESOURCE_DATA vertexData = {};
		vertexData.pData = reinterpret_cast<BYTE*>(vertices.data());
		vertexData.RowPitch = vertexBufferSize;
		vertexData.SlicePitch = vertexData.RowPitch;

		UpdateSubresources<1>(commandList, m_vertexBuffer.Get(), vertexBufferUploadHeap.Get(), 0, 0, 1, &vertexData);
		auto resourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
		commandList->ResourceBarrier(1, &resourceBarrier);

		// SRV を作成
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = m_vertexCount;
		srvDesc.Buffer.StructureByteStride = sizeof(Vertex);
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN; // StructuredBuffer の場合は UNKNOWN
		device->CreateShaderResourceView(m_vertexBuffer.Get(), &srvDesc, cbvSrvHandle);
	}

	DX::ThrowIfFailed(commandList->Close());
	ID3D12CommandList* ppCommandLists[] = { commandList };
	commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
	m_deviceResources->WaitForGpu();
}

void Game::PopulateCommandList()
{
	auto commandList = m_deviceResources->GetCommandList();

	const auto rtvDescriptor = m_deviceResources->GetRenderTargetView();
	const auto dsvDescriptor = m_deviceResources->GetDepthStencilView();
	commandList->OMSetRenderTargets(1, &rtvDescriptor, FALSE, &dsvDescriptor);

	// 必要な状態を設定します。
	commandList->SetPipelineState(m_pipelineState.Get());
	commandList->SetGraphicsRootSignature(m_rootSignature.Get());

	ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvHeap.Get() };
	commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	//commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
	//commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	auto heapStart = m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
	commandList->SetGraphicsRootDescriptorTable(0, heapStart); // CBV
	heapStart.ptr += m_cbvSrvDescriptorSize;
	commandList->SetGraphicsRootDescriptorTable(1, heapStart); // SRV
	//commandList->DrawInstanced(m_vertexCount, 1, 0, 0);

	ID3D12GraphicsCommandList6* meshCommandList = nullptr;
	commandList->QueryInterface(IID_PPV_ARGS(&meshCommandList));
	constexpr UINT THREADS_PER_GROUP = 128;
	UINT groupCount = (m_vertexCount + THREADS_PER_GROUP - 1) / THREADS_PER_GROUP;
	meshCommandList->DispatchMesh(groupCount, 1, 1);
	//meshCommandList->DispatchMesh(m_vertexCount, 1, 1);
}

static Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderWithDXC(
	const std::wstring& filename,
	const std::wstring& entryPoint,
	const std::wstring& target,
	bool enableDebug)
{
	using Microsoft::WRL::ComPtr;

	ComPtr<IDxcUtils> utils;
	ComPtr<IDxcCompiler3> compiler3;
	DX::ThrowIfFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)));
	DX::ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler3)));

	ComPtr<IDxcBlobEncoding> sourceBlob;
	// IDxcLibrary の代わりに IDxcUtils::LoadFile を使用
	DX::ThrowIfFailed(utils->LoadFile(filename.c_str(), nullptr, &sourceBlob));

	// DxcBuffer に詰める
	DxcBuffer sourceBuffer;
	sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
	sourceBuffer.Size = sourceBlob->GetBufferSize();
	sourceBuffer.Encoding = DXC_CP_UTF8;

	// 引数を作る
	std::vector<LPCWSTR> args;

	args.push_back(filename.c_str());

	args.push_back(L"-E");
	args.push_back(entryPoint.c_str());

	args.push_back(L"-T");
	args.push_back(target.c_str());

	if (enableDebug)
	{
		args.push_back(L"-Zi");            // デバッグ情報を生成
		args.push_back(L"-Qembed_debug");  // デバッグ情報をシェーダーに埋め込む
		args.push_back(L"-Od");            // 最適化無効（任意）
	}

	ComPtr<IDxcIncludeHandler> includeHandler;
	DX::ThrowIfFailed(utils->CreateDefaultIncludeHandler(&includeHandler));

	ComPtr<IDxcResult> result;
	DX::ThrowIfFailed(compiler3->Compile(
		&sourceBuffer,
		args.empty() ? nullptr : args.data(),
		static_cast<UINT32>(args.size()),
		includeHandler.Get(),
		IID_PPV_ARGS(&result)));

	HRESULT hrStatus = S_OK;
	DX::ThrowIfFailed(result->GetStatus(&hrStatus));
	if (FAILED(hrStatus))
	{
		ComPtr<IDxcBlobUtf8> errors;
		result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
		if (errors)
		{
			auto errStr = errors->GetStringPointer();
			OutputDebugStringA(reinterpret_cast<const char*>(errors->GetBufferPointer()));
		}
		throw std::runtime_error("DXC shader compile failed");
	}

	ComPtr<IDxcBlob> program;
	DX::ThrowIfFailed(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&program), nullptr));

	// IDxcBlob -> ID3DBlob にコピーして返す
	ComPtr<ID3DBlob> d3dBlob;
	DX::ThrowIfFailed(::D3DCreateBlob(program->GetBufferSize(), &d3dBlob));
	memcpy(d3dBlob->GetBufferPointer(), program->GetBufferPointer(), program->GetBufferSize());
	return d3dBlob;
}