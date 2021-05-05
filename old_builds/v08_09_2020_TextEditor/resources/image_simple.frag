#version 430

uniform float time;
uniform sampler2D sampler;

in vec2 uv_coords;
out vec4 output_color;

void main() {
	float linear = texelFetch(sampler, ivec2(textureSize(sampler, 0) * uv_coords), 0).r;
	float intensity = texture(sampler, uv_coords).r;
	intensity = abs(intensity);
	//intensity = linear / 5.0;
	vec3 result = vec3(1.0-intensity);
	output_color = vec4(result, 1.0);
	/*
	if (color.r >= 0.0) {
		output_color = vec4(1, 0, 0, 1);
	}
	else {
		output_color = vec4(0, 1, 0, 1);
	}
	*/
	//output_color = vec4(uv_coords, 0.5, 1.0);
}
