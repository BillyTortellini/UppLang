#include "texture.hpp"

/*
    Texture from Bitmap
*/
#include <cmath>

#include "../utility/utils.hpp"
#include "../datastructures/array.hpp"
#include "opengl_function_pointers.hpp"
#include "opengl_state.hpp"
#include "../math/scalars.hpp"
#include "../math/vectors.hpp"

Texture_Bitmap texture_bitmap_create_from_data(int width, int height, int channel_count, byte* data) 
{
    Texture_Bitmap result;
    result.width = width;
    result.height = height;
    result.channel_count = channel_count;
    result.data = array_create_copy(data, width*height*channel_count);
    return result;
}

Texture_Bitmap texture_bitmap_create_from_data_with_pitch(int width, int height, int pitch, byte* data)
{
    Texture_Bitmap result;
    result.width = width;
    result.height = height;
    result.channel_count = 1;
    result.data = array_create_empty<byte>(width*height);
    for (int x = 0; x < width; x++) 
    {
        for (int y = 0; y < height; y++) 
        {
            int source_index = x + (height-1-y) * pitch;
            int destination_index = x + y * width;
            result.data.data[destination_index] = data[source_index];
        }
    }
    return result;
}

Texture_Bitmap texture_bitmap_create_from_bitmap_with_pitch(int width, int height, int pitch, byte* data)
{
    Texture_Bitmap result;
    result.width = width;
    result.height = height;
    result.channel_count = 1;
    result.data = array_create_empty<byte>(width*height);
    for (int x = 0; x < width; x++) 
    {
        for (int y = 0; y < height; y++) 
        {
            int source_byte_index = (height-1-y) * pitch + x/8;
            int source_bit_index = 7-(x % 8);
            int destination_index = x + y * width;
            if (data[source_byte_index] & (1 << source_bit_index)) {
                result.data.data[destination_index] = 255;
            }
            else {
                result.data.data[destination_index] = 0;
            }
        }
    }
    return result;
}

Texture_Bitmap texture_bitmap_create_empty(int width, int height, int channel_count) 
{
    Texture_Bitmap result;
    result.width = width;
    result.height = height;
    result.channel_count = channel_count;
    result.data = array_create_empty<byte>(width*height*channel_count);
    return result;
}

void texture_bitmap_destroy(Texture_Bitmap* texture_data) {
    array_destroy(&texture_data->data);
}

Texture_Bitmap texture_bitmap_create_empty_mono(int width, int height, byte fill_value)
{
    Texture_Bitmap result = texture_bitmap_create_empty(width, height, 1);
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            result.data[x + y * width] = fill_value;
        }
    }
    return result;
}

void texture_bitmap_inpaint_complete(Texture_Bitmap* destination, Texture_Bitmap* source, int position_x, int position_y)
{
    // Check if bitmaps are compatible
    if (destination->channel_count != source->channel_count) {
        logg("Bitmap Inpaint failed, channel count did not match!\n");
        return;
    }

    // Put pixels
    for (int source_x = 0; source_x < source->width; source_x++) {
        for (int source_y = 0; source_y < source->height; source_y++) {
            int destination_x = source_x + position_x;
            int destination_y = source_y + position_y;
            int destination_index = destination_x + destination_y * destination->width;
            int source_index = source_x + source_y * source->width;
            // Skip pixels not inside destination
            if (destination_index > destination->data.size || destination_index < 0 ||
                source_index > source->data.size || source_index < 0) {
                panic("Inpaint failed!\n");
            }
            destination->data[destination_index] = source->data[source_index];
        }
    }
}

Texture_Bitmap texture_bitmap_create_test_bitmap(int size)
{
    struct Color {
        byte r, g, b, a;
        Color(float r, float g, float b, float a) {
            this->r = (byte)(r * 255);
            this->g = (byte)(g * 255);
            this->b = (byte)(b * 255);
            this->a = (byte)(a * 255);
        }
        Color() {};
    };
    struct Color_Mono {
        byte value;
        Color_Mono(float value) { this->value = (byte)(value*255); }
        Color_Mono() {}
    };

    Array<Color_Mono> image_data = array_create_empty<Color_Mono>(size*size);
    SCOPE_EXIT(array_destroy(&image_data));
    for (int x_int = 0; x_int < size; x_int++) {
        for (int y_int = 0; y_int < size; y_int++) {
            float x = (((float)x_int+0.5f) / size) * 2.0f - 1.0f;
            float y = (((float)y_int+0.5f) / size) * 2.0f - 1.0f;
            float dist = math_square_root(x*x + y*y);
            Color_Mono pixel_color = dist < 0.5f ? Color_Mono(1) : Color_Mono(0);
            image_data.data[x_int + y_int * size] = pixel_color;
        }
    }

    return texture_bitmap_create_from_data(size, size, 1, (byte*)image_data.data);
}

