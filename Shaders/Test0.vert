#version 420
/*
float4 Main(float4 Pos : POSITION) : SV_POSITION
{
	return Pos;
}
*/

in vec4 position;

out gl_PerVertex {
    vec4  gl_Position;
};

void main()
{
	gl_Position = position;
}
