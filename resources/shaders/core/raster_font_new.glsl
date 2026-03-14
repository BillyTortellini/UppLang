#ifdef VERTEX

in vec2 a_pos;   //@Position2D
in vec2 a_uv;    //@TextureCoordinates
in uint a_color; //@RGBA_Int

out vec2 f_uv;
out flat uint f_color;

void main() 
{
	gl_Position = vec4(a_pos, 0, 1);
	f_uv = a_uv;
	f_color = a_color;
	// if (a_color == 0) {
	// 	f_color = vec4(1, 0, 0, 1);
	// }
	// else {
	// 	f_color = vec4(0, 1, 0, 1);
	// }
}

#endif

#ifdef FRAGMENT

uniform sampler2D sampler;

in vec2 f_uv;
in flat uint f_color;
out vec4 o_color;

void main() 
{
	float alpha = texture(sampler, f_uv).r;
	// The 1.8 is pretty random, if we would do this completely correct, we would
	// need to do alpha-blending in linear colors, and then have gamma-correction afterwards on the whole image
	alpha = pow(alpha, 1 / 1.8);

	vec4 color = vec4(
		float((f_color >> 24) & 0xFF),
		float((f_color >> 16) & 0xFF),
		float((f_color >>  8) & 0xFF),
		float((f_color >>  0) & 0xFF)
	) / 255.0;

	o_color = vec4(color.xyz, alpha * color.w);
}


#endif