void texture_bitmap_binary_parser_write(Texture_Bitmap* bitmap, BinaryParser* parser)
{
    binary_parser_write_int(parser, bitmap->width);
    binary_parser_write_int(parser, bitmap->height);
    binary_parser_write_int(parser, bitmap->channel_count);
    binary_parser_write_bytes(parser, bitmap->data);
}

Texture_Bitmap texture_bitmap_binary_parser_read(BinaryParser* parser)
{
    Texture_Bitmap result;
    result.width = binary_parser_read_int(parser);
    result.height = binary_parser_read_int(parser);
    result.channel_count = binary_parser_read_int(parser);
    result.data = array_create_empty<byte>(result.width * result.height * result.channel_count);
    binary_parser_read_bytes(parser, result.data);
    return result;
}



/*
    Distance field functions
*/
float parabola_intersection_x(vec2 p, vec2 q) {
    return ((q.y + q.x*q.x) - (p.y + p.x*p.x)) / (2*q.x - 2*p.x);
}

void distance_field_find_hull_parabolas(Array<float> row, Array<vec2>& hull_vertices, Array<float>& hull_intersections)
{
    hull_vertices[0].x = 0.0f;
    hull_vertices[0].y = row[0];
    hull_intersections[0] = 0.0f;
    hull_intersections[1] = +INFINITY;

    int last_vertex_index = 0;
    for (int i = 1; i < row.size; i++) 
    {
        vec2 last_vertex = hull_vertices[last_vertex_index];
        vec2 new_vertex = vec2((float)i, row[i]);

        if (new_vertex.y == INFINITY) {
            continue;
        }

        float intersection_x = parabola_intersection_x(last_vertex, new_vertex);
        // Remove vertices if new vertex parabola covers the old ones
        while (last_vertex_index != -1 && intersection_x <= hull_intersections[last_vertex_index]) {
            last_vertex_index--;
            if (last_vertex_index < 0) {
                break;
            }
            last_vertex = hull_vertices[last_vertex_index];
            intersection_x = parabola_intersection_x(new_vertex, last_vertex);
        }

        // Always add new vertex
        last_vertex_index++;
        hull_vertices[last_vertex_index] = new_vertex;
        hull_intersections[last_vertex_index] = intersection_x;
    }
    hull_intersections[last_vertex_index+1] = +INFINITY;

    /*
    String message = string_create("\nRow:\n\t");
    SCOPE_EXIT(string_destroy(&message));
    for (int i = 0; i < row.size; i++) {
        string_append_formated(&message, "%3.3f ", row[i]);
    }
    string_append_formated(&message, "\nHull vertices: (#%d)\n\t", last_vertex_index+1);
    for (int i = 0; i <= last_vertex_index; i++) {
        string_append_formated(&message, "%2.2f/%2.2f ", hull_vertices[i].x, hull_vertices[i].y);
    }
    //logg("%s\n", message.characters);
    */
}

void distance_field_horizontal_pass(Array<float> row)
{
    Array<float> hull_intersections = array_create_empty<float>(row.size*2);
    SCOPE_EXIT(array_destroy(&hull_intersections));
    Array<vec2> hull_vertices = array_create_empty<vec2>(row.size);
    SCOPE_EXIT(array_destroy(&hull_vertices));

    distance_field_find_hull_parabolas(row, hull_vertices, hull_intersections);
    // March paraboloas
    int current_vertex = 0;
    //String row_data = string_create_formated("\nROW: ");
    for (int i = 0; i < row.size; i++)
    {
        while (hull_intersections[current_vertex+1] < i) {
            current_vertex++;
        }
        float delta_x = i - hull_vertices[current_vertex].x;
        row[i] = delta_x*delta_x + hull_vertices[current_vertex].y;
        //string_append_formated(&row_data, "%3.2f ", row[i]);
    }
    //logg("%s\n", row_data.characters);
}

void float_array_transpose(Array<float> distances, int size)
{
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size-y; x++) {
            int source_index = x + y * size;
            int destination_index = (size-y-1) + (size-x-1)*size;
            float swap = distances[source_index];
            distances[source_index] = distances[destination_index];
            distances[destination_index] = swap;
        }
    }
}

