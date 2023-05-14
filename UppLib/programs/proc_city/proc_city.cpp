#include "proc_city.hpp"

#include "../../win32/window.hpp"
#include "../../win32/timing.hpp"
#include "../../utility/file_listener.hpp"
#include "../../utility/file_io.hpp"
#include "../../rendering/rendering_core.hpp"
#include "../../rendering/cameras.hpp"
#include "../../rendering/camera_controllers.hpp"
#include "../../rendering/gpu_buffers.hpp"
#include "../../rendering/mesh_utils.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/shader_program.hpp"
#include "../../utility/bounding_box.hpp"
#include "../../rendering/renderer_2D.hpp"
#include "../../utility/gui.hpp"
#include "../../math/scalars.hpp"
#include "../../utility/random.hpp"

/*
    Next up:
     - Street 3D model generation
     - Draw street with one building model placed everywhere
     - Textures and better building generation --> City generation done

    * Street 3d generation
    * Simple textures
            ||
            ||
            \/
    We need this data (Street model + building models + building placements + building textures) in seperate file thing
    First full city rendering in slow
    Refactor so that we can compare approaches
    Do AZDO OpenGL rendering
*/

/* 
Procedural city generation:

Important parts:
    - Street Placement
    - Building Placement
    - Building Generation
    - Texture Generation

One parameter should be size of the city

First Idea:
    Create random points on 2d coordinate system
    - Start big street generation by picking one point
    - Grow stree in random direction, gravitate towards nearest point
    - Join street with point if it is close enough or it works out, otherwise terminate if the street goes out of visible size

TODO:
    - Currently the "main" street generations is not that great/does not exist
    - I dont want so much small street intersections to the main streets, they should be more like highways (Maybe)
    - I want small streets to not terminate after some time, but rather have them branch out a lot more...
    - I should set the size to something reasonable

    Procedural building generation (I want different models, not just cubes):
        - Floorplan
        - Textures
        - L-Systems

*/
Random g_random;

struct City_Vertex
{
    vec3 position;
    vec3 color;
};

City_Vertex city_vertex_make(vec3 pos, vec3 color) {
    City_Vertex result; result.position = pos; result.color = color; return result;
}

void street_generate_from_points(
    Array<vec2> street_positions,
    Dynamic_Array<City_Vertex>* vertex_buffer,
    Dynamic_Array<uint32>* index_buffer,
    float thickness)
{
    if (street_positions.size <= 2) {
        panic("Dont do that\n");
    }

    //dynamic_array_reset(vertex_buffer);
    //dynamic_array_reset(index_buffer);

    vec2 start_point = street_positions[0];
    {
        vec2 normal = vector_rotate_90_degree_counter_clockwise(vector_normalize(street_positions[1] - start_point));
        vec2 p0 = start_point + normal * thickness;
        vec2 p1 = start_point - normal * thickness;
        dynamic_array_push_back(vertex_buffer, city_vertex_make(vec3(p0.x, 0.0f, p0.y), vec3(0.3f)));
        dynamic_array_push_back(vertex_buffer, city_vertex_make(vec3(p1.x, 0.0f, p1.y), vec3(0.3f)));
    }
    for (int i = 1; i < street_positions.size; i++)
    {
        vec2 end_point = street_positions[i];
        vec2 start_end_dir = vector_normalize(end_point - start_point);
        vec2 next_point;
        if (i + 1 < street_positions.size) {
            next_point = street_positions[i + 1];
        }
        else {
            next_point = end_point + start_end_dir;
        }

        vec2 end_next_dir = vector_normalize(next_point - end_point);
        vec2 normal = vector_rotate_90_degree_counter_clockwise(vector_normalize(end_next_dir + start_end_dir));
        vec2 p0 = end_point + normal * thickness;
        vec2 p1 = end_point - normal * thickness;
        vec3 color = (i % 2 == 0) ? vec3(0.3f) : vec3(0.3f, 0.3f, 0.7f);
        dynamic_array_push_back(vertex_buffer, city_vertex_make(vec3(p0.x, 0.0f, p0.y), color));
        dynamic_array_push_back(vertex_buffer, city_vertex_make(vec3(p1.x, 0.0f, p1.y), color));
        dynamic_array_push_back(index_buffer, (uint32)vertex_buffer->size - 4);
        dynamic_array_push_back(index_buffer, (uint32)vertex_buffer->size - 3);
        dynamic_array_push_back(index_buffer, (uint32)vertex_buffer->size - 2);
        dynamic_array_push_back(index_buffer, (uint32)vertex_buffer->size - 3);
        dynamic_array_push_back(index_buffer, (uint32)vertex_buffer->size - 1);
        dynamic_array_push_back(index_buffer, (uint32)vertex_buffer->size - 2);

        start_point = end_point;
    }

    //mesh_gpu_data_update_index_buffer(street_mesh, dynamic_array_to_array(index_buffer), state);
    //gpu_buffer_update(&street_mesh->vertex_buffers[0].vertex_buffer, dynamic_array_to_bytes(vertex_buffer));
}

/*
void street_generate_hotspot_points(DynamicArray<vec2>* to_fill, int point_count, vec2 size, float min_distance)
{
    // Generate point_count points
    for (int i = 0; i < point_count; i++) {
        dynamic_array_push_back(to_fill, vec2(random_next_float(&g_random, ), random_next_float(&g_random, )) * size - size / 2.0f);
    }
    // Remove points that dont fullfill min_distance
    for (int i = 0; i < to_fill->size; i++) {
        for (int j = i + 1; j < to_fill->size; j++) {
            if (i == j) continue;
            if (vector_distance_between(to_fill->data[i], to_fill->data[j]) < min_distance) {
                dynamic_array_swap_remove(to_fill, j);
                j -= 1;
            }
        }
    }
}
*/

// What do i need?
/*
    Scalability:
        It does not need to be as fast as possible, it needs
        to be fast enough that it does not suck, and that it can generate
        a city so huge that the GPU melts

    2D Collision detection with
        Lines (Streets) and
        Rotated Boxes (Buildings)
        Not just overlap, but ray casting

    Final Algorithm:
        Place MAIN-Street randomly between hotspots
        Foreach MAIN-Street:
            Start growing smaller streets from main street
            Merge where appropriate (To be decided)
        Place Buildings next to roads

    Requirements:
        Main-Street placement: Ray casts between cities, split roads where there are intersections
        Same for smaller streets, ability to create splits in lines, raycasts
        House placement: Check if bounding box is occluded, if not place

    What to do next:
        Think about datastructure (for rays)
        Ray Intersection inside world
        Place mainstreet as straight line_index, and grow some smaller streets from it
        Place mainstreet better
        Add buildings
        Building generation
        Texture generation

    Make sure that streets do not overlap
*/

/*
    How to represent multiple lines (Splitting is required, and walking along edge is required)
    Simplest: Array<tuple<start, end>>
    Walking along lines requires neighborhood information/shared vertices:
        Adjecency matrix: Not hard but bad because the connections are sparse
        Adjecency list: For each point a list connecting them
        Vertex list and start_end list: Not so much memory manegement stuff? -> Easier to iterator over all lines, but not much different
    Just for testing the algorithm, i wont need an acceleration structure, just do the dumbest thing possible for now
*/

struct StreetLine
{
    int start;
    int end;
    bool main_road;
};

StreetLine streetline_make(int start, int end, bool main_road) {
    StreetLine result;
    result.start = start;
    result.end = end;
    result.main_road = main_road;
    return result;
}

struct StreetBranch
{
    int position_index;
    vec2 normal;
};

StreetBranch streetbranch_make(int index, vec2 normal) {
    StreetBranch result; result.position_index = index, result.normal = normal; return result;
}

struct StreetBuildingPlaceholder
{
    vec2 position;
    float radius;
    vec2 normal_to_street;
};

StreetBuildingPlaceholder street_buidling_placeholder_make(vec2 position, float radius, vec2 normal_to_street)
{
    StreetBuildingPlaceholder result;
    result.position = position;
    result.radius = radius;
    result.normal_to_street = normal_to_street;
    return result;
}

struct StreetNetwork
{
    Dynamic_Array<vec2> positions;
    Dynamic_Array<StreetLine> lines;
    Dynamic_Array<StreetBranch> open_branches;
    Dynamic_Array<StreetBuildingPlaceholder> buildings;
    Array<Dynamic_Array<int>> grid;
    float grid_width;
    int row_count;
};

