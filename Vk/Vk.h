// Vk.h

#pragma once

#define VK_USE_PLATFORM_WIN32_KHR

#include <vulkan/vulkan.h>
#include <vector>
#include <list>
#include <map>

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



void DoInit(HINSTANCE hInstance, HWND hWnd, uint32& Width, uint32& Height);
void DoRender();
void DoResize(int32 Width, int32 Height);
void DoDeinit();
