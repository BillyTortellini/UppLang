#ifdef VERTEX_SHADER

layout (location = 1) in vec2 attribute_position;
layout (location = 2) in vec2 texture_coordinates;

out vec2 uv_coords;

void main() 
{
	gl_Position = vec4(attribute_position, 0, 1);
	uv_coords = texture_coordinates;
}

#endif

#ifdef FRAGMENT_SHADER

uniform sampler2D sampler;

in vec2 uv_coords;
out vec4 output_color;

void main() 
{
	float alpha = texture(sampler, uv_coords).r;
	output_color = vec4(vec3(1.0), alpha);
}


#endif