StreetNetwork streetnetwork_create(float grid_width, int row_count) {
    StreetNetwork result;
    result.positions = dynamic_array_create_empty<vec2>(32);
    result.lines = dynamic_array_create_empty<StreetLine>(32);
    result.open_branches = dynamic_array_create_empty<StreetBranch>(32);
    result.buildings = dynamic_array_create_empty<StreetBuildingPlaceholder>(128);
    result.grid_width = grid_width;
    result.row_count = row_count;
    result.grid = array_create_empty<Dynamic_Array<int>>(row_count * row_count);
    for (int i = 0; i < row_count * row_count; i++) {
        result.grid[i] = dynamic_array_create_empty<int>(4);
    }
    return result;
}

void streetnetwork_destroy(StreetNetwork* network) {
    dynamic_array_destroy(&network->positions);
    dynamic_array_destroy(&network->lines);
    dynamic_array_destroy(&network->buildings);
    dynamic_array_destroy(&network->open_branches);
    for (int i = 0; i < network->row_count * network->row_count; i++) {
        dynamic_array_destroy(&network->grid[i]);
    }
    array_destroy(&network->grid);
}

void streetnetwork_place_buildings_random(StreetNetwork* network, float radius) {
    dynamic_array_reset(&network->buildings);
    for (int i = 0; i < network->lines.size; i++) {
        StreetLine& line_index = network->lines[i];
        vec2 a = network->positions[line_index.start];
        vec2 b = network->positions[line_index.end];
        vec2 normal = vector_normalize(vector_rotate_90_degree_clockwise(b - a));
        int building_count = vector_length(b - a) / (radius * 2.0f * 1.2f);
        for (int j = 0; j < building_count; j++) {
            float alpha = (float)j / building_count;
            vec2 pos = (1.0f - alpha) * a + alpha * b;
            if (random_next_bool(&g_random, 0.7f)) {
                dynamic_array_push_back(&network->buildings, street_buidling_placeholder_make(pos + normal * (radius * 1.2f + 0.1f), radius, -normal));
            }
            if (random_next_bool(&g_random, 0.7f)) {
                dynamic_array_push_back(&network->buildings, street_buidling_placeholder_make(pos - normal * (radius * 1.2f + 0.1f), radius, normal));
            }
            /*
            */
        }
    }
}

void streetnetwork_update_grid_size(StreetNetwork* network, float size, int row_count)
{
    for (int i = 0; i < network->row_count * network->row_count; i++) {
        dynamic_array_destroy(&network->grid[i]);
    }
    array_destroy(&network->grid);

    network->grid_width = size;
    network->row_count = row_count;
    network->grid = array_create_empty<Dynamic_Array<int>>(row_count * row_count);
    for (int i = 0; i < row_count * row_count; i++) {
        network->grid[i] = dynamic_array_create_empty<int>(4);
    }
}


int streetnetwork_get_nearest_point(StreetNetwork* network, vec2 point)
{
    float distance = 10000.0f;
    int nearest = 0;
    for (int i = 0; i < network->positions.size; i++) {
        vec2 p = network->positions[i];
        float d = vector_distance_between(p, point);
        if (d < distance) {
            nearest = i;
            distance = d;
        }
    }
    return nearest;
}

void streetnetwork_add_line_from_points(StreetNetwork* network, Array<vec2> points, bool main_road)
{
    int start_index = network->positions.size;
    for (int i = 0; i < points.size; i++) {
        dynamic_array_push_back(&network->positions, points[i]);
    }
    for (int i = 0; i < points.size - 1; i++) {
        dynamic_array_push_back(&network->lines, streetline_make(start_index + i, start_index + i + 1, main_road));
    }
}

Optional<float> line_segment_ray_intersection(vec2 a, vec2 b, vec2 o, vec2 d)
{
    float det = d.x * (a.y - b.y) - d.y * (a.x - b.x);
    if (math_absolute(det) < 0.001f) {
        return optional_make_failure<float>();
    }
    float alpha = d.x * (a.y - o.y) - d.y * (a.x - o.x);
    alpha = alpha / det;
    float t = ((a.x - o.x) * (a.y - b.y) - (a.y - o.y) * (a.x - b.x)) / det;
    if (alpha > (0.0f - 0.001f) && alpha < (1.0f + 0.001f) && t > 0.0f) {
        return optional_make_success(t);
    }
    return optional_make_failure<float>();
}

struct Intersection
{
    bool intersected;
    int line_index; // Intersected line_index index in associated network
    vec2 position;
    float t;
};

struct Streetnetwork_Grid_Line_Iterator
{
    vec2 origin;
    vec2 direction;
    float t;
    float max_t;
    int grid_x, grid_y;
};

Streetnetwork_Grid_Line_Iterator streetnetwork_grid_line_iterator_make(StreetNetwork* network, vec2 a, vec2 b)
{
    Bounding_Box2 grid_bb = bounding_box_2_make_center_size(vec2(0.0f), vec2(network->grid_width));
    if (!bounding_box_2_is_point_inside(grid_bb, a) || !bounding_box_2_is_point_inside(grid_bb, b))
        panic("Shit");

    Streetnetwork_Grid_Line_Iterator result;
    result.origin = a - grid_bb.min;
    result.direction = vector_normalize(b - a);
    result.t = 0;
    result.max_t = vector_length(b - a);
    result.grid_x = (int)(result.origin.x / network->grid_width * network->row_count);
    result.grid_y = (int)(result.origin.y / network->grid_width * network->row_count);
    return result;
}

bool streetnetwork_grid_line_iterator_has_next(Streetnetwork_Grid_Line_Iterator* iterator) {
    return iterator->t < iterator->max_t;
}

void streetnetwork_grid_line_iterator_step(Streetnetwork_Grid_Line_Iterator* iterator, StreetNetwork* network)
{
    vec2 p = iterator->origin + iterator->direction * iterator->t;
    Bounding_Box2 cell_bb;
    cell_bb.min = vec2(iterator->grid_x * (network->grid_width / network->row_count), iterator->grid_y * (network->grid_width / network->row_count));
    cell_bb.max = cell_bb.min + vec2(network->grid_width / network->row_count);

    float travel_x = iterator->direction.x >= 0.0f ? cell_bb.max.x - p.x : p.x - cell_bb.min.x;
    float travel_y = iterator->direction.y >= 0.0f ? cell_bb.max.y - p.y : p.y - cell_bb.min.y;
    float d_x = math_absolute(iterator->direction.x);
    float d_y = math_absolute(iterator->direction.y);
    if (travel_x* d_y < travel_y* d_x) {
        iterator->t += travel_x / d_x;
        iterator->grid_x += iterator->direction.x > 0.0f ? 1 : -1;
    }
    else {
        iterator->t += travel_y / d_y;
        iterator->grid_y += iterator->direction.y > 0.0f ? 1 : -1;
    }
}

int streetnetwork_grid_line_iterator_get_index(Streetnetwork_Grid_Line_Iterator* iterator, StreetNetwork* network) {
    return iterator->grid_x + network->row_count * iterator->grid_y;
}

void streetnetwork_grid_add_line(StreetNetwork* network, int line_index)
{
    StreetLine& line = network->lines[line_index];
    vec2& a = network->positions[line.start];
    vec2& b = network->positions[line.end];
    Streetnetwork_Grid_Line_Iterator iterator = streetnetwork_grid_line_iterator_make(network, a, b);
    while (streetnetwork_grid_line_iterator_has_next(&iterator)) {
        int grid_index = streetnetwork_grid_line_iterator_get_index(&iterator, network);
        dynamic_array_push_back(&network->grid[grid_index], line_index);
        streetnetwork_grid_line_iterator_step(&iterator, network);
    }
}

void streetnetwork_grid_remove_line(StreetNetwork* network, int line_index)
{
    StreetLine& line = network->lines[line_index];
    vec2& a = network->positions[line.start];
    vec2& b = network->positions[line.end];
    Streetnetwork_Grid_Line_Iterator iterator = streetnetwork_grid_line_iterator_make(network, a, b);
    while (streetnetwork_grid_line_iterator_has_next(&iterator)) {
        int grid_index = streetnetwork_grid_line_iterator_get_index(&iterator, network);
        for (int i = 0; i < network->grid[grid_index].size; i++) {
            if (network->grid[grid_index].data[i] == line_index) {
                dynamic_array_swap_remove(&network->grid[grid_index], i);
                break;
            }
        }
        streetnetwork_grid_line_iterator_step(&iterator, network);
    }
}

