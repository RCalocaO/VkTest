#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

/*
float4 Main(float4 Pos : POSITION) : SV_POSITION
{
	return Pos;
}
*/

layout (binding = 0, set = 0) uniform UB
{
	mat4 ViewMtx;
	mat4 ProjectionMtx;
} ViewUB;

layout (binding = 1, set = 0) uniform UB2
{
	mat4 ObjMtx;
} ObjectUB;

layout (location = 0) in vec3 InPosition;
layout (location = 1) in vec4 InColor;
layout (location = 2) in vec2 InUVs;

layout (location = 0) out vec4 Color;
layout (location = 1) out vec2 UVs;

void main()
{
	vec4 Position = ObjectUB.ObjMtx * vec4(InPosition.xyz, 1.0);
	Position = ViewUB.ViewMtx * Position;

	gl_Position = ViewUB.ProjectionMtx * Position;
	UVs = InUVs;

	Color = InColor;
}
