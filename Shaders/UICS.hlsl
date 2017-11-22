
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

void RenderString(uint2 CharStartIndex, uint2 TextPos, uint2 GlobalInvocationID)
{
	int2 Delta = GlobalInvocationID - TextPos;
	if (Delta.y >= 0 && Delta.y < CHAR_HEIGHT)
	{
		uint TextLength = 10;
		if (Delta.x >= 0 && Delta.x < TextLength * CHAR_BITS_WIDTH)
		{
			uint CharIndex = Delta.x / CHAR_BITS_WIDTH;
			uint Char = CharStartIndex + CharIndex;
			uint CharOffset = Char * CHAR_HEIGHT;
			Delta.x = Delta.x % CHAR_BITS_WIDTH;
			DrawChar(CharOffset, GlobalInvocationID, Delta);
		}
	}
}

cbuffer UIUB : register(b2)
{
	uint2 TextPos;
	//uint NumChars;
	//uint Chars[64];
}

[numthreads(8,8,1)]
void Main(int3 GlobalInvocationID : SV_DispatchThreadID)
{
//	uint2 TextPos = uint2(100, 100);
	RenderString(48, TextPos, GlobalInvocationID);
#if 0
	TextPos.y += CHAR_HEIGHT;
	RenderString(16, TextPos, GlobalInvocationID);
	TextPos.y += CHAR_HEIGHT;
	RenderString(32, TextPos, GlobalInvocationID);
	TextPos.y += CHAR_HEIGHT;
	RenderString(48, TextPos, GlobalInvocationID);
	TextPos.y += CHAR_HEIGHT;
	RenderString(64, TextPos, GlobalInvocationID);
	TextPos.y += CHAR_HEIGHT;
	RenderString(80, TextPos, GlobalInvocationID);
	TextPos.y += CHAR_HEIGHT;
	RenderString(96, TextPos, GlobalInvocationID);
	TextPos.y += CHAR_HEIGHT;
	RenderString(112, TextPos, GlobalInvocationID);
#endif
}
