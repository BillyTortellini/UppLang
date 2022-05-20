#include "ast.hpp"

namespace AST
{
    void base_destroy(Base* node)
    {
        switch (node->type)
        {
        case Base_Type::PROJECT_IMPORT: 
        case Base_Type::PARAMETER: 
        case Base_Type::ARGUMENT: 
        case Base_Type::SYMBOL_READ: 
        case Base_Type::DEFINITION: 
            break;
        case Base_Type::CODE_BLOCK: {
            auto block = (Code_Block*)node;
            if (block->statements.data != 0) {
                dynamic_array_destroy(&block->statements);
            }
            break;
        }
        case Base_Type::MODULE: {
            auto module = (Module*)node;
            if (module->definitions.data != 0) {
                dynamic_array_destroy(&module->definitions);
            }
            if (module->imports.data != 0) {
                dynamic_array_destroy(&module->imports);
            }
            break;
        }
        case Base_Type::EXPRESSION:
        {
            auto expr = (Expression*)node;
            switch (expr->type)
            {
            case Expression_Type::STRUCT_INITIALIZER: {
                auto& init = expr->options.struct_initializer;
                dynamic_array_destroy(&init.arguments);
                break;
            }
            case Expression_Type::ARRAY_INITIALIZER: {
                auto& init = expr->options.array_initializer;
                dynamic_array_destroy(&init.values);
                break;
            }
            case Expression_Type::FUNCTION_CALL: {
                auto& call = expr->options.call;
                if (call.arguments.data != 0) {
                    dynamic_array_destroy(&call.arguments);
                }
                break;
            }
            case Expression_Type::FUNCTION_SIGNATURE: {
                auto& sig = expr->options.function_signature;
                if (sig.parameters.data != 0) {
                    dynamic_array_destroy(&sig.parameters);
                }
                break;
            }
            case Expression_Type::STRUCTURE_TYPE: {
                auto& members = expr->options.structure.members;
                if (members.data != 0) {
                    dynamic_array_destroy(&members);
                }
                break;
            }
            case Expression_Type::ENUM_TYPE: {
                auto& members = expr->options.enum_members;
                if (members.data != 0) {
                    dynamic_array_destroy(&members);
                }
                break;
            }
            }
            break;
        }
        case Base_Type::STATEMENT: {
            auto stat = (Statement*)node;
            switch (stat->type)
            {
            case Statement_Type::SWITCH_STATEMENT: {
                auto cases = stat->options.switch_statement.cases;
                if (cases.data != 0) {
                    dynamic_array_destroy(&cases);
                }
                break;
            }
            }
            break;
        }
        default: panic("");
        }
        delete node;
    }
    
