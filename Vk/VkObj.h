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

	FVertexBuffer ObjVB;
	FIndexBuffer ObjIB;

	std::map<std::string, FImage2DWithView*> Textures;

	uint32 NumVertices = 0;
	uint32 NumIndices = 0;

	void Create(FDevice* Device, FCmdBufferMgr* CmdBufMgr, FStagingManager* StagingMgr, FMemManager* MemMgr);

	void Destroy()
	{
		check(0);
		ObjIB.Destroy();
		ObjVB.Destroy();
	}

	bool Load(const char* Filename);

	uint32 GetNumVertices() const
	{
		return NumVertices;
	}

	uint32 GetNumIndices() const
	{
		return NumIndices;
	}

	FTinyObj* Loaded = nullptr;
};

void LoadTexturesForMesh(FDevice* Device, FMemManager* MemMgr, FMesh& Mesh, const std::string& BaseDir);
