#include "semantic_analyser.hpp"

#include "../../datastructures/string.hpp"

String primitive_type_to_string(Primitive_Type type)
{
    switch (type)
    {
    case Primitive_Type::BOOLEAN: return string_create_static("BOOL");
    case Primitive_Type::FLOAT_32: return string_create_static("FLOAT_32");
    case Primitive_Type::FLOAT_64: return string_create_static("FLOAT_64");
    case Primitive_Type::SIGNED_INT_8: return string_create_static("SIGNED_INT_8");
    case Primitive_Type::SIGNED_INT_16: return string_create_static("SIGNED_INT_16");
    case Primitive_Type::SIGNED_INT_32: return string_create_static("SIGNED_INT_32");
    case Primitive_Type::SIGNED_INT_64: return string_create_static("SIGNED_INT_64");
    case Primitive_Type::UNSIGNED_INT_8: return string_create_static("UNSIGNED_INT_8");
    case Primitive_Type::UNSIGNED_INT_16: return string_create_static("UNSIGNED_INT_16");
    case Primitive_Type::UNSIGNED_INT_32: return string_create_static("UNSIGNED_INT_32");
    case Primitive_Type::UNSIGNED_INT_64: return string_create_static("UNSIGNED_iNT_64");
    }
    return string_create_static("INVALID_VALUE_TYPE_ENUM");
}

Type_Signature type_signature_make_error() 
{
    Type_Signature result;
    result.type = Signature_Type::ERROR_TYPE;
    result.size_in_bytes = 0;
    result.alignment_in_bytes = 0;
    return result;
}

void type_signature_destroy(Type_Signature* sig) {
    if (sig->type == Signature_Type::FUNCTION)
        dynamic_array_destroy(&sig->parameter_types);
}

Type_Signature type_signature_make_primitive(Primitive_Type type) 
{
    Type_Signature result;
    result.type = Signature_Type::PRIMITIVE;
    result.primitive_type = type;
    switch (type) 
    {
    case Primitive_Type::BOOLEAN: result.size_in_bytes = 1; result.alignment_in_bytes = 1; break;
    case Primitive_Type::SIGNED_INT_8: result.size_in_bytes = 1; result.alignment_in_bytes = 1; break;
    case Primitive_Type::SIGNED_INT_16: result.size_in_bytes = 2; result.alignment_in_bytes = 2; break;
    case Primitive_Type::SIGNED_INT_32: result.size_in_bytes = 4; result.alignment_in_bytes = 4; break;
    case Primitive_Type::SIGNED_INT_64: result.size_in_bytes = 8; result.alignment_in_bytes = 8; break;
    case Primitive_Type::UNSIGNED_INT_8: result.size_in_bytes = 1; result.alignment_in_bytes = 1; break;
    case Primitive_Type::UNSIGNED_INT_16: result.size_in_bytes = 2; result.alignment_in_bytes = 2; break;
    case Primitive_Type::UNSIGNED_INT_32: result.size_in_bytes = 4; result.alignment_in_bytes = 4; break;
    case Primitive_Type::UNSIGNED_INT_64: result.size_in_bytes = 8; result.alignment_in_bytes = 8; break;
    case Primitive_Type::FLOAT_32: result.size_in_bytes = 4; result.alignment_in_bytes = 4; break;
    case Primitive_Type::FLOAT_64: result.size_in_bytes = 8; result.alignment_in_bytes = 8; break;
    default: panic("Wehl scheit 2!\n");
    }
    return result;
}

