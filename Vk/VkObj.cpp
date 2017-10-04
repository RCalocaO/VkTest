// VkObj.cpp

#include "stdafx.h"
#include "VkObj.h"

#pragma optimize( "gt", on )

#include <unordered_map>

#define TINYOBJLOADER_IMPLEMENTATION
#include "../Meshes/tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ASSERT(x) check(x)
#include "../Utils/stb_image.h"

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
			if (index.normal_index != -1)
			{
				Vertex.Color = PackNormalToU32(
					FVector3({
					Loaded->attrib.normals[3 * index.normal_index + 0],
					Loaded->attrib.normals[3 * index.normal_index + 1],
					Loaded->attrib.normals[3 * index.normal_index + 2] })
					);
			}
			Vertex.u = Loaded->attrib.texcoords[2 * index.texcoord_index + 0];
			Vertex.v = Loaded->attrib.texcoords[2 * index.texcoord_index + 1];

			//if (uniqueVertices.count(Vertex) == 0)
			{				
				uint32 VertexIndex = (uint32)Vertices.size();
				uniqueVertices[Vertex] = VertexIndex;
				Vertices.push_back(Vertex);
				Indices.push_back(VertexIndex);
			}
/*
			else
			{
				Indices.push_back(uniqueVertices[Vertex]);
			}*/
		}
	}

	NumVertices = (uint32)Vertices.size();

	ObjVB.Create(Device->Device, sizeof(FPosColorUVVertex) * NumVertices, MemMgr);

	auto FillVB = [&](void* VertexData, void* UserData)
	{
		memcpy(VertexData, &Vertices[0], NumVertices * sizeof(FPosColorUVVertex));
	};
	MapAndFillBufferSyncOneShotCmdBuffer(Device, CmdBufMgr, StagingMgr, &ObjVB.Buffer, FillVB, sizeof(FPosColorUVVertex) * NumVertices, this);

	NumIndices = (uint32)Indices.size();
	ObjIB.Create(Device->Device, NumIndices, VK_INDEX_TYPE_UINT32, MemMgr);

	auto FillIB = [&](void* IndexData, void* UserData)
	{
		memcpy(IndexData, &Indices[0], NumIndices * sizeof(uint32));
	};
	MapAndFillBufferSyncOneShotCmdBuffer(Device, CmdBufMgr, StagingMgr, &ObjIB.Buffer, FillIB, sizeof(uint32) * NumIndices, this);

	for (auto& Material : Loaded->materials)
	{
		if (!Material.diffuse_texname.empty())
		{
			std::string Texture = FileUtils::MakePath(BaseDir, Material.diffuse_texname);
			std::vector<char> FileData = LoadFile(Texture.c_str());
			if (!FileData.empty())
			{
				int W, H, C;
				auto* PixelData = stbi_load_from_memory((stbi_uc*)&FileData[0], (int)FileData.size(), &W, &H, &C, 4);
				if (PixelData)
				{
					FImage2DWithView* Image = new FImage2DWithView;
					Image->Create(Device->Device, W, H, VK_FORMAT_R8G8B8A8_UNORM,
						VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, MemMgr);

					uint32 Size = W * H * 4;

					MapAndFillImageSyncOneShotCmdBuffer(Device, CmdBufMgr, StagingMgr, &Image->Image, 
						[&](FPrimaryCmdBuffer* CmdBuffer, void* Data, uint32 Width, uint32 Height)
					{
						ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, Image->Image.Image,
							VK_IMAGE_LAYOUT_UNDEFINED, 0,
							VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
							VK_IMAGE_ASPECT_COLOR_BIT);
						memcpy(Data, PixelData, Size);
					}, Size);
					Textures[Material.diffuse_texname] = Image;
				}
				stbi_image_free(PixelData);
			}
		}
	}
}

bool FMesh::Load(const char* Filename)
{
	std::string err;
	Loaded = new FTinyObj;
	std::string Dummy;
	FileUtils::SplitPath(Filename, BaseDir, Dummy, true);
	if (!tinyobj::LoadObj(&Loaded->attrib, &Loaded->shapes, &Loaded->materials, &err, Filename, BaseDir.c_str()/*basepath*/, true/*triangulate*/))
	{
		return false;
	}

	return true;
}

#pragma optimize( "", off )
