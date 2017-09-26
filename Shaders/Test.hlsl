cbuffer ViewUB
{
	float4x4 ViewMtx;
	float4x4 ProjectionMtx;
};
cbuffer ObjUB
{
	float4x4 ObjMtx;
};

struct FVSIn
{
	float3 Position : POSITION;
	float4 Color : COLOR;
	float2 UVs : TEXCOORD0;
};

struct FVSOut
{
	float4 Pos : SV_POSITION;
	float4 Color : COLOR;
	float2 UVs : TEXCOORD0;
};

FVSOut MainVS(FVSIn In)
{
	FVSOut Out;
	float4 Position = ObjMtx * float4(In.Position.xyz, 1.0);
	Position = ViewMtx * Position;

	Out.UVs = In.UVs;

	Out.Color = In.Color;

	Out.Pos = ProjectionMtx * Position;
	return Out;
}

SamplerState SS;
Texture2D Tex;

float4 MainPS(FVSOut In)
{
	return Tex.Sample(SS, In.UVs);// * ColorIn;
}
