#version 430

uniform sampler2D sampler;

in vec2 uv_coords;
in float pixel_ratio;
in vec3 frag_color;

out vec4 output_color;

void main() 
{
	float alpha = texture(sampler, uv_coords).r;
	alpha = alpha * pixel_ratio;

	float s = 1.0;
	alpha = smoothstep(-s, s, alpha);

	output_color = vec4(frag_color, alpha);
}