Array<float> distance_field_create_from_bool_array(Array<byte> source, int width, bool antialiased)
{
    Array<float> distances = array_create_empty<float>(width*width);

    // Initialize distances to a high value
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < width; y++) {
            int index = x + y * width;
            if (antialiased)
            {
                if (source[index] < 254) {
                    distances[index] = source[index] / 255.0f;
                }
                else {
                    distances[index] = INFINITY;
                }
            }
            else {
                if (source[index] == 0) {
                    distances[index] = 0.0f;
                }
                else {
                    distances[index] = INFINITY;
                }
            }
        }
    }
    // First horizontal pass
    for (int y = 0; y < width; y++) {
        distance_field_horizontal_pass(array_create_static<float>(&distances[y*width], width));
    }
    float_array_transpose(distances, width);
    // Second horizontal pass
    for (int y = 0; y < width; y++) {
        distance_field_horizontal_pass(array_create_static<float>(&distances[y*width], width));
    }
    float_array_transpose(distances, width);

    // Create result (From squared distance to distance)
    for (int y = 0; y < width; y++) {
        for (int x = 0; x < width; x++) {
            int index = x + y * width;
            distances[index] = math_square_root(distances[index]);
        }
    }

    return distances;
}

Array<float> texture_bitmap_create_distance_field(Texture_Bitmap* source)
{
    if (source->channel_count != 1) {
        panic("To create distance field, we need a channel count of 1!\n");
    }
    if (source->width != source->height) {
        panic("For distnace fields, bitmap must be square!");
    }

    int width = source->width;
    Array<byte> source_bytes = array_create_copy<byte>(source->data.data, width*width);
    SCOPE_EXIT(array_destroy(&source_bytes));

    Array<float> outer_distances = distance_field_create_from_bool_array(source_bytes, width, true);
    // Negate boolean map
    for (int y = 0; y < width; y++) {
        for (int x = 0; x < width; x++) {
            source_bytes[x + y * width] = 255 - source_bytes[x + y * width];
        }
    }
    Array<float> inner_distances = distance_field_create_from_bool_array(source_bytes, width, true);
    SCOPE_EXIT(array_destroy(&inner_distances));

    // Combine inner and outer distances into one bitmap (Reusing the memory from outer_distances)
    for (int y = 0; y < width; y++) {
        for (int x = 0; x < width; x++) {
            int index = x + y * width;
            if (inner_distances[index] > 0.0f) {
                outer_distances[index] = -inner_distances[index] + 1.0f;
            }
        }
    }

    return outer_distances;
}

Array<float> texture_bitmap_create_distance_field_bad(Texture_Bitmap* source)
{
    int width = source->width;
    Array<float> distances = array_create_empty<float>(width*width);

    for (int x = 0; x < width; x++) {
        for (int y = 0; y < width; y++) {
            float min_dist = 100000.0f;
            int index = x + y * width;
            bool is_inside = source->data[index] > 128;
            for (int x_2 = 0; x_2 < width; x_2++) {
                for (int y_2 = 0; y_2 < width; y_2++) {
                    int index2 = x_2 + y_2 * width;
                    int dx = x - x_2;
                    int dy = y - y_2;
                    float distance = (float)(dx * dx + dy * dy);
                    bool is_inside2 = source->data[index2] > 128;
                    if (is_inside2 != is_inside && distance < min_dist) {
                        min_dist = distance;
                    }
                }
            }
            distances[index] = math_square_root(min_dist);
            if (is_inside) {
                distances[index] = -distances[index]+1.0f;
            }
        }
    }

    return distances;
}

void texture_bitmap_print_distance_field(Array<float> data, int width)
{
    String result = string_create_empty(1024);
    SCOPE_EXIT(string_destroy(&result));
    string_append_formated(&result, "\nPrinting bitmap, width = %d\n", width);
    for (int y = 0; y < width; y++)
    {
        string_append_formated(&result, "Row %3d =", y);
        for (int x = 0; x < width; x++)
        {
            int index = x + y * width;
            string_append_formated(&result, "%06.2f ", data[index]);
        }
        string_append_formated(&result, "\n");
    }
    logg("\n%s\n", result.characters);
}



