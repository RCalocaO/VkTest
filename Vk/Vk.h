// Vk.h

#pragma once

#include "../Utils/Util.h"

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>


bool DoInit(HINSTANCE hInstance, HWND hWnd, uint32& Width, uint32& Height);
void DoRender();
void DoResize(uint32 Width, uint32 Height);
void DoDeinit();
