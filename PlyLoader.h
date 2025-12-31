#pragma once

#include <string>
#include <vector>

#include "Vertex.h"

struct ExtendedProps;

struct BasicProps
{
	float position[3];
	float normal[3];
	float f_dc[3];
	float opacity;
	float scale[3];
	float rotation[4];

	void copy_from_extended(const ExtendedProps& ext);
};

struct ExtendedProps
{
	float position[3];
	float normal[3];
	float f_dc[3];
	float f_rest[45];
	float opacity;
	float scale[3];
	float rotation[4];
};

class PlyLoader
{
public:
	static std::vector<Vertex> Load(std::string filename);

};