    Base* base_get_child(Base* node, int child_index)
    {
        int index = 0;
#define FILL(x) { if (child_index == index) {return &x->base;} else {index += 1;}}
#define FILL_OPTIONAL(x) if (x.available) {FILL(x.value);}
#define FILL_ARRAY(x) {if (child_index < index + x.size) {return &x[child_index - index]->base;} else {index += x.size;}}
        switch (node->type)
        {
        case Base_Type::PARAMETER: {
            auto param = (Parameter*)node;
            FILL(param->type);
            FILL_OPTIONAL(param->default_value);
            break;
        }
        case Base_Type::SYMBOL_READ: {
            auto read = (Symbol_Read*)node;
            FILL_OPTIONAL(read->path_child);
            break;
        }
        case Base_Type::ARGUMENT: {
            auto arg = (Argument*)node;
            FILL(arg->value);
            break;
        }
        case Base_Type::CODE_BLOCK: {
            auto block = (Code_Block*)node;
            FILL_ARRAY(block->statements);
            break;
        }
        case Base_Type::DEFINITION: {
            auto def = (Definition*)node;
            FILL_OPTIONAL(def->type);
            FILL_OPTIONAL(def->value);
            break;
        }
        case Base_Type::PROJECT_IMPORT: {
            break;
        }
        case Base_Type::MODULE: {
            auto module = (Module*)node;
            FILL_ARRAY(module->imports);
            FILL_ARRAY(module->definitions);
            break;
        }
        case Base_Type::EXPRESSION:
        {
            auto expr = (Expression*)node;
            switch (expr->type)
            {
            case Expression_Type::BINARY_OPERATION: {
                auto& binop = expr->options.binop;
                FILL(binop.left);
                FILL(binop.right);
                break;
            }
            case Expression_Type::UNARY_OPERATION: {
                auto& unop = expr->options.unop;
                FILL(unop.expr);
                break;
            }
            case Expression_Type::NEW_EXPR: {
                auto& new_expr = expr->options.new_expr;
                FILL_OPTIONAL(new_expr.count_expr);
                FILL(new_expr.type_expr);
                break;
            }
            case Expression_Type::CAST: {
                auto& cast = expr->options.cast;
                FILL_OPTIONAL(cast.to_type);
                FILL(cast.operand);
                break;
            }
            case Expression_Type::SYMBOL_READ: {
                FILL(expr->options.symbol_read);
                break;
            }
            case Expression_Type::LITERAL_READ: {
                break;
            }
            case Expression_Type::ARRAY_ACCESS: {
                auto& access = expr->options.array_access;
                FILL(access.array_expr);
                FILL(access.index_expr);
                break;
            }
            case Expression_Type::MEMBER_ACCESS: {
                auto& access = expr->options.member_access;
                FILL(access.expr);
                break;
            }
            case Expression_Type::MODULE: {
                auto& module = expr->options.module;
                FILL(module);
                break;
            }
            case Expression_Type::STRUCT_INITIALIZER: {
                auto& init = expr->options.struct_initializer;
                FILL_OPTIONAL(init.type_expr);
                FILL_ARRAY(init.arguments);
                break;
            }
            case Expression_Type::BAKE_BLOCK: {
                FILL(expr->options.bake_block);
                break;
            }
            case Expression_Type::BAKE_EXPR: {
                FILL(expr->options.bake_expr);
                break;
            }
            case Expression_Type::ARRAY_INITIALIZER: {
                auto& init = expr->options.array_initializer;
                FILL_OPTIONAL(init.type_expr);
                FILL_ARRAY(init.values);
                break;
            }
            case Expression_Type::ARRAY_TYPE: {
                auto& array = expr->options.array_type;
                FILL(array.size_expr);
                FILL(array.type_expr);
                break;
            }
            case Expression_Type::SLICE_TYPE: {
                auto& slice = expr->options.slice_type;
                FILL(slice);
                break;
            }
            case Expression_Type::AUTO_ENUM: {
                break;
            }
            case Expression_Type::FUNCTION: {
                auto& func = expr->options.function;
                FILL(func.signature);
                FILL(func.body);
                break;
            }
            case Expression_Type::ERROR_EXPR: {
                break;
            }
            case Expression_Type::FUNCTION_CALL: {
                auto& call = expr->options.call;
                FILL(call.expr);
                FILL_ARRAY(call.arguments);
                break;
            }
            case Expression_Type::FUNCTION_SIGNATURE: {
                auto& sig = expr->options.function_signature;
                FILL_ARRAY(sig.parameters);
                FILL_OPTIONAL(sig.return_value);
                break;
            }
            case Expression_Type::STRUCTURE_TYPE: {
                auto& str = expr->options.structure;
                FILL_ARRAY(str.members);
                break;
            }
            case Expression_Type::ENUM_TYPE: {
                //auto& members = expr->options.enum_members;
                //FILL_ARRAY(members);
                break;
            }
            default: panic("");
            }
            break;
        }
        case Base_Type::STATEMENT:
        {
            auto stat = (Statement*)node;
            switch (stat->type)
            {
            case Statement_Type::DEFINITION: {
                auto def = stat->options.definition;
                FILL(def);
                break;
            }
            case Statement_Type::BLOCK: {
                auto block = stat->options.block;
                FILL(block);
                break;
            }
            case Statement_Type::ASSIGNMENT: {
                auto ass = stat->options.assignment;
                FILL(ass.left_side);
                FILL(ass.right_side);
                break;
            }
            case Statement_Type::EXPRESSION_STATEMENT: {
                auto expr = stat->options.expression;
                FILL(expr);
                break;
            }
            case Statement_Type::DEFER: {
                auto defer = stat->options.defer_block;
                FILL(defer);
                break;
            }
            case Statement_Type::IF_STATEMENT: {
                auto if_stat = stat->options.if_statement;
                FILL(if_stat.condition);
                FILL(if_stat.block);
                FILL_OPTIONAL(if_stat.else_block);
                break;
            }
            case Statement_Type::WHILE_STATEMENT: {
                auto while_stat = stat->options.while_statement;
                FILL(while_stat.condition);
                FILL(while_stat.block);
                break;
            }
            case Statement_Type::BREAK_STATEMENT: {
                break;
            }
            case Statement_Type::CONTINUE_STATEMENT: {
                break;
            }
            case Statement_Type::RETURN_STATEMENT: {
                auto ret = stat->options.return_value;
                FILL_OPTIONAL(ret);
                break;
            }
            case Statement_Type::DELETE_STATEMENT: {
                auto del = stat->options.delete_expr;
                FILL(del);
                break;
            }
            case Statement_Type::SWITCH_STATEMENT: {
                auto cases = stat->options.switch_statement.cases;
                FILL(stat->options.switch_statement.condition);
                for (int i = 0; i < cases.size; i++) {
                    auto& cas = cases[i];
                    FILL_OPTIONAL(cas.value);
                    FILL(cas.block);
                }
                break;
            }
            default: panic("HEY");
            }
            break;
        }
        default: panic("");
        }
        return 0;
#undef FILL
#undef FILL_OPTIONAL
#undef FILL_ARRAY
    }

