#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/array.hpp"
#include "source_code.hpp"
#include "code_history.hpp"
#include "lexer.hpp"
#include "ast.hpp"


namespace AST
{
    struct Node;
    struct Module;
}

namespace Parser 
{
    // PARSER
    void initialize();
    void reset();
    void destroy();

    // Parse Items (Incremental Parsing)
    struct Block_Parse;
    struct Error_Message
    {
        const char* msg;
        AST::Node_Range range;
        Block_Parse* block_parse;
        int origin_line_index; // May not be the same as the line index of token_range. Required for incremental parsing
    };

    struct Line_Item
    {
        int line_start;
        int line_count; 
        AST::Node* node;
    };

    enum class Block_Context
    {
        STATEMENTS,
        STRUCT,
        SWITCH,
        MODULE,
        ENUM,
    };

    struct Block_Parse
    {
        Block_Context context;
        Block_Index index;
        Dynamic_Array<Line_Item> items;
        Dynamic_Array<Block_Parse*> child_block_parses;
        Block_Parse* parent_parse;
        AST::Node* parent;
        int line_count;
    };

    struct Parsed_Code
    {
        Source_Code* code;
        AST::Module* root;
        Dynamic_Array<Error_Message> error_messages;
        Hashtable<Block_Index, Block_Parse*> block_parses;
        History_Timestamp timestamp;
    };

    Parsed_Code* execute_clean(Source_Code* code);
    void execute_incremental(Parsed_Code* parsed_code, Code_History* history);
    void source_parse_destroy(Parsed_Code* parsed_code);



    // Utility
    enum class Section
    {
        WHOLE,             // Every character, including child text
        WHOLE_NO_CHILDREN, // Every character without child text
        IDENTIFIER,        // Highlight Identifier if the node has any
        KEYWORD,           // Highlight keyword if the node contains one
        ENCLOSURE,         // Highlight enclosures, e.g. (), {}, []
        NONE,              // Not quite sure if this is usefull at all
        FIRST_TOKEN,       // To display error that isn't specific to all internal tokens
        END_TOKEN,         // To display that something is missing
    };

    void ast_base_get_section_token_range(AST::Node* base, Section section, Dynamic_Array<Token_Range>* ranges);
    AST::Node* find_smallest_enclosing_node(AST::Node* base, Token_Index index);
}
