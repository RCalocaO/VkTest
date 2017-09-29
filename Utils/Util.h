#pragma once

#include <vector>
#include <list>
#include <map>
#include <algorithm>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef int16_t int16;
typedef uint32_t uint32;
typedef int32_t int32;
typedef uint64_t uint64;
typedef int64_t int64;

#define check(x) if (!(x)) __debugbreak();

#define checkVk(r) check((r) == VK_SUCCESS)


template <typename T>
inline void MemZero(T& Struct)
{
	ZeroMemory(&Struct, sizeof(T));
}

inline std::vector<char> LoadFile(const char* Filename)
{
	std::vector<char> Data;

	FILE* File = nullptr;
	fopen_s(&File, Filename, "rb");
	if (File)
	{
		fseek(File, 0, SEEK_END);
		auto Size = ftell(File);
		fseek(File, 0, SEEK_SET);
		Data.resize(Size);
		fread(&Data[0], 1, Size, File);
		fclose(File);
	}
	return Data;
}

inline bool IsPowerOfTwo(uint64 N)
{
	return (N != 0) && !(N & (N - 1));
}

template <typename T>
inline T Align(T Value, T Alignment)
{
	check(IsPowerOfTwo(Alignment));
	return (Value + (Alignment - 1)) & ~(Alignment - 1);
}

inline float ToRadians(float Deg)
{
	return Deg * (3.14159265f / 180.0f);
}

inline float ToDegrees(float Rad)
{
	return Rad * (180.0f / 3.14159265f);
}

struct FVector2
{
	union
	{
		float Values[2];
		struct
		{
			float x, y;
		};
		struct
		{
			float u, v;
		};
	};

	static FVector2 GetZero()
	{
		FVector2 New;
		MemZero(New);
		return New;
	}
};

struct FVector3
{
	union
	{
		float Values[3];
		struct
		{
			float x, y, z;
		};
		struct  
		{
			float u, v, w;
		};
	};

	void Set(float InX, float InY, float InZ)
	{
		x = InX;
		y = InY;
		z = InZ;
	}

	static FVector3 GetZero()
	{
		FVector3 New;
		MemZero(New);
		return New;
	}

	FVector3 Mul(float f) const
	{
		FVector3 V;
		V.x = x * f;
		V.y = y * f;
		V.z = z * f;
		return V;
	}

	FVector3 Add(const FVector3& V) const
	{
		FVector3 O;
		O.x = x + V.x;
		O.y = y + V.y;
		O.z = z + V.z;
		return O;
	}

	FVector3() = default;

	FVector3(float f)
	{
		Set(f, f, f);
	}

	FVector3(float InX, float InY, float InZ)
	{
		Set(InX, InY, InZ);
	}

	friend FVector3 operator- (const FVector3& A, const FVector3& B)
	{
		return FVector3(A.x - B.x, A.y - B.y, A.z - B.z);
	}

	friend FVector3 operator* (const FVector3& A, const FVector3& B)
	{
		return FVector3(A.x * B.x, A.y * B.y, A.z * B.z);
	}
};

inline FVector3 Cross(const FVector3& A, const FVector3& B)
{
	FVector3 R;
	float u1 = A.x;
	float u2 = A.y;
	float u3 = A.z;
	float v1 = B.x;
	float v2 = B.y;
	float v3 = B.z;
	R.Set(u2*v3-u3*v2, u3*v1-u1*v3, u1*v2-u2*v1);
	return R;
}

struct FVector4
{
	union
	{
		float Values[4];
		struct
		{
			float x, y, z, w;
		};
	};

	static FVector4 GetZero()
	{
		FVector4 New;
		MemZero(New);
		return New;
	}

	FVector4() {}

	FVector4(const FVector3& V, float W)
	{
		x = V.x;
		y = V.y;
		z = V.z;
		w = W;
	}

	FVector4(float InX, float InY, float InZ, float InW)
	{
		x = InX;
		y = InY;
		z = InZ;
		w = InW;
	}

	void Set(float InX, float InY, float InZ, float InW)
	{
		x = InX;
		y = InY;
		z = InZ;
		w = InW;
	}

	FVector4 Add(const FVector3& V) const
	{
		FVector4 O;
		O.x = x + V.x;
		O.y = y + V.y;
		O.z = z + V.z;
		O.w = w;
		return O;
	}
};

struct FMatrix3x3
{
	union
	{
		float Values[9];
		FVector3 Rows[3];
	};

	FMatrix3x3() {}

