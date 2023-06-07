#ifdef VERTEX

in vec2 a_position; //@Position2D
in vec2 a_uvs; //@TextureCoordinates
in vec3 a_color; //@Color3
in float a_pixel_size; //@Pixel_Size

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

#ifdef FRAGMENT

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
			* Better (More Precise) calculation of distance field (E.g. directly from Outline-Data instead of Outline -> Raster Image -> Distance-Field)
			* Higher resolution distance fields
			* Change alpha like it is currently done (Similar to gamma correction)
			* Inflate the text when it is smaller so it is more readable
	*/
	float alpha = texture(sampler, uv_coords).r;
	//output_color = vec4(uv_coords, 0.0, 1.0);

	alpha = alpha * pixel_ratio;
	float s = 1.0; // Actually, 0.5 looks better on very small text, since on smoll texts there should not be a lot of alpha
	alpha = smoothstep(-s, s, alpha);
	alpha = pow(alpha, 1.0/1.5);
	output_color = vec4(frag_color, alpha);
}

#endif
