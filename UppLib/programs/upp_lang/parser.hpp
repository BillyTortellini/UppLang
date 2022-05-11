#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/array.hpp"
#include "source_code.hpp"

namespace AST
{
    struct Base;
    struct Module;
}

struct Syntax_Block;
struct Syntax_Range;

namespace Parser 
{
    struct Error_Message
    {
        const char* msg;
        Syntax_Range range;
    };

    enum class Section
    {
        WHOLE,             // Every character, including child text
        WHOLE_NO_CHILDREN, // Every character without child text
        IDENTIFIER,        // Highlight Identifier if the node has any
        KEYWORD,           // Highlight keyword if the node contains one
        ENCLOSURE,         // Highlight enclosures, e.g. (), {}, []
        NONE,              // Not quite sure if this is usefull at all
        END_TOKEN,         // To display that something is missing
    };

    void initialize();
    void reset();
    void destroy();
    AST::Module* execute(Syntax_Block* root_block);
    void ast_base_get_section_token_range(AST::Base* base, Section section, Dynamic_Array<Syntax_Range>* ranges);
    Array<Error_Message> get_error_messages();
}
