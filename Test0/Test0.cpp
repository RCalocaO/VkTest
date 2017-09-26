// Test0.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "Test0.h"
#include "../vk/Vk.h"

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
static HWND GMainWindow = 0;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
					 _In_opt_ HINSTANCE hPrevInstance,
					 _In_ LPWSTR    lpCmdLine,
					 _In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// TODO: Place code here.

	// Initialize global strings
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_TEST0, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance(hInstance, nCmdShow))
	{
		return FALSE;
	}

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_TEST0));

	MSG msg;

	// Main message loop:
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style          = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc    = WndProc;
	wcex.cbClsExtra     = 0;
	wcex.cbWndExtra     = 0;
	wcex.hInstance      = hInstance;
	wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TEST0));
	wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_TEST0);
	wcex.lpszClassName  = szWindowClass;
	wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
	  CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
	  return FALSE;
   }

   GMainWindow = hWnd;

   RECT Rect;
   GetWindowRect(hWnd, &Rect);

   uint32 Width = Rect.right - Rect.left;
   uint32 Height = Rect.bottom - Rect.top;
   if (!DoInit(hInstance, hWnd, Width, Height))
   {
	   check(0);
   }
   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	static bool bLButton = false;
	static bool bRButton = false;
	static bool bDestroyed = false;
	switch (Message)
	{
	case WM_COMMAND:
		{
			int wmId = LOWORD(wParam);
			// Parse the menu selections:
			switch (wmId)
			{
			case IDM_ABOUT:
				DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
				break;
			case IDM_EXIT:
				DestroyWindow(hWnd);
				break;
			default:
				return DefWindowProc(hWnd, Message, wParam, lParam);
			}
		}
		break;
	case WM_PAINT:
		{
			static bool bInsidePaint = false;
			if (!bDestroyed && !bInsidePaint && hWnd == GMainWindow)
			{
				bInsidePaint = true;
				DoRender();
				bInsidePaint = false;
			}
			//PAINTSTRUCT ps;
			//HDC hdc = BeginPaint(hWnd, &ps);
			// TODO: Add any drawing code that uses hdc here...
			//EndPaint(hWnd, &ps);
		}
		break;
	case WM_SIZE:
		// Resize the application to the new window size, except when
		// it was minimized. Vulkan doesn't support images or swapchains
		// with width=0 and height=0.
		if (wParam != SIZE_MINIMIZED)
		{
			int width = lParam & 0xffff;
			int height = (lParam & 0xffff0000) >> 16;
			DoResize(width, height);
		}
		break;
	case WM_CHAR:
		{
			int Key = LOWORD(wParam);
			switch (Key)
			{
			case '1':
				GRequestControl.ViewMode = EViewMode::Wireframe;
				break;
			case '2':
				GRequestControl.ViewMode = EViewMode::Solid;
				break;
			case 'P':
			case 'p':
				GRequestControl.DoPost = !GRequestControl.DoPost;
				break;
			case 'M':
			case 'm':
				GRequestControl.DoMSAA = !GRequestControl.DoMSAA;
				break;
			case '.':
				GRequestControl.DoRecompileShaders = true;
				break;
			default:
				break;
			}
		}
		break;

	case WM_KEYDOWN:
	case WM_KEYUP:
		{
			float UpDown = (Message == WM_KEYUP) ? 0 : 1.0f;
			int Key = LOWORD(wParam);
			switch (Key)
			{
			case VK_UP:
			case 'W':
				GRequestControl.StepDirection.z = UpDown * Key;
				break;
			case VK_DOWN:
			case 'S':
				GRequestControl.StepDirection.z = -UpDown * Key;
				break;
			case VK_LEFT:
			case 'A':
				GRequestControl.StepDirection.x = UpDown * Key;
				break;
			case VK_RIGHT:
			case 'D':
				GRequestControl.StepDirection.x = -UpDown * Key;
				break;
			case 'Q':
				GRequestControl.StepDirection.y = UpDown * Key;
				break;
			case 'E':
				GRequestControl.StepDirection.y = -UpDown * Key;
				break;
			default:
				break;
			}
		}
		break;

	case WM_RBUTTONDOWN:
		bRButton = true;
		GRequestControl.MouseMoveX = 0;
		GRequestControl.MouseMoveY = 0;
		GRequestControl.StepDirection = {0, 0, 0};
		break;

	case WM_RBUTTONUP:
		bRButton = false;
		GRequestControl.MouseMoveX = 0;
		GRequestControl.MouseMoveY = 0;
		GRequestControl.StepDirection = { 0, 0, 0 };
		break;

	case WM_LBUTTONDOWN:
		bLButton = true;
		GRequestControl.MouseMoveX = 0;
		GRequestControl.MouseMoveY = 0;
		GRequestControl.StepDirection = { 0, 0, 0 };
		break;

	case WM_LBUTTONUP:
		bLButton = false;
		GRequestControl.MouseMoveX = 0;
		GRequestControl.MouseMoveY = 0;
		GRequestControl.StepDirection = { 0, 0, 0 };
		break;

	case WM_MOUSEMOVE:
		{
			if (hWnd == GMainWindow)
			{
				int32 X = LOWORD(lParam);
				int32 Y = HIWORD(lParam);
				static int32 OldX = X;
				static int32 OldY = Y;
				int32 DeltaX = X - OldX;
				int32 DeltaY = Y - OldY;

				if (bRButton)
				{
					GRequestControl.StepDirection.x = (float)DeltaX;
					GRequestControl.StepDirection.y = (float)DeltaY;
				}
				else if (bLButton)
				{
					GRequestControl.MouseMoveX = DeltaX;
					GRequestControl.MouseMoveY = DeltaY;
				}
				OldX = X;
				OldY = Y;
			}
		}
		break;

	case WM_DESTROY:
		bDestroyed = true;
		DoDeinit();
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, Message, wParam, lParam);
	}
	return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}
