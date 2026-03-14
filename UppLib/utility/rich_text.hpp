#pragma once

#include "../math/vectors.hpp"
#include "../programs/upp_lang/syntax_colors.hpp"
#include "../datastructures/array.hpp"
#include "../datastructures/allocators.hpp"

struct Raster_Font;
struct Text_Renderer;
struct Renderer_2D;
struct Render_Pass;
struct Arena;
struct String;

enum class Style_Code : u8
{
    TEXT_COLOR,
    BACKGROUND_COLOR,
    UNDERLINE,
    PUSH_STYLE, // Saves current style
    POP_STYLE,  // Returns style to last saved
};

void string_style_add_code(String* str, Style_Code code, Syntax_Color color = Syntax_Color::NONE);
void string_style_remove_codes(String* str);
// Note: line count is always >= 1
ivec2 string_style_calculated_2D_size(String str);



struct Rich_Char
{
    Syntax_Color text_color;
    Syntax_Color background_color;
    Syntax_Color underline_color;
    char character;
};

enum class Mark_Type : u8
{
    TEXT_COLOR,
    BACKGROUND_COLOR,
    UNDERLINE
};

struct Rich_Text_Area
{
    ivec2 size;
    Array<Rich_Char> buffer; // Buffer is stored top-down, y = line-index
    Arena* arena; // For convenience in pen 
    Rich_Char dummy;

    static Rich_Text_Area create(Arena* arena, ivec2 size);
    void Rich_Text_Area::fill_from_string(
        String string,
        Syntax_Color default_text = Syntax_Color::WHITE,
        Syntax_Color default_bg = Syntax_Color::NONE,
        Syntax_Color default_underline = Syntax_Color::NONE
    );
    Rich_Char* get_char(ivec2 pos);
    void set_text(ivec2 pos, String string);
    void mark(ivec2 pos, int length, Mark_Type type, Syntax_Color color);
    void render(ibox2 box, Raster_Font* font, Renderer_2D* renderer_2D, Render_Pass* render_pass);
};