    void base_enumerate_children(Base* node, Dynamic_Array<Base*>* fill)
    {
#define FILL(x) {dynamic_array_push_back(fill, &x->base);};
#define FILL_OPTIONAL(x) if (x.available) {dynamic_array_push_back(fill, &x.value->base);}
#define FILL_ARRAY(x) for (int i = 0; i < x.size; i++) {dynamic_array_push_back(fill, &x[i]->base);}
        switch (node->type)
        {
        case Base_Type::PROJECT_IMPORT: {
            break;
        }
        case Base_Type::PARAMETER: {
            auto param = (Parameter*)node;
            FILL(param->type);
            FILL_OPTIONAL(param->default_value);
            break;
        }
        case Base_Type::SYMBOL_READ: {
            auto read = (Symbol_Read*)node;
            FILL_OPTIONAL(read->path_child);
            break;
        }
        case Base_Type::ARGUMENT: {
            auto arg = (Argument*)node;
            FILL(arg->value);
            break;
        }
        case Base_Type::CODE_BLOCK: {
            auto block = (Code_Block*)node;
            FILL_ARRAY(block->statements);
            break;
        }
        case Base_Type::DEFINITION: {
            auto def = (Definition*)node;
            FILL_OPTIONAL(def->type);
            FILL_OPTIONAL(def->value);
            break;
        }
        case Base_Type::MODULE: {
            auto module = (Module*)node;
            FILL_ARRAY(module->imports);
            FILL_ARRAY(module->definitions);
            break;
        }
        case Base_Type::EXPRESSION:
        {
            auto expr = (Expression*)node;
            switch (expr->type)
            {
            case Expression_Type::BINARY_OPERATION: {
                auto& binop = expr->options.binop;
                FILL(binop.left);
                FILL(binop.right);
                break;
            }
            case Expression_Type::UNARY_OPERATION: {
                auto& unop = expr->options.unop;
                FILL(unop.expr);
                break;
            }
            case Expression_Type::NEW_EXPR: {
                auto& new_expr = expr->options.new_expr;
                FILL_OPTIONAL(new_expr.count_expr);
                FILL(new_expr.type_expr);
                break;
            }
            case Expression_Type::CAST: {
                auto& cast = expr->options.cast;
                FILL_OPTIONAL(cast.to_type);
                FILL(cast.operand);
                break;
            }
            case Expression_Type::SYMBOL_READ: {
                FILL(expr->options.symbol_read);
                break;
            }
            case Expression_Type::LITERAL_READ: {
                break;
            }
            case Expression_Type::ARRAY_ACCESS: {
                auto& access = expr->options.array_access;
                FILL(access.array_expr);
                FILL(access.index_expr);
                break;
            }
            case Expression_Type::MEMBER_ACCESS: {
                auto& access = expr->options.member_access;
                FILL(access.expr);
                break;
            }
            case Expression_Type::MODULE: {
                auto& module = expr->options.module;
                FILL(module);
                break;
            }
            case Expression_Type::STRUCT_INITIALIZER: {
                auto& init = expr->options.struct_initializer;
                FILL_OPTIONAL(init.type_expr);
                FILL_ARRAY(init.arguments);
                break;
            }
            case Expression_Type::BAKE_BLOCK: {
                FILL(expr->options.bake_block);
                break;
            }
            case Expression_Type::BAKE_EXPR: {
                FILL(expr->options.bake_expr);
                break;
            }
            case Expression_Type::ARRAY_INITIALIZER: {
                auto& init = expr->options.array_initializer;
                FILL_OPTIONAL(init.type_expr);
                FILL_ARRAY(init.values);
                break;
            }
            case Expression_Type::ARRAY_TYPE: {
                auto& array = expr->options.array_type;
                FILL(array.size_expr);
                FILL(array.type_expr);
                break;
            }
            case Expression_Type::SLICE_TYPE: {
                auto& slice = expr->options.slice_type;
                FILL(slice);
                break;
            }
            case Expression_Type::AUTO_ENUM: {
                break;
            }
            case Expression_Type::FUNCTION: {
                auto& func = expr->options.function;
                FILL(func.signature);
                FILL(func.body);
                break;
            }
            case Expression_Type::ERROR_EXPR: {
                break;
            }
            case Expression_Type::FUNCTION_CALL: {
                auto& call = expr->options.call;
                FILL(call.expr);
                FILL_ARRAY(call.arguments);
                break;
            }
            case Expression_Type::FUNCTION_SIGNATURE: {
                auto& sig = expr->options.function_signature;
                FILL_ARRAY(sig.parameters);
                FILL_OPTIONAL(sig.return_value);
                break;
            }
            case Expression_Type::STRUCTURE_TYPE: {
                auto& str = expr->options.structure;
                FILL_ARRAY(str.members);
                break;
            }
            case Expression_Type::ENUM_TYPE: {
                //auto& members = expr->options.enum_members;
                //FILL_ARRAY(members);
                break;
            }
            default: panic("");
            }
            break;
        }
        case Base_Type::STATEMENT:
        {
            auto stat = (Statement*)node;
            switch (stat->type)
            {
            case Statement_Type::DEFINITION: {
                auto def = stat->options.definition;
                FILL(def);
                break;
            }
            case Statement_Type::BLOCK: {
                auto block = stat->options.block;
                FILL(block);
                break;
            }
            case Statement_Type::ASSIGNMENT: {
                auto ass = stat->options.assignment;
                FILL(ass.left_side);
                FILL(ass.right_side);
                break;
            }
            case Statement_Type::EXPRESSION_STATEMENT: {
                auto expr = stat->options.expression;
                FILL(expr);
                break;
            }
            case Statement_Type::DEFER: {
                auto defer = stat->options.defer_block;
                FILL(defer);
                break;
            }
            case Statement_Type::IF_STATEMENT: {
                auto if_stat = stat->options.if_statement;
                FILL(if_stat.condition);
                FILL(if_stat.block);
                FILL_OPTIONAL(if_stat.else_block);
                break;
            }
            case Statement_Type::WHILE_STATEMENT: {
                auto while_stat = stat->options.while_statement;
                FILL(while_stat.condition);
                FILL(while_stat.block);
                break;
            }
            case Statement_Type::BREAK_STATEMENT: {
                break;
            }
            case Statement_Type::CONTINUE_STATEMENT: {
                break;
            }
            case Statement_Type::RETURN_STATEMENT: {
                auto ret = stat->options.return_value;
                FILL_OPTIONAL(ret);
                break;
            }
            case Statement_Type::DELETE_STATEMENT: {
                auto del = stat->options.delete_expr;
                FILL(del);
                break;
            }
            case Statement_Type::SWITCH_STATEMENT: {
                auto& cases = stat->options.switch_statement.cases;
                FILL(stat->options.switch_statement.condition);
                for (int i = 0; i < cases.size; i++) {
                    auto& cas = cases[i];
                    FILL_OPTIONAL(cas.value);
                    FILL(cas.block);
                }
                break;
            }
            default: panic("HEY");
            }
            break;
        }
        default: panic("");
        }
#undef FILL
#undef FILL_OPTIONAL
#undef FILL_ARRAY
    }