Intersection streetnetwork_grid_cast_ray(StreetNetwork* network, vec2 origin, vec2 direction, float min_distance, float max_distance)
{
    Intersection result;
    result.intersected = false;
    Streetnetwork_Grid_Line_Iterator iterator = streetnetwork_grid_line_iterator_make(network, origin + direction * min_distance, origin + direction * max_distance);
    while (streetnetwork_grid_line_iterator_has_next(&iterator))
    {
        int grid_index = streetnetwork_grid_line_iterator_get_index(&iterator, network);
        float found_min = 100000.0f;
        int intersection_index = -1;
        for (int i = 0; i < network->grid[grid_index].size; i++) {
            int line_index = network->grid[grid_index].data[i];
            StreetLine& line = network->lines[line_index];
            Optional<float> intersection = line_segment_ray_intersection(network->positions[line.start], network->positions[line.end], origin, direction);
            if (intersection.available) {
                float dist = intersection.value;
                if (dist < min_distance || dist > max_distance || dist < found_min) {
                    found_min = dist;
                    intersection_index = i;
                }
            }
        }
        if (intersection_index != -1) {
            result.intersected = true;
            result.line_index = intersection_index;
            result.t = found_min;
            result.position = origin + found_min * direction;
            return result;
        }
        streetnetwork_grid_line_iterator_step(&iterator, network);
    }
    return result;
}

Intersection streetnetwork_cast_ray(StreetNetwork* network, vec2 origin, vec2 direction, float min_distance, float max_distance)
{
    Intersection result;
    result.intersected = false;
    result.t = 10000.0f;
    for (int i = 0; i < network->lines.size; i++)
    {
        vec2 a = network->positions[network->lines[i].start];
        vec2 b = network->positions[network->lines[i].end];
        Optional<float> intersection = line_segment_ray_intersection(a, b, origin, direction);
        if (intersection.available) {
            float t = intersection.value;
            if (t > min_distance && t < max_distance && t < result.t) {
                result.t = t;
                result.intersected = true;
                result.line_index = i;
            }
        }
    }
    if (result.intersected) {
        result.position = origin + result.t * direction;
    }
    return result;
}

void streetnetwork_draw(StreetNetwork* network, Renderer_2D* renderer, vec2 center, float size)
{
    for (int i = 0; i < network->open_branches.size; i++) {
        vec2 pos = (network->positions[network->open_branches[i].position_index] - center) / size;
        renderer_2D_add_rectangle(renderer, pos, vec2(0.05f), vec3(0.0f, 1.0f, 0.0f), 0.0f);
    }
    for (int i = 0; i < network->buildings.size; i++) {
        StreetBuildingPlaceholder& building = network->buildings[i];
        renderer_2D_add_rectangle(renderer, building.position / size, vec2(building.radius) / size, vec3(0.0f, 1.0f, 0.3f), 0.0f);
    }
    for (int i = 0; i < network->lines.size; i++) {
        vec2 a = network->positions[network->lines[i].start] - center;
        vec2 b = network->positions[network->lines[i].end] - center;
        float thickness = network->lines[i].main_road ? 6.0f : 3.0f;
        renderer_2D_add_line(renderer, a / size, b / size, vec3(1.0f), thickness, 0.0f);
    }
}

/*
void streetnetwork_grow_branch(StreetNetwork* network, int start_point, vec2 direction, float length, int step)
{
    if (step < 0) return;

    // Alter directio by some random value
    direction = vector_normalize(direction + (vec2(random_next_float(), random_next_float()) - 0.5f) * 0.6f);
    vec2 origin = network->positions[start_point];
    Intersection intersection = streetnetwork_cast_ray(network, origin, direction, 0.05f, length * 1.3f);
    int end_index;
    float merge_percent = 0.25f;
    if (intersection.intersected)
    {
        if (intersection.t < length * 0.5f) { return; }
        StreetLine to_split = network->lines[intersection.line_index];
        vec2 middle = intersection.position;
        int nearest_index = streetnetwork_get_nearest_point(network, middle);
        if (vector_distance_between(network->positions[nearest_index], middle) < length * merge_percent) {
            end_index = nearest_index;
        }
        else {
            dynamic_array_swap_remove(&network->lines, intersection.line_index);
            dynamic_array_push_back(&network->positions, middle);
            dynamic_array_push_back(&network->lines, streetline_make(to_split.start, network->positions.size - 1, to_split.main_road));
            dynamic_array_push_back(&network->lines, streetline_make(network->positions.size - 1, to_split.end, to_split.main_road));
            end_index = network->positions.size - 1;
        }
    }
    else {
        // We need to add the end position
        vec2 end = origin + direction * length;
        int nearest_index = streetnetwork_get_nearest_point(network, end);
        float d = vector_distance_between(network->positions[nearest_index], end);
        if (d < length * merge_percent) {
            end_index = nearest_index;
        }
        else {
            dynamic_array_push_back(&network->positions, end);
            end_index = network->positions.size - 1;
        }
    }
    dynamic_array_push_back(&network->lines, streetline_make(start_point, end_index, false));
    if (!intersection.intersected) {
        vec2 next_dir = vector_normalize(network->positions[end_index] - network->positions[start_point] + (vec2(random_next_float(), random_next_float()) - 0.5f) * 0.5f);
        streetnetwork_grow_branch(network, end_index, next_dir, length * 0.8f, step - 1);
    }
}

void streetnetwork_grow_main_road(StreetNetwork* network, float split_distance, float branch_length)
{
    // Search for a position on one of the main road where we can branch off
    vec2 pos;
    bool found = false;
    int line_index;
    for (int i = 0; i < network->lines.size; i++) {
        StreetLine& line_index = network->lines[i];
        if (!line_index.main_road) continue;
        vec2 a = network->positions[line_index.start];
        vec2 b = network->positions[line_index.end];
        if (vector_distance_between(a, b) > split_distance) {
            found = true;
            line_index = i;
        }
    }
    if (!found) return;

    // Split line_index
    StreetLine line_index = network->lines[line_index];
    vec2 a = network->positions[line_index.start];
    vec2 b = network->positions[line_index.end];
    vec2 middle = (a + b) / 2.0f;
    dynamic_array_swap_remove(&network->lines, line_index);
    dynamic_array_push_back(&network->positions, middle);
    dynamic_array_push_back(&network->lines, streetline_make(line_index.start, network->positions.size - 1, true));
    dynamic_array_push_back(&network->lines, streetline_make(network->positions.size - 1, line_index.end, true));
    // Start growing smoll branches
    vec2 normal = vector_rotate_90_degree_clockwise(vector_normalize(b - a));
    int middle_index = network->positions.size - 1;
    streetnetwork_grow_branch(network, middle_index, normal, branch_length, 3);
    streetnetwork_grow_branch(network, middle_index, -normal, branch_length, 2);
}
*/

void streetnetwork_generate_seedpoints_for_branches(StreetNetwork* network, float split_distance, float fail_percentage)
{
    int line_count = network->lines.size;
    // Loop over all main roads, split them in smaller split distance regions, add branches left/right/both dependent on random
    for (int i = 0; i < line_count; i++)
    {
        vec2 a = network->positions[network->lines[i].start];
        vec2 b = network->positions[network->lines[i].end];
        vec2 normal = vector_rotate_90_degree_clockwise(vector_normalize(b - a));
        int line_index = i;

        // Calculate subdivisions
        float d = vector_distance_between(a, b);
        int subdiv_count = (int)(d / split_distance);
        for (int j = 1; j < subdiv_count - 1; j++)
        {
            if (random_next_bool(&g_random, fail_percentage)) continue;
            float alpha = (float)(j) / subdiv_count;
            vec2 pos = (1.0f - alpha) * a + (alpha)*b;
            dynamic_array_push_back(&network->positions, pos);
            dynamic_array_push_back(&network->lines, streetline_make(network->positions.size - 1, network->lines[line_index].end, true));
            network->lines[line_index].end = network->positions.size - 1; // Change end to our new position
            line_index = network->lines.size - 1;
            // Generate seeds
            if (random_next_bool(&g_random, 0.66f))
            {
                if (random_next_bool(&g_random, 0.5f)) { // left
                    dynamic_array_push_back(&network->open_branches, streetbranch_make(network->positions.size - 1, normal));
                }
                else { // right
                    dynamic_array_push_back(&network->open_branches, streetbranch_make(network->positions.size - 1, -normal));
                }
            }
            else { // Both
                dynamic_array_push_back(&network->open_branches, streetbranch_make(network->positions.size - 1, normal));
                dynamic_array_push_back(&network->open_branches, streetbranch_make(network->positions.size - 1, -normal));
            }
            /*
            */
        }
    }
}

