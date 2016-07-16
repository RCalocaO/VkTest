#version 420

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

/*
float4 Main(float4 Pos : POSITION) : SV_POSITION
{
	return Pos;
}
*/

layout (location = 0) in vec3 Position;
layout (location = 1) in vec4 InColor;

layout (location = 1) out vec4 Color;

void main()
{
	gl_Position = vec4(Position.xyz, 1.0);
	Color = InColor;
}
