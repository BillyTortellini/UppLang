#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/array.hpp"
#include "source_code.hpp"
#include "code_history.hpp"
#include "lexer.hpp"


namespace AST
{
    struct Base;
    struct Module;
}

namespace Parser 
{
    struct Error_Message
    {
        const char* msg;
        Token_Range range;
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
    struct Parse_Info
    {
        AST::Base* allocation;
        Token_Range range;
        Token_Range bounding_range;
    };


    void initialize();
    void reset();
    void destroy();
    AST::Module* execute(Source_Code* code);
    AST::Module* execute_incrementally(Code_History* history);

    Parse_Info* get_parse_info(AST::Base* base);
    void ast_base_get_section_token_range(AST::Base* base, Section section, Dynamic_Array<Token_Range>* ranges);
    Array<Error_Message> get_error_messages();
    AST::Base* find_smallest_enclosing_node(AST::Base* base, Token_Index index);
}
