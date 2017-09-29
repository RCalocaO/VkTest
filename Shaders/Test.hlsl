cbuffer ViewUB : register(b0)
{
	float4x4 ViewMtx;
	float4x4 ProjectionMtx;
};

cbuffer ObjUB : register(b1)
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
	float4 Position = mul(ObjMtx, float4(In.Position.xyz, 1.0));
	Position = mul(ViewMtx, Position);

	Out.UVs = In.UVs;

	Out.Color = In.Color;

	Out.Pos = mul(ProjectionMtx, Position);
	return Out;
}

SamplerState SS : register(s2);
Texture2D Tex : register(t3);

float4 MainPS(FVSOut In)
{
	return Tex.Sample(SS, In.UVs);// * ColorIn;
}