/*
    Texture
*/
Texture_Filtermode texture_filtermode_make(GLenum minification_mode, GLenum magnification_mode,
    GLenum u_axis_wrapping, GLenum v_axis_wrapping)
{
    Texture_Filtermode result;
    result.minification_mode = minification_mode;
    result.magnification_mode = magnification_mode;
    result.u_axis_wrapping = u_axis_wrapping;
    result.v_axis_wrapping = v_axis_wrapping;
    return result;
}
Texture_Filtermode texture_filtermode_make_nearest() {
    Texture_Filtermode result;
    result.minification_mode = GL_NEAREST;
    result.magnification_mode = GL_NEAREST;
    result.u_axis_wrapping = GL_CLAMP_TO_EDGE;
    result.v_axis_wrapping = GL_CLAMP_TO_EDGE;
    return result;
}
Texture_Filtermode texture_filtermode_make_mipmap() {
    Texture_Filtermode result;
    result.minification_mode = GL_LINEAR_MIPMAP_LINEAR;
    result.magnification_mode = GL_LINEAR;
    result.u_axis_wrapping = GL_CLAMP_TO_EDGE;
    result.v_axis_wrapping = GL_CLAMP_TO_EDGE;
    return result;
}
Texture_Filtermode texture_filtermode_make_linear() {
    Texture_Filtermode result;
    result.minification_mode = GL_LINEAR;
    result.magnification_mode = GL_LINEAR;
    result.u_axis_wrapping = GL_CLAMP_TO_EDGE;
    result.v_axis_wrapping = GL_CLAMP_TO_EDGE;
    return result;
}

Texture texture_create_from_bytes(byte* data, int width, int height, int channel_count,
    GLenum data_format, Texture_Filtermode filtermode, OpenGLState* state)
{
    Texture result;
    result.width = width;
    result.height = height;

    // Determine format
    GLenum texture_format;
    if (channel_count > 4 || channel_count < 0) {
        panic("Texture data more than 4 channels are not supported!\n");
    }

    switch (channel_count)
    {
    case 1: texture_format = GL_RED; break;
    case 2: texture_format = GL_RG; break;
    case 3: texture_format = GL_RGB; break;
    case 4: texture_format = GL_RGBA; break;
    }

    if (channel_count < 4) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }

    result.cpu_data_format = texture_format;
    result.internal_gpu_format = texture_format;
    if (data_format == GL_FLOAT) {
        result.internal_gpu_format = GL_R32F;
    }
    result.sampler_type = GL_SAMPLER_2D;

    glGenTextures(1, &result.texture_id);
    opengl_state_bind_texture(state, GL_TEXTURE_2D, result.texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0,
        result.internal_gpu_format,
        width, height,
        0,
        result.cpu_data_format,
        data_format,
        data);

    // Set texture filtering mode
    result.has_mipmap = false;
    texture_set_texture_filtermode(&result, filtermode, state);

    return result;

}

Texture texture_create_from_bytes(byte* data, int width, int height, int channel_count, Texture_Filtermode filtermode, OpenGLState* state) {
    return texture_create_from_bytes(data, width, height, channel_count, GL_UNSIGNED_BYTE, filtermode, state);
}

Texture texture_create_from_texture_bitmap(Texture_Bitmap* texture_data, Texture_Filtermode filtermode, OpenGLState* state)
{
    return texture_create_from_bytes(texture_data->data.data,
        texture_data->width, texture_data->height,
        texture_data->channel_count, filtermode, state);
}

void texture_destroy(Texture* texture) {
    glDeleteTextures(1, &texture->texture_id);
}


Texture texture_create_test_texture(OpenGLState* state)
{
    Texture_Bitmap bitmap_data = texture_bitmap_create_test_bitmap(32);
    SCOPE_EXIT(texture_bitmap_destroy(&bitmap_data));
    return texture_create_from_texture_bitmap(&bitmap_data, texture_filtermode_make(GL_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT), state);
}

GLint texture_bind_to_unit(Texture* texture, OpenGLState* state) {
    return opengl_state_bind_texture_to_unit(state, GL_TEXTURE_2D, texture->texture_id);
}

void texture_set_texture_filtermode(Texture* texture, Texture_Filtermode filtermode, OpenGLState* state)
{
    opengl_state_bind_texture(state, GL_TEXTURE_2D, texture->texture_id);
    texture->filtermode = filtermode;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtermode.minification_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtermode.magnification_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, filtermode.u_axis_wrapping);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, filtermode.v_axis_wrapping);
    if (!texture->has_mipmap &&
        (filtermode.minification_mode == GL_LINEAR_MIPMAP_LINEAR ||
            filtermode.minification_mode == GL_NEAREST_MIPMAP_LINEAR ||
            filtermode.minification_mode == GL_LINEAR_MIPMAP_NEAREST ||
            filtermode.minification_mode == GL_NEAREST_MIPMAP_NEAREST))
    {
        glGenerateMipmap(GL_TEXTURE_2D);
        texture->has_mipmap = true;
    }
}

