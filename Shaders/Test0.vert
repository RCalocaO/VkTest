#version 420

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

/*
float4 Main(float4 Pos : POSITION) : SV_POSITION
{
	return Pos;
}
*/

layout (binding = 0, set=0) uniform UB
{
	mat4 ProjectionMtx;
} VSUB;


layout (location = 0) in vec3 Position;
layout (location = 1) in vec4 InColor;

layout (location = 1) out vec4 Color;

void main()
{
	vec4 Mat0 = vec4(0.8823998, 0.0, 0.0, 0.0);
	vec4 Mat1 = vec4(0.0, 1.73205090, 0.0, 0.0);
	vec4 Mat2 = vec4(0.0, 0.0, -1.0001, -1.0);
	vec4 Mat3 = vec4(0.0, 0.0, -0.10001, 0.0);

	mat4 Mat = mat4(Mat0, Mat1, Mat2, Mat3);
	gl_Position = Mat * vec4(Position.xyz, 1.0);

	Color = InColor;
}
