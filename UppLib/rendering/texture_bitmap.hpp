#pragma once

#include "../utility/datatypes.hpp"
#include "../datastructures/array.hpp"
#include "../utility/binary_parser.hpp"

struct Texture_Bitmap
{
    int width;
    int height;
    int channel_count;
    Array<byte> data;
};

Texture_Bitmap texture_bitmap_create_from_data(int width, int height, int channel_count, byte* data);
Texture_Bitmap texture_bitmap_create_from_data_with_pitch(int width, int height, int pitch, byte* data);
Texture_Bitmap texture_bitmap_create_from_bitmap_with_pitch(int width, int height, int pitch, byte* data);
Texture_Bitmap texture_bitmap_create_empty(int width, int height, int channel_count);
Texture_Bitmap texture_bitmap_create_empty_mono(int width, int height, byte fill_value);
Texture_Bitmap texture_bitmap_create_test_bitmap(int size);
void texture_bitmap_destroy(Texture_Bitmap* texture_data);

Texture_Bitmap texture_bitmap_binary_parser_read(BinaryParser* parser);
void texture_bitmap_binary_parser_write(Texture_Bitmap* bitmap, BinaryParser* parser);

Array<float> texture_bitmap_create_distance_field(Texture_Bitmap* source);
Array<float> texture_bitmap_create_distance_field_bad(Texture_Bitmap* source);
void texture_bitmap_print_distance_field(Array<float> data, int width);
void texture_bitmap_inpaint_complete(Texture_Bitmap* destination, Texture_Bitmap* source, int position_x, int position_y);

