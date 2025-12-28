//
// Game.h
//

#pragma once

#include "DeviceResources.h"
#include "StepTimer.h"
#include "SimpleCamera.h"

#include <memory>


// D3D12 デバイスを作成し、ゲームループを提供する基本的なゲーム実装。
class Game final : public DX::IDeviceNotify
{
public:

	Game() noexcept(false);
	~Game();

	Game(Game&&) = default;
	Game& operator= (Game&&) = default;

	Game(Game const&) = delete;
	Game& operator= (Game const&) = delete;

	// 初期化と管理
	void Initialize(HWND window, int width, int height);

	// 基本的なゲームループ
	void Tick();

	// IDeviceNotify の実装
	void OnDeviceLost() override;
	void OnDeviceRestored() override;

	// メッセージ
	void OnActivated();
	void OnDeactivated();
	void OnSuspending();
	void OnResuming();
	void OnWindowMoved();
	void OnDisplayChange();
	void OnWindowSizeChanged(int width, int height);
	void OnKeyDown(UINT8 key);
	void OnKeyUp(UINT8 key);

	// プロパティ
	void GetDefaultSize(int& width, int& height) const noexcept;

private:

	void Update(DX::StepTimer const& timer);
	void Render();

	void Clear();

	void CreateDeviceDependentResources();
	void CreateWindowSizeDependentResources();

	// デバイスに依存するリソース。
	std::unique_ptr<DX::DeviceResources>        m_deviceResources;

	// レンダリングループ用タイマー。
	DX::StepTimer                               m_timer;

	// DX12 用 DirectX Tool Kit を使用している場合はこの行のコメントを外してください:
	// std::unique_ptr<DirectX::GraphicsMemory> m_graphicsMemory;

	struct Vertex
	{
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT4 color;
	};

	struct SceneConstantBuffer
	{
		XMFLOAT4X4 mvp;        // Model-view-projection (MVP) matrix.
		FLOAT padding[48];
	};
	
	// Pipeline objects.
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_cbvSrvHeap;

	// App resources.
	UINT m_numIndices;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_cbvUploadHeap;
	SceneConstantBuffer* m_pConstantBuffers;
	UINT m_cbvSrvDescriptorSize;
	SimpleCamera m_camera;

	void LoadPipeline();
	void LoadAssets();
	void PopulateCommandList();

};
