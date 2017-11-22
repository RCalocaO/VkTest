
StructuredBuffer<uint> FontBuffer : register(u0);
RWTexture2D<float4> RWImage : register(u1);

void DrawChar(uint2 CharSize, uint CharOffset, uint2 XY, uint2 Delta)
{
		uint BitRow = FontBuffer[CharOffset + Delta.y];
		if (BitRow != 0)
		{
			uint ColMax = 1 << (CharSize.x - Delta.x);
			if (ColMax & BitRow)
			{
				float4 Color = float4(1,0,0,1);
				RWImage[XY] = Color;
			}
		}
}

void RenderString(uint2 CharStartIndex, uint2 TextPos, uint2 CharSize, uint2 GlobalInvocationID)
{
	int2 Delta = GlobalInvocationID - TextPos;
	if (Delta.y >= 0 && Delta.y < CharSize.y)
	{
		uint TextLength = 16;
		if (Delta.x >= 0 && Delta.x < TextLength * CharSize.x )
		{
			uint CharIndex = Delta.x / CharSize.x;
			uint Char = CharStartIndex + CharIndex;
			uint CharOffset = Char * CharSize.y;
			Delta.x = Delta.x % CharSize.x;
			DrawChar(CharSize, CharOffset, GlobalInvocationID, Delta);
		}
	}
}

[numthreads(8,8,1)]
void Main(int3 GlobalInvocationID : SV_DispatchThreadID)
{
	uint2 CharSize = uint2(16, 32);

	uint2 TextPos = uint2(0, 100);
	RenderString(0, TextPos, CharSize, GlobalInvocationID);
	TextPos.y += CharSize.y;
	RenderString(16, TextPos, CharSize, GlobalInvocationID);
	TextPos.y += CharSize.y;
	RenderString(32, TextPos, CharSize, GlobalInvocationID);
	TextPos.y += CharSize.y;
	RenderString(48, TextPos, CharSize, GlobalInvocationID);
	TextPos.y += CharSize.y;
	RenderString(64, TextPos, CharSize, GlobalInvocationID);
	TextPos.y += CharSize.y;
	RenderString(80, TextPos, CharSize, GlobalInvocationID);
	TextPos.y += CharSize.y;
	RenderString(96, TextPos, CharSize, GlobalInvocationID);
	TextPos.y += CharSize.y;
	RenderString(112, TextPos, CharSize, GlobalInvocationID);
}
