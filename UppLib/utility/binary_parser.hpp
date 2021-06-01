#pragma once

#include "../datastructures/dynamic_array.hpp"
#include "../utility/utils.hpp"

struct BinaryParser
{
    Dynamic_Array<byte> data;
    int current_position;
};

BinaryParser binary_parser_create_empty(int capacity);
BinaryParser binary_parser_create_from_bytes(Array<byte> data_to_read);
Optional<BinaryParser> binary_parser_create_from_file(const char* filename);
void binary_parser_destroy(BinaryParser* parser);

bool binary_parser_write_to_file(BinaryParser* parser, const char* filepath);
Array<byte> binary_parser_get_data(BinaryParser* parser);

void binary_parser_write_bytes(BinaryParser* parser, Array<byte> data);
void binary_parser_write_byte(BinaryParser* parser, byte value);
void binary_parser_write_int(BinaryParser* parser, int value);
void binary_parser_write_float(BinaryParser* parser, float value);

void binary_parser_read_bytes(BinaryParser* parser, Array<byte> destination);
byte binary_parser_read_byte(BinaryParser* parser);
int binary_parser_read_int(BinaryParser* parser);
float binary_parser_read_float(BinaryParser* parser);