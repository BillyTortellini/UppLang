#ifdef VERTEX_SHADER

layout (location = 1) in vec2 a_position;
layout (location = 2) in vec2 a_uvs;
layout (location = 9) in vec3 a_color;
layout (location = 11) in float a_pixel_size;

out vec2 uv_coords;
out float pixel_ratio;
out vec3 frag_color;

void main() 
{
	gl_Position = vec4(a_position, 0, 1);
	uv_coords = a_uvs;
	pixel_ratio = a_pixel_size;
	frag_color = a_color;
}

#endif

#ifdef FRAGMENT_SHADER

uniform sampler2D sampler;

in vec2 uv_coords;
in float pixel_ratio;
in vec3 frag_color;

out vec4 output_color;

void main() 
{
	/* 
		When the text gets small, text gets harder to read since alpha gets smaller
		Some methods to better this would be:
			* Better (More Precise) calculation of distance field
			* Higher resolution distance fields
			* Change alpha like it is currently done (Similar to gamma correction)
			* Inflate the text when it is smaller so it is more readable
	*/
	float alpha = texture(sampler, uv_coords).r;
	alpha = alpha * pixel_ratio;
	float s = 1.0; // Actually, 0.5 looks better on very small text, since on smoll texts there should not be a lot of alpha
	//s = 1.5;
	float bloat = -1.0 * pixel_ratio;
	bloat = 0.0;
	alpha = smoothstep(-s + bloat, s + bloat, alpha);
	alpha = pow(alpha, 1.0/2.2);
	output_color = vec4(frag_color, alpha);
}

#endif
