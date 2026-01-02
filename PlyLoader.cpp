#include <fstream>
#include <regex>

#include "PlyLoader.h"

template<typename T>
void read_vertex(std::ifstream& ifs, T& out)
{
	ifs.read(reinterpret_cast<char*>(&out), sizeof(T));
}

std::vector<Vertex> PlyLoader::Load(std::string filename)
{
	std::ifstream ifs(filename, std::ios::binary);
	if (!ifs)
	{
		throw std::runtime_error("Failed to open file: " + filename);
	}

	// 「end_header」という行が出てくるまでヘッダーを読み飛ばす
	std::string line;
	int line_count = 0;
	int vertex_count = 0;
	while (std::getline(ifs, line))
	{
		line_count++;
		if (line.starts_with("end_header")) {
			break;
		}

		std::smatch match;
		if (std::regex_match(line, match, std::regex("element vertex (\\d+)")) && match.length() > 1)
		{
			vertex_count = std::stoi(match.str(1));
		}
	}

	std::vector<Vertex> vertices;
	ExtendedProps extended_props;
	BasicProps basic_props;

	for (size_t i = 0; i < vertex_count; i++)
	{
		if (line_count > 21)
		{
			read_vertex(ifs, extended_props);
			basic_props.copy_from_extended(extended_props);
		}
		else {
			read_vertex(ifs, basic_props);
		}

		Vertex v;
		// 位置
		v.position = DirectX::XMFLOAT4(
			basic_props.position[0],
			-basic_props.position[1],
			basic_props.position[2], 
			1.0f
		);
		// 色
		{
			float alpha = std::exp(basic_props.opacity) / (1 + std::exp(basic_props.opacity));
			const float C0 = 0.28209479177387814f;
			float r, g, b;
			r = basic_props.f_dc[0] * C0 + 0.5f;
			g = basic_props.f_dc[1] * C0 + 0.5f;
			b = basic_props.f_dc[2] * C0 + 0.5f;
			r = std::max(std::min(r, 1.0f), 0.0f);
			g = std::max(std::min(g, 1.0f), 0.0f);
			b = std::max(std::min(b, 1.0f), 0.0f);
			alpha = std::max(std::min(alpha, 1.0f), 0.0f);
			v.color = DirectX::XMFLOAT4(
				r, g, b, alpha
			);
		}
		// 3D共分散行列
		{
			DirectX::XMFLOAT3 scale = { std::exp(basic_props.scale[0]), std::exp(basic_props.scale[1]), std::exp(basic_props.scale[2]) };
			DirectX::XMVECTOR quate_rot = { basic_props.rotation[0], basic_props.rotation[1], basic_props.rotation[2], basic_props.rotation[3] };
			quate_rot = DirectX::XMVector4Normalize(quate_rot);

			// 回転行列をクォータニオンから生成し、スケール行列と掛け合わせる
			DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationQuaternion(quate_rot);
			DirectX::XMMATRIX scale_mat = DirectX::XMMatrixScaling(scale.x, scale.y, scale.z);
			DirectX::XMMATRIX transform_matrix = DirectX::XMMatrixMultiply(scale_mat, rotation);

			// 共分散行列 = transform^T * transform
			auto transform_matrix_t = DirectX::XMMatrixTranspose(transform_matrix);
			auto cov = DirectX::XMMatrixMultiply(transform_matrix_t, transform_matrix);
			DirectX::XMStoreFloat3x3(&v.cov3d, cov);
		}
		vertices.push_back(v);
	}

	return vertices;
}

void BasicProps::copy_from_extended(const ExtendedProps& ext)
{
	for (int i = 0; i < 3; i++)
	{
		position[i] = ext.position[i];
		normal[i] = ext.normal[i];
		f_dc[i] = ext.f_dc[i];
		scale[i] = ext.scale[i];
	}
	opacity = ext.opacity;
	for (int i = 0; i < 4; i++)
	{
		rotation[i] = ext.rotation[i];
	}
}
