
SamplerState SS;
Texture2D<float4> InTexture;

float4 Main(float2 UV) : SV_TARGET0
{
	float4 C0 = InTexture.Sample(SS, UV);
	return C0;
}
