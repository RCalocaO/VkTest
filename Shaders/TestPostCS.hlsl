
RWTexture2D<float4> InImage;
RWTexture2D<float4> RWImage;

[numthreads(8,8,1)]
void Main(int3 GlobalInvocationID : SV_DispatchThreadID)
{
	if (
	//((gl_GlobalInvocationID.x % 4) == 0) ||
	((GlobalInvocationID.y % 4) != 0)
	)
	{
		float4 Color0 = InImage[float2(GlobalInvocationID.xy) + int2(-1, -1)] * 0.2;
		float4 Color1 = InImage[float2(GlobalInvocationID.xy) + int2(-1, 1)] * 0.2;
		float4 Color2 = InImage[float2(GlobalInvocationID.xy) + int2(0, 0)] * 0.2;
		float4 Color3 = InImage[float2(GlobalInvocationID.xy) + int2(1, 1)] * 0.2;
		float4 Color4 = InImage[float2(GlobalInvocationID.xy) + int2(1, -1)] * 0.2;
		float4 Color = Color0 + Color1 + Color2 + Color3 + Color4;
		RWImage[int2(GlobalInvocationID.xy)] = Color;
	}
	else
	{
		float4 Color = 0;
		RWImage[int2(GlobalInvocationID.xy)] = Color;
	}
}