Optional<int> streetnetwork_add_line_between_points_with_collision(StreetNetwork* network, int a_index, vec2 b, float merge_radius, bool main_road)
{
    vec2& a = network->positions[a_index];
    float d = vector_distance_between(a, b);
    Intersection intersection = streetnetwork_cast_ray(network, a, vector_normalize(b - a), 0.01f, d);
    if (intersection.intersected)
    {
        int nearest_index = streetnetwork_get_nearest_point(network, intersection.position);
        if (vector_distance_between(network->positions[nearest_index], intersection.position) < merge_radius) {
            dynamic_array_push_back(&network->lines, streetline_make(nearest_index, a_index, main_road));
            return optional_make_failure<int>();
        }

        // Split hit road
        StreetLine& to_split = network->lines[intersection.line_index];
        //dynamic_array_swap_remove(&network->lines, intersection.line_index);
        streetnetwork_grid_remove_line(network, intersection.line_index);
        dynamic_array_push_back(&network->positions, intersection.position);
        int end_index = network->positions.size - 1;
        dynamic_array_push_back(&network->lines, streetline_make(to_split.end, end_index, to_split.main_road));
        to_split.end = end_index;
        dynamic_array_push_back(&network->lines, streetline_make(a_index, end_index, main_road));
        streetnetwork_grid_add_line(network, intersection.line_index);
        streetnetwork_grid_add_line(network, network->lines.size - 1);
        streetnetwork_grid_add_line(network, network->lines.size - 2);
        return optional_make_failure<int>();
    }
    else {
        dynamic_array_push_back(&network->positions, b);
        dynamic_array_push_back(&network->lines, streetline_make(a_index, network->positions.size - 1, main_road));
        streetnetwork_grid_add_line(network, network->lines.size - 1);
        return optional_make_success(network->positions.size - 1);
    }
}

void streetnetwork_grow_branches(StreetNetwork* network, float dist, float destroy_start_radius, float max_radius)
{
    // Dont do something lame, do something cooler 
    /*
        Options:
            - Turn left and grow
            - Turn right and grow
            - Terminate
            - Split in normal direciton
            - Create all kinds of intersections
    */
    float turn_left_percentage = 0.3f;
    float turn_right_percentage = 0.3f;
    float terminate_percentage = 0.05f;
    float split_percentage = 0.2f;
    float t_junction_percentage = 0.1f;
    float sum = turn_left_percentage + turn_right_percentage + terminate_percentage + split_percentage + t_junction_percentage;

    int branch_count = network->open_branches.size;
    for (int i = 0; i < branch_count; i++)
    {
        StreetBranch branch = network->open_branches[i];
        vec2 a = network->positions[branch.position_index];
        // Do Termination
        {
            float d = vector_length(a);
            if (d > max_radius) {
                continue;
            }
            else if (d > destroy_start_radius)
            {
                float percent = ((d - destroy_start_radius) / (max_radius - destroy_start_radius));
                if (random_next_bool(&g_random, percent)) continue;
            }
        }
        float r = random_next_float(&g_random);
        if (r < (terminate_percentage) / sum) {
            //  Terminate
        }
        else if (r < (terminate_percentage + turn_left_percentage) / sum) {
            //else if (true) {
                // Turn left
            float angle = math_arctangent_2(branch.normal.y, branch.normal.x);
            angle = angle + (PI / 16.0f + random_next_float(&g_random) * PI / 16.0f);
            vec2 normal = vec2(math_cosine(angle), math_sine(angle));
            //normal = branch.normal;
            Optional<int> result = streetnetwork_add_line_between_points_with_collision(
                network, branch.position_index, a + normal * dist, dist / 0.8f, false);
            if (result.available) dynamic_array_push_back(&network->open_branches, streetbranch_make(result.value, normal));
        }
        else if (r < (terminate_percentage + turn_left_percentage + turn_right_percentage) / sum) {
            // Turn right
            float angle = math_arctangent_2(branch.normal.y, branch.normal.x);
            angle = angle - (PI / 16.0f + random_next_float(&g_random));
            vec2 normal = vec2(math_cosine(angle), math_sine(angle));
            Optional<int> result = streetnetwork_add_line_between_points_with_collision(
                network, branch.position_index, a + normal * dist, dist / 0.8f, false);
            if (result.available) dynamic_array_push_back(&network->open_branches, streetbranch_make(result.value, normal));
        }
        else if (r < (terminate_percentage + turn_left_percentage + turn_right_percentage + split_percentage)) {
            // Split
            float angle = math_arctangent_2(branch.normal.y, branch.normal.x);
            float angle1 = angle + (PI * 2.0f / 16.0f);
            float angle2 = angle - (PI * 2.0f / 16.0f);
            vec2 normal1 = vec2(math_cosine(angle1), math_sine(angle1));
            vec2 normal2 = vec2(math_cosine(angle2), math_sine(angle2));
            Optional<int> result = streetnetwork_add_line_between_points_with_collision(
                network, branch.position_index, a + normal1 * dist, dist / 0.8f, false);
            if (result.available) dynamic_array_push_back(&network->open_branches, streetbranch_make(result.value, normal1));
            result = streetnetwork_add_line_between_points_with_collision(
                network, branch.position_index, a + normal2 * dist, dist / 0.8f, false);
            if (result.available) dynamic_array_push_back(&network->open_branches, streetbranch_make(result.value, normal2));
        }
        else {
            // T_Junciton
            float angle = math_arctangent_2(branch.normal.y, branch.normal.x);
            float angle1 = angle + (PI / 2.0f);
            float angle2 = angle - (PI / 2.0f);
            vec2 normal1 = vec2(math_cosine(angle1), math_sine(angle1));
            vec2 normal2 = vec2(math_cosine(angle2), math_sine(angle2));
            Optional<int> result = streetnetwork_add_line_between_points_with_collision(
                network, branch.position_index, a + normal1 * dist, dist / 0.8f, false);
            if (result.available) dynamic_array_push_back(&network->open_branches, streetbranch_make(result.value, normal1));
            result = streetnetwork_add_line_between_points_with_collision(
                network, branch.position_index, a + normal2 * dist, dist / 0.8f, false);
            if (result.available) dynamic_array_push_back(&network->open_branches, streetbranch_make(result.value, normal2));
        }
    }

    dynamic_array_remove_range_ordered(&network->open_branches, 0, branch_count); // @Error this function changed since last build of proc_city
}

void streetnetwork_generate_main_road(StreetNetwork* network, vec2 size, int hotspot_count, float base_min_distance, int closest_count,
    float merge_radius)
{
    dynamic_array_reset(&network->lines);
    dynamic_array_reset(&network->open_branches);
    dynamic_array_reset(&network->positions);

    // Generate positions
    float radius = base_min_distance;
    int ring = 0;
    while (network->positions.size < hotspot_count && radius < vector_get_minimum_axis(size))
    {
        int step_count_per_radius = 100;
        float start_angle = random_next_float(&g_random) * PI * 2.0f;
        for (int i = 0; i < step_count_per_radius; i++)
        {
            float angle = PI * 2.0f * (i / (float)step_count_per_radius) + start_angle;
            vec2 pos = vec2(math_sine(angle), math_cosine(angle)) * (radius + random_next_float(&g_random) * base_min_distance);
            // Check if the min_distance is satisfied
            bool skip = false;
            for (int j = 0; j < network->positions.size; j++) {
                if (vector_distance_between(pos, network->positions[j]) < base_min_distance * (1.0f + 0.3 * ring)) {
                    skip = true;
                }
            }
            if (skip) continue;
            dynamic_array_push_back(&network->positions, pos);
        }
        radius += base_min_distance * (0.333f + 0.3f * ring);
        ring++;
    }

    // Create streets for x closest points
    Dynamic_Array<int> connected_indices = dynamic_array_create_empty<int>(16);
    SCOPE_EXIT(dynamic_array_destroy(&connected_indices));
    int position_count = network->positions.size;
    for (int i = 0; i < position_count; i++)
    {
        // Get all connections
        dynamic_array_reset(&connected_indices);
        for (int j = 0; j < network->lines.size; j++) {
            if (network->lines[j].end == i || network->lines[j].start == i) {
                dynamic_array_push_back(&connected_indices, network->lines[j].end == i ? network->lines[j].start : network->lines[j].end);
            }
        }

        while (connected_indices.size < closest_count)
        {
            // Search nearest not in indices
            float distance = 10000.0f;
            int nearest_index = -1;
            for (int j = 0; j < position_count; j++)
            {
                if (j == i) continue;
                bool skip = false;
                for (int k = 0; k < connected_indices.size; k++) {
                    if (connected_indices[k] == j) skip = true;
                }
                if (skip) continue;

                float d = vector_distance_between(network->positions[i], network->positions[j]);
                if (d < distance) {
                    distance = d;
                    nearest_index = j;
                }
            }

            streetnetwork_add_line_between_points_with_collision(network, i, network->positions[nearest_index], merge_radius, true);
            dynamic_array_push_back(&connected_indices, nearest_index);
        }
    }
}


