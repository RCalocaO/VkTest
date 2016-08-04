#include "stdafx.h"
#include "../Utils/Util.h"
#include "ObjLoader.h"

namespace Obj
{
	static int32 ReadIntAndAdvance(const char*& Line)
	{
		int32 f = atoi(Line);
		if (Line[0] == '-')
		{
			++Line;
		}
		while (isdigit(Line[0]))
		{
			++Line;
		}

		return f;
	}

	static float ReadFloatAndAdvance(const char*& Line)
	{
		float f = (float)atof(Line);
		if (Line[0] == '-')
		{
			++Line;
		}
		while (isdigit(Line[0]))
		{
			++Line;
		}
		if (Line[0] == '.')
		{
			++Line;
			while (isdigit(Line[0]))
			{
				++Line;
			}
		}
		if (Line[0] == 'e' || Line[0] == 'E')
		{
			++Line;
			if (Line[0] == '-')
			{
				++Line;
			}
			while (isdigit(Line[0]))
			{
				++Line;
			}
		}

		return f;
	}

	bool Load(const char* Filename, FObj& OutObj)
	{
		FILE* File = nullptr;
		fopen_s(&File, Filename, "r");

		while (!feof(File))
		{
			char Line[2048];
			fgets(Line, sizeof(Line) - 2, File);

			if (Line[0] == '#' || Line[0] == '\n')
			{
				continue;
			}
			else
			{
				if (!strncmp(Line, "mtllib", 6))
				{
					continue;
				}
				else if (!strncmp(Line, "usemtl", 6))
				{
					continue;
				}
				else if (!strncmp(Line, "g ", 2))
				{
					continue;
				}
				else if (!strncmp(Line, "v ", 2))
				{
					const char* Ptr = Line + 2;
					FVector3 V;
					V.x = ReadFloatAndAdvance(Ptr);
					check(*Ptr++ == ' ');
					V.y = ReadFloatAndAdvance(Ptr);
					check(*Ptr++ == ' ');
					V.z = ReadFloatAndAdvance(Ptr);
					OutObj.Vs.push_back(V);
				}
				else if (!strncmp(Line, "vt ", 3))
				{
					const char* Ptr = Line + 3;
					FVector2 V;
					V.x = ReadFloatAndAdvance(Ptr);
					check(*Ptr == ' ');
					V.y = ReadFloatAndAdvance(Ptr);
					OutObj.VTs.push_back(V);
				}
				else if (!strncmp(Line, "vn ", 3))
				{
					const char* Ptr = Line + 3;
					FVector3 V;
					V.x = ReadFloatAndAdvance(Ptr);
					check(*Ptr++ == ' ');
					V.y = ReadFloatAndAdvance(Ptr);
					check(*Ptr++ == ' ');
					V.z = ReadFloatAndAdvance(Ptr);
					OutObj.VNs.push_back(V);
				}
				else if (!strncmp(Line, "f ", 2))
				{
					const char* Ptr = Line + 2;
					FFace Face;
					for (uint32 Index = 0; Index < 3; ++Index)
					{
						{
							int32 VIndex = ReadIntAndAdvance(Ptr);
							if (VIndex < 0)
							{
								VIndex = VIndex + OutObj.Vs.size();
							}
							Face.Corners[Index].Pos = VIndex;
						}
						check(*Ptr++ == '/');
						{
							int32 VIndex = ReadIntAndAdvance(Ptr);
							if (VIndex < 0)
							{
								VIndex = VIndex + OutObj.VTs.size();
							}
							Face.Corners[Index].UV = VIndex;
						}
						check(*Ptr++ == '/');
						{
							int32 VIndex = ReadIntAndAdvance(Ptr);
							if (VIndex < 0)
							{
								VIndex = VIndex + OutObj.VNs.size();
							}
							Face.Corners[Index].Normal = VIndex;
						}
						if (Index < 2)
						{
							check(*Ptr++ == ' ');
						}
					}
					OutObj.Faces.push_back(Face);
				}
			}
		}

		fclose(File);

		return true;
	}
}
