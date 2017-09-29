
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
	std::string SourceFile;
	std::string BinaryFile;
	std::string AsmFile;
	std::string Entry;
	EShaderStage Stage = EShaderStage::Unknown;

	bool NeedsRecompiling()
	{
		if (FileUtils::IsNewerThan(SourceFile, BinaryFile))
		{
			return true;
		}

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
				DoCompileFromSource(ShaderInfo);
			}
			else if (!ShaderInfo.Shader)
			{
				DoCompileFromBinary(ShaderInfo);
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
		Info.Entry = EntryPoint;
		Info.Stage = InStage;
		SetupFilenames(HlslFilename, Info);

		ShaderInfos.push_back(Info);
		return Info.Handle;
	}

	virtual IShader* CreateShader(FShaderInfo& Info, std::vector<char>& Data) = 0;

	virtual void SetupFilenames(const std::string& OriginalFilename, FShaderInfo& Info) = 0;

	virtual bool DoCompileFromBinary(FShaderInfo& Info) = 0;
	virtual bool DoCompileFromSource(FShaderInfo& Info) = 0;

	void Destroy(FShaderHandle& Handle)
	{
		ShaderInfos[Handle.ID].Shader->Destroy();
		delete ShaderInfos[Handle.ID].Shader;
		ShaderInfos[Handle.ID].Shader = nullptr;
	}

	std::vector<FShaderInfo> ShaderInfos;
};
