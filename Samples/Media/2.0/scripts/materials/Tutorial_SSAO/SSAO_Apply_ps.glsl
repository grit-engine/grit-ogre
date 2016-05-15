#version 330

uniform sampler2D ssaoTexture;
uniform sampler2D sceneTexture;

in block
{
	vec2 uv0;
} inPs;

out vec4 fragColour;

uniform float powerScale;

void main()
{
	float ssao = texture(ssaoTexture, inPs.uv0).x;

	ssao = clamp(pow(ssao, powerScale), 0.0, 1.0);

	vec4 col = texture(sceneTexture, inPs.uv0);
	fragColour = vec4( col.x*ssao, col.y*ssao, col.z*ssao, col.a );
}