	FMatrix3x3 GetTranspose() const
	{
		FMatrix3x3 New;
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				New.Values[i * 3 + j] = Values[j * 3 + i];
			}
		}

		return New;
	}

	static FMatrix3x3 GetZero()
	{
		FMatrix3x3 New;
		MemZero(New);
		return New;
	}

	static FMatrix3x3 GetIdentity()
	{
		FMatrix3x3 New;
		MemZero(New);
		New.Values[0] = 1;
		New.Values[4] = 1;
		New.Values[8] = 1;
		return New;
	}

	static FMatrix3x3 GetRotationX(float AngleRad)
	{
		FMatrix3x3 New;
		MemZero(New);
		float Cos = cos(AngleRad);
		float Sin = sin(AngleRad);
		New.Rows[0].x = 1;
		New.Rows[1].y = Cos;
		New.Rows[1].z = -Sin;
		New.Rows[2].y = Sin;
		New.Rows[2].z = Cos;
		return New;
	}

	static FMatrix3x3 GetRotationY(float AngleRad)
	{
		FMatrix3x3 New;
		MemZero(New);
		float Cos = cos(AngleRad);
		float Sin = sin(AngleRad);
		New.Rows[0].x = Cos;
		New.Rows[0].z = Sin;
		New.Rows[1].y = 1;
		New.Rows[2].x = -Sin;
		New.Rows[2].z = Cos;
		return New;
	}

	static FMatrix3x3 GetRotationZ(float AngleRad)
	{
		FMatrix3x3 New;
		MemZero(New);
		float Cos = cos(AngleRad);
		float Sin = sin(AngleRad);
		New.Rows[0].x = Cos;
		New.Rows[0].y = -Sin;
		New.Rows[1].x = Sin;
		New.Rows[1].y = Cos;
		New.Rows[2].z = 1;
		return New;
	}

	void Set(int32 Row, int32 Col, float Value)
	{
		Values[Row * 3 + Col] = Value;
	}
};

struct FMatrix4x4
{
	union
	{
		float Values[16];
		FVector4 Rows[4];
	};

	FMatrix4x4() {}

	FMatrix4x4 GetTranspose() const
	{
		FMatrix4x4 New;
		for (int i = 0; i < 4; ++i)
		{
			for (int j = 0; j < 4; ++j)
			{
				New.Values[i * 4 + j] = Values[j * 4 + i];
			}
		}

		return New;
	}

	static FMatrix4x4 GetZero()
	{
		FMatrix4x4 New;
		MemZero(New);
		return New;
	}

	static FMatrix4x4 GetIdentity()
	{
		FMatrix4x4 New;
		MemZero(New);
		New.Values[0] = 1;
		New.Values[5] = 1;
		New.Values[10] = 1;
		New.Values[15] = 1;
		return New;
	}

	static FMatrix4x4 GetRotationX(float AngleRad)
	{
		FMatrix4x4 New;
		MemZero(New);
		float Cos = cos(AngleRad);
		float Sin = sin(AngleRad);
		New.Rows[0].x = 1;
		New.Rows[1].y = Cos;
		New.Rows[1].z = -Sin;
		New.Rows[2].y = Sin;
		New.Rows[2].z = Cos;
		New.Rows[3].w = 1;
		return New;
	}

	static FMatrix4x4 GetRotationY(float AngleRad)
	{
		FMatrix4x4 New;
		MemZero(New);
		float Cos = cos(AngleRad);
		float Sin = sin(AngleRad);
		New.Rows[0].x = Cos;
		New.Rows[0].z = Sin;
		New.Rows[1].y = 1;
		New.Rows[2].x = -Sin;
		New.Rows[2].z = Cos;
		New.Rows[3].w = 1;
		return New;
	}

	static FMatrix4x4 GetRotationZ(float AngleRad)
	{
		FMatrix4x4 New;
		MemZero(New);
		float Cos = cos(AngleRad);
		float Sin = sin(AngleRad);
		New.Rows[0].x = Cos;
		New.Rows[0].y = -Sin;
		New.Rows[1].x = Sin;
		New.Rows[1].y = Cos;
		New.Rows[2].z = 1;
		New.Rows[3].w = 1;
		return New;
	}

	void Set(int32 Row, int32 Col, float Value)
	{
		Values[Row * 4 + Col] = Value;
	}
};

inline uint32 PackNormalToU32(const FVector3& V)
{
	uint32 Out = 0;
	Out |= ((uint32)((V.x + 1.0f) * 127.5f) & 0xff) << 0;
	Out |= ((uint32)((V.y + 1.0f) * 127.5f) & 0xff) << 8;
	Out |= ((uint32)((V.z + 1.0f) * 127.5f) & 0xff) << 16;
	return Out;
};

