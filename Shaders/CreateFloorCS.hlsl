

RWBuffer<int> OutIndices;

struct FPosColorUVVertex
{
	float x, y, z;
	uint Color;
	float u, v;
};

RWStructuredBuffer<FPosColorUVVertex> OutVertices;

cbuffer UB
{
	float Y;
	float Extent;
	int NumQuadsX;
	int NumQuadsZ;
	float Elevation;
}

SamplerState SS;
Texture2D<float4> Heightmap;

[numthreads(1, 1, 1)]
void Main(int3 GlobalInvocationID : SV_DispatchThreadID)
{
	int QuadIndexX = int(GlobalInvocationID.x);
	int QuadIndexZ = int(GlobalInvocationID.z);
	int QuadIndex = QuadIndexX + QuadIndexZ * NumQuadsX;

	if (QuadIndexX < NumQuadsX && QuadIndexZ < NumQuadsZ)
	{
		float Width = 2.0 * Extent;
		float WidthPerQuadX = Width / float(NumQuadsX);
		float WidthPerQuadZ = Width / float(NumQuadsZ);
		float X = -Extent + WidthPerQuadX * float(QuadIndexX);
		float Z = -Extent + WidthPerQuadZ * float(QuadIndexZ);

		float UWidth = 1.0 / float(NumQuadsX);
		float U = UWidth * float(QuadIndexX);
		float VWidth = 1.0 / float(NumQuadsZ);
		float V = VWidth * float(QuadIndexZ);

		float Height = Heightmap.Sample(SS, float2(U, V)).x;
		float Y = Y + Elevation * Height;

		OutVertices[QuadIndex].x = X;
		OutVertices[QuadIndex].y = Y;
		OutVertices[QuadIndex].z = Z;
		OutVertices[QuadIndex].Color = QuadIndexX * 65536 + QuadIndexZ;
		OutVertices[QuadIndex].u = U;
		OutVertices[QuadIndex].v = V;
		if (QuadIndexX != 0 && QuadIndexZ != 0)
		{
			OutIndices[(QuadIndexX - 1 + NumQuadsX * (QuadIndexZ - 1)) * 6 + 0] = NumQuadsX * QuadIndexZ + QuadIndexX;
			OutIndices[(QuadIndexX - 1 + NumQuadsX * (QuadIndexZ - 1)) * 6 + 1] = NumQuadsX * QuadIndexZ + QuadIndexX - 1;
			OutIndices[(QuadIndexX - 1 + NumQuadsX * (QuadIndexZ - 1)) * 6 + 2] = NumQuadsX * (QuadIndexZ - 1 ) + QuadIndexX - 1;
			OutIndices[(QuadIndexX - 1 + NumQuadsX * (QuadIndexZ - 1)) * 6 + 3] = NumQuadsX * (QuadIndexZ - 1 ) + QuadIndexX - 1;
			OutIndices[(QuadIndexX - 1 + NumQuadsX * (QuadIndexZ - 1)) * 6 + 4] = NumQuadsX * (QuadIndexZ - 1) + QuadIndexX;
			OutIndices[(QuadIndexX - 1 + NumQuadsX * (QuadIndexZ - 1)) * 6 + 5] = NumQuadsX * QuadIndexZ + QuadIndexX;
		}
	}
}