    void base_append_to_string(Base* base, String* str)
    {
        switch (base->type)
        {
        case Base_Type::DEFINITION:
            string_append_formated(str, "DEFINITION ");
            string_append_string(str, ((Definition*)base)->name);
            break;
        case Base_Type::PROJECT_IMPORT:
            string_append_formated(str, "IMPORT ");
            string_append_string(str, ((Project_Import*)base)->filename);
            break;
        case Base_Type::SYMBOL_READ:
            string_append_formated(str, "SYMBOL_READ ");
            string_append_string(str, ((Symbol_Read*)base)->name);
            break;
        case Base_Type::CODE_BLOCK: string_append_formated(str, "CODE_BLOCK"); break;
        case Base_Type::MODULE: string_append_formated(str, "MODULE"); break;
        case Base_Type::ARGUMENT: {
            string_append_formated(str, "ARGUMENT");
            auto arg = (Argument*)base;
            if (arg->name.available) {
                string_append_formated(str, " ");
                string_append_string(str, arg->name.value);
            }
            break;
        }
        case Base_Type::PARAMETER: {
            auto param = (Parameter*)base;
            string_append_formated(str, "PARAMETER ");
            string_append_string(str, param->name);
            break;
        }
        case Base_Type::EXPRESSION:
        {
            auto expr = (Expression*)base;
            switch (expr->type)
            {
            case Expression_Type::BINARY_OPERATION: string_append_formated(str, "BINARY_OPERATION"); break;
            case Expression_Type::UNARY_OPERATION: string_append_formated(str, "UNARY_OPERATION"); break;
            case Expression_Type::FUNCTION_CALL: string_append_formated(str, "FUNCTION_CALL"); break;
            case Expression_Type::NEW_EXPR: string_append_formated(str, "NEW_EXPR"); break;
            case Expression_Type::CAST: string_append_formated(str, "CAST"); break;
            case Expression_Type::BAKE_BLOCK: string_append_formated(str, "BAKE_BLOCK"); break;
            case Expression_Type::BAKE_EXPR: string_append_formated(str, "BAKE_EXPR"); break;
            case Expression_Type::SYMBOL_READ: string_append_formated(str, "SYMBOL_READ "); break;
            case Expression_Type::LITERAL_READ: string_append_formated(str, "LITERAL_READ"); break;
            case Expression_Type::ARRAY_ACCESS: string_append_formated(str, "ARRAY_ACCESS"); break;
            case Expression_Type::MEMBER_ACCESS: string_append_formated(str, "MEMBER_ACCESS"); break;
            case Expression_Type::MODULE: string_append_formated(str, "MODULE"); break;
            case Expression_Type::FUNCTION: string_append_formated(str, "FUNCTION"); break;
            case Expression_Type::FUNCTION_SIGNATURE: string_append_formated(str, "FUNCTION_SIGNATURE"); break;
            case Expression_Type::STRUCTURE_TYPE: string_append_formated(str, "STRUCTURE_TYPE"); break;
            case Expression_Type::ENUM_TYPE: string_append_formated(str, "ENUM_TYPE"); break;
            case Expression_Type::ARRAY_TYPE: string_append_formated(str, "ARRAY_TYPE"); break;
            case Expression_Type::SLICE_TYPE: string_append_formated(str, "SLICE_TYPE"); break;
            case Expression_Type::ERROR_EXPR: string_append_formated(str, "ERROR_EXPR"); break;
            case Expression_Type::STRUCT_INITIALIZER: string_append_formated(str, "STRUCT_INITIALIZER"); break;
            case Expression_Type::ARRAY_INITIALIZER: string_append_formated(str, "ARRAY_INITIZALIZER"); break;
            case Expression_Type::AUTO_ENUM: string_append_formated(str, "AUTO_ENUM"); break;
            default: panic("");
            }
            break;
        }
        case Base_Type::STATEMENT:
        {
            auto stat = (Statement*)base;
            switch (stat->type)
            {
            case Statement_Type::DEFINITION: string_append_formated(str, "STAT_DEF"); break;
            case Statement_Type::BLOCK: string_append_formated(str, "STAT_BLOCK"); break;
            case Statement_Type::ASSIGNMENT: string_append_formated(str, "ASSIGNMENT"); break;
            case Statement_Type::EXPRESSION_STATEMENT: string_append_formated(str, "EXPRESSION_STATEMENT"); break;
            case Statement_Type::DEFER: string_append_formated(str, "DEFER"); break;
            case Statement_Type::IF_STATEMENT: string_append_formated(str, "IF_STATEMENT"); break;
            case Statement_Type::WHILE_STATEMENT: string_append_formated(str, "WHILE_STATEMENT"); break;
            case Statement_Type::SWITCH_STATEMENT: string_append_formated(str, "SWITCH_STATEMENT"); break;
            case Statement_Type::BREAK_STATEMENT: string_append_formated(str, "BREAK_STATEMENT"); break;
            case Statement_Type::CONTINUE_STATEMENT: string_append_formated(str, "CONTINUE_STATEMENT"); break;
            case Statement_Type::RETURN_STATEMENT: string_append_formated(str, "RETURN_STATEMENT"); break;
            case Statement_Type::DELETE_STATEMENT: string_append_formated(str, "DELETE_STATEMENT"); break;
            default:panic("");
            }
            break;
        }
        default:panic("");
        }
    }