/*
void street_generate_between_hotspots_random(
    DynamicArray<vec2>* hotspots,
    DynamicArray<City_Vertex>* vertex_buffer,
    DynamicArray<uint32>* index_buffer,
    float step_size,
    vec2 bounding_size,
    int max_step_size,
    float min_distance_to_hotspot,
    float thickness,
    int* start,
    OpenGLState* state)
{
    int start_index = random_next_int(&g_random, ) % hotspots->size;
    *start = start_index;
    vec2 current_point = hotspots->data[start_index];
    vec2 direction;
    {
        float angle = random_next_float(&g_random, ) * PI;
        direction = vec2(math_sine(angle), math_cosine(angle));
    }

    DynamicArray<vec2> street_points = dynamic_array_create_empty<vec2>(64);
    SCOPE_EXIT(dynamic_array_destroy(&street_points));
    dynamic_array_push_back(&street_points, current_point);

    BoundingBox2 bb = bounding_box_2_make_center_size(vec2(0.0f), bounding_size);
    for (int i = 0; i < max_step_size; i++)
    {
        // Push direction towards nearest point
        vec2 nearest_point;
        float dist = 10000.0f;
        for (int j = 0; j < hotspots->size; j++) {
            if (j == start_index) continue;
            float d = vector_distance_between(hotspots->data[j], current_point);
            if (d < dist) { dist = d; nearest_point = hotspots->data[j]; }
        }
        if (dist < min_distance_to_hotspot) {
            break;
        }
        if (!bounding_box_2_is_point_inside(bb, current_point)) {
            break;
        }

        vec2 to_nearest = vector_normalize_safe(nearest_point - current_point);
        direction = vector_normalize(to_nearest * 0.3f + direction);
        current_point += direction * step_size;
        dynamic_array_push_back(&street_points, current_point);
    }

    street_generate_from_points(dynamic_array_to_array(&street_points), vertex_buffer, index_buffer, state, thickness);
}
*/

struct Polygon2D
{
    Dynamic_Array<vec2> positions;
};

Polygon2D polygon_2d_create() {
    Polygon2D result;
    result.positions = dynamic_array_create_empty<vec2>(16);
    return result;
}

void polygon_2d_destroy(Polygon2D* polygon) {
    dynamic_array_destroy(&polygon->positions);
}

void polygon_2d_rotate(Polygon2D* polygon, float angle) {
    mat2 rotation;
    rotation.columns[0] = vec2(math_cosine(angle), math_sine(angle));
    rotation.columns[1] = vec2(-math_sine(angle), math_cosine(angle));
    for (int i = 0; i < polygon->positions.size; i++) {
        polygon->positions[i] = rotation * polygon->positions[i];
    }
}

void polygon_2d_fill_with_rectangle(Polygon2D* polygon, vec2 center, vec2 size) {
    dynamic_array_reset(&polygon->positions);
    dynamic_array_push_back(&polygon->positions, center + vec2(-size.x, -size.y));
    dynamic_array_push_back(&polygon->positions, center + vec2(size.x, -size.y));
    dynamic_array_push_back(&polygon->positions, center + vec2(size.x, size.y));
    dynamic_array_push_back(&polygon->positions, center + vec2(-size.x, size.y));
}

void polygon_2d_fill_with_ngon(Polygon2D* polygon, float radius, int n) {
    dynamic_array_reset(&polygon->positions);
    float angle = 0.0f;
    for (int i = 0; i < n; i++) {
        angle = i / (float)n * 2.0f * PI;
        dynamic_array_push_back(&polygon->positions, radius * vec2(math_cosine(angle), math_sine(angle)));
    }
}

bool vec2_is_right_handed(vec2 a, vec2 b) {
    return a.x * b.y - a.y * b.x > 0.0f;
}

bool triangle_2d_point_inside(vec2 a, vec2 b, vec2 c, vec2 p)
{
    vec2 e0 = b - a;
    vec2 e1 = c - b;
    vec2 e2 = a - c;
    if (vec2_is_right_handed(e0, p - a) && vec2_is_right_handed(e1, p - b) && vec2_is_right_handed(e2, p - c)) {
        return true;
    }
    return false;
}

void polygon_2d_triangulate(Polygon2D* polygon, Dynamic_Array<uint32>* index_buffer)
{
    Dynamic_Array<vec2>& positions = polygon->positions;
    Dynamic_Array<int> indices = dynamic_array_create_empty<int>(positions.size);
    SCOPE_EXIT(dynamic_array_destroy(&indices));
    for (int i = 0; i < positions.size; i++) {
        dynamic_array_push_back(&indices, i);
    }
    dynamic_array_reset(index_buffer);

    while (indices.size > 3)
    {
        int ear_index = -1;
        bool found_convex = false;
        int not_ear_index = -1;
        for (int i = 0; i < indices.size && ear_index == -1; i++)
        {
            vec2 prev = positions[indices[math_modulo(i - 1, indices.size)]];
            vec2 curr = positions[indices[i]];
            vec2 next = positions[indices[math_modulo(i + 1, indices.size)]];

            vec2 a = prev - curr;
            vec2 b = next - curr;
            if (b.x * a.y - b.y * a.x > 0.0f) // Convexity test
            {
                found_convex = true;
                // Check if all others are on the other side
                //vec2 normal = vector_rotate_90_degree_clockwise(next - prev);
                //float n0 = vector_dot(normal, prev);
                bool is_ear = true;
                for (int j = 0; j < indices.size; j++) {
                    if (math_absolute(j - i) <= 1) {
                        continue;
                    }
                    //if (vector_dot(positions[indices[j]], normal) > n0) {
                    if (triangle_2d_point_inside(prev, curr, next, positions[indices[j]])) {
                        is_ear = false;
                        not_ear_index = j;
                        break;
                    }
                }
                if (is_ear) {
                    ear_index = i;
                }
            }
            if (ear_index == -1) {
                if (!found_convex) {
                    logg("  %d is not convex, not ear\n", indices[i]);
                }
                else {
                    logg("  %d is not ear, intersecting %d\n", indices[i], indices[not_ear_index]);
                }
            }
        }
        if (ear_index == -1) {
            panic("No Ears!\n");
            ear_index = 0;
            for (int i = 0; i < indices.size; i++) {
                logg("\t %d\n", indices[i]);
            }
            break;
        }
        // Add ear, remove vertex
        dynamic_array_push_back(index_buffer, (uint32)indices[math_modulo(ear_index-1, indices.size)]);
        dynamic_array_push_back(index_buffer, (uint32)indices[math_modulo(ear_index, indices.size)]);
        dynamic_array_push_back(index_buffer, (uint32)indices[math_modulo(ear_index+1, indices.size)]);
        logg("Ear found: %d %d %d\n",
            indices[math_modulo(ear_index - 1, indices.size)],
            indices[math_modulo(ear_index, indices.size)],
            indices[math_modulo(ear_index + 1, indices.size)]);
        dynamic_array_remove_ordered(&indices, ear_index);
    }
    dynamic_array_push_back(index_buffer, (uint32)indices[0]);
    dynamic_array_push_back(index_buffer, (uint32)indices[1]);
    dynamic_array_push_back(index_buffer, (uint32)indices[2]);
}

bool polygon_2d_point_inside(Polygon2D* p, vec2 point) {
    vec2 direction = vec2(1.0f, 0.0f);
    for (int i = 0; i < p->positions.size; i++) {
        vec2& a = p->positions[i];
        vec2& b = p->positions[math_modulo(i + 1, p->positions.size)];
        Optional<float> intersection = line_segment_ray_intersection(a, b, point, direction);
        if (intersection.available) {
            return vec2_is_right_handed(direction, b - a);
        }
    }
    return false;
}

