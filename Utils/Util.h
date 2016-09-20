#pragma once

#include <vector>
#include <list>
#include <map>
#include <algorithm>

typedef uint8_t uint8;
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
	check(File);
	fseek(File, 0, SEEK_END);
	auto Size = ftell(File);
	fseek(File, 0, SEEK_SET);
	Data.resize(Size);
	fread(&Data[0], 1, Size, File);
	fclose(File);

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
};

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

struct FMatrix4x4
{
	union
	{
		float Values[16];
		FVector4 Rows[4];
	};

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