inline FMatrix4x4 CalculateProjectionMatrix(float FOVRadians, float Aspect, float NearZ, float FarZ)
{
	const float HalfTanFOV = (float)tan(FOVRadians / 2.0);
	FMatrix4x4 New = FMatrix4x4::GetZero();
	New.Set(0, 0, 1.0f / (Aspect * HalfTanFOV));
	New.Set(1, 1, 1.0f / HalfTanFOV);
	New.Set(2, 3, -1);
	New.Set(2, 2, FarZ / (NearZ - FarZ));
	New.Set(3, 2, -(FarZ * NearZ) / (FarZ - NearZ));
	return New;
}

inline uint8 To8BitClamped(float f)
{
	f = f > 1.0f ? 1.0f : f;
	f = f < 0.0f ? 0.0f : f;
	return (uint8)(f * 255.0f);
}

struct FColor
{
	union
	{
		struct
		{
			uint8 R, G, B, A;
		};
		uint32 Raw;
	};
};

inline uint32 ToRGB8Color(FVector3 V, uint8 Alpha)
{
	FColor Color;
	Color.R = To8BitClamped(V.x);
	Color.G = To8BitClamped(V.y);
	Color.B = To8BitClamped(V.z);
	Color.A = Alpha;
	return Color.Raw;
}


namespace FileUtils
{
	// Returns Extension
	inline std::string SplitPath(const std::string& FullPathToFilename, std::string& OutPath, std::string& OutFilename, bool bIncludeExtension)
	{
		char Buffer[1024];
		char* PtrFilename = nullptr;
		::GetFullPathNameA(FullPathToFilename.c_str(), sizeof(Buffer), Buffer, &PtrFilename);
		OutPath = Buffer;
		if (PtrFilename)
		{
			OutPath.resize(PtrFilename - Buffer);
			OutFilename = PtrFilename;
		}

		std::string Extension;

		auto ExtensionFound = OutFilename.rfind('.');
		if (ExtensionFound != std::string::npos)
		{
			Extension = OutFilename.substr(ExtensionFound + 1);
			if (!bIncludeExtension)
			{
				OutFilename.resize(ExtensionFound);
			}
		}

		return Extension;
	}

	inline std::string GetBaseName(const std::string& FullPathToFilename, bool bExtension)
	{
		std::string Path;
		std::string Filename;
		SplitPath(FullPathToFilename, Path, Filename, bExtension);
		return Filename;
	}

	inline std::string GetPath(const std::string& FullPathToFilename, bool bExtension)
	{
		std::string Path;
		std::string Filename;
		SplitPath(FullPathToFilename, Path, Filename, bExtension);
		return Path;
	}

	inline void RemoveQuotes(std::string& Path)
	{
		if (Path.size() > 2 && Path.front() == '"')
		{
			check(Path.back() == '"');
			Path.pop_back();
			Path = Path.substr(1);
		}
	}

	inline std::string MakePath(const std::string& Root, const std::string& DirOrFile)
	{
		std::string Out;
		if (!Root.empty())
		{
			Out = Root;
			RemoveQuotes(Out);
			if (Root.back() != '\\')
			{
				Out += '\\';
			}
		}

		Out += DirOrFile;
		return Out;
	}

	inline std::string AddQuotes(const std::string& InPath)
	{
		std::string Path = InPath;
		if (Path.size() > 2 && Path.front() == '"')
		{
			check(Path.back() == '"');
		}
		else
		{
			Path = "\"" + Path;
			Path +="\"";
		}

		return Path;
	}

	// Returns true is Src is newer than Dst or if Dst doesn't exist
	inline bool IsNewerThan(const std::string& Src, const std::string& Dst)
	{
		HANDLE SrcHandle = ::CreateFileA(Src.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		if (SrcHandle == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		HANDLE DstHandle = ::CreateFileA(Dst.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		if (DstHandle == INVALID_HANDLE_VALUE)
		{
			::CloseHandle(SrcHandle);
			return true;
		}

		FILETIME SrcTime, DstTime;
		if (!::GetFileTime(SrcHandle, nullptr, nullptr, &SrcTime))
		{
			auto Error = ::GetLastError();
			check(0);
			return false;
		}
		
		if (!::GetFileTime(DstHandle, nullptr, nullptr, &DstTime))
		{
			auto Error = ::GetLastError();
			check(0);
			return false;
		}

		bool bResult = false;
		if (SrcTime.dwHighDateTime < DstTime.dwHighDateTime)
		{
			bResult = false;
		}
		else if (SrcTime.dwHighDateTime == DstTime.dwHighDateTime)
		{
			bResult = SrcTime.dwLowDateTime > DstTime.dwLowDateTime;
		}
		else
		{
			bResult = true;
		}

		::CloseHandle(DstHandle);
		::CloseHandle(SrcHandle);
		return bResult;
	}
}
