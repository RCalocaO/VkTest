#version 420

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


layout (location = 0) in vec3 InPosition;
layout (location = 1) in vec4 InColor;
layout (location = 2) in vec2 InUVs;

layout (location = 0) out vec4 Color;
layout (location = 1) out vec2 UVs;

void main()
{
	vec4 ViewMat0 = vec4(1.0, 0.0, 0.0, 0.0);
	vec4 ViewMat1 = vec4(0.0, 1.0, 0.0, 0.0);
	vec4 ViewMat2 = vec4(0.0, 0.0, 1.0, 0.0);
	vec4 ViewMat3 = vec4(0.0, 0.0, -2.0, 1.0);

	mat4 ViewMat = mat4(ViewMat0, ViewMat1, ViewMat2, ViewMat3);
	//vec4 Position = ViewMat * vec4(InPosition.xyz, 1.0);
	vec4 Position = ViewUB.ViewMtx * vec4(InPosition.xyz, 1.0);

	vec4 ProjMat0 = vec4(0.8823998, 0.0, 0.0, 0.0);
	vec4 ProjMat1 = vec4(0.0, 1.73205090, 0.0, 0.0);
	vec4 ProjMat2 = vec4(0.0, 0.0, -1.0001, -1.0);
	vec4 ProjMat3 = vec4(0.0, 0.0, -0.10001, 0.0);

	mat4 ProjMat = mat4(ProjMat0, ProjMat1, ProjMat2, ProjMat3);
	//gl_Position = ProjMat * Position;
	gl_Position = ViewUB.ProjectionMtx * Position;
	UVs = InUVs;

	Color = InColor;
}