void polygon_2d_union_new(Polygon2D* p0, Polygon2D* p1)
{
    int start_index = -1;
    {
        bool intersect = false;
        for (int i = 0; i < p0->positions.size; i++) {
            if (!polygon_2d_point_inside(p1, p0->positions[i])) {
                start_index = i;
                break;
            }
            else {
                intersect = true;
            }
        }
        for (int i = 0; i < p1->positions.size; i++) {
            if (polygon_2d_point_inside(p0, p1->positions[i])) {
                intersect = true;
                break;
            }
        }
        if (!intersect) {
            for (int i = 0; i < p1->positions.size; i++) {
                dynamic_array_push_back(&p0->positions, p1->positions[i]);
            }
            return;
        }
        if (start_index == -1) start_index = 0;
    }

    Array<int> collisions_p0 = array_create_empty<int>(p0->positions.size);
    Array<int> collisions_p1 = array_create_empty<int>(p1->positions.size);
    Array<int> intersections = array_create_empty<int>(p1->positions.size);
    SCOPE_EXIT(array_destroy(&collisions_p0));
    SCOPE_EXIT(array_destroy(&collisions_p1));
    for (int i = 0; i < collisions_p0.size; i++) collisions_p0[i] = -1;
    for (int i = 0; i < collisions_p1.size; i++) collisions_p1[i] = -1;

    // Find all collisions
    for (int i = 0; i < p0->positions.size; i++) 
    {
        vec2 a = p0->positions[i];
        vec2 b = p0->positions[(i+1)%p0->positions.size];
        for (int j = 0; j < p1->positions.size; j++) 
        {
            vec2 c = p1->positions[j];
            vec2 d = p1->positions[(j+1)%p1->positions.size];

            int collision_index = -1;
            float min_distance = 10000.0f;
            Optional<float> intersection = line_segment_ray_intersection(c, d, a, vector_normalize(b - a));
            if (intersection.available && intersection.value < min_distance && intersection.value > 0.0f) {
                collision_index = math_modulo(j + 1, p1->positions.size);
                min_distance = intersection.value;
                //intersection_point = a + intersection.value * vector_normalize(b - a);
            }
        }

    }

    Dynamic_Array<vec2> points = dynamic_array_create_empty<vec2>(16);
    SCOPE_EXIT(dynamic_array_destroy(&points));
    if (start_index == -1) { // All points inside
        dynamic_array_reset(&p0->positions);
        for (int i = 0; i < p1->positions.size; i++) {
            dynamic_array_push_back(&p0->positions, p1->positions[i]);
        }
        return;
    }
}

void polygon_2d_union(Polygon2D* p0, Polygon2D* p1)
{
    // Walk along p0, intersect line_index with all other lines in p1, if int
    Dynamic_Array<vec2> points = dynamic_array_create_empty<vec2>(16);
    SCOPE_EXIT(dynamic_array_destroy(&points));
    int start_index = -1;
    for (int i = 0; i < p0->positions.size; i++) {
        if (!polygon_2d_point_inside(p1, p0->positions[i])) {
            start_index = i;
            break;
        }
    }
    if (start_index == -1) { // All points inside
        dynamic_array_reset(&p0->positions);
        for (int i = 0; i < p1->positions.size; i++) {
            dynamic_array_push_back(&p0->positions, p1->positions[i]);
        }
        return;
    }

    // Walk from start point forward
    int i = start_index;
    bool on_p1 = false;
    logg("\n\nSTART_UNION\n");
    do
    {
        vec2 a, b;
        logg("\t #%d, on_p1: %s\n", i, on_p1 ? "TRUE" : "FALSE");
        if (!on_p1)
        {
            a = p0->positions[i];
            b = p0->positions[math_modulo(i + 1, p0->positions.size)];
            // Search for potential collision
            float min_dist = 10000.0f;
            int collision_index = -1;
            vec2 intersection_point;
            for (int j = 0; j < p1->positions.size; j++) {
                vec2 c = p1->positions[j];
                vec2 d = p1->positions[math_modulo(j + 1, p1->positions.size)];
                Optional<float> intersection = line_segment_ray_intersection(c, d, a, vector_normalize(b - a));
                if (intersection.available && intersection.value < min_dist && intersection.value > 0.01f) {
                    collision_index = math_modulo(j + 1, p1->positions.size);
                    min_dist = intersection.value;
                    intersection_point = a + intersection.value * vector_normalize(b - a);
                }
            }
            if (collision_index == -1) {
                dynamic_array_push_back(&points, b);
                i = math_modulo(i + 1, p0->positions.size);
                //logg("P0: index: %d\n", i);
            }
            else {
                dynamic_array_push_back(&points, intersection_point);
                dynamic_array_push_back(&points, p1->positions[collision_index]);
                on_p1 = true;
                i = collision_index;
                //logg("P0 collision with : %d\n", collision_index);
            }
        }
        else {
            a = p1->positions[i];
            b = p1->positions[math_modulo(i + 1, p1->positions.size)];
            // Search for potential collision
            float min_dist = 10000.0f;
            int collision_index = -1;
            vec2 intersection_point;
            for (int j = 0; j < p0->positions.size; j++) {
                vec2 c = p0->positions[j];
                vec2 d = p0->positions[math_modulo(j + 1, p0->positions.size)];
                Optional<float> intersection = line_segment_ray_intersection(c, d, a, vector_normalize(b - a));
                if (intersection.available && intersection.value < min_dist && intersection.value > -0.01f) {
                    collision_index = math_modulo(j + 1, p0->positions.size);
                    min_dist = intersection.value;
                    intersection_point = a + intersection.value * vector_normalize(b - a);
                }
            }
            if (collision_index == -1) {
                dynamic_array_push_back(&points, b);
                i = math_modulo(i + 1, p1->positions.size);
                //logg("P1: index: %d\n", math_modulo(i + 1, p1->positions.size));
            }
            else {
                dynamic_array_push_back(&points, intersection_point);
                dynamic_array_push_back(&points, p0->positions[collision_index]);
                //logg("P1 collision with : %d\n", collision_index);
                i = collision_index;
                on_p1 = false;
            }
        }
    } while (!(i == start_index && !on_p1));

    dynamic_array_reset(&p0->positions);
    for (int i = 0; i < points.size; i++) {
        dynamic_array_push_back(&p0->positions, points[i]);
    }
}

void polygon_2d_cleanup_near_points_and_colinear_points(Polygon2D* p, float dist, float coplanar_dist)
{
    // First source_parse: Remove points that are too close together
    for (int i = 0; i < p->positions.size && p->positions.size > 3; i++) {
        vec2 a = p->positions[i];
        vec2 b = p->positions[math_modulo(i + 1, p->positions.size)];
        if (vector_distance_between(a, b) < dist) {
            dynamic_array_remove_ordered(&p->positions, math_modulo(i + 1, p->positions.size));
            if (i + 1 < p->positions.size) {
                i--;
            }
        }
    }

    // Second source_parse: For each two lines, check if they are *almost* in the same direction, if so, remove
    for (int i = 0; i < p->positions.size; i++) {
        vec2 prev = p->positions[math_modulo(i - 1, p->positions.size)];
        vec2 curr = p->positions[math_modulo(i, p->positions.size)];
        vec2 next = p->positions[math_modulo(i + 1, p->positions.size)];
        // Check if direction *almost* match
        vec2 a = vector_normalize(curr - prev);
        vec2 b = vector_normalize(next - curr);
        if (vector_length(a - b) < coplanar_dist) {
            dynamic_array_remove_ordered(&p->positions, i);
            i--;
        }
    }
}

struct Building_Vertex
{
    vec3 position;
    vec3 normal;
    vec2 uv;
};

Building_Vertex building_vertex_make(vec3 pos, vec3 normal, vec2 uv) {
    Building_Vertex result;
    result.position = pos;
    result.normal = normal;
    result.uv = uv;
    return result;
}

Bounding_Box2 polygon_2d_get_bounding_box(Polygon2D* polygon) {
    vec2 minimum(10000.0f, 100000.0f);
    vec2 maximum(-1000000.0f, -1000000.0f);
    for (int i = 0; i < polygon->positions.size; i++) {
        vec2& p = polygon->positions[i];
        if (p.x > maximum.x) maximum.x = p.x;
        if (p.y > maximum.y) maximum.y = p.y;
        if (p.x < minimum.x) minimum.x = p.x;
        if (p.y < minimum.y) minimum.y = p.y;
    }
    return bounding_box_2_make_min_max(minimum, maximum);
}

