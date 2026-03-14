#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/array.hpp"
#include "../../datastructures/allocators.hpp"
#include "source_code.hpp"
#include "tokenizer.hpp"
#include "ast.hpp"

namespace AST
{
    struct Node;
    struct Module;
}

struct Compilation_Unit;
struct Predefined_IDs;
struct Identifier_Pool;

namespace Parser 
{
    // PARSER
    void execute_clean(Compilation_Unit* unit, Identifier_Pool* identifier_pool, Arena* arena);

    // Utility
    void ast_base_get_section_token_range(Source_Code* code, AST::Node* base, Node_Section section, Dynamic_Array<Text_Range>* ranges);
}
