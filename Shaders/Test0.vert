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
#if 0
	vec4 Mat0 = vec4(0.938194156, 0.0, 0.0, 0.0);
	vec4 Mat1 = vec4(0.0, 1.73205090, 0.0, 0.0);
	vec4 Mat2 = vec4(0.0, 0.0, -1.00039077, -1.0);
	vec4 Mat3 = vec4(0.0, 0.0, -0.100039080, 0.0);
#else
	vec4 Mat0 = vec4(0.938194156, 0.0, 0.0, 0.0);
	vec4 Mat1 = vec4(0.0, 1.73205090, 0.0, 0.0);
	vec4 Mat2 = vec4(0.0, 0.0, -1.00039077, -0.100039080);
	vec4 Mat3 = vec4(0.0, 0.0, -1.0, 0.0);
#endif

	gl_Position.x = dot(Mat0, vec4(Position.xyz, 1.0));
	gl_Position.y = dot(Mat1, vec4(Position.xyz, 1.0));
	gl_Position.z = dot(Mat2, vec4(Position.xyz, 1.0));
	gl_Position.w = dot(Mat3, vec4(Position.xyz, 1.0));
	Color = InColor;
}
