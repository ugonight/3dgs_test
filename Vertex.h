#pragma once

struct Vertex
{
	DirectX::XMFLOAT4 position;
	DirectX::XMFLOAT4 color;
	DirectX::XMFLOAT3X3 cov3d;
};