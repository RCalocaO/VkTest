// VkObj.cpp

#include "stdafx.h"
#include "VkObj.h"
#include <unordered_map>

//#pragma optimize( "gt", on )

#define TINYOBJLOADER_IMPLEMENTATION
#include "../Meshes/tiny_obj_loader.h"

namespace std
{
	template<> struct hash<FPosColorUVVertex>
	{
		size_t operator()(FPosColorUVVertex const& Vertex) const
		{
			static_assert(sizeof(Vertex) == 6 * 4, "");
			uint32* Alias = (uint32*)&Vertex;
			return Alias[0] ^ (Alias[1] >> 1) ^ (Alias[2] << 1) ^ Alias[3] ^ (Alias[4] >> 1) ^ (Alias[5] << 1); //((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.texCoord) << 1);
		}
	};
}


struct FTinyObj
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
};

void FMesh::Create(FDevice* Device, FCmdBufferMgr* CmdBufMgr, FStagingManager* StagingMgr, FMemManager* MemMgr)
{
	std::unordered_map<FPosColorUVVertex, uint32> uniqueVertices;
	std::vector<FPosColorUVVertex> Vertices;
	std::vector<uint32> Indices;
	for (auto& Shape : Loaded->shapes)
	{
		for (const auto& index : Shape.mesh.indices)
		{
			FPosColorUVVertex Vertex;
			Vertex.x = Loaded->attrib.vertices[3 * index.vertex_index + 0];
			Vertex.y = Loaded->attrib.vertices[3 * index.vertex_index + 1];
			Vertex.z = Loaded->attrib.vertices[3 * index.vertex_index + 2];
			Vertex.Color = PackNormalToU32(
				FVector3({
				Loaded->attrib.normals[3 * index.normal_index + 0],
				Loaded->attrib.normals[3 * index.normal_index + 1],
				Loaded->attrib.normals[3 * index.normal_index + 2] })
				);
			Vertex.u = Loaded->attrib.texcoords[2 * index.texcoord_index + 0];
			Vertex.v = Loaded->attrib.texcoords[2 * index.texcoord_index + 1];

			if (uniqueVertices.count(Vertex) == 0)
			{				
				uint32 VertexIndex = (uint32)Vertices.size();
				uniqueVertices[Vertex] = VertexIndex;
				Vertices.push_back(Vertex);
				Indices.push_back(VertexIndex);
			}
			else
			{
				Indices.push_back(uniqueVertices[Vertex]);
			}
		}
	}

	NumVertices = (uint32)Vertices.size();

	ObjVB.Create(Device->Device, sizeof(FPosColorUVVertex) * NumVertices, MemMgr);

	auto FillObj = [&](void* VertexData, /*void* IndexData, */void* UserData)
	{
		auto* Mesh = (FTinyObj*)UserData;
		auto* Vertex = (FPosColorUVVertex*)VertexData;
		memcpy(Vertex, &Vertices[0], NumVertices * sizeof(FPosColorUVVertex));
/*
		for (auto& Shape : Mesh->shapes)
		{
			for (auto& index : Shape.mesh.indices)
			{
				Vertex->x = Mesh->attrib.vertices[3 * index.vertex_index + 0];
				Vertex->y = Mesh->attrib.vertices[3 * index.vertex_index + 1];
				Vertex->z = Mesh->attrib.vertices[3 * index.vertex_index + 2];
				Vertex->Color = PackNormalToU32(
					FVector3({
					Mesh->attrib.normals[3 * index.normal_index + 0],
					Mesh->attrib.normals[3 * index.normal_index + 1],
					Mesh->attrib.normals[3 * index.normal_index + 2] })
				);
				Vertex->u = Mesh->attrib.texcoords[2 * index.texcoord_index + 0];
				Vertex->v = Mesh->attrib.texcoords[2 * index.texcoord_index + 1];
				++Vertex;
			}
		}
*/
	};

	MapAndFillBufferSyncOneShotCmdBuffer(Device, CmdBufMgr, StagingMgr, &ObjVB.Buffer, FillObj, sizeof(FPosColorUVVertex) * GetNumVertices(), this);
}

bool FMesh::Load(const char* Filename)
{
	std::string err;
	Loaded = new FTinyObj;
	if (!tinyobj::LoadObj(&Loaded->attrib, &Loaded->shapes, &Loaded->materials, &err, Filename, nullptr/*basepath*/, true/*triangulate*/))
	{
		return false;
	}

	return true;
}

#pragma optimize( "", off )
