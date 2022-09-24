#version 430

uniform sampler2D sampler;

in vec2 uv_coords;
out vec4 output_color;

void main() 
{
	float alpha = texture(sampler, uv_coords).r;
	output_color = vec4(vec3(1.0), alpha);
}

