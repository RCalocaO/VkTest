// Vk.h

#pragma once

#define VK_USE_PLATFORM_WIN32_KHR

#include <vulkan/vulkan.h>
#include <vector>

typedef uint32_t uint32;
typedef int32_t int32;

__forceinline void check(bool Condition)
{
	if (!Condition)
	{
		__debugbreak();
	}
}

__forceinline void checkVk(VkResult Result)
{
	check(Result == VK_SUCCESS);
}

template <typename T>
inline void MemZero(T& Struct)
{
	ZeroMemory(&Struct, sizeof(T));
}



void DoInit(HINSTANCE hInstance, HWND hWnd);
void DoRender();
void DoResize(int32 Width, int32 Height);
void DoDeinit();