void building_create_from_polygon_2d(Polygon2D* polygon, Dynamic_Array<Building_Vertex>* vertices, Dynamic_Array<uint32>* indices, float height)
{
    dynamic_array_reset(vertices);
    dynamic_array_reset(indices);
    // Lets go for a stupid uv projection for now, and fix it later (Also different materials/split model in multiple meshes)
    Bounding_Box2 bb = polygon_2d_get_bounding_box(polygon);
    vec2 center = (bb.min + bb.max) / 2.0f;
    for (int i = 0; i < polygon->positions.size; i++) {
        vec2 a = polygon->positions[i] - center;
        vec2 b = polygon->positions[math_modulo(i + 1, polygon->positions.size)] - center;
        vec2 normal = vector_normalize(vector_rotate_90_degree_clockwise(b - a));
        dynamic_array_push_back(vertices, building_vertex_make(vec3(a.x, 0.0f, -a.y), vec3(normal.x, 0.0f, normal.y), vec2(0.0f, 0.0f)));
        dynamic_array_push_back(vertices, building_vertex_make(vec3(b.x, 0.0f, -b.y), vec3(normal.x, 0.0f, normal.y), vec2(1.0f, 0.0f)));
        dynamic_array_push_back(vertices, building_vertex_make(vec3(b.x, height, -b.y), vec3(normal.x, 0.0f, normal.y), vec2(1.0f, 1.0f)));
        dynamic_array_push_back(vertices, building_vertex_make(vec3(a.x, height, -a.y), vec3(normal.x, 0.0f, normal.y), vec2(0.0f, 1.0f)));
        dynamic_array_push_back(indices, (uint32)vertices->size - 4);
        dynamic_array_push_back(indices, (uint32)vertices->size - 3);
        dynamic_array_push_back(indices, (uint32)vertices->size - 2);
        dynamic_array_push_back(indices, (uint32)vertices->size - 4);
        dynamic_array_push_back(indices, (uint32)vertices->size - 2);
        dynamic_array_push_back(indices, (uint32)vertices->size - 1);
    }

    for (int i = 0; i < polygon->positions.size; i++) {
        vec2 p = polygon->positions[i];
        vec2 uv = (p - bb.min) / (bb.max - bb.min);
        p = p - center;
        dynamic_array_push_back(vertices, building_vertex_make(vec3(p.x, height, -p.y), vec3(0.0f, 1.0f, 0.0f), uv));
    }

    Dynamic_Array<uint32> ceiling_indices = dynamic_array_create_empty<uint32>(polygon->positions.size * 2);
    SCOPE_EXIT(dynamic_array_destroy(&ceiling_indices));
    polygon_2d_triangulate(polygon, &ceiling_indices);
    for (int i = 0; i < ceiling_indices.size; i++) {
        dynamic_array_push_back(indices, vertices->size + ceiling_indices[i] - polygon->positions.size);
    }
}

/*
Mesh_GPU_Data city_street_create_mesh_from_network()
{
}
*/

Mesh_GPU_Buffer city_building_create_mesh_from_polygon(Polygon2D* polygon, float height, Rendering_Core* core)
{
    Dynamic_Array<Building_Vertex> building_vertices = dynamic_array_create_empty<Building_Vertex>(32);
    Dynamic_Array<uint32> building_indices = dynamic_array_create_empty<uint32>(32);
    SCOPE_EXIT(dynamic_array_destroy(&building_vertices));
    SCOPE_EXIT(dynamic_array_destroy(&building_indices));
    building_create_from_polygon_2d(polygon, &building_vertices, &building_indices, 5.0f);

    REMOVE_ME a;
    REMOVE_ME vertex_attributes[] = {
        a
    };
    Mesh_GPU_Buffer building_mesh = mesh_gpu_buffer_create_with_single_vertex_buffer(
        gpu_buffer_create(dynamic_array_as_bytes(&building_vertices), GPU_Buffer_Type::VERTEX_BUFFER, GPU_Buffer_Usage::STATIC),
        array_create_static(vertex_attributes, 1),
        gpu_buffer_create(dynamic_array_as_bytes(&building_indices), GPU_Buffer_Type::INDEX_BUFFER, GPU_Buffer_Usage::STATIC),
        Mesh_Topology::TRIANGLES,
        building_indices.size
    );

    return building_mesh;
}

#include <cstdio>

void polygon_2d_draw(Polygon2D* polygon, Renderer_2D* renderer, vec2 offset, float size)
{
    Bounding_Box2 bb = polygon_2d_get_bounding_box(polygon);
    char buffer[100];
    for (int i = 0; i < polygon->positions.size; i++) {
        vec2 a = (polygon->positions[i]) / (size * 1.3f) + offset;
        vec2 b = (polygon->positions[math_modulo(i + 1, polygon->positions.size)]) / (size * 1.3f) + offset;
        renderer_2D_add_line(renderer, a, b, vec3(1.0f), 3.0f, 0.0f);
        sprintf_s(buffer, "%d", i);
        String buf = string_create_static(buffer);
        renderer_2D_add_text_in_box(renderer, &buf, 0.1f, vec3(0.5f), a - vec2(0.05f, 0.0f),
            vec2(0.1f, 0.1f), Text_Alignment_Horizontal::CENTER, Text_Alignment_Vertical::CENTER, Text_Wrapping_Mode::SCALE_DOWN);
    }
}

void polygon_2d_draw_scaled(Polygon2D* polygon, Renderer_2D* renderer, vec2 offset)
{
    Bounding_Box2 bb = polygon_2d_get_bounding_box(polygon);
    vec2 center = (bb.max + bb.min) / 2.0f;
    float size = vector_get_maximum_axis(bb.max - bb.min);
    char buffer[100];
    for (int i = 0; i < polygon->positions.size; i++) {
        vec2 a = (polygon->positions[i] - center) / (size * 1.3f) + offset;
        vec2 b = (polygon->positions[math_modulo(i + 1, polygon->positions.size)] - center) / (size * 1.3f) + offset;
        renderer_2D_add_line(renderer, a, b, vec3(1.0f), 3.0f, 0.0f);
        sprintf_s(buffer, "%d", i);
        String buf = string_create_static(buffer);
        renderer_2D_add_text_in_box(renderer, &buf, 0.1f, vec3(0.5f), a - vec2(0.05f, 0.0f),
            vec2(0.1f, 0.1f), Text_Alignment_Horizontal::CENTER, Text_Alignment_Vertical::CENTER, Text_Wrapping_Mode::SCALE_DOWN);
    }
}

void polygon_2d_translate_positions(Polygon2D* polygon, vec2 translation)
{
    for (int i = 0; i < polygon->positions.size; i++) {
        polygon->positions[i] = polygon->positions[i] + translation;
    }
}

void polygon_2d_transform(Polygon2D* polygon, mat3 transform) {
    for (int i = 0; i < polygon->positions.size; i++) {
        vec3 transformed =  transform * vec3(polygon->positions[i], 1.0f);
        polygon->positions[i] = vec2(transformed.x, transformed.y);
    }
}

void polygon_2d_fill_random_polygon(Polygon2D* polygon, float radius)
{
    dynamic_array_reset(&polygon->positions);
    int count = (random_next_u32(&g_random) % 3) + 3;
    polygon_2d_fill_with_ngon(polygon, radius, count);
    polygon_2d_transform(polygon, mat3(mat2_make_rotation_matrix(random_next_float(&g_random) * 2 * PI)));
    polygon_2d_translate_positions(polygon, (vec2(random_next_float(&g_random), random_next_float(&g_random)) - 0.5f) * radius);
}

void polygon_2d_fill_random(Polygon2D* polygon, float radius, GUI* gui, Rendering_Core* core, Window* window)
{
    Polygon2D addition_shape = polygon_2d_create();
    SCOPE_EXIT(polygon_2d_destroy(&addition_shape));
    dynamic_array_reset(&polygon->positions);
    int shape_count = (random_next_u32(&g_random) % 3) + 2;

    for (int i = 0; i < shape_count; i++) {
        int count = (random_next_u32(&g_random) % 3) + 3;
        polygon_2d_fill_with_ngon(&addition_shape, radius, count);
        polygon_2d_translate_positions(&addition_shape, (vec2(random_next_float(&g_random), random_next_float(&g_random)) - 0.5f) * radius);

        gui_render(gui, core);
        window_swap_buffers(window);
        polygon_2d_draw_scaled(polygon, gui->renderer_2d, vec2(0.0f));
        polygon_2d_draw_scaled(&addition_shape, gui->renderer_2d, vec2(0.0f));
        polygon_2d_draw_scaled(&addition_shape, gui->renderer_2d, vec2(0.0f));
        gui_render(gui, core);
        window_swap_buffers(window);

        polygon_2d_union(polygon, &addition_shape);
        polygon_2d_cleanup_near_points_and_colinear_points(polygon, radius / 10.0f, 0.01f);
    }
}

