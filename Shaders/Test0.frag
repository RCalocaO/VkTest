#version 420

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

/*
float4 main() : SV_Target0
{
	return float4(1,0,0,1);
}
*/

layout (location = 0) in vec4 ColorIn;

layout (location = 0) out vec4 ColorOut;

void main()
{
	ColorOut = ColorIn;
}
