void MainVS(in uint VertexID : SV_VertexID, out float2 OutUVs : TEXCOORD0, out float4 Position : SV_Position)
{
	OutUVs = float2((VertexID << 1) & 2, VertexID & 2);
	Position = float4(OutUVs * 2.0f + -1.0f, 0.0f, 1.0f);
}
