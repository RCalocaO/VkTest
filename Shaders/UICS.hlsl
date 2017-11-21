
RWTexture2D<float4> RWImage : register(u1);

void DrawChar(uint Char[12], uint2 XY, uint2 Delta)
{
		uint BitRow = Char[Delta.y];
		uint ColMax = 1 << Delta.x;
		if (ColMax & BitRow)
		{
			float4 Color = float4(1,0,0,1);
			RWImage[XY] = Color;
		}
}

[numthreads(8,8,1)]
void Main(int3 GlobalInvocationID : SV_DispatchThreadID)
{
	uint A[12]=
	{
		0x0180, // .......11.......
		0x0240, // ......1..1......
		0x0660, // .....11..11.....
		0x0C30, // ....11....11....
		0x0810, // ....1......1....
		0x1818, // ...11......11...
		0x1008, // ...1........1...
		0x1FF8, // ...1111111111...
		0x1008, // ...1........1...
		0x1008, // ...1........1...
		0x300C, // ..11........11..
		0x300C, // ..11........11..
	};
	uint2 CharSize = uint2(16, 12);

	uint2 TextPos = uint2(160, 100);

	int2 Delta = GlobalInvocationID - TextPos;
	if (Delta.x >= 0 && Delta.x <= CharSize.x
		&& Delta.y >= 0 && Delta.y <= CharSize.y)
	{
		DrawChar(A,GlobalInvocationID, Delta);
	}
}
