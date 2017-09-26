
RWTexture2D<float4> RWImage;
//layout (binding = 0, rgba8) uniform image2D RWImage;

[numthreads(8,8,1)]
void Main(int3 GlobalInvocationID : SV_DispatchThreadID)
{
	//ivec2 Size = imageSize(RWImage);
	int NumSquares = 8;
	uint2 Block = GlobalInvocationID.xy / uint2(NumSquares, NumSquares);
	uint BlockIndex = Block.y * NumSquares + Block.x;
	BlockIndex += Block.y & 1;
	float4 Color = float4(1.0, 1.0, 1.0, 1.0);
	if ((BlockIndex & 1) == 0)
	{
		Color = float4(0.0, 0.0, 0.0, 1.0);
	}
	//Color.xy = vec2(gl_GlobalInvocationID.xy) / vec2(imageSize(RWImage).xy);
	RWImage[int2(GlobalInvocationID.xy)] = Color;
}