struct Camera_3D_Uniform_Data {
    mat4 view;
    mat4 projection;
    mat4 view_projection;
    mat4 view_inverse;
    // TODO: Other inverse matrices
    vec4 position; // As Homogeneous vectors, w = 1.0f
    vec4 direction; // As Homogeneous vector
    vec4 parameters; // Packed data: x = near, y = far, z = time, w = unused
};

Camera_3D_Uniform_Data camera_3d_uniform_data_make(Camera_3D* camera, float time) {
    Camera_3D_Uniform_Data result;
    result.projection = camera->projection_matrix;
    result.view = camera->view_matrix;
    result.view_projection = camera->view_projection_matrix;
    result.view_inverse = matrix_transpose(result.view);
    result.position = vec4(camera->position, 1.0f);
    result.direction = vec4(camera->view_direction, 1.0f);
    result.parameters = vec4(camera->near_distance, camera->far_distance, time, 0.0f);
    return result;
}

void streetnetwork_regenerate(StreetNetwork* network, float max_radius)
{
    streetnetwork_update_grid_size(network, max_radius * 2.2f, 100);
    streetnetwork_generate_main_road(network, vec2(max_radius / 3.0f), 1000.0f, 3.0f, 3, 2.0f);
    streetnetwork_generate_seedpoints_for_branches(network, 0.8f, 0.1f);
    int iteration = 0;
    while (network->open_branches.size > 0 && iteration < 100) {
        iteration++;
        logg("iteration: %d\n", iteration);
        streetnetwork_grow_branches(network, 1.5f, max_radius / 3.0f * 2.0f, max_radius);
    }
    streetnetwork_place_buildings_random(network, 0.15f);
}

void proc_city_main()
{
    /*
    Window* window = window_create("Proc_City", 0);
    SCOPE_EXIT(window_destroy(window));

    Window_State* window_state = window_get_window_state(window);
    g_random = random_make_time_initalized();
    Timer timer = timer_make();

    Rendering_Core core = rendering_core_create(window_state->width, window_state->height, (float)window_state->dpi);
    SCOPE_EXIT(rendering_core_destroy(&core));

    Pipeline_State pipeline_state = pipeline_state_make_default();
    pipeline_state.depth_state.test_type = Depth_Test_Type::TEST_DEPTH;
    pipeline_state.blending_state.blending_enabled = false;
    rendering_core_update_pipeline_state(&core, pipeline_state);

    Input* input = window_get_input(window);
    Camera_3D* camera = camera_3D_create(&core, math_degree_to_radians(90.0f), 0.1f, 1000.0f);
    SCOPE_EXIT(camera_3D_destroy(camera, &core));
    Camera_Controller_Arcball controller = camera_controller_arcball_make(vec3(0.0f), 1.0f);

    GPU_Buffer camera_uniform_buffer = gpu_buffer_create_empty(sizeof(Camera_3D_Uniform_Data), GPU_Buffer_Type::UNIFORM_BUFFER, GPU_Buffer_Usage::DYNAMIC);
    gpu_buffer_bind_indexed(&camera_uniform_buffer, 0);
    SCOPE_EXIT(gpu_buffer_destroy(&camera_uniform_buffer));

    Text_Renderer* text_renderer = text_renderer_create_from_font_atlas_file(&core, "resources/fonts/glyph_atlas.atlas");
    SCOPE_EXIT(text_renderer_destroy(text_renderer, &core));
    Renderer_2D* renderer_2D = renderer_2D_create(&core, text_renderer);
    SCOPE_EXIT(renderer_2D_destroy(renderer_2D, &core));
    GUI gui = gui_create(renderer_2D, input, &timer);
    SCOPE_EXIT(gui_destroy(&gui));

    glViewport(0, 0, window_state->width, window_state->height);
    glClearColor(0.05f, 0.05f, 0.05f, 0.0f);

    // City Stuff
    Shader_Program* city_shader = shader_program_create(&core, { "resources/shaders/city_shader.glsl" });
    SCOPE_EXIT(shader_program_destroy(city_shader));

    // Building mesh
    Polygon2D square = polygon_2d_create();
    SCOPE_EXIT(polygon_2d_destroy(&square));
    polygon_2d_fill_with_rectangle(&square, vec2(0.5f), vec2(5.0f, 1.3f));

    Polygon2D hex = polygon_2d_create();
    SCOPE_EXIT(polygon_2d_destroy(&hex));
    polygon_2d_fill_with_ngon(&hex, 2.0f, 5);

    polygon_2d_union(&square, &hex);
    polygon_2d_cleanup_near_points_and_colinear_points(&square, 0.3f, 0.01f);

    Mesh_GPU_Buffer building_mesh = city_building_create_mesh_from_polygon(&square, 5.0f, &core);
    SCOPE_EXIT(mesh_gpu_buffer_destroy(&building_mesh));
    dynamic_array_reset(&square.positions);

    float city_size = 3.0f;
    StreetNetwork street_network = streetnetwork_create(50.0f, 20);
    SCOPE_EXIT(streetnetwork_destroy(&street_network));
    streetnetwork_regenerate(&street_network, city_size);

    Polygon2D polygon_random = polygon_2d_create();
    SCOPE_EXIT(polygon_2d_destroy(&hex));
    //polygon_2d_fill_random(&polygon_random, 3.0f, &gui, &opengl_state, window);
    String string = string_create_empty(16);

    while (!input->close_request_issued)
    {
        double now = timer_current_time_in_seconds(&timer);

        // Update
        input_reset(input);
        window_handle_messages(window, false);
        gui_update(&gui, input, window_state->width, window_state->height);

        if (input->key_down[(int)Key_Code::ESCAPE]) {
            window_close(window);
        }
        camera_controller_arcball_update(&controller, camera, input, window_state->width, window_state->height);

        static float city_size = 3.0f;
        bool changed = gui_slider(&gui, gui_position_make_on_window_border(&gui, vec2(0.5f, 0.1f), Anchor_2D::TOP_LEFT), &city_size, 3.0f, 20.0f);
        if (changed || gui_button(&gui, gui_position_make_on_window_border(&gui, vec2(0.3f, 0.1f), Anchor_2D::CENTER_LEFT), "Reset network")) {
            streetnetwork_regenerate(&street_network, city_size);
        }

        // Render
        rendering_core_prepare_frame(&core, camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH, (float)now, window_state->width, window_state->height);
        // Set state
        Camera_3D_Uniform_Data d = camera_3d_uniform_data_make(camera, (float)now);
        gpu_buffer_update(&camera_uniform_buffer, array_create_static_as_bytes(&d, 1)); // Update camera data

        // Draw City
        polygon_2d_draw(&polygon_random, renderer_2D, vec2(0.0f), 3.0f);
        string_reset(&string);
        string_append_formated(&string, "%d", polygon_random.positions.size);
        gui_label(&gui, gui_position_make(vec2(-0.8f, 0.0f), vec2(0.5f, 0.1f)), string.characters);
        if (gui_button(&gui, gui_position_make_on_window_border(&gui, vec2(0.5f, 0.2f), Anchor_2D::TOP_CENTER), "Random poly")) {
            polygon_2d_fill_random_polygon(&square, 3.0f);
            polygon_2d_draw(&square, renderer_2D, vec2(0.0f, 0.0f), 3.0f);
            gui_render(&gui, &core);
            window_swap_buffers(window);

            polygon_2d_union(&polygon_random, &square);
            polygon_2d_cleanup_near_points_and_colinear_points(&polygon_random, 3.0f / 10.0f, 0.01f);
            polygon_2d_translate_positions(&square, vec2(3.0f, 0.0f));
        }

        //streetnetwork_draw(&street_network, &renderer_2d, vec2(0.0f), city_size);
        for (int i = 0; i < street_network.buildings.size; i++) {
            StreetBuildingPlaceholder& p = street_network.buildings[i];
            mat4 model = mat4_make_translation_matrix(vec3(p.position.x, 0.0f, -p.position.y) * 10.0f) *
                mat4(mat3_make_rotation_matrix_around_y(math_arctangent_2(p.normal_to_street.x, -p.normal_to_street.y))) *
                mat4(mat3_make_scaling_matrix(vec3(0.1f)));
            shader_program_draw_mesh(city_shader, &building_mesh, &core, { uniform_value_make_mat4("u_model", model) });
        }

        gui_render(&gui, &core);

        // Present and sleep
        window_swap_buffers(window);
        timer_sleep_until(&timer, now + 1 / 60.0);
    }

    logg("what");
    */
}
