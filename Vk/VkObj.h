// VkObj.h

#pragma once

#include "../Utils/Util.h"
#pragma optimize( "gt", on )
#include "../Meshes/tiny_obj_loader.h"
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


struct FMesh
{
	std::string BaseDir;

	std::map<std::string, FImage2DWithView*> Textures;

	struct FBatch
	{
		FVertexBuffer ObjVB;
		FIndexBuffer ObjIB;
		FImage2DWithView* Image = nullptr;
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

	void Create(FDevice* Device, FCmdBufferMgr* CmdBufMgr, FStagingManager* StagingMgr, FMemManager* MemMgr);

	void Destroy()
	{
		check(0);
		//ObjIB.Destroy();
		//ObjVB.Destroy();
	}

	bool Load(const char* Filename);

	//uint32 GetNumVertices() const
	//{
	//	return NumVertices;
	//}

	//uint32 GetNumIndices() const
	//{
	//	return NumIndices;
	//}

	FTinyObj* Loaded = nullptr;
};


struct FMeshInstance
{
	FMesh* Mesh = nullptr;

	struct FObjUB
	{
		FMatrix4x4 Obj;
	};
	FUniformBuffer<FObjUB> ObjUB;
};

void LoadTexturesForMesh(FDevice* Device, FMemManager* MemMgr, FMesh& Mesh, const std::string& BaseDir);