    void base_append_to_string_recursive(Base* base, String* str, int indentation)
    {
        base_append_to_string(base, str);
        Dynamic_Array<Base*> children = dynamic_array_create_empty<Base*>(1);
        SCOPE_EXIT(dynamic_array_destroy(&children));
        base_enumerate_children(base, &children);

        if (children.size == 1) {
            string_append_formated(str, ": ");
            base_append_to_string_recursive(children[0], str, indentation + 1);
        }
        else {
            string_append_formated(str, "\n");
            for (int i = 0; i < children.size; i++) {
                for (int i = 0; i < indentation + 1; i++) {
                    string_append_formated(str, "  ");
                }
                base_append_to_string_recursive(children[i], str, indentation + 1);
            }
        }
    }

    void base_print(Base* node)
    {
        String text = string_create_empty(1024);
        SCOPE_EXIT(string_destroy(&text));
        base_append_to_string_recursive(node, &text, 0);
        logg("AST:\n------------------------\n%s\n", text.characters);
    }

    int binop_priority(Binop binop)
    {
        switch (binop)
        {
        case Binop::AND: return 0;
        case Binop::OR: return 1;
        case Binop::POINTER_EQUAL: return 2;
        case Binop::POINTER_NOT_EQUAL: return 2;
        case Binop::EQUAL: return 2;
        case Binop::NOT_EQUAL: return 2;
        case Binop::GREATER: return 3;
        case Binop::GREATER_OR_EQUAL: return 3;
        case Binop::LESS: return 3;
        case Binop::LESS_OR_EQUAL: return 3;
        case Binop::ADDITION: return 4;
        case Binop::SUBTRACTION: return 4;
        case Binop::MULTIPLICATION: return 5;
        case Binop::DIVISION: return 5;
        case Binop::MODULO: return 6;
        default: panic("");
        }
        panic("");
        return 0;
    }

    void symbol_read_append_to_string(Symbol_Read* read, String* string)
    {
        string_append_string(string, read->name);
        if (read->path_child.available) {
            string_append_formated(string, "~");
            symbol_read_append_to_string(read->path_child.value, string);
        }
    }
}
