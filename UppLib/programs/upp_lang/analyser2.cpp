#include "analyser2.hpp"

#include "ast.hpp"
#include "../../utility/utils.hpp"

namespace Semantic_Analysis
{
    using namespace AST;

    struct Symbol
    {
        String* id;
        Base* origin;
    };

    struct Symbol_Table
    {

    };

    union Information
    {
        Symbol_Table* module_table;

    };

    struct Analyser
    {
        Module* root;
        Dynamic_Array<Information> infos;
        int information_offset;
    };

    Analyser analyser;

    void initialize()
    {
        analyser.root = 0;
    }

    void destroy()
    {

    }

    void enumerate_bases(Base* base, int* index)
    {
        base->index = *index;
        *index += 1;
        Dynamic_Array<Base*> children = dynamic_array_create_empty<Base*>(1);
        SCOPE_EXIT(dynamic_array_destroy(&children));
        for (int i = 0; i < children.size; i++) {
            enumerate_bases(children[i], index);
        }
    }

    void analyse_ast(Module* root)
    {
        analyser.root = root;
        // Enumerate all bases
        int index = 0;
        enumerate_bases(&root->base, &index);
    }
}