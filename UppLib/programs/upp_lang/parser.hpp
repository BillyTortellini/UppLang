#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/array.hpp"
#include "source_code.hpp"
#include "lexer.hpp"
#include "ast.hpp"


namespace AST
{
    struct Node;
    struct Module;
}

struct Compilation_Unit;
struct Predefined_IDs;

namespace Parser 
{
    // PARSER
    void execute_clean(Compilation_Unit* unit, Predefined_IDs* predefined_ids);

    // Utility
    void ast_base_get_section_token_range(Source_Code* code, AST::Node* base, Node_Section section, Dynamic_Array<Token_Range>* ranges);
}
