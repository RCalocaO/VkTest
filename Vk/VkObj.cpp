// VkObj.cpp

#include "stdafx.h"
#include "VkObj.h"

#pragma optimize( "gt", on )

#include <unordered_map>

#define TINYOBJLOADER_IMPLEMENTATION
#include "../Utils/External/tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ASSERT(x) check(x)
#include "../Utils/External/stb_image.h"

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

	template<> struct hash<FPosNormalUVVertex>
	{
		size_t operator()(FPosNormalUVVertex const& Vertex) const
		{
			static_assert(sizeof(Vertex) == 8 * 4, "");
			uint32* Alias = (uint32*)&Vertex;
			return Alias[0] ^ (Alias[1] >> 1) ^ (Alias[2] << 1) ^ Alias[3] ^ (Alias[4] >> 1) ^ (Alias[5] << 1) ^ Alias[6] ^ Alias[7];
		}
	};
}


struct FTinyObj
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
};

void FMesh::CreateFromObj(FObj* Obj, FDevice* Device, FCmdBufferMgr* CmdBufMgr, FStagingManager* StagingMgr, FMemManager* MemMgr)
{
	std::unordered_map<FPosNormalUVVertex, uint32> uniqueVertices;
	std::map<uint32, std::vector<FPosNormalUVVertex>> Vertices;
	std::map<uint32, std::vector<uint32>> Indices;
	std::set<uint32> MaterialIndices;
	bool bNeedsNormals = true;
	for (auto& Shape : Obj->Loaded->shapes)
	{
		check(!(Shape.mesh.indices.size() % 3));
		for (size_t i = 0; i < Shape.mesh.indices.size(); ++i)
		{
			auto MeshIndex = Shape.mesh.indices[i];
			FPosNormalUVVertex Vertex;
			Vertex.x = Obj->Loaded->attrib.vertices[3 * MeshIndex.vertex_index + 0];
			Vertex.y = -Obj->Loaded->attrib.vertices[3 * MeshIndex.vertex_index + 1];
			Vertex.z = Obj->Loaded->attrib.vertices[3 * MeshIndex.vertex_index + 2];
			if (MeshIndex.normal_index != -1)
			{
				bNeedsNormals = false;
				Vertex.nx = Obj->Loaded->attrib.normals[3 * MeshIndex.normal_index + 0];
				Vertex.ny = Obj->Loaded->attrib.normals[3 * MeshIndex.normal_index + 1];
				Vertex.nz = Obj->Loaded->attrib.normals[3 * MeshIndex.normal_index + 2];
			}

			if (MeshIndex.texcoord_index != -1)
			{
				Vertex.u = Obj->Loaded->attrib.texcoords[2 * MeshIndex.texcoord_index + 0];
				Vertex.v = Obj->Loaded->attrib.texcoords[2 * MeshIndex.texcoord_index + 1];
			}
			else
			{
				Vertex.u = 0;
				Vertex.v = 0;
			}

			//if (uniqueVertices.count(Vertex) == 0)
			{
				int MaterialIndex = Shape.mesh.material_ids[i / 3];
				uint32 VertexIndex = (uint32)Vertices[MaterialIndex].size();
				uniqueVertices[Vertex] = VertexIndex;
				Vertices[MaterialIndex].push_back(Vertex);
				Indices[MaterialIndex].push_back(VertexIndex);
				MaterialIndices.insert(MaterialIndex);
			}
/*
			else
			{
				Indices.push_back(uniqueVertices[Vertex]);
			}*/
		}
	}

	for (auto MaterialIndex : MaterialIndices)
	{
		if (bNeedsNormals)
		{
			for (size_t Index = 0; Index < Indices[MaterialIndex].size(); Index += 3)
			{
				int VertexIndex0 = Indices[MaterialIndex][Index + 0];
				int VertexIndex1 = Indices[MaterialIndex][Index + 1];
				int VertexIndex2 = Indices[MaterialIndex][Index + 2];
				FPosNormalUVVertex& V0 = Vertices[MaterialIndex][VertexIndex0];
				FPosNormalUVVertex& V1 = Vertices[MaterialIndex][VertexIndex1];
				FPosNormalUVVertex& V2 = Vertices[MaterialIndex][VertexIndex2];
				FVector3 A(V0.x, V0.y, V0.z);
				FVector3 B(V1.x, V1.y, V1.z);
				FVector3 C(V2.x, V2.y, V2.z);
				FVector3 AB = B - A;
				AB.Normalize();
				FVector3 AC = C - A;
				AC.Normalize();
				FVector3 Normal = Cross(AB, AC);
				Normal.Normalize();
				V0.nx = V1.nx = V2.nx = Normal.x;
				V0.ny = V1.ny = V2.ny = Normal.y;
				V0.nz = V1.nz = V2.nz = Normal.z;
			}
		}

		auto* Batch = new FBatch;
		Batch->NumVertices = (uint32)Vertices[MaterialIndex].size();

		Batch->ObjVB.Create(Device->Device, sizeof(FPosColorUVVertex) * Batch->NumVertices, MemMgr);

		auto FillVB = [&](void* VertexData, void* UserData)
		{
			memcpy(VertexData, &Vertices[MaterialIndex][0], Batch->NumVertices * sizeof(FPosColorUVVertex));
		};
		MapAndFillBufferSyncOneShotCmdBuffer(Device, CmdBufMgr, StagingMgr, &Batch->ObjVB.Buffer, FillVB, sizeof(FPosColorUVVertex) * Batch->NumVertices, this, __FILE__, __LINE__);

		Batch->NumIndices = (uint32)Indices[MaterialIndex].size();
		Batch->ObjIB.Create(Device->Device, Batch->NumIndices, VK_INDEX_TYPE_UINT32, MemMgr);

		auto FillIB = [&](void* IndexData, void* UserData)
		{
			memcpy(IndexData, &Indices[MaterialIndex][0], Batch->NumIndices * sizeof(uint32));
		};
		MapAndFillBufferSyncOneShotCmdBuffer(Device, CmdBufMgr, StagingMgr, &Batch->ObjIB.Buffer, FillIB, sizeof(uint32) * Batch->NumIndices, this, __FILE__, __LINE__);
		Batch->MaterialID = (int)MaterialIndex;
		Batches.push_back(Batch);
	}

	for (size_t Index = 0; Index < Obj->Loaded->materials.size(); ++Index)
	{
		auto& Material = Obj->Loaded->materials[Index];
		SetupTexture(Obj, Device, CmdBufMgr, StagingMgr, MemMgr, (int32)Index, Material.diffuse_texname, [](FBatch* Batch) -> FImage2DWithView*& { return Batch->DiffuseTexture; } );
		SetupTexture(Obj, Device, CmdBufMgr, StagingMgr, MemMgr, (int32)Index, Material.bump_texname, [](FBatch* Batch) -> FImage2DWithView*& { return Batch->BumpTexture; });
	}
}

