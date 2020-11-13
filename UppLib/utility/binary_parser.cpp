#include "binary_parser.hpp"

#include <cstring>
#include "../utility/file_io.hpp"

BinaryParser binary_parser_create_empty(int capacity) {
    BinaryParser result;
    result.current_position = 0;
    result.data = dynamic_array_create_empty<byte>(capacity);
    return result;
}

BinaryParser binary_parser_create_from_bytes(Array<byte> data_to_read)
{
    BinaryParser result;
    result.current_position = 0;
    result.data = dynamic_array_create_copy<byte>(data_to_read.data, data_to_read.size);
    return result;
}

Optional<BinaryParser> binary_parser_create_from_file(const char* filename)
{
    BinaryParser result;
    result.current_position = 0;
    Optional<Array<byte>> file_data = file_io_load_binary_file(filename);
    if (!file_data.available) {
        return optional_make_failure<BinaryParser>();
    }
    result.data = array_to_dynamic_array(&file_data.value);
    return optional_make_success(result);
}

void binary_parser_destroy(BinaryParser* parser) {
    dynamic_array_destroy(&parser->data);
}

bool binary_parser_write_to_file(BinaryParser* parser, const char* filepath) {
    return file_io_write_file(filepath, dynamic_array_to_array(&parser->data));
}

Array<byte> binary_parser_get_data(BinaryParser* parser) {
    return dynamic_array_to_array(&parser->data);
}

void binary_parser_write_bytes(BinaryParser* parser, Array<byte> data)
{
    dynamic_array_reserve(&parser->data, parser->data.size + data.size+1);
    memcpy(parser->data.data + parser->data.size, data.data, data.size);
    parser->data.size += data.size;
    parser->current_position += data.size;
}

void binary_parser_write_byte(BinaryParser* parser, byte value) {
    dynamic_array_push_back(&parser->data, value);
    parser->current_position += 1;
}

void binary_parser_write_int(BinaryParser* parser, int value) {
    byte* data = (byte*)&value;
    dynamic_array_push_back(&parser->data, *(data+0));
    dynamic_array_push_back(&parser->data, *(data+1));
    dynamic_array_push_back(&parser->data, *(data+2));
    dynamic_array_push_back(&parser->data, *(data+3));
    parser->current_position += 4;
}

void binary_parser_write_float(BinaryParser* parser, float value) {
    byte* data = (byte*)&value;
    dynamic_array_push_back(&parser->data, *(data+0));
    dynamic_array_push_back(&parser->data, *(data+1));
    dynamic_array_push_back(&parser->data, *(data+2));
    dynamic_array_push_back(&parser->data, *(data+3));
    parser->current_position += 4;
}

byte binary_parser_read_byte(BinaryParser* parser) {
    if (parser->current_position + 1 >= parser->data.size) {
        panic("Parser reading over given data!\n");
    }
    
    byte value = parser->data[parser->current_position];
    parser->current_position += 1;
    return value;
}

int binary_parser_read_int(BinaryParser* parser) 
{
    if (parser->current_position + 4 >= parser->data.size) {
        panic("Parser reading over given data!\n");
    }
    
    int value;
    *(((byte*)&value) + 0) = parser->data[parser->current_position];
    *(((byte*)&value) + 1) = parser->data[parser->current_position+1];
    *(((byte*)&value) + 2) = parser->data[parser->current_position+2];
    *(((byte*)&value) + 3) = parser->data[parser->current_position+3];
    parser->current_position += 4;
    return value;
}

float binary_parser_read_float(BinaryParser* parser) 
{
    if (parser->current_position + 4 >= parser->data.size) {
        panic("Parser reading over given data!\n");
    }
    
    float value;
    *(((byte*)&value) + 0) = parser->data[parser->current_position];
    *(((byte*)&value) + 1) = parser->data[parser->current_position+1];
    *(((byte*)&value) + 2) = parser->data[parser->current_position+2];
    *(((byte*)&value) + 3) = parser->data[parser->current_position+3];
    parser->current_position += 4;
    return value;
}

void binary_parser_read_bytes(BinaryParser* parser, Array<byte> destination)
{
    memcpy(destination.data, parser->data.data+parser->current_position, destination.size);
    parser->current_position += destination.size;
}
