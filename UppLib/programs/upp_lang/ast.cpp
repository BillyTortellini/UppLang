#include "ast.hpp"

#include "compiler.hpp"

namespace AST
{
    void base_destroy(Node* node)
    {
        switch (node->type)
        {
        case Node_Type::SWITCH_CASE: 
        case Node_Type::SYMBOL_LOOKUP: 
        case Node_Type::IMPORT: 
        case Node_Type::PARAMETER: 
        case Node_Type::ARGUMENT: 
        case Node_Type::DEFINITION_SYMBOL: 
        case Node_Type::ENUM_MEMBER: 
            break;
        case Node_Type::STRUCT_MEMBER: {
            auto member = (Structure_Member_Node*)node;
            if (!member->is_expression) {
                dynamic_array_destroy(&member->options.subtype_members);
            }
            break;
        }
        case Node_Type::CONTEXT_CHANGE: {
            auto change = (Context_Change*)node;
            if (change->is_import) {
                break;
            }
            dynamic_array_destroy(&change->options.setting.arguments);
            break;
        }
        case Node_Type::DEFINITION: {
            auto def = (Definition*)node;
            dynamic_array_destroy(&def->values);
            dynamic_array_destroy(&def->types);
            dynamic_array_destroy(&def->symbols);
            break;
        }
        case Node_Type::PATH_LOOKUP: {
            dynamic_array_destroy(&((Path_Lookup*)node)->parts);
            break;
        }
        case Node_Type::CODE_BLOCK: {
            auto block = (Code_Block*)node;
            if (block->statements.data != 0) {
                dynamic_array_destroy(&block->statements);
            }
            if (block->context_changes.data != 0) {
                dynamic_array_destroy(&block->context_changes);
            }
            break;
        }
        case Node_Type::MODULE: {
            auto module = (Module*)node;
            if (module->definitions.data != 0) {
                dynamic_array_destroy(&module->definitions);
            }
            if (module->import_nodes.data != 0) {
                dynamic_array_destroy(&module->import_nodes);
            }
            if (module->context_changes.data != 0) {
                dynamic_array_destroy(&module->context_changes);
            }
            break;
        }
        case Node_Type::EXPRESSION:
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
            case Expression_Type::INSTANCIATE: {
                auto& instance = expr->options.instanciate;
                if (instance.arguments.data != 0) {
                    dynamic_array_destroy(&instance.arguments);
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
                auto& s = expr->options.structure;
                if (s.parameters.data != 0) {
                    dynamic_array_destroy(&s.parameters);
                }
                auto& members = s.members;
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
        case Node_Type::STATEMENT: {
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
            case Statement_Type::ASSIGNMENT: {
                auto as = stat->options.assignment;
                dynamic_array_destroy(&as.left_side);
                dynamic_array_destroy(&as.right_side);
                break;
            }
            }
            break;
        }
        default: panic("");
        }
        delete node;
    }
    
    Node* base_get_child(Node* node, int child_index)
    {
        int index = 0;
#define FILL(x) { if (child_index == index) {return &x->base;} else {index += 1;}}
#define FILL_OPTIONAL(x) if (x.available) {FILL(x.value);}
#define FILL_ARRAY(x) {if (child_index < index + x.size) {return &x[child_index - index]->base;} else {index += x.size;}}
        switch (node->type)
        {
        case Node_Type::SWITCH_CASE: {
            auto sw_case = (Switch_Case*)node;
            FILL_OPTIONAL(sw_case->value);
            FILL_OPTIONAL(sw_case->variable_definition);
            FILL(sw_case->block);
            break;
        }
        case Node_Type::PARAMETER: {
            auto param = (Parameter*)node;
            FILL(param->type);
            FILL_OPTIONAL(param->default_value);
            break;
        }
        case Node_Type::PATH_LOOKUP: {
            auto path = (Path_Lookup*)node;
            FILL_ARRAY(path->parts);
            break;
        }
        case Node_Type::ENUM_MEMBER: {
            auto enum_member = (Enum_Member_Node*)node;
            FILL_OPTIONAL(enum_member->value);
            break;
        }
        case Node_Type::STRUCT_MEMBER: {
            auto member = (Structure_Member_Node*)node;
            if (member->is_expression) {
                FILL(member->options.expression);
            }
            else {
                FILL_ARRAY(member->options.subtype_members);
            }
            break;
        }
        case Node_Type::ARGUMENT: {
            auto arg = (Argument*)node;
            FILL(arg->value);
            break;
        }
        case Node_Type::CODE_BLOCK: {
            auto block = (Code_Block*)node;
            FILL_ARRAY(block->context_changes);
            FILL_ARRAY(block->statements);
            break;
        }
        case Node_Type::DEFINITION_SYMBOL: {
            break;
        }
        case Node_Type::DEFINITION: {
            auto def = (Definition*)node;
            FILL_ARRAY(def->symbols);
            FILL_ARRAY(def->types);
            FILL_ARRAY(def->values);
            break;
        }
        case Node_Type::SYMBOL_LOOKUP: {
            break;
        }
        case Node_Type::CONTEXT_CHANGE: {
            auto context = (Context_Change*)node;
            if (context->is_import) {
                FILL(context->options.import_path);
            }
            else {
                FILL_ARRAY(context->options.setting.arguments);
            }
            break;
        }
        case Node_Type::IMPORT: {
            auto import = (Import*)node;
            if (import->type != Import_Type::FILE) {
                FILL(import->path);
            }
            break;
        }
        case Node_Type::MODULE: {
            auto module = (Module*)node;
            FILL_ARRAY(module->import_nodes);
            FILL_ARRAY(module->context_changes);
            FILL_ARRAY(module->definitions);
            break;
        }
        case Node_Type::EXPRESSION:
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
            case Expression_Type::TEMPLATE_PARAMETER: {
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
            case Expression_Type::PATH_LOOKUP: {
                FILL(expr->options.path_lookup);
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
            case Expression_Type::INSTANCIATE: {
                FILL_ARRAY(expr->options.instanciate.arguments);
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
            case Expression_Type::CONST_TYPE: {
                auto& const_type = expr->options.const_type;
                FILL(const_type);
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
                FILL_ARRAY(str.parameters);
                FILL_ARRAY(str.members);
                break;
            }
            case Expression_Type::ENUM_TYPE: {
                auto& members = expr->options.enum_members;
                FILL_ARRAY(members);
                break;
            }
            default: panic("");
            }
            break;
        }
        case Node_Type::STATEMENT:
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
                FILL_ARRAY(ass.left_side);
                FILL_ARRAY(ass.right_side);
                break;
            }
            case Statement_Type::BINOP_ASSIGNMENT: {
                auto ass = stat->options.binop_assignment;
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
            case Statement_Type::IMPORT: {
                FILL(stat->options.import_node);
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
            case Statement_Type::FOREACH_LOOP: {
                auto loop = stat->options.foreach_loop;
                FILL(loop.loop_variable_definition);
                FILL_OPTIONAL(loop.index_variable_definition);
                FILL(loop.expression);
                FILL(loop.body_block);
                break;
            }
            case Statement_Type::FOR_LOOP: {
                auto loop = stat->options.for_loop;
                FILL(loop.loop_variable_definition);
                FILL_OPTIONAL(loop.loop_variable_type);
                FILL(loop.initial_value);
                FILL(loop.condition);
                FILL(loop.increment_statement);
                FILL(loop.body_block);
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
                FILL(stat->options.switch_statement.condition);
                FILL_ARRAY(stat->options.switch_statement.cases);
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

    void base_enumerate_children(Node* node, Dynamic_Array<Node*>* fill)
    {
#define FILL(x) {dynamic_array_push_back(fill, &x->base);};
#define FILL_OPTIONAL(x) if (x.available) {dynamic_array_push_back(fill, &x.value->base);}
#define FILL_ARRAY(x) for (int i = 0; i < x.size; i++) {dynamic_array_push_back(fill, &x[i]->base);}
        switch (node->type)
        {
        case Node_Type::SWITCH_CASE: {
            auto sw_case = (Switch_Case*)node;
            FILL_OPTIONAL(sw_case->value);
            FILL_OPTIONAL(sw_case->variable_definition);
            FILL(sw_case->block);
            break;
        }
        case Node_Type::ENUM_MEMBER: {
            auto enum_member = (Enum_Member_Node*)node;
            FILL_OPTIONAL(enum_member->value);
            break;
        }
        case Node_Type::STRUCT_MEMBER: {
            auto member = (Structure_Member_Node*)node;
            if (member->is_expression) {
                FILL(member->options.expression);
            }
            else {
                FILL_ARRAY(member->options.subtype_members);
            }
            break;
        }
        case Node_Type::SYMBOL_LOOKUP: {
            break;
        }
        case Node_Type::CONTEXT_CHANGE: {
            auto context = (Context_Change*)node;
            if (context->is_import) {
                FILL(context->options.import_path);
            }
            else {
                FILL_ARRAY(context->options.setting.arguments);
            }
            break;
        }
        case Node_Type::IMPORT: {
            auto import = (Import*)node;
            if (import->type != Import_Type::FILE) {
                FILL(import->path);
            }
            break;
        }
        case Node_Type::PARAMETER: {
            auto param = (Parameter*)node;
            FILL(param->type);
            FILL_OPTIONAL(param->default_value);
            break;
        }
        case Node_Type::PATH_LOOKUP: {
            auto path = (Path_Lookup*)node;
            FILL_ARRAY(path->parts);
            break;
        }
        case Node_Type::ARGUMENT: {
            auto arg = (Argument*)node;
            FILL(arg->value);
            break;
        }
        case Node_Type::CODE_BLOCK: {
            auto block = (Code_Block*)node;
            FILL_ARRAY(block->context_changes);
            FILL_ARRAY(block->statements);
            break;
        }
        case Node_Type::DEFINITION_SYMBOL: {
            break;
        }
        case Node_Type::DEFINITION: {
            auto def = (Definition*)node;
            FILL_ARRAY(def->symbols);
            FILL_ARRAY(def->types);
            FILL_ARRAY(def->values);
            break;
        }
        case Node_Type::MODULE: {
            auto module = (Module*)node;
            FILL_ARRAY(module->import_nodes);
            FILL_ARRAY(module->context_changes);
            FILL_ARRAY(module->definitions);
            break;
        }
        case Node_Type::EXPRESSION:
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
            case Expression_Type::TEMPLATE_PARAMETER: {
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
            case Expression_Type::PATH_LOOKUP: {
                FILL(expr->options.path_lookup);
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
            case Expression_Type::INSTANCIATE: {
                FILL_ARRAY(expr->options.instanciate.arguments);
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
            case Expression_Type::CONST_TYPE: {
                auto& const_type = expr->options.const_type;
                FILL(const_type);
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
                FILL_ARRAY(str.parameters);
                FILL_ARRAY(str.members);
                break;
            }
            case Expression_Type::ENUM_TYPE: {
                auto& members = expr->options.enum_members;
                FILL_ARRAY(members);
                break;
            }
            default: panic("");
            }
            break;
        }
        case Node_Type::STATEMENT:
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
                FILL_ARRAY(ass.left_side);
                FILL_ARRAY(ass.right_side);
                break;
            }
            case Statement_Type::BINOP_ASSIGNMENT: {
                auto ass = stat->options.binop_assignment;
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
            case Statement_Type::IMPORT: {
                FILL(stat->options.import_node);
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
            case Statement_Type::FOREACH_LOOP: {
                auto loop = stat->options.foreach_loop;
                FILL(loop.loop_variable_definition);
                FILL_OPTIONAL(loop.index_variable_definition);
                FILL(loop.expression);
                FILL(loop.body_block);
                break;
            }
            case Statement_Type::FOR_LOOP: {
                auto loop = stat->options.for_loop;
                FILL(loop.loop_variable_definition);
                FILL_OPTIONAL(loop.loop_variable_type);
                FILL(loop.initial_value);
                FILL(loop.condition);
                FILL(loop.increment_statement);
                FILL(loop.body_block);
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
                FILL(stat->options.switch_statement.condition);
                FILL_ARRAY(stat->options.switch_statement.cases);
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

    void base_append_to_string(Node* base, String* str)
    {
        switch (base->type)
        {
        case Node_Type::DEFINITION_SYMBOL: {
            string_append_formated(str, "DEFINITION_SYMBOL %s", ((Definition_Symbol*)base)->name->characters);
            break;
        }
        case Node_Type::DEFINITION: {
            string_append_formated(str, "DEFINITION");
            break;
        }
        case Node_Type::IMPORT: {
            auto import = (Import*)base;
            string_append_formated(str, "IMPORT ");
            if (import->type == Import_Type::FILE) {
                string_append_formated(str, "\"%s\" ", import->file_name->characters);
            }
            else if (import->type == Import_Type::MODULE_SYMBOLS) {
                string_append_formated(str, "~* ");
            }
            else if (import->type == Import_Type::MODULE_SYMBOLS_TRANSITIVE) {
                string_append_formated(str, "~** ");
            }
            if (import->alias_name != 0) {
                string_append_formated(str, "as %s ", import->alias_name->characters);
            }
            break;
        }
        case Node_Type::CONTEXT_CHANGE: {
            auto context = (Context_Change*)base;
            if (context->is_import) {
                string_append_formated(str, "CONTEXT_IMPORT");
            }
            else {
                string_append_formated(str, "CONTEXT_CHANGE");
            }
            break;
        }
        case Node_Type::PATH_LOOKUP:
            string_append_formated(str, "PATH_LOOKUP ");
            break;
        case Node_Type::SYMBOL_LOOKUP:
            string_append_formated(str, "SYMBOL_LOOKUP ");
            string_append_string(str, ((Symbol_Lookup*)base)->name);
            break;
        case Node_Type::SWITCH_CASE: string_append_formated(str, "SWITCH_CASE"); break;
        case Node_Type::CODE_BLOCK: string_append_formated(str, "CODE_BLOCK"); break;
        case Node_Type::MODULE: string_append_formated(str, "MODULE"); break;
        case Node_Type::ARGUMENT: {
            string_append_formated(str, "ARGUMENT");
            auto arg = (Argument*)base;
            if (arg->name.available) {
                string_append_formated(str, " ");
                string_append_string(str, arg->name.value);
            }
            break;
        }
        case Node_Type::ENUM_MEMBER: {
            auto mem = (Enum_Member_Node*)base;
            string_append_formated(str, "ENUM_MEMBER ");
            string_append_string(str, mem->name);
            break;
        }
        case Node_Type::STRUCT_MEMBER: {
            auto mem = (Structure_Member_Node*)base;
            string_append_formated(str, "STRUCT_MEMBER ");
            string_append_string(str, mem->name);
            break;
        }
        case Node_Type::PARAMETER: {
            auto param = (Parameter*)base;
            string_append_formated(str, "PARAMETER ");
            string_append_string(str, param->name);
            break;
        }
        case Node_Type::EXPRESSION:
        {
            auto expr = (Expression*)base;
            switch (expr->type)
            {
            case Expression_Type::BINARY_OPERATION: string_append_formated(str, "BINARY_OPERATION"); break;
            case Expression_Type::UNARY_OPERATION: string_append_formated(str, "UNARY_OPERATION"); break;
            case Expression_Type::TEMPLATE_PARAMETER: string_append_formated(str, "TEMPLATE_PARAMETER %s", expr->options.polymorphic_symbol_id->characters); break;
            case Expression_Type::FUNCTION_CALL: string_append_formated(str, "FUNCTION_CALL"); break;
            case Expression_Type::NEW_EXPR: string_append_formated(str, "NEW_EXPR"); break;
            case Expression_Type::CAST: string_append_formated(str, "CAST"); break;
            case Expression_Type::BAKE_BLOCK: string_append_formated(str, "BAKE_BLOCK"); break;
            case Expression_Type::BAKE_EXPR: string_append_formated(str, "BAKE_EXPR"); break;
            case Expression_Type::INSTANCIATE: string_append_formated(str, "INSTANCIATE"); break;
            case Expression_Type::PATH_LOOKUP: string_append_formated(str, "EXPR_LOOKUP "); break;
            case Expression_Type::LITERAL_READ: {
                string_append_formated(str, "LITERAL_READ "); 
                auto& read = expr->options.literal_read;
                switch (read.type) {
                case Literal_Type::BOOLEAN: string_append_formated(str, read.options.boolean ? "true" : "false"); break;
                case Literal_Type::INTEGER: string_append_formated(str, "%d", read.options.int_val); break;
                case Literal_Type::FLOAT_VAL: string_append_formated(str, "%f", read.options.float_val); break;
                case Literal_Type::NULL_VAL: string_append_formated(str, "null"); break;
                case Literal_Type::STRING: string_append_formated(str, "%s", read.options.string->characters); break;
                default: panic("");
                }
                break; 
            }
            case Expression_Type::ARRAY_ACCESS: string_append_formated(str, "ARRAY_ACCESS"); break;
            case Expression_Type::MEMBER_ACCESS: string_append_formated(str, "MEMBER_ACCESS"); break;
            case Expression_Type::MODULE: string_append_formated(str, "MODULE"); break;
            case Expression_Type::FUNCTION: string_append_formated(str, "FUNCTION"); break;
            case Expression_Type::FUNCTION_SIGNATURE: string_append_formated(str, "FUNCTION_SIGNATURE"); break;
            case Expression_Type::STRUCTURE_TYPE: string_append_formated(str, "STRUCTURE_TYPE"); break;
            case Expression_Type::ENUM_TYPE: string_append_formated(str, "ENUM_TYPE"); break;
            case Expression_Type::ARRAY_TYPE: string_append_formated(str, "ARRAY_TYPE"); break;
            case Expression_Type::SLICE_TYPE: string_append_formated(str, "SLICE_TYPE"); break;
            case Expression_Type::CONST_TYPE: string_append_formated(str, "CONST_TYPE"); break;
            case Expression_Type::ERROR_EXPR: string_append_formated(str, "ERROR_EXPR"); break;
            case Expression_Type::STRUCT_INITIALIZER: string_append_formated(str, "STRUCT_INITIALIZER"); break;
            case Expression_Type::ARRAY_INITIALIZER: string_append_formated(str, "ARRAY_INITIZALIZER"); break;
            case Expression_Type::AUTO_ENUM: string_append_formated(str, "AUTO_ENUM"); break;
            default: panic("");
            }
            break;
        }
        case Node_Type::STATEMENT:
        {
            auto stat = (Statement*)base;
            switch (stat->type)
            {
            case Statement_Type::DEFINITION: string_append_formated(str, "STAT_DEF"); break;
            case Statement_Type::BLOCK: string_append_formated(str, "STAT_BLOCK"); break;
            case Statement_Type::ASSIGNMENT: {
                if (stat->options.assignment.is_pointer_assign) {
                    string_append_formated(str, "POINTER-");
                }
                string_append_formated(str, "ASSIGNMENT"); 
                break;
            }
            case Statement_Type::BINOP_ASSIGNMENT: string_append_formated(str, "BINOP_ASSIGNMENT"); break;
            case Statement_Type::EXPRESSION_STATEMENT: string_append_formated(str, "EXPRESSION_STATEMENT"); break;
            case Statement_Type::DEFER: string_append_formated(str, "DEFER"); break;
            case Statement_Type::IMPORT: string_append_formated(str, "IMPORT"); break;
            case Statement_Type::IF_STATEMENT: string_append_formated(str, "IF_STATEMENT"); break;
            case Statement_Type::WHILE_STATEMENT: string_append_formated(str, "WHILE_STATEMENT"); break;
            case Statement_Type::FOR_LOOP: string_append_formated(str, "FOR_LOOP"); break;
            case Statement_Type::FOREACH_LOOP: string_append_formated(str, "FOREACH_LOOP"); break;
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

    void base_append_to_string_recursive(Node* base, String* str, int indentation)
    {
        base_append_to_string(base, str);
        Dynamic_Array<Node*> children = dynamic_array_create<Node*>(1);
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

    void base_print(Node* node)
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

    void path_lookup_append_to_string(Path_Lookup* path, String* string)
    {
        for (int i = 0; i < path->parts.size; i++) {
            string_append_formated(string, "%s", path->parts[i]->name->characters);
            if (i != path->parts.size - 1) {
                string_append_character(string, '~');
            }
        }
    }

    namespace Helpers
    {
        bool type_correct(Context_Change* base) {
            return base->base.type == Node_Type::CONTEXT_CHANGE;
        }
        bool type_correct(Structure_Member_Node* base) {
            return base->base.type == Node_Type::STRUCT_MEMBER;
        }
        bool type_correct(Symbol_Lookup* base) {
            return base->base.type == Node_Type::SYMBOL_LOOKUP;
        }
        bool type_correct(Path_Lookup* base) {
            return base->base.type == Node_Type::PATH_LOOKUP;
        }
        bool type_correct(Definition* base) {
            return base->base.type == Node_Type::DEFINITION;
        }
        bool type_correct(Definition_Symbol* base) {
            return base->base.type == Node_Type::DEFINITION_SYMBOL;
        }
        bool type_correct(Switch_Case* base) {
            return base->base.type == Node_Type::SWITCH_CASE;
        }
        bool type_correct(Statement* base) {
            return base->base.type == Node_Type::STATEMENT;
        }
        bool type_correct(Argument* base) {
            return base->base.type == Node_Type::ARGUMENT;
        }
        bool type_correct(Parameter* base) {
            return base->base.type == Node_Type::PARAMETER;
        }
        bool type_correct(Expression* base) {
            return base->base.type == Node_Type::EXPRESSION;
        }
        bool type_correct(Enum_Member_Node* base) {
            return base->base.type == Node_Type::ENUM_MEMBER;
        }
        bool type_correct(Module* base) {
            return base->base.type == Node_Type::MODULE;
        }
        bool type_correct(Import* base) {
            return base->base.type == Node_Type::IMPORT;
        }
        bool type_correct(Code_Block* base) {
            return base->base.type == Node_Type::CODE_BLOCK;
        }
    }

    Token_Index node_position_to_token_index(Node_Position pos) {
        switch (pos.type)
        {
        case AST::Node_Position_Type::TOKEN_INDEX:
            return pos.options.token_index;
        case AST::Node_Position_Type::BLOCK_START:
            return token_index_make_block_start(pos.options.block_index);
        case AST::Node_Position_Type::BLOCK_END:
            return token_index_make_block_end(pos.options.block_index);
        }
        panic("");
        return token_index_make(line_index_make(block_index_make_root(0), 0), 0);
    }
    
    Token_Range node_range_to_token_range(Node_Range range) {
        return token_range_make(node_position_to_token_index(range.start), node_position_to_token_index(range.end));
    }
    
    int node_position_compare(Node_Position a, Node_Position b) {
        return index_compare(node_position_to_token_index(a), node_position_to_token_index(b));
    }
    
    Node_Range node_range_make(Node_Position a, Node_Position b) {
        Node_Range result;
        result.start = a;
        result.end = b;
        return result;
    }
    
    Node_Position node_position_make_token_index(Token_Index index) {
        Node_Position result;
        auto line = index_value(index.line_index);
        if (line->is_block_reference) {
            result.type = Node_Position_Type::BLOCK_START;
            result.options.block_index = line->options.block_index;
            return result;
        }
        result.type = Node_Position_Type::TOKEN_INDEX;
        result.options.token_index = index;
        return result;
    }
    
    Node_Range node_range_make(Token_Index a, Token_Index b) {
        return node_range_make(node_position_make_token_index(a), node_position_make_token_index(b));
    }
    
    Node_Position node_position_make_block_end(Block_Index block_index) {
        Node_Position result;
        result.type = Node_Position_Type::BLOCK_END;
        result.options.block_index = block_index;
        return result;
    }
    
    Node_Position node_position_make_block_start(Block_Index block_index) {
        Node_Position result;
        result.type = Node_Position_Type::BLOCK_START;
        result.options.block_index = block_index;
        return result;
    }
    
    Node_Position node_position_make_end_of_line(Line_Index line_index) {
        auto line = index_value(line_index);
        if (line->is_block_reference) {
           return node_position_make_block_end(line->options.block_index);
        }
        else {
            return node_position_make_token_index(token_index_make_line_end(line_index));
        }
    }
    
    Node_Position node_position_make_start_of_line(Line_Index line_index) {
        auto line = index_value(line_index);
        if (line->is_block_reference) {
           return node_position_make_block_start(line->options.block_index);
        }
        else {
            return node_position_make_token_index(token_index_make_line_start(line_index));
        }
    }
    
    Node_Range node_range_make_block(Block_Index index)
    {
        Node_Range result;
        result.start = node_position_make_block_start(index);
        result.end = node_position_make_block_end(index);
        return result;
    }
}
