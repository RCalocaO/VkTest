#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

/*
float4 main() : SV_Target0
{
	return float4(1,0,0,1);
}
*/

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec4 ColorIn;
layout (location = 1) in vec2 UVs;

layout (location = 0) out vec4 ColorOut;

layout(set = 0, binding = 2) uniform sampler2D Texture;

void main()
{
	ColorOut = texture(Texture, UVs);// * ColorIn;
}