void FMesh::SetupTexture(FObj* Obj, FDevice* Device, FCmdBufferMgr* CmdBufMgr, FStagingManager* StagingMgr, FMemManager* MemMgr, int32 Index, const std::string& MaterialTextureName, std::function<FImage2DWithView*&(FBatch* Batch)> Callback)
{
	if (!MaterialTextureName.empty())
	{
		FImage2DWithView* Image = nullptr;
		auto Found = Textures.find(MaterialTextureName);
		if (Found != Textures.end())
		{
			Image = Found->second;
		}
		else
		{
			std::string Texture = FileUtils::MakePath(Obj->BaseDir, MaterialTextureName);
			std::vector<char> FileData = LoadFile(Texture.c_str());
			if (!FileData.empty())
			{
				int W, H, C;
				auto* PixelData = stbi_load_from_memory((stbi_uc*)&FileData[0], (int)FileData.size(), &W, &H, &C, 4);
				if (PixelData)
				{
					Image = new FImage2DWithView;
					Image->Create(Device->Device, W, H, VK_FORMAT_R8G8B8A8_UNORM,
						VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, MemMgr, 1, VK_SAMPLE_COUNT_1_BIT, __FILE__, __LINE__);

					uint32 Size = W * H * 4;

					MapAndFillImageSyncOneShotCmdBuffer(Device, CmdBufMgr, StagingMgr, &Image->Image,
						[&](FPrimaryCmdBuffer* CmdBuffer, void* Data, uint32 Width, uint32 Height)
					{
						ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, Image->Image.Image,
							VK_IMAGE_LAYOUT_UNDEFINED, 0,
							VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
							VK_IMAGE_ASPECT_COLOR_BIT);
						memcpy(Data, PixelData, Size);
					}, Size, __FILE__, __LINE__);
				}
				stbi_image_free(PixelData);
				Textures[MaterialTextureName] = Image;
			}
		}
		FBatch* Batch = FindBatchByMaterialID(Index);
		if (Batch)
		{
			Callback(Batch) = Image;
		}
	}
}

bool FObj::Load(const char* Filename)
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
