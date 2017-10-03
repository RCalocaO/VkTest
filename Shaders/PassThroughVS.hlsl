void MainVS(in uint VertexID : SV_VertexID, out float2 OutUVs : TEXCOORD0, out float4 Position : SV_Position)
{
	Position.x = (float)(VertexID / 2) * 4.0 - 1.0;
	Position.y = (float)(VertexID % 2) * 4.0 - 1.0;
	Position.zw = float2(0, 1);
	OutUVs.x = float2(VertexID / 2) * 2.0;
	OutUVs.y = float2(VertexID % 2) * 2.0;
}
