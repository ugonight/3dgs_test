//
// Game.cpp
//

#include <D3Dcompiler.h>

#include "pch.h"
#include "Game.h"

extern void ExitGame() noexcept;

using namespace DirectX;

using Microsoft::WRL::ComPtr;

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
	elapsedTime;

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
	commandList->ClearRenderTargetView(rtvDescriptor, Colors::CornflowerBlue, 0, nullptr);
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

void Game::LoadAssets()
{
	auto device = m_deviceResources->GetD3DDevice();

	// 空のルートシグネチャを作成します。
	{
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		DX::ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		DX::ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}

	// パイプラインステートを作成します（シェーダーのコンパイルと読み込みを含む）。
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
		// グラフィックスデバッグツールでシェーダーデバッグを強化します。
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif

		DX::ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
		DX::ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

		// 頂点入力レイアウトを定義します。
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		// グラフィックスパイプラインステートオブジェクト（PSO）を記述して作成します。
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = m_deviceResources->GetBackBufferFormat();
		psoDesc.SampleDesc.Count = 1;
		DX::ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
	}

	// 頂点バッファを作成します。
	{
		// 三角形のジオメトリを定義します。
		auto output_rect = m_deviceResources->GetOutputSize();
		auto aspect_ratio = static_cast<float>(output_rect.right - output_rect.left) / static_cast<float>(output_rect.bottom - output_rect.top);
		// クリップ座標をそのまま指定します（左手系、Y 上方向）。
		Vertex triangleVertices[] =
		{
			{ { 0.0f, 0.25f * aspect_ratio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } }, // 上頂点 (赤)
			{ { 0.25f, -0.25f * aspect_ratio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } }, // 右下頂点 (緑)
			{ { -0.25f, -0.25f * aspect_ratio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } } // 左下頂点 (青)
		};

		const UINT vertexBufferSize = sizeof(triangleVertices);

		// 注: 頂点バッファのような静的データ転送にアップロードヒープを使用することは推奨されません。
		// GPU が必要とするたびに、アップロードヒープがマージされます。
		// Default Heap の使用方法を参照してください。
		// このサンプルではコードの簡潔さと転送する頂点数が非常に少ないため、アップロードヒープを使用しています。
		auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
		DX::ThrowIfFailed(device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer)));

		// 三角形データを頂点バッファにコピーします。
		UINT8* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);        // このリソースを CPU から読み取る意図はありません。
		DX::ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
		m_vertexBuffer->Unmap(0, nullptr);

		// 頂点バッファビューを初期化します。
		m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferView.StrideInBytes = sizeof(Vertex);
		m_vertexBufferView.SizeInBytes = vertexBufferSize;
	}
}

void Game::PopulateCommandList()
{
	auto commandList = m_deviceResources->GetCommandList();
	// 必要な状態を設定します。
	commandList->SetPipelineState(m_pipelineState.Get());
	commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	// 三角形を描画します。
	commandList->DrawInstanced(3, 1, 0, 0);
}
