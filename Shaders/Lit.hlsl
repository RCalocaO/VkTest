cbuffer ViewUB : register(b0)
{
	float4x4 ViewMtx;
	float4x4 ProjectionMtx;
};

cbuffer ObjUB : register(b1)
{
	float4x4 ObjMtx;
	float4 Tint;
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
	float3 Normal : NORMAL;
};


cbuffer DataUB : register(b2)
{
float3 baseColor = float3(0.82f, 0.67f, 0.16f);
float metallic = 0;// 0 1
float subsurface = 0;// 0 1
float specular = 0.5;// 0 1
float roughness = 0.5;// 0 1 
float specularTint = 0; // 0 1
float anisotropic = 0; //0 1
float sheen = 0;// 0 1
float sheenTint = 0.5;// 0 1
float clearcoat = 0;// 0 1
float clearcoatGloss = 1;// 0 1
};


const float PI = 3.14159265358979323846;

float sqr(float x) { return x*x; }

float SchlickFresnel(float u)
{
	float m = clamp(1-u, 0, 1);
	float m2 = m*m;
	return m2*m2*m; // pow(m,5)
}

float GTR1(float NdotH, float a)
{
	if (a >= 1) return 1/PI;
	float a2 = a*a;
	float t = 1 + (a2-1)*NdotH*NdotH;
	return (a2-1) / (PI*log(a2)*t);
}

float GTR2(float NdotH, float a)
{
	float a2 = a*a;
	float t = 1 + (a2-1)*NdotH*NdotH;
	return a2 / (PI * t*t);
}

float GTR2_aniso(float NdotH, float HdotX, float HdotY, float ax, float ay)
{
	return 1 / (PI * ax*ay * sqr( sqr(HdotX/ax) + sqr(HdotY/ay) + NdotH*NdotH ));
}

float smithG_GGX(float NdotV, float alphaG)
{
	float a = alphaG*alphaG;
	float b = NdotV*NdotV;
	return 1 / (NdotV + sqrt(a + b - a*b));
}

float smithG_GGX_aniso(float NdotV, float VdotX, float VdotY, float ax, float ay)
{
	return 1 / (NdotV + sqrt( sqr(VdotX*ax) + sqr(VdotY*ay) + sqr(NdotV) ));
}

float3 mon2lin(float3 x)
{
	return float3(pow(x[0], 2.2), pow(x[1], 2.2), pow(x[2], 2.2));
}


float3 BRDF( float3 L, float3 V, float3 N, float3 X, float3 Y )
{
	float NdotL = dot(N,L);
	float NdotV = dot(N,V);
	if (NdotL < 0 || NdotV < 0) return float3(0);

	float3 H = normalize(L+V);
	float NdotH = dot(N,H);
	float LdotH = dot(L,H);

	float3 Cdlin = mon2lin(baseColor);
	float Cdlum = .3*Cdlin[0] + .6*Cdlin[1]  + .1*Cdlin[2]; // luminance approx.

	float3 Ctint = Cdlum > 0 ? Cdlin/Cdlum : float3(1); // normalize lum. to isolate hue+sat
	float3 Cspec0 = lerp(specular*.08*lerp(float3(1), Ctint, specularTint), Cdlin, metallic);
	float3 Csheen = lerp(float3(1), Ctint, sheenTint);

	// Diffuse fresnel - go from 1 at normal incidence to .5 at grazing
	// and mix in diffuse retro-reflection based on roughness
	float FL = SchlickFresnel(NdotL), FV = SchlickFresnel(NdotV);
	float Fd90 = 0.5 + 2 * LdotH*LdotH * roughness;
	float Fd = lerp(1.0, Fd90, FL) * lerp(1.0, Fd90, FV);

	// Based on Hanrahan-Krueger brdf approximation of isotropic bssrdf
	// 1.25 scale is used to (roughly) preserve albedo
	// Fss90 used to "flatten" retroreflection based on roughness
	float Fss90 = LdotH*LdotH*roughness;
	float Fss = lerp(1.0, Fss90, FL) * lerp(1.0, Fss90, FV);
	float ss = 1.25 * (Fss * (1 / (NdotL + NdotV) - .5) + .5);

	// specular
	float aspect = sqrt(1-anisotropic*.9);
	float ax = max(.001, sqr(roughness)/aspect);
	float ay = max(.001, sqr(roughness)*aspect);
	float Ds = GTR2_aniso(NdotH, dot(H, X), dot(H, Y), ax, ay);
	float FH = SchlickFresnel(LdotH);
	float3 Fs = lerp(Cspec0, float3(1), FH);
	float Gs;
	Gs  = smithG_GGX_aniso(NdotL, dot(L, X), dot(L, Y), ax, ay);
	Gs *= smithG_GGX_aniso(NdotV, dot(V, X), dot(V, Y), ax, ay);

	// sheen
	float3 Fsheen = FH * sheen * Csheen;

	// clearcoat (ior = 1.5 -> F0 = 0.04)
	float Dr = GTR1(NdotH, lerp(.1,.001,clearcoatGloss));
	float Fr = lerp(.04, 1.0, FH);
	float Gr = smithG_GGX(NdotL, .25) * smithG_GGX(NdotV, .25);

	return ((1/PI) * lerp(Fd, ss, subsurface)*Cdlin + Fsheen)
		* (1-metallic)
		+ Gs*Fs*Ds + .25*clearcoat*Gr*Fr*Dr;
}


FVSOut MainVS(FVSIn In)
{
	FVSOut Out;
	float4 Position = mul(ObjMtx, float4(In.Position.xyz, 1.0));
	Position = mul(ViewMtx, Position);

	Out.Normal = normalize(Position.xyz);

	Out.UVs = In.UVs;
	Out.Color = In.Color * Tint;
	Out.Pos = mul(ProjectionMtx, Position);
	return Out;
}

SamplerState SS : register(s3);
Texture2D Tex : register(t4);

float4 MainPS(FVSOut In)
{
	float3 L = float3(0, 0, -1);
	float3 V = -ViewMtx[3].xyz;
	float3 N = In.Normal;
	float3 X = float3(1, 0, 0);
	float3 Y = float3(0, 1, 0);
	return float4(
		0, BRDF(L, V, N, X, Y).xy,
		1);
}
