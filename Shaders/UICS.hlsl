
StructuredBuffer<uint> FontBuffer : register(u0);
RWTexture2D<float4> RWImage : register(u1);

#define CHAR_BITS_WIDTH		9
#define CHAR_HEIGHT			16

void DrawChar(uint CharOffset, uint2 XY, uint2 Delta)
{
	uint BitRow = FontBuffer[CharOffset + Delta.y];
	if (BitRow != 0)
	{
		uint ColMax = 1 << Delta.x;
		if (ColMax & BitRow)
		{
			float4 Color = float4(1,1,1,1);
			RWImage[XY] = Color;
		}
		else
		{
//			RWImage[XY]= float4(1,0,0,1);
		}
	}
	else
	{
//		RWImage[XY]= 0;
	}
}

cbuffer UIUB : register(b2)
{
	uint2 TextPos;
	uint NumChars;
	uint Dummy;
	uint4 Chars[16];
}

[numthreads(8,8,1)]
void Main(int3 GlobalInvocationID : SV_DispatchThreadID)
{
	int2 Delta = GlobalInvocationID - TextPos;
	if (Delta.y >= 0 && Delta.y < CHAR_HEIGHT)
	{
		if (Delta.x >= 0 && Delta.x < NumChars * CHAR_BITS_WIDTH)
		{
			uint CharIndex = Delta.x / CHAR_BITS_WIDTH;
			uint Char = Chars[CharIndex / 4][CharIndex % 4];
			uint CharOffset = Char * CHAR_HEIGHT;
			Delta.x = Delta.x % CHAR_BITS_WIDTH;
			DrawChar(CharOffset, GlobalInvocationID, Delta);
		}
	}
}
