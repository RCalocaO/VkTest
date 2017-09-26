
#pragma once

#include <functional>
#include <list>
#include "Util.h"

enum class EShaderStage
{
	Unknown = -1,
	Vertex,
	Pixel,
	Compute,
};

struct FShaderHandle
{
	int32_t ID = -1;
};

struct FShaderInfo
{
	FShaderHandle Handle;
	std::string Filename;
	std::string Entry;
	EShaderStage Stage = EShaderStage::Unknown;

	bool NeedsRecompiling()
	{
		if (!Shader)
		{
			return true;
		}

		//#todo: Check SPV file, check timestamp
		return false;
	}

	struct IShader* Shader = nullptr;
};

struct IShader
{
	EShaderStage Stage = EShaderStage::Unknown;
	FShaderInfo Info;

	IShader(const FShaderInfo& InInfo)
		: Info(InInfo)
	{
	}

	virtual void Destroy() = 0;
};

struct FShaderCollection
{
	void ReloadShaders()
	{
		//std::map<uint32_t, IShader*> RecompileList;

		for (auto& ShaderInfo : ShaderInfos)
		{
			if (ShaderInfo.NeedsRecompiling())
			{
				//RecompileList[ShaderInfo.ID] = nullptr;
				DoCompile(ShaderInfo);
			}
		}
	}

	IShader* GetShader(FShaderHandle Handle)
	{
		check(Handle.ID >= 0 && Handle.ID < (int)ShaderInfos.size());
		return ShaderInfos[Handle.ID].Shader;
	}

	const std::string& GetEntryPoint(FShaderHandle Handle)
	{
		IShader* Shader = GetShader(Handle);
		check(Shader);
		return Shader->Info.Entry;
	}

	FShaderHandle Register(const char* HlslFilename, EShaderStage InStage, const char* EntryPoint)
	{
		//#todo: Mutex
		FShaderInfo Info;
		Info.Handle.ID = (int32_t)ShaderInfos.size();
		Info.Filename = HlslFilename;
		Info.Entry = EntryPoint;
		Info.Stage = InStage;

		ShaderInfos.push_back(Info);
		return Info.Handle;
	}

	virtual IShader* CreateShader(FShaderInfo& Info, std::vector<char>& Data) = 0;

	virtual bool DoCompile(FShaderInfo& Info) = 0;

	void Destroy(FShaderHandle& Handle)
	{
		ShaderInfos[Handle.ID].Shader->Destroy();
		delete ShaderInfos[Handle.ID].Shader;
		ShaderInfos[Handle.ID].Shader = nullptr;
	}

	std::vector<FShaderInfo> ShaderInfos;
};
