// VkObj.h

#pragma once

#include "../Utils/Util.h"
#pragma optimize( "gt", on )
#include "../Utils/External/tiny_obj_loader.h"
#pragma optimize( "gt", off )
#include "VkDevice.h"
#include "VkResources.h"

struct FPosColorUVVertex
{
	float x, y, z;
	uint32 Color;
	float u, v;

	inline bool operator == (const FPosColorUVVertex& A) const
	{
		return A.x == x &&
			A.y == y &&
			A.z == z &&
			A.Color == Color &&
			A.u == u &&
			A.v == v;
	}
};

struct FTinyObj;

struct FObj
{
	std::string BaseDir;
	bool Load(const char* Filename);
	FTinyObj* Loaded = nullptr;
};


struct FMesh
{
	std::map<std::string, FImage2DWithView*> Textures;

	struct FBatch
	{
		FVertexBuffer ObjVB;
		FIndexBuffer ObjIB;
		FImage2DWithView* DiffuseTexture = nullptr;
		FImage2DWithView* BumpTexture = nullptr;
		uint32 NumVertices = 0;
		uint32 NumIndices = 0;
		int MaterialID = -1;
	};
	std::vector<FBatch*> Batches;

	FBatch* FindBatchByMaterialID(int MaterialID)
	{
		for (auto* Batch : Batches)
		{
			if (Batch->MaterialID == MaterialID)
			{
				return Batch;
			}
		}

		check(0);
		return nullptr;
	}

	void CreateFromObj(FObj* Obj, FDevice* Device, FCmdBufferMgr* CmdBufMgr, FStagingManager* StagingMgr, FMemManager* MemMgr);

	void Destroy()
	{
		for (auto& Batch : Batches)
		{
			Batch->ObjIB.Destroy();
			Batch->ObjVB.Destroy();
			delete Batch;
		}
		Batches.clear();

		for (auto Pair : Textures)
		{
			Pair.second->Destroy();
			delete Pair.second;
		}
		Textures.clear();
	}

	void SetupTexture(FObj* Obj, FDevice* Device, FCmdBufferMgr* CmdBufMgr, FStagingManager* StagingMgr, FMemManager* MemMgr, int32 Index, const std::string& MaterialTextureName, size_t OffsetIntoBatchMemberImage);
};


struct FMeshInstance
{
	FMesh* Mesh = nullptr;

	struct FObjUB
	{
		FMatrix4x4 Obj;
		FVector4 Tint;
	};
	FGPUUniformBuffer<FObjUB> ObjUB;
};

void LoadTexturesForMesh(FDevice* Device, FMemManager* MemMgr, FMesh& Mesh, const std::string& BaseDir);
