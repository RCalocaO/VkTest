// Vk.h

#pragma once

#include "../Utils/Util.h"
#include "../Meshes/ObjLoader.h"
#include "VkDevice.h"

struct FPosColorUVVertex
{
	float x, y, z;
	uint32 Color;
	float u, v;
};

struct FMesh
{
	FVertexBuffer ObjVB;
	Obj::FObj Obj;

	void Create(VkDevice Device, FMemManager* MemMgr)
	{
		ObjVB.Create(Device, sizeof(FPosColorUVVertex) * Obj.Faces.size() * 3, MemMgr);

		auto FillObj = [](void* VertexData, void* UserData)
		{
			auto* Mesh = (FMesh*)UserData;
			auto* Vertex = (FPosColorUVVertex*)VertexData;
			for (uint32 Index = 0; Index < Mesh->Obj.Faces.size(); ++Index)
			{
				auto& Face = Mesh->Obj.Faces[Index];
				for (uint32 Corner = 0; Corner < 3; ++Corner)
				{
					int32 Pos = Face.Corners[Corner].Pos;
					int32 Normal = Face.Corners[Corner].Normal;
					int32 UV = Face.Corners[Corner].UV;
					Vertex->x = Mesh->Obj.Vs[Pos].x;
					Vertex->y = Mesh->Obj.Vs[Pos].y;
					Vertex->z = Mesh->Obj.Vs[Pos].z;
					Vertex->Color = PackNormalToU32(Mesh->Obj.VNs[Normal]);
					Vertex->u = Mesh->Obj.VTs[UV].u;
					Vertex->v = Mesh->Obj.VTs[UV].v;
					++Vertex;
				}
			}
		};

		MapAndFillBufferSyncOneShotCmdBuffer(&ObjVB.Buffer, FillObj, sizeof(FPosColorUVVertex) * (uint32)Obj.Faces.size() * 3, this);
	}

	void Destroy()
	{
		ObjVB.Destroy();
	}

	bool Load(const char* Filename)
	{
		if (!Obj::Load(Filename, Obj))
		{
			return false;
		}

		return true;
	}
};
