#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/array.hpp"
#include "../../datastructures/allocators.hpp"
#include "source_code.hpp"
#include "tokenizer.hpp"
#include "ast.hpp"
#include "compiler_misc.hpp"

/*
Some parsing decisions that are not obvious:
    Foreach loop works with and without parenthesis, and uses semicolon
        loop i := 0; i < 10; i += 1
    Anonymous scopes require the scope keyword (I think)
        x := 1
        scope
            x := 10
    Struct-Initializer uses {} parenthesis
        x := Player.{"Peter", 15, alive = true}
    Parenthesis only work in context of continuation, so without seperators this does not parse:
        Player :: struct {
            a: name
            b: name
        }
*/

namespace AST
{
    struct Node;
}

struct Compilation_Data;
struct Compilation_Unit;
struct Predefined_IDs;
struct Identifier_Pool;

namespace Parser 
{
    // PARSER
    void execute_clean(Compilation_Unit* unit, Compilation_Data* compilation_data);

    // Utility
    DynArray<Text_Range> ast_base_get_section_token_range(Source_Code* code, AST::Node* base, Node_Section section, Arena* arena);
}
