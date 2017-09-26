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
	int32 MouseMoveX = 0;
	int32 MouseMoveY = 0;
	bool DoPost;
	bool DoMSAA;
	bool DoRecompileShaders = false;

	FControl();
};
extern FControl GRequestControl;

bool DoInit(HINSTANCE hInstance, HWND hWnd, uint32& Width, uint32& Height);
void DoRender();
void DoResize(uint32 Width, uint32 Height);
void DoDeinit();
