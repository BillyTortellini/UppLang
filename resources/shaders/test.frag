#version 430

uniform float time;
uniform float aspect_ratio;
uniform mat4 view_matrix;
uniform vec3 camera_position;

in vec2 uv_coords;
out vec4 output_color;

#define MAX_ITERATIONS 1000
#define MAX_DISTANCE 1000.0
#define STOP_DISTANCE 0.0001

float sdf_sphere(vec3 position, vec3 sphere_position, float radius) {
	return distance(position, sphere_position) - radius;
}

float sdf_plane(vec3 position, vec3 plane_normal, float dist) {
	return dot(position, plane_normal) - dist;
}

float sdf_scene(vec3 position) {
	float d = sdf_sphere(position, vec3(0.0), 0.5);
	d = min(d, sdf_sphere(position, vec3(0.0, 1.0, 1.0), 0.2));
	float platform_sdf = max(sdf_plane(position, vec3(0.0, 1.0, 0.0), 0.0), sdf_sphere(position, vec3(0.0), 1.0));
	d = min(d, platform_sdf);
	return d;
}

int scene_material_index(vec3 position) 
{
	int index = 0;
	float d = sdf_sphere(position, vec3(0.0), 0.5);
	if (d > MAX_DISTANCE) {
		return 0; // Out of scene is material 0
	}
	index = 1;
	float nd = sdf_sphere(position, vec3(0.0, 1.0, 1.0), 0.2);
	if (nd < d) {
		d = nd;
		index = 2;
	}
	nd = max(sdf_plane(position, vec3(0.0, 1.0, 0.0), 0.0), sdf_sphere(position, vec3(0.0), 1.0));
	if (nd < d) {
		d = nd;
		index = 3;
	}
	return index;
}

#define DISTANCE_FOR_NORMAL 0.0001
vec3 scene_normal(vec3 position) {
	float dist = sdf_scene(position);
	return normalize(vec3(
		(dist - sdf_scene(position - vec3(DISTANCE_FOR_NORMAL, 0, 0))),
		(dist - sdf_scene(position - vec3(0, DISTANCE_FOR_NORMAL, 0))),
		(dist - sdf_scene(position - vec3(0, 0, DISTANCE_FOR_NORMAL)))
		)/DISTANCE_FOR_NORMAL);
}

float march_ray(vec3 origin, vec3 direction)
{
	float t = 0.0;
	for (int i = 0; i < MAX_ITERATIONS; i++)
	{
		vec3 position = origin + direction*t;
		float distance_to_scene = sdf_scene(position);
		if (distance_to_scene < STOP_DISTANCE) {
			return t;
		}
		if (distance_to_scene > MAX_DISTANCE) {
			break;
		}
		t += distance_to_scene;
	}
	return MAX_DISTANCE * 1000.0;
}

void main() 
{
	vec2 pos = uv_coords * 2.0 - 1.0;
	pos.x *= aspect_ratio;
	float inside = smoothstep(0.99, 0.98, length(pos));

	// Calculate ray origin
	vec3 origin = camera_position;
	vec3 view_dir = transpose(mat3(view_matrix)) * normalize(vec3(0.0, 0.0, -1.0) + vec3(pos.x, pos.y, 0.0));
	//vec3 origin = view_projection * vec3(0);

	float t = march_ray(origin, view_dir);
	vec3 red = vec3(1.0, 0.0, 0.0);
	vec3 green = vec3(0.0, 1.0, 0.0);
	vec3 blue = vec3(0.0, 0.0, 1.0);
	vec3 white = vec3(1.0);
	vec3 black = vec3(0.0);
	vec3 result_color;
	int material_index = scene_material_index(origin + view_dir * t);
	if (material_index == 0) { // Scene not hit
		result_color = black; 
	}
	else if (material_index == 1) { // Hit first ball
		result_color = scene_normal(origin + view_dir * t) * 0.5 + 0.5;
	}
	else if (material_index == 2){ // Hit second ball
		result_color = green + red;
	}
	else if (material_index == 3) {
		result_color = white * 0.3;
	}
	output_color = vec4(result_color, 1.0);	
}
