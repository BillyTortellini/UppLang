#version 430 core

#ifdef VERTEX

in vec2 position; //@Position2D
out vec2 uv_coords;

void main() {
	gl_Position = vec4(position, 0.0, 1.0);
	uv_coords = position * 0.5 + 0.5;
}

#endif

#ifdef FRAGMENT

layout (std140, binding = 0) uniform Render_Information
{
    float backbuffer_width;
    float backbuffer_height;
    float monitor_dpi;
    float current_time_in_seconds;
} u_render_info;

layout (std140, binding = 1) uniform Camera
{
    mat4 view;
    mat4 inverse_view;
    mat4 projection;
    mat4 view_projection;

    vec4 camera_position; // Packed, w = 1.0
    vec4 camera_direction; // Packed, w = 1.0
    vec4 camera_up;        // Packed w = 1.0
    float near_distance;
    float far_distance;
    float field_of_view_x;
    float field_of_view_y;
} u_camera;

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
	float aspect_ratio = u_render_info.backbuffer_width / u_render_info.backbuffer_height;
	pos.x *= aspect_ratio;
	float inside = smoothstep(0.99, 0.98, length(pos));

	// Calculate ray origin
	vec3 origin = u_camera.camera_position.xyz;
	mat4 view = mat4(1.0);
	vec3 view_dir = transpose(mat3(u_camera.view)) * normalize(vec3(0.0, 0.0, -1.0) + vec3(pos.x, pos.y, 0.0));

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

#endif