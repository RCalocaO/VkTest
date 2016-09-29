// Vk.h

#pragma once

#include "../Utils/Util.h"

enum class EViewMode
{
	Solid,
	Wireframe,
};

struct FControl
{
	FVector3 StepDirection;
	FVector4 CameraPos;
	EViewMode ViewMode;
	bool DoPost;
	bool DoMSAA;

	FControl();
};
extern FControl GRequestControl;

bool DoInit(HINSTANCE hInstance, HWND hWnd, uint32& Width, uint32& Height);
void DoRender();
void DoResize(uint32 Width, uint32 Height);
void DoDeinit();
