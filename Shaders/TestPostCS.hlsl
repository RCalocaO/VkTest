
Texture2D<float4> InImage : register(u0);
RWTexture2D<float4> RWImage : register(u1);

[numthreads(8,8,1)]
void Main(int3 GlobalInvocationID : SV_DispatchThreadID)
{
	if (
	((GlobalInvocationID.y % 4) != 0)
	)
	{
		float4 Color0 = InImage.Load(int3(GlobalInvocationID.xy + int2(-1, -1), 0)) * 0.2;
		float4 Color1 = InImage.Load(int3(GlobalInvocationID.xy + int2(-1, 1), 0)) * 0.2;
		float4 Color2 = InImage.Load(int3(GlobalInvocationID.xy + int2(0, 0), 0)) * 0.2;
		float4 Color3 = InImage.Load(int3(GlobalInvocationID.xy + int2(1, 1), 0)) * 0.2;
		float4 Color4 = InImage.Load(int3(GlobalInvocationID.xy + int2(1, -1), 0)) * 0.2;
		float4 Color = Color0 + Color1 + Color2 + Color3 + Color4;
		RWImage[int2(GlobalInvocationID.xy)] = Color;
	}
	else
	{
		float4 Color = 0;
		RWImage[int2(GlobalInvocationID.xy)] = Color;
	}
}
