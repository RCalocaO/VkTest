// Vk.h

#pragma once

#include "../Utils/Util.h"

enum class EViewMode
{
	Solid,
	Wireframe,
};
extern EViewMode GViewMode;

bool DoInit(HINSTANCE hInstance, HWND hWnd, uint32& Width, uint32& Height);
void DoRender();
void DoResize(uint32 Width, uint32 Height);
void DoDeinit();
