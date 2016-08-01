#pragma once

namespace Obj
{
	struct FFace
	{
		struct FCorner
		{
			int32 Pos;
			int32 UV;
			int32 Normal;
		};
		FCorner Corners[3];
	};

	struct FObj
	{
		std::vector<FVector3> Vs;
		std::vector<FVector2> VTs;
		std::vector<FVector3> VNs;
		std::vector<FFace> Faces;
	};

	bool Load(const char* Filename, FObj& OutObj);
}