bool type_signatures_are_equal(Type_Signature* sig1, Type_Signature* sig2)
{
    if (sig1->type == sig2->type) 
    {
        if (sig1->type == Signature_Type::ARRAY_SIZED && sig1->child_type== sig2->child_type&& sig1->array_element_count == sig2->array_element_count) return true;
        if (sig1->type == Signature_Type::ARRAY_UNSIZED && sig1->child_type == sig2->child_type) return true;
        if (sig1->type == Signature_Type::POINTER && sig1->child_type == sig2->child_type) return true;
        if (sig1->type == Signature_Type::ERROR_TYPE) return true;
        if (sig1->type == Signature_Type::VOID_TYPE) return true;
        if (sig1->type == Signature_Type::PRIMITIVE && sig1->primitive_type == sig2->primitive_type) return true;
        if (sig1->type == Signature_Type::FUNCTION) 
        {
            if (sig1->return_type!= sig2->return_type) return false;
            if (sig1->parameter_types.size != sig2->parameter_types.size) return false;
            for (int i = 0; i < sig1->parameter_types.size; i++) {
                if (sig1->parameter_types[i] != sig2->parameter_types[i]) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

void type_signature_append_to_string(String* string, Type_Signature* signature)
{
    switch (signature->type)
    {
    case Signature_Type::VOID_TYPE:
        string_append_formated(string, "VOID");
        break;
    case Signature_Type::ARRAY_SIZED:
        string_append_formated(string, "[%d]", signature->array_element_count);
        type_signature_append_to_string(string, signature->child_type);
        break;
    case Signature_Type::ARRAY_UNSIZED:
        string_append_formated(string, "[]");
        type_signature_append_to_string(string, signature->child_type);
        break;
    case Signature_Type::ERROR_TYPE:
        string_append_formated(string, "ERROR-Type");
        break;
    case Signature_Type::POINTER:
        string_append_formated(string, "*");
        type_signature_append_to_string(string, signature->child_type);
        break;
    case Signature_Type::PRIMITIVE:
        String s = primitive_type_to_string(signature->primitive_type);
        string_append_string(string, &s);
        break;
    case Signature_Type::FUNCTION:
        string_append_formated(string, "(");
        for (int i = 0; i < signature->parameter_types.size; i++) {
            type_signature_append_to_string(string, signature->parameter_types[i]);
            if (i != signature->parameter_types.size - 1) {
                string_append_formated(string, ", ");
            }
        }
        string_append_formated(string, ") -> ");
        type_signature_append_to_string(string, signature->return_type);
    }
}


void type_system_add_primitives(Type_System* system)
{
    system->bool_type = new Type_Signature();
    system->i8_type = new Type_Signature();
    system->i16_type = new Type_Signature();
    system->i32_type = new Type_Signature();
    system->i64_type = new Type_Signature();
    system->u8_type = new Type_Signature();
    system->u16_type = new Type_Signature();
    system->u32_type = new Type_Signature();
    system->u64_type = new Type_Signature();
    system->f32_type = new Type_Signature();
    system->f64_type = new Type_Signature();
    system->error_type = new Type_Signature();
    system->void_type = new Type_Signature();

    *system->bool_type = type_signature_make_primitive(Primitive_Type::BOOLEAN);
    *system->i8_type   = type_signature_make_primitive(Primitive_Type::SIGNED_INT_8);
    *system->i16_type  = type_signature_make_primitive(Primitive_Type::SIGNED_INT_16);
    *system->i32_type  = type_signature_make_primitive(Primitive_Type::SIGNED_INT_32);
    *system->i64_type  = type_signature_make_primitive(Primitive_Type::SIGNED_INT_64);
    *system->u8_type   = type_signature_make_primitive(Primitive_Type::UNSIGNED_INT_8);
    *system->u16_type  = type_signature_make_primitive(Primitive_Type::UNSIGNED_INT_16);
    *system->u32_type  = type_signature_make_primitive(Primitive_Type::UNSIGNED_INT_32);
    *system->u64_type  = type_signature_make_primitive(Primitive_Type::UNSIGNED_INT_64);
    *system->f32_type  = type_signature_make_primitive(Primitive_Type::FLOAT_32);
    *system->f64_type  = type_signature_make_primitive(Primitive_Type::FLOAT_64);
    *system->error_type  = type_signature_make_error();
    system->void_type->type = Signature_Type::VOID_TYPE;
    system->void_type->size_in_bytes = 0;
    system->void_type->alignment_in_bytes = 0;

    dynamic_array_push_back(&system->types, system->bool_type);
    dynamic_array_push_back(&system->types, system->i8_type);
    dynamic_array_push_back(&system->types, system->i16_type);
    dynamic_array_push_back(&system->types, system->i32_type);
    dynamic_array_push_back(&system->types, system->i64_type);
    dynamic_array_push_back(&system->types, system->u8_type);
    dynamic_array_push_back(&system->types, system->u16_type);
    dynamic_array_push_back(&system->types, system->u32_type);
    dynamic_array_push_back(&system->types, system->u64_type);
    dynamic_array_push_back(&system->types, system->f32_type);
    dynamic_array_push_back(&system->types, system->f64_type);
    dynamic_array_push_back(&system->types, system->error_type);
    dynamic_array_push_back(&system->types, system->void_type);
}

Type_System type_system_create()
{
    Type_System result;
    result.types = dynamic_array_create_empty<Type_Signature*>(256);
    type_system_add_primitives(&result);
    return result;
}

void type_system_destroy(Type_System* system) {
    dynamic_array_destroy(&system->types);
}

void type_system_reset_all(Type_System* system) {
    for (int i = 0; i < system->types.size; i++) {
        type_signature_destroy(system->types[i]);
        delete system->types[i];
    }
    dynamic_array_reset(&system->types);
    type_system_add_primitives(system);
}

Type_Signature* type_system_make_type(Type_System* system, Type_Signature signature)
{
    for (int i = 0; i < system->types.size; i++)
    {
        Type_Signature* cmp = system->types[i];
        if (type_signatures_are_equal(cmp, &signature)) {
            type_signature_destroy(&signature);
            return cmp;
        }
    }
    Type_Signature* new_sig = new Type_Signature();
    *new_sig = signature;
    dynamic_array_push_back(&system->types, new_sig);
    return new_sig;
}

Type_Signature* type_system_make_pointer(Type_System* system, Type_Signature* child_type) 
{
    Type_Signature result;
    result.type = Signature_Type::POINTER;
    result.child_type = child_type;
    result.size_in_bytes = 8;
    result.alignment_in_bytes = 8;
    return type_system_make_type(system, result);
}

Type_Signature* type_system_make_array_sized(Type_System* system, Type_Signature* element_type, int array_element_count) 
{
    Type_Signature result;
    result.type = Signature_Type::ARRAY_SIZED;
    result.child_type = element_type;
    result.alignment_in_bytes = element_type->alignment_in_bytes;
    result.size_in_bytes = element_type->size_in_bytes * array_element_count;
    result.array_element_count = array_element_count;
    return type_system_make_type(system, result);
}

Type_Signature* type_system_make_array_unsized(Type_System* system, Type_Signature* element_type) 
{
    Type_Signature result;
    result.type = Signature_Type::ARRAY_UNSIZED;
    result.child_type = element_type;
    result.alignment_in_bytes = 8;
    result.size_in_bytes = 16;
    return type_system_make_type(system, result);
}

Type_Signature* type_system_make_function(Type_System* system, DynamicArray<Type_Signature*> parameter_types, Type_Signature* return_type) 
{
    Type_Signature result;
    result.type = Signature_Type::FUNCTION;
    result.alignment_in_bytes = 0;
    result.size_in_bytes = 0;
    result.parameter_types = parameter_types;
    result.return_type = return_type;
    return type_system_make_type(system, result);
}

void type_system_print(Type_System* system)
{
    String msg = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&msg));
    string_append_formated(&msg, "Type_System: ");
    for (int i = 0; i < system->types.size; i++)
    {
        Type_Signature* type = system->types[i];
        string_append_formated(&msg, "\n\t%d: ", i);
        type_signature_append_to_string(&msg, type);
        string_append_formated(&msg, " size: %d, alignment: %d", type->size_in_bytes, type->alignment_in_bytes);
    }
    string_append_formated(&msg, "\n");
    logg("%s", msg.characters);
}



Symbol_Table symbol_table_create(Symbol_Table* parent)
{
    Symbol_Table result;
    result.parent = parent;
    result.symbols = dynamic_array_create_empty<Symbol>(8);
    return result;
}

void symbol_table_destroy(Symbol_Table* table) {
    dynamic_array_destroy(&table->symbols);
}

Symbol* symbol_table_find_symbol(Symbol_Table* table, int name_handle, bool* in_current_scope)
{
    *in_current_scope = false;
    for (int i = 0; i < table->symbols.size; i++) {
        if (table->symbols[i].name_handle == name_handle) {
            *in_current_scope = true;
            return &table->symbols[i];
        }
    }
    if (table->parent != 0) {
        Symbol* result = symbol_table_find_symbol(table->parent, name_handle, in_current_scope);
        *in_current_scope = false;
        return result;
    }
    return 0;
}

Symbol* symbol_table_find_symbol_of_type_with_scope_info(Symbol_Table* table, int name_handle, Symbol_Type::ENUM symbol_type, bool* in_current_scope)
{
    *in_current_scope = false;
    for (int i = 0; i < table->symbols.size; i++) {
        if (table->symbols[i].name_handle == name_handle && table->symbols[i].symbol_type == symbol_type) {
            *in_current_scope = true;
            return &table->symbols[i];
        }
    }
    if (table->parent != 0) {
        Symbol* result = symbol_table_find_symbol_of_type_with_scope_info(table->parent, name_handle, symbol_type, in_current_scope);
        *in_current_scope = false;
        return result;
    }
    return 0;
}

Symbol* symbol_table_find_symbol_of_type(Symbol_Table* table, int name_handle, Symbol_Type::ENUM symbol_type)
{
    for (int i = 0; i < table->symbols.size; i++) {
        if (table->symbols[i].name_handle == name_handle && table->symbols[i].symbol_type == symbol_type) {
            return &table->symbols[i];
        }
    }
    if (table->parent != 0) {
        Symbol* result = symbol_table_find_symbol_of_type(table->parent, name_handle, symbol_type);
        return result;
    }
    return 0;
}

void symbol_table_define_type(Symbol_Table* table, int name_id, Type_Signature* type)
{
    Symbol* sym = symbol_table_find_symbol_of_type(table, name_id, Symbol_Type::TYPE);
    if (sym != 0) {
        panic("Types should not overlap currently!\n");
        return;
    }

    Symbol s;
    s.symbol_type = Symbol_Type::TYPE;
    s.type = type;
    s.name_handle = name_id;
    dynamic_array_push_back(&table->symbols, s);
}

/*
    SEMANTIC ANALYSER
*/
void semantic_analyser_log_error(Semantic_Analyser* analyser, const char* msg, int node_index)
{
    Compiler_Error error;
    error.message = msg;
    error.range = analyser->parser->token_mapping[node_index];
    dynamic_array_push_back(&analyser->errors, error);
}

void semantic_analyser_log_error(Semantic_Analyser* analyser, const char* msg, int node_start_index, int node_end_index)
{
    Compiler_Error error;
    error.message = msg;
    error.range.start_index = analyser->parser->token_mapping[node_start_index].start_index;
    error.range.end_index = analyser->parser->token_mapping[node_end_index].end_index;
    dynamic_array_push_back(&analyser->errors, error);
}

Symbol_Table* semantic_analyser_install_symbol_table(Semantic_Analyser* analyser, Symbol_Table* parent, int node_index)
{
    Symbol_Table* table = new Symbol_Table();
    *table = symbol_table_create(parent);
    dynamic_array_push_back(&analyser->symbol_tables, table);
    analyser->semantic_information[node_index].symbol_table_index = analyser->symbol_tables.size - 1;
    return table;
}

void semantic_analyser_define_variable(Semantic_Analyser* analyser, Symbol_Table* table, int node_index, Type_Signature* type)
{
    bool in_current_scope;
    int var_name = analyser->parser->nodes[node_index].name_id;
    Symbol* var_symbol = symbol_table_find_symbol_of_type_with_scope_info(table, var_name, Symbol_Type::VARIABLE, &in_current_scope);
    if (var_symbol != 0 && in_current_scope) {
        semantic_analyser_log_error(analyser, "Variable already defined!", node_index);
        return;
    }

    Symbol s;
    s.symbol_type = Symbol_Type::VARIABLE;
    s.type = type;
    s.name_handle = var_name;
    dynamic_array_push_back(&table->symbols, s);
}

Type_Signature* semantic_analyser_analyse_type(Semantic_Analyser* analyser, int type_node_index)
{
    AST_Node* type_node = &analyser->parser->nodes[type_node_index];
    switch (type_node->type)
    {
    case AST_Node_Type::TYPE_IDENTIFIER:
    {
        Symbol* symbol_type = symbol_table_find_symbol_of_type(analyser->symbol_tables[0], type_node->name_id, Symbol_Type::TYPE);
        if (symbol_type == 0) {
            semantic_analyser_log_error(analyser, "Invalid type, identifier is not a type!", type_node_index);
            return analyser->type_system.error_type;
        }
        return symbol_type->type;
    }
    case AST_Node_Type::TYPE_POINTER_TO: {
        return type_system_make_pointer(&analyser->type_system, semantic_analyser_analyse_type(analyser, type_node->children[0]));
    }
    case AST_Node_Type::TYPE_ARRAY_SIZED:
    {
        // TODO check if expression is compile time known, currently just literal value
        int index_node_array_size = type_node->children[0];
        AST_Node* node_array_size = &analyser->parser->nodes[index_node_array_size];
        if (node_array_size->type != AST_Node_Type::EXPRESSION_LITERAL) {
            semantic_analyser_log_error(analyser, "Array size is not a expression literal, currently not evaluable", index_node_array_size);
            return analyser->type_system.error_type;
        }
        Token literal_token = analyser->parser->lexer->tokens[analyser->parser->token_mapping[index_node_array_size].start_index];
        if (literal_token.type != Token_Type::INTEGER_LITERAL) {
            semantic_analyser_log_error(analyser, "Array size is not an integer literal, currently not evaluable", index_node_array_size);
            return analyser->type_system.error_type;
        }

        return type_system_make_array_sized(
            &analyser->type_system, 
            semantic_analyser_analyse_type(analyser, type_node->children[1]), 
            literal_token.attribute.integer_value
        );
    }
    case AST_Node_Type::TYPE_ARRAY_UNSIZED: {
        return type_system_make_array_unsized(&analyser->type_system, semantic_analyser_analyse_type(analyser, type_node->children[0]));
    }
    }

    panic("This should not happen, this means that the child was not a type!\n");
    return analyser->type_system.error_type;
}

Expression_Analysis_Result expression_analysis_result_make(Type_Signature* expression_result, bool has_memory_address)
{
    Expression_Analysis_Result result;
    result.type = expression_result;
    result.has_memory_address = has_memory_address;
    return result;
}

Expression_Analysis_Result semantic_analyser_analyse_expression(Semantic_Analyser* analyser, Symbol_Table* table, int expression_index)
{
    AST_Node* expression = &analyser->parser->nodes[expression_index];
    analyser->semantic_information[expression_index].expression_result_type = analyser->type_system.error_type;

    // For unary and binary operations
    bool is_binary_op = false;
    bool is_unary_op = false;
    bool int_valid, float_valid, bool_valid;
    int_valid = float_valid = bool_valid = false;
    bool return_left_type = false;
    Type_Signature* return_type = analyser->type_system.error_type;

    switch (expression->type)
    {
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL:
    {
        Symbol* func_symbol = symbol_table_find_symbol_of_type(table, expression->name_id, Symbol_Type::FUNCTION);
        if (func_symbol == 0) {
            semantic_analyser_log_error(analyser, "Function call to not defined Function!", expression_index);
            lexer_print_identifiers(analyser->parser->lexer);
            type_system_print(&analyser->type_system);
            return expression_analysis_result_make(analyser->type_system.error_type, true);
        }

        Type_Signature* signature = func_symbol->type;
        if (expression->children.size != signature->parameter_types.size) {
            semantic_analyser_log_error(analyser, "Argument size does not match function parameter size!", expression_index);
        }
        for (int i = 0; i < signature->parameter_types.size && i < expression->children.size; i++)
        {
            Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(analyser, table, expression->children[i]);
            if (expr_result.type != signature->parameter_types[i] || expr_result.type == analyser->type_system.error_type) {
                semantic_analyser_log_error(analyser, "Argument type does not match parameter type", expression->children[i]);
            }
        }

        analyser->semantic_information[expression_index].expression_result_type = signature->return_type;
        return expression_analysis_result_make(signature->return_type, false);
    }
    case AST_Node_Type::EXPRESSION_VARIABLE_READ:
    {
        Symbol* s = symbol_table_find_symbol_of_type(table, expression->name_id, Symbol_Type::VARIABLE);
        if (s == 0) {
            semantic_analyser_log_error(analyser, "Expression variable not defined", expression_index);
            return expression_analysis_result_make(analyser->type_system.error_type, true);
        }
        analyser->semantic_information[expression_index].expression_result_type = s->type;
        return expression_analysis_result_make(s->type, true);
    }
    case AST_Node_Type::EXPRESSION_LITERAL:
    {
        Token_Type::ENUM type = analyser->parser->lexer->tokens[analyser->parser->token_mapping[expression_index].start_index].type;
        if (type == Token_Type::BOOLEAN_LITERAL) {
            analyser->semantic_information[expression_index].expression_result_type = analyser->type_system.bool_type;
        }
        if (type == Token_Type::INTEGER_LITERAL) {
            analyser->semantic_information[expression_index].expression_result_type = analyser->type_system.i32_type;
        }
        if (type == Token_Type::FLOAT_LITERAL) {
            analyser->semantic_information[expression_index].expression_result_type = analyser->type_system.f32_type;
        }
        return expression_analysis_result_make(analyser->semantic_information[expression_index].expression_result_type, false);
    }
    case AST_Node_Type::EXPRESSION_ARRAY_ACCESS: {
        Expression_Analysis_Result array_access_expr = semantic_analyser_analyse_expression(analyser, table, expression->children[0]);
        Type_Signature* access_signature = array_access_expr.type;
        if (access_signature->type != Signature_Type::ARRAY_SIZED && access_signature->type != Signature_Type::ARRAY_UNSIZED) {
            semantic_analyser_log_error(analyser, "Expression is not an array, cannot access with []!", expression->children[0]);
            return expression_analysis_result_make(analyser->type_system.error_type, true);
        }
        Expression_Analysis_Result index_expr_result = semantic_analyser_analyse_expression(analyser, table, expression->children[1]);
        if (index_expr_result.type != analyser->type_system.i32_type) {
            semantic_analyser_log_error(analyser, "Array index must be integer!", expression->children[1]);
            return expression_analysis_result_make(analyser->type_system.error_type, true);
        }
        analyser->semantic_information[expression_index].expression_result_type = access_signature->child_type;
        return expression_analysis_result_make(access_signature->child_type, true);
    }
    case AST_Node_Type::EXPRESSION_MEMBER_ACCESS:
    {
        Expression_Analysis_Result access_expr_result = semantic_analyser_analyse_expression(analyser, table, expression->children[0]);
        Type_Signature* type_signature = access_expr_result.type;
        if (type_signature->type == Signature_Type::ERROR_TYPE) {
            return expression_analysis_result_make(analyser->type_system.error_type, true);;
        }
        if (type_signature->type != Signature_Type::ARRAY_SIZED && type_signature->type != Signature_Type::ARRAY_UNSIZED) {
            semantic_analyser_log_error(analyser, "Expression type does not have any members to access!", expression_index);
            return expression_analysis_result_make(analyser->type_system.error_type, true);
        }
        if (expression->name_id != analyser->size_token_index && expression->name_id != analyser->data_token_index) {
            semantic_analyser_log_error(analyser, "Arrays only have .size or .data as member!", expression_index);
            return expression_analysis_result_make(analyser->type_system.error_type, true);
        }
        Type_Signature* result_type;
        if (expression->name_id == analyser->size_token_index) {
            result_type = analyser->type_system.i32_type;
        }
        else {
            result_type = type_system_make_pointer(&analyser->type_system, type_signature->child_type);
        }
        analyser->semantic_information[expression_index].expression_result_type = result_type;

        if (type_signature->type == Signature_Type::ARRAY_SIZED) {
            return expression_analysis_result_make(result_type, false);
        }
        else {
            return expression_analysis_result_make(result_type, true);
        }
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION: {
        is_binary_op = true;
        int_valid = float_valid = true;
        return_left_type = true;
        break;
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL: {
        is_binary_op = true;
        int_valid = float_valid = true;
        return_type = analyser->type_system.bool_type;
        break;
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO: {
        is_binary_op = true;
        int_valid = true;
        return_left_type = true;
        break;
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR: {
        is_binary_op = true;
        bool_valid = true;
        return_left_type = true;
        break;
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL: {
        is_binary_op = true;
        bool_valid = int_valid = float_valid = true;
        return_type = analyser->type_system.bool_type;
        break;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT: {
        is_unary_op = true;
        bool_valid = true;
        return_type = analyser->type_system.bool_type;
        break;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE: {
        is_unary_op = true;
        float_valid = int_valid = true;
        return_left_type = true;
        break;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF:
    {
        Expression_Analysis_Result result = semantic_analyser_analyse_expression(analyser, table, expression->children[0]);
        if (!result.has_memory_address) {
            semantic_analyser_log_error(analyser, "Cannot get address of expression!", expression->children[0]);
        }
        bool unused;
        Type_Signature* result_type = type_system_make_pointer(&analyser->type_system, result.type);
        analyser->semantic_information[expression_index].expression_result_type = result_type;
        return expression_analysis_result_make(result_type, false);
        break;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE: 
    {
        Expression_Analysis_Result result = semantic_analyser_analyse_expression(analyser, table, expression->children[0]);
        Type_Signature* signature = result.type;
        if (signature->type != Signature_Type::POINTER) {
            semantic_analyser_log_error(analyser, "Tried to dereference non pointer type!", expression->children[0]);
            return expression_analysis_result_make(analyser->type_system.error_type, false);
        }
        analyser->semantic_information[expression_index].expression_result_type = signature->child_type;
        return expression_analysis_result_make(signature->child_type, true);
        break;
    }
    default: {
        panic("Not all expression covered!\n");
        break;
    }
    }

    if (is_binary_op)
    {
        Expression_Analysis_Result left_expr_result = semantic_analyser_analyse_expression(analyser, table, expression->children[0]);
        Expression_Analysis_Result right_expr_result = semantic_analyser_analyse_expression(analyser, table, expression->children[1]);
        if (left_expr_result.type == analyser->type_system.error_type || right_expr_result.type == analyser->type_system.error_type) {
            return expression_analysis_result_make(analyser->type_system.error_type, true);
        }
        if (left_expr_result.type != right_expr_result.type) {
            semantic_analyser_log_error(analyser, "Left and right of binary operation do not match", expression_index);
        }
        if (!int_valid && left_expr_result.type == analyser->type_system.i32_type) {
            semantic_analyser_log_error(analyser, "Operands cannot be integers", expression_index);
            return expression_analysis_result_make(analyser->type_system.error_type, false);
        }
        if (!bool_valid && left_expr_result.type == analyser->type_system.bool_type) {
            semantic_analyser_log_error(analyser, "Operands cannot be booleans", expression_index);
            return expression_analysis_result_make(analyser->type_system.error_type, false);
        }
        if (!float_valid && left_expr_result.type == analyser->type_system.f32_type) {
            semantic_analyser_log_error(analyser, "Operands cannot be floats", expression_index);
            return expression_analysis_result_make(analyser->type_system.error_type, false);
        }
        if (return_left_type) {
            analyser->semantic_information[expression_index].expression_result_type = left_expr_result.type;
            return expression_analysis_result_make(left_expr_result.type, false);
        }
        analyser->semantic_information[expression_index].expression_result_type = return_type;
        return expression_analysis_result_make(return_type, false);
    }
    if (is_unary_op)
    {
        Type_Signature* left_type = semantic_analyser_analyse_expression(analyser, table, expression->children[0]).type;
        if (!int_valid && left_type == analyser->type_system.i32_type) {
            semantic_analyser_log_error(analyser, "Operand cannot be integer", expression_index);
            return expression_analysis_result_make(analyser->type_system.error_type, false);
        }
        if (!bool_valid && left_type == analyser->type_system.bool_type) {
            semantic_analyser_log_error(analyser, "Operand cannot be boolean", expression_index);
            return expression_analysis_result_make(analyser->type_system.error_type, false);
        }
        if (!float_valid && left_type == analyser->type_system.f32_type) {
            semantic_analyser_log_error(analyser, "Operand cannot be float", expression_index);
            return expression_analysis_result_make(analyser->type_system.error_type, false);
        }
        if (return_left_type) {
            analyser->semantic_information[expression_index].expression_result_type = left_type;
            return expression_analysis_result_make(left_type, false);
        }
        analyser->semantic_information[expression_index].expression_result_type = return_type;
        return expression_analysis_result_make(return_type, false);
    }

    return expression_analysis_result_make(return_type, false);
}

Statement_Analysis_Result semantic_analyser_analyse_statement_block(Semantic_Analyser* analyser, Symbol_Table* parent, int block_index);
Statement_Analysis_Result semantic_analyser_analyse_statement(Semantic_Analyser* analyser, Symbol_Table* parent, int statement_index)
{
    AST_Node* statement = &analyser->parser->nodes[statement_index];
    switch (statement->type)
    {
    case AST_Node_Type::STATEMENT_RETURN: {
        Type_Signature* return_type;
        if (statement->children.size == 0) {
            return_type = analyser->type_system.void_type;
        }
        else {
            return_type = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]).type;
            if (return_type == analyser->type_system.void_type) {
                semantic_analyser_log_error(analyser, "Cannot return void type", statement_index);
                return Statement_Analysis_Result::RETURN;
            }
        }
        if (return_type != analyser->function_return_type && return_type != analyser->type_system.error_type) {
            semantic_analyser_log_error(analyser, "Return type does not match function return type", statement_index);
        }
        analyser->semantic_information[statement_index].expression_result_type = return_type;
        return Statement_Analysis_Result::RETURN;
    }
    case AST_Node_Type::STATEMENT_BREAK: {
        if (analyser->loop_depth <= 0) {
            semantic_analyser_log_error(analyser, "Break not inside loop!", statement_index);
        }
        return Statement_Analysis_Result::BREAK;
    }
    case AST_Node_Type::STATEMENT_CONTINUE: {
        if (analyser->loop_depth <= 0) {
            semantic_analyser_log_error(analyser, "Continue not inside loop!", statement_index);
        }
        return Statement_Analysis_Result::CONTINUE;
    }
    case AST_Node_Type::STATEMENT_EXPRESSION: {
        AST_Node* node = &analyser->parser->nodes[statement->children[0]];
        if (node->type != AST_Node_Type::EXPRESSION_FUNCTION_CALL) {
            semantic_analyser_log_error(analyser, "Expression statement must be funciton call!", statement_index);
            return Statement_Analysis_Result::NO_RETURN;
        }
        semantic_analyser_analyse_expression(analyser, parent, statement->children[0]);
        return Statement_Analysis_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_BLOCK: {
        return semantic_analyser_analyse_statement_block(analyser, parent, statement->children[0]);
    }
    case AST_Node_Type::STATEMENT_IF:
    {
        Type_Signature* condition_type = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]).type;
        if (condition_type != analyser->type_system.bool_type) {
            semantic_analyser_log_error(analyser, "If condition must be of boolean type!", statement_index);
        }
        semantic_analyser_analyse_statement_block(analyser, parent, statement->children[1]);
        return Statement_Analysis_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_IF_ELSE:
    {
        Type_Signature* condition_type = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]).type;
        if (condition_type != analyser->type_system.bool_type) {
            semantic_analyser_log_error(analyser, "If condition must be of boolean type!", statement_index);
        }
        Statement_Analysis_Result if_result = semantic_analyser_analyse_statement_block(analyser, parent, statement->children[1]);
        Statement_Analysis_Result else_result = semantic_analyser_analyse_statement_block(analyser, parent, statement->children[2]);
        if (if_result == else_result) return if_result;
        return Statement_Analysis_Result::NO_RETURN; // Maybe i need to do something different here, but I dont think so
    }
    case AST_Node_Type::STATEMENT_WHILE:
    {
        Type_Signature* condition_type = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]).type;
        if (condition_type != analyser->type_system.bool_type) {
            semantic_analyser_log_error(analyser, "If condition must be of boolean type!", statement_index);
        }
        analyser->loop_depth++;
        Statement_Analysis_Result block_result = semantic_analyser_analyse_statement_block(analyser, parent, statement->children[1]);
        analyser->loop_depth--;
        if (block_result == Statement_Analysis_Result::RETURN) {
            semantic_analyser_log_error(analyser, "While loop never runs more than once, since it always returns!", statement_index);
        }
        else if (block_result == Statement_Analysis_Result::CONTINUE) {
            semantic_analyser_log_error(analyser, "While loop stops, since it always continues!", statement_index);
        }
        else if (block_result == Statement_Analysis_Result::BREAK) {
            semantic_analyser_log_error(analyser, "While loop never more than once, since it always breaks!", statement_index);
        }
        return Statement_Analysis_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_ASSIGNMENT:
    {
        Expression_Analysis_Result left_result = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]);
        Expression_Analysis_Result right_result = semantic_analyser_analyse_expression(analyser, parent, statement->children[1]);
        if (right_result.type == analyser->type_system.void_type) {
            semantic_analyser_log_error(analyser, "Cannot assign void type to anything", statement_index);
            return Statement_Analysis_Result::NO_RETURN;
        }
        if (!left_result.has_memory_address) {
            semantic_analyser_log_error(analyser, "Left side of assignment cannot be assigned to, does not have a memory address", statement_index);
        }
        if (left_result.type != right_result.type) {
            semantic_analyser_log_error(analyser, "Left side of assignment is not the same as right side", statement_index);
        }
        return Statement_Analysis_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION:
    {
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_of_type_with_scope_info(parent, statement->name_id, Symbol_Type::VARIABLE, &in_current_scope);
        if (s != 0 && in_current_scope) {
            semantic_analyser_log_error(analyser, "Variable already defined", statement_index);
            break;
        }
        Type_Signature* var_type = semantic_analyser_analyse_type(analyser, statement->children[0]);
        if (var_type == analyser->type_system.void_type) {
            semantic_analyser_log_error(analyser, "Cannot create variable of void type", statement_index);
            return Statement_Analysis_Result::NO_RETURN;
        }
        semantic_analyser_define_variable(analyser, parent, statement_index, var_type);
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
    {
        bool in_current_scope;
        {
            Symbol* s = symbol_table_find_symbol_of_type_with_scope_info(parent, statement->name_id, Symbol_Type::VARIABLE, &in_current_scope);
            if (s != 0 && in_current_scope) {
                semantic_analyser_log_error(analyser, "Variable already defined", statement_index);
                break;
            }
        }
        Type_Signature* var_type = semantic_analyser_analyse_type(analyser, statement->children[0]);
        Type_Signature* assignment_type = semantic_analyser_analyse_expression(analyser, parent, statement->children[1]).type;
        if (var_type == analyser->type_system.void_type) {
            semantic_analyser_log_error(analyser, "Cannot create variable of void type", statement_index);
            return Statement_Analysis_Result::NO_RETURN;
        }
        if (assignment_type == analyser->type_system.void_type) {
            semantic_analyser_log_error(analyser, "Trying to assign void type to variable", statement_index);
            return Statement_Analysis_Result::NO_RETURN;
        }
        if (assignment_type != var_type && assignment_type != analyser->type_system.error_type) {
            semantic_analyser_log_error(analyser, "Variable type does not match expression type", statement_index);
        }
        semantic_analyser_define_variable(analyser, parent, statement_index, var_type);
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER:
    {
        {
            bool in_current_scope;
            Symbol* s = symbol_table_find_symbol_of_type_with_scope_info(parent, statement->name_id, Symbol_Type::VARIABLE, &in_current_scope);
            if (s != 0 && in_current_scope) {
                semantic_analyser_log_error(analyser, "Variable already defined", statement_index);
                break;
            }
        }
        Type_Signature* var_type = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]).type;
        if (var_type == analyser->type_system.void_type) {
            semantic_analyser_log_error(analyser, "Trying to create variable as void type", statement_index);
            return Statement_Analysis_Result::NO_RETURN;
        }
        semantic_analyser_define_variable(analyser, parent, statement_index, var_type);
        break;
    }
    default: {
        panic("Should be covered!\n");
        break;
    }
    }

    return Statement_Analysis_Result::NO_RETURN;
}

Statement_Analysis_Result semantic_analyser_analyse_statement_block(Semantic_Analyser* analyser, Symbol_Table* parent, int block_index)
{
    Symbol_Table* table = semantic_analyser_install_symbol_table(analyser, parent, block_index);

    int result_type_found = false; // Continue or break make 'dead code' returns or other things invalid
    Statement_Analysis_Result result = Statement_Analysis_Result::NO_RETURN;
    AST_Node* block = &analyser->parser->nodes[block_index];
    for (int i = 0; i < block->children.size; i++)
    {
        Statement_Analysis_Result statement_result = semantic_analyser_analyse_statement(analyser, table, block->children[i]);
        switch (statement_result)
        {
        case Statement_Analysis_Result::BREAK:
        case Statement_Analysis_Result::CONTINUE: {
            if (!result_type_found)
            {
                result = Statement_Analysis_Result::NO_RETURN;
                if (i != block->children.size - 1) {
                    // This should probably be a warning
                    semantic_analyser_log_error(analyser, "Code will never be reached, break or continue before prevents that!",
                        block->children[i + 1], block->children[block->children.size - 1]);
                }
                result_type_found = true;
            }
            break;
        }
        case Statement_Analysis_Result::RETURN:
            if (!result_type_found)
            {
                result = Statement_Analysis_Result::RETURN;
                if (i != block->children.size - 1) {
                    // This should probably be a warning
                    semantic_analyser_log_error(analyser, "Code will never be reached, return before prevents that!",
                        block->children[i + 1], block->children[block->children.size - 1]);
                }
                result_type_found = true;
            }
            break;
        case Statement_Analysis_Result::NO_RETURN:
            break;
        }
    }
    return result;
}

void semantic_analyser_analyse_function(Semantic_Analyser* analyser, Symbol_Table* parent, int function_node_index)
{
    AST_Node* function = &analyser->parser->nodes[function_node_index];
    Symbol_Table* table = semantic_analyser_install_symbol_table(analyser, parent, function_node_index);

    // Define parameter variables
    AST_Node* parameter_block = &analyser->parser->nodes[function->children[0]];
    Type_Signature* function_signature = symbol_table_find_symbol_of_type(parent, function->name_id, Symbol_Type::FUNCTION)->type;
    for (int i = 0; i < parameter_block->children.size; i++) {
        semantic_analyser_define_variable(analyser, table, parameter_block->children[i], function_signature->parameter_types[i]);
    }

    analyser->function_return_type = function_signature->return_type;
    analyser->loop_depth = 0;
    Statement_Analysis_Result result = semantic_analyser_analyse_statement_block(analyser, table, function->children[2]);
    if (result != Statement_Analysis_Result::RETURN) {
        semantic_analyser_log_error(analyser, "Not all code paths return a value!", function_node_index);
    }
}

// Semantic Analyser
Semantic_Analyser semantic_analyser_create()
{
    Semantic_Analyser result;
    result.symbol_tables = dynamic_array_create_empty<Symbol_Table*>(64);
    result.semantic_information = dynamic_array_create_empty<Semantic_Node_Information>(64);
    result.errors = dynamic_array_create_empty<Compiler_Error>(64);
    result.type_system = type_system_create();
    result.hardcoded_functions = array_create_empty<Hardcoded_Function>((i32)Hardcoded_Function_Type::HARDCODED_FUNCTION_COUNT);
    for (int i = 0; i < (i32)Hardcoded_Function_Type::HARDCODED_FUNCTION_COUNT; i++) {
        result.hardcoded_functions[i].type = (Hardcoded_Function_Type)i;
    }
    return result;
}

void semantic_analyser_destroy(Semantic_Analyser* analyser)
{
    for (int i = 0; i < analyser->symbol_tables.size; i++) {
        symbol_table_destroy(analyser->symbol_tables[i]);
        delete analyser->symbol_tables[i];
    }
    dynamic_array_destroy(&analyser->symbol_tables);
    dynamic_array_destroy(&analyser->semantic_information);
    dynamic_array_destroy(&analyser->errors);
    array_destroy(&analyser->hardcoded_functions);
    type_system_destroy(&analyser->type_system);
}

void semantic_analyser_analyse_function_header(Semantic_Analyser* analyser, Symbol_Table* table, int function_node_index)
{
    AST_Node* function = &analyser->parser->nodes[function_node_index];
    int function_name = analyser->parser->nodes[function_node_index].name_id;
    Symbol* func = symbol_table_find_symbol_of_type(table, function_name, Symbol_Type::FUNCTION);
    if (func != 0) {
        semantic_analyser_log_error(analyser, "Function already defined!", function_node_index);
        return;
    }

    AST_Node* parameter_block = &analyser->parser->nodes[function->children[0]];
    DynamicArray<Type_Signature*> parameter_types = dynamic_array_create_empty<Type_Signature*>(parameter_block->children.size);
    for (int i = 0; i < parameter_block->children.size; i++) {
        int parameter_index = parameter_block->children[i];
        AST_Node* parameter = &analyser->parser->nodes[parameter_index];
        dynamic_array_push_back(&parameter_types, semantic_analyser_analyse_type(analyser, parameter->children[0]));
    }
    Type_Signature* return_type = semantic_analyser_analyse_type(analyser, function->children[1]);
    Type_Signature* function_type = type_system_make_function(&analyser->type_system, parameter_types, return_type);

    Symbol s;
    s.symbol_type = Symbol_Type::FUNCTION;
    s.name_handle = function_name;
    s.type = function_type;
    dynamic_array_push_back(&table->symbols, s);

    analyser->semantic_information[function_node_index].function_signature = function_type;
}

void semantic_analyser_analyse(Semantic_Analyser* analyser, AST_Parser* parser)
{
    // TODO: We could also reuse the previous memory in the symbol tables, like in the parser
    for (int i = 0; i < analyser->symbol_tables.size; i++) {
        symbol_table_destroy(analyser->symbol_tables[i]);
        delete analyser->symbol_tables[i];
    }
    type_system_reset_all(&analyser->type_system);
    dynamic_array_reset(&analyser->symbol_tables);
    dynamic_array_reset(&analyser->semantic_information);
    dynamic_array_reset(&analyser->errors);
    analyser->parser = parser;

    dynamic_array_reserve(&analyser->semantic_information, parser->nodes.size);
    for (int i = 0; i < parser->nodes.size; i++) {
        Semantic_Node_Information info;
        info.expression_result_type = analyser->type_system.error_type;
        info.function_signature = analyser->type_system.error_type;
        info.symbol_table_index = 0;
        dynamic_array_push_back(&analyser->semantic_information, info);
    }

    // Create root table
    Symbol_Table* root_table = semantic_analyser_install_symbol_table(analyser, 0, 0);

    // Add tokens for basic datatypes
    {
        int int_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("int"));
        int bool_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("bool"));
        int float_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("float"));
        int u8_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("u8"));
        int u16_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("u16"));
        int u32_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("u32"));
        int u64_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("u64"));
        int i8_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("i8"));
        int i16_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("i16"));
        int i32_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("i32"));
        int i64_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("i64"));
        int f64_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("f64"));
        int f32_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("f32"));
        int byte_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("byte"));
        int void_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("void"));

        symbol_table_define_type(root_table, int_token_index, analyser->type_system.i32_type);
        symbol_table_define_type(root_table, bool_token_index, analyser->type_system.bool_type);
        symbol_table_define_type(root_table, float_token_index, analyser->type_system.f32_type);
        symbol_table_define_type(root_table, f32_token_index, analyser->type_system.f32_type);
        symbol_table_define_type(root_table, f64_token_index, analyser->type_system.f64_type);
        symbol_table_define_type(root_table, u8_token_index, analyser->type_system.u8_type);
        symbol_table_define_type(root_table, byte_token_index, analyser->type_system.u8_type);
        symbol_table_define_type(root_table, u16_token_index, analyser->type_system.u16_type);
        symbol_table_define_type(root_table, u32_token_index, analyser->type_system.u32_type);
        symbol_table_define_type(root_table, u64_token_index, analyser->type_system.u64_type);
        symbol_table_define_type(root_table, i8_token_index, analyser->type_system.i8_type);
        symbol_table_define_type(root_table, i16_token_index, analyser->type_system.i16_type);
        symbol_table_define_type(root_table, i32_token_index, analyser->type_system.i32_type);
        symbol_table_define_type(root_table, i64_token_index, analyser->type_system.i64_type);
        symbol_table_define_type(root_table, void_token_index, analyser->type_system.void_type);

        analyser->size_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("size"));
        analyser->data_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("data"));
        analyser->main_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("main"));
    }

    // Add symbols for hardcoded functions
    for (int i = 0; i < analyser->hardcoded_functions.size; i++)
    {
        Hardcoded_Function_Type type = analyser->hardcoded_functions[i].type;
        DynamicArray<Type_Signature*> parameter_types = dynamic_array_create_empty<Type_Signature*>(1);
        Type_Signature* return_type = analyser->type_system.void_type;
        int name_handle;
        switch (type)
        {
        case Hardcoded_Function_Type::PRINT_I32: {
            dynamic_array_push_back(&parameter_types, analyser->type_system.i32_type);
            name_handle = lexer_add_or_find_identifier_by_string(analyser->parser->lexer, string_create_static("print_i32"));
            break;
        }
        case Hardcoded_Function_Type::PRINT_F32: {
            dynamic_array_push_back(&parameter_types, analyser->type_system.f32_type);
            name_handle = lexer_add_or_find_identifier_by_string(analyser->parser->lexer, string_create_static("print_f32"));
            break;
        }
        case Hardcoded_Function_Type::PRINT_BOOL: {
            dynamic_array_push_back(&parameter_types, analyser->type_system.bool_type);
            name_handle = lexer_add_or_find_identifier_by_string(analyser->parser->lexer, string_create_static("print_bool"));
            break;
        }
        case Hardcoded_Function_Type::PRINT_LINE: {
            name_handle = lexer_add_or_find_identifier_by_string(analyser->parser->lexer, string_create_static("print_line"));
            break;
        }
        case Hardcoded_Function_Type::READ_I32: {
            name_handle = lexer_add_or_find_identifier_by_string(analyser->parser->lexer, string_create_static("read_i32"));
            return_type = analyser->type_system.i32_type;
            break;
        }
        case Hardcoded_Function_Type::READ_F32: {
            name_handle = lexer_add_or_find_identifier_by_string(analyser->parser->lexer, string_create_static("read_f32"));
            return_type = analyser->type_system.f32_type;
            break;
        }
        case Hardcoded_Function_Type::READ_BOOL: {
            name_handle = lexer_add_or_find_identifier_by_string(analyser->parser->lexer, string_create_static("read_bool"));
            return_type = analyser->type_system.bool_type;
            break;
        }
        case Hardcoded_Function_Type::RANDOM_I32: {
            name_handle = lexer_add_or_find_identifier_by_string(analyser->parser->lexer, string_create_static("random_i32"));
            return_type = analyser->type_system.i32_type;
            break;
        }
        default:
            panic("What");
        }
        analyser->hardcoded_functions[i].name_handle = name_handle;
        analyser->hardcoded_functions[i].function_type = type_system_make_function(&analyser->type_system, parameter_types, return_type);
        Symbol* func = symbol_table_find_symbol_of_type(root_table, analyser->hardcoded_functions[i].name_handle, Symbol_Type::FUNCTION);
        if (func != 0) {
            semantic_analyser_log_error(analyser, "Hardcoded_Function already defined!", 0);
        }
        else {
            Symbol s;
            s.symbol_type = Symbol_Type::FUNCTION;
            s.type = analyser->hardcoded_functions[i].function_type;
            s.name_handle = analyser->hardcoded_functions[i].name_handle;
            dynamic_array_push_back(&root_table->symbols, s);
        }
    }

    // Add all functions to root_table
    AST_Node* root = &analyser->parser->nodes[0];
    for (int i = 0; i < root->children.size; i++) {
        semantic_analyser_analyse_function_header(analyser, root_table, root->children[i]);
    }
    analyser->semantic_information[0].symbol_table_index = 0;

    // Analyse all functions
    for (int i = 0; i < root->children.size; i++) {
        semantic_analyser_analyse_function(analyser, root_table, root->children[i]);
    }

    // Search for main function
    Symbol* main_symbol = symbol_table_find_symbol_of_type(root_table, analyser->main_token_index, Symbol_Type::FUNCTION);
    if (main_symbol == 0) {
        semantic_analyser_log_error(analyser, "Main function not defined", 0);
    }
}