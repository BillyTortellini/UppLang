#include "semantic_analyser.hpp"

#include "../../datastructures/string.hpp"
#include "compiler.hpp"
#include "../../utility/hash_functions.hpp"
#include "../../datastructures/hashset.hpp"

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

bool primitive_type_is_integer(Primitive_Type type)
{
    switch (type)
    {
    case Primitive_Type::BOOLEAN:
    case Primitive_Type::FLOAT_32:
    case Primitive_Type::FLOAT_64: return false;
    case Primitive_Type::SIGNED_INT_8:
    case Primitive_Type::SIGNED_INT_16:
    case Primitive_Type::SIGNED_INT_32:
    case Primitive_Type::SIGNED_INT_64:
    case Primitive_Type::UNSIGNED_INT_8:
    case Primitive_Type::UNSIGNED_INT_16:
    case Primitive_Type::UNSIGNED_INT_32:
    case Primitive_Type::UNSIGNED_INT_64: return true;
    }
    panic("Shit");
    return false;
}

bool primitive_type_is_signed(Primitive_Type type)
{
    switch (type)
    {
    case Primitive_Type::BOOLEAN: return false;
    case Primitive_Type::FLOAT_32:
    case Primitive_Type::FLOAT_64: return true;
    case Primitive_Type::SIGNED_INT_8:
    case Primitive_Type::SIGNED_INT_16:
    case Primitive_Type::SIGNED_INT_32:
    case Primitive_Type::SIGNED_INT_64: return true;
    case Primitive_Type::UNSIGNED_INT_8:
    case Primitive_Type::UNSIGNED_INT_16:
    case Primitive_Type::UNSIGNED_INT_32:
    case Primitive_Type::UNSIGNED_INT_64: return false;
    }
    panic("Shit");
    return false;
}

bool primitive_type_is_float(Primitive_Type type)
{
    switch (type)
    {
    case Primitive_Type::BOOLEAN: return false;
    case Primitive_Type::FLOAT_32:
    case Primitive_Type::FLOAT_64: return true;
    case Primitive_Type::SIGNED_INT_8:
    case Primitive_Type::SIGNED_INT_16:
    case Primitive_Type::SIGNED_INT_32:
    case Primitive_Type::SIGNED_INT_64:
    case Primitive_Type::UNSIGNED_INT_8:
    case Primitive_Type::UNSIGNED_INT_16:
    case Primitive_Type::UNSIGNED_INT_32:
    case Primitive_Type::UNSIGNED_INT_64: return false;
    }
    panic("Shit");
    return false;
}

Type_Signature type_signature_make_error() 
{
    Type_Signature result;
    result.type = Signature_Type::ERROR_TYPE;
    result.size_in_bytes = 0;
    result.alignment_in_bytes = 1;
    result.struct_name_handle = -1;
    return result;
}

void type_signature_destroy(Type_Signature* sig) {
    if (sig->type == Signature_Type::FUNCTION)
        dynamic_array_destroy(&sig->parameter_types);
    if (sig->type == Signature_Type::STRUCT)
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
        if (sig1->type == Signature_Type::ARRAY_SIZED && sig1->child_type == sig2->child_type && sig1->array_element_count == sig2->array_element_count) return true;
        if (sig1->type == Signature_Type::ARRAY_UNSIZED && sig1->child_type == sig2->child_type) return true;
        if (sig1->type == Signature_Type::POINTER && sig1->child_type == sig2->child_type) return true;
        if (sig1->type == Signature_Type::ERROR_TYPE) return true;
        if (sig1->type == Signature_Type::VOID_TYPE) return true;
        if (sig1->type == Signature_Type::PRIMITIVE && sig1->primitive_type == sig2->primitive_type) return true;
        if (sig1->type == Signature_Type::FUNCTION)
        {
            if (sig1->return_type != sig2->return_type) return false;
            if (sig1->parameter_types.size != sig2->parameter_types.size) return false;
            for (int i = 0; i < sig1->parameter_types.size; i++) {
                if (sig1->parameter_types[i] != sig2->parameter_types[i]) {
                    return false;
                }
            }
            return true;
        }
        if (sig1->type == Signature_Type::STRUCT) {
            return sig1->struct_name_handle == sig2->struct_name_handle;
        }
    }
    return false;
}

void type_signature_append_to_string_with_children(String* string, Type_Signature* signature, bool print_child)
{
    switch (signature->type)
    {
    case Signature_Type::VOID_TYPE:
        string_append_formated(string, "VOID");
        break;
    case Signature_Type::ARRAY_SIZED:
        string_append_formated(string, "[%d]", signature->array_element_count);
        type_signature_append_to_string_with_children(string, signature->child_type, print_child);
        break;
    case Signature_Type::ARRAY_UNSIZED:
        string_append_formated(string, "[]");
        type_signature_append_to_string_with_children(string, signature->child_type, print_child);
        break;
    case Signature_Type::ERROR_TYPE:
        string_append_formated(string, "ERROR-Type");
        break;
    case Signature_Type::POINTER:
        string_append_formated(string, "*");
        type_signature_append_to_string_with_children(string, signature->child_type, print_child);
        break;
    case Signature_Type::PRIMITIVE:
        String s = primitive_type_to_string(signature->primitive_type);
        string_append_string(string, &s);
        break;
    case Signature_Type::STRUCT:
        string_append_formated(string, "STRUCT {");
        for (int i = 0; i < signature->member_types.size && print_child; i++) {
            type_signature_append_to_string_with_children(string, signature->member_types[i].type, false);
            if (i != signature->parameter_types.size - 1) {
                string_append_formated(string, ", ");
            }
        }
        string_append_formated(string, "}");
        break;
    case Signature_Type::FUNCTION:
        string_append_formated(string, "(");
        for (int i = 0; i < signature->parameter_types.size; i++) {
            type_signature_append_to_string_with_children(string, signature->parameter_types[i], print_child);
            if (i != signature->parameter_types.size - 1) {
                string_append_formated(string, ", ");
            }
        }
        string_append_formated(string, ") -> ");
        type_signature_append_to_string_with_children(string, signature->return_type, print_child);
    }
}

void type_signature_append_to_string(String* string, Type_Signature* signature)
{
    type_signature_append_to_string_with_children(string, signature, true);
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
    system->void_ptr_type = new Type_Signature();

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
    system->void_type->alignment_in_bytes = 1;

    system->void_ptr_type->type = Signature_Type::POINTER;
    system->void_ptr_type->size_in_bytes = 8;
    system->void_ptr_type->alignment_in_bytes = 8;
    system->void_ptr_type->child_type = system->void_type;

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
    dynamic_array_push_back(&system->types, system->void_ptr_type);

    {
        Struct_Member character_buffer_member;
        character_buffer_member.name_handle = lexer_add_or_find_identifier_by_string(system->lexer, string_create_static("character_buffer"));
        character_buffer_member.offset = 0;
        character_buffer_member.type = type_system_make_array_unsized(system, system->u8_type);

        Struct_Member size_member;
        size_member.name_handle = lexer_add_or_find_identifier_by_string(system->lexer, string_create_static("size"));
        size_member.offset = 16;
        size_member.type = system->i32_type;

        Dynamic_Array<Struct_Member> string_members = dynamic_array_create_empty<Struct_Member>(2);
        dynamic_array_push_back(&string_members, character_buffer_member);
        dynamic_array_push_back(&string_members, size_member);

        system->string_type = new Type_Signature();
        system->string_type->type = Signature_Type::STRUCT;
        system->string_type->alignment_in_bytes = 8;
        system->string_type->size_in_bytes = 20;
        system->string_type->member_types = string_members;
        system->string_type->struct_name_handle = lexer_add_or_find_identifier_by_string(system->lexer, string_create_static("String"));
        dynamic_array_push_back(&system->types, system->string_type);
    }
}

Type_System type_system_create(Lexer* lexer)
{
    Type_System result;
    result.lexer = lexer;
    result.types = dynamic_array_create_empty<Type_Signature*>(256);
    type_system_add_primitives(&result);
    return result;
}

void type_system_destroy(Type_System* system) {
    dynamic_array_destroy(&system->types);
}

void type_system_reset_all(Type_System* system, Lexer* lexer) {
    for (int i = 0; i < system->types.size; i++) {
        type_signature_destroy(system->types[i]);
        delete system->types[i];
    }
    dynamic_array_reset(&system->types);
    system->lexer = lexer;
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
    result.size_in_bytes = math_round_next_multiple(element_type->size_in_bytes, element_type->alignment_in_bytes) * array_element_count;
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

Type_Signature* type_system_make_function(Type_System* system, Dynamic_Array<Type_Signature*> parameter_types, Type_Signature* return_type) 
{
    Type_Signature result;
    result.type = Signature_Type::FUNCTION;
    result.alignment_in_bytes = 1;
    result.size_in_bytes = 0;
    result.parameter_types = parameter_types;
    result.return_type = return_type;
    return type_system_make_type(system, result);
}

void type_system_register_type(Type_System* system, Type_Signature* signature)
{
    dynamic_array_push_back(&system->types, signature);
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



/*
    Symbol Table
*/
Symbol* symbol_table_find_symbol_with_scope_info(Symbol_Table* table, int name_handle, bool* in_current_scope)
{
    *in_current_scope = false;
    for (int i = 0; i < table->symbols.size; i++) {
        if (table->symbols[i].name_handle == name_handle) {
            *in_current_scope = true;
            return &table->symbols[i];
        }
    }
    if (table->parent != 0) {
        Symbol* result = symbol_table_find_symbol_with_scope_info(table->parent, name_handle, in_current_scope);
        *in_current_scope = false;
        return result;
    }
    return 0;
}

Symbol* symbol_table_find_symbol(Symbol_Table* table, int name_handle)
{
    bool unused;
    return symbol_table_find_symbol_with_scope_info(table, name_handle, &unused);
}

Symbol* symbol_table_find_symbol_by_string(Symbol_Table* table, String* string, Lexer* lexer)
{
    int* index = hashtable_find_element(&lexer->identifier_index_lookup_table, *string);
    if (index == 0) return 0;
    else {
        return symbol_table_find_symbol(table, *index);
    }
}

Symbol* symbol_table_find_symbol_of_type_with_scope_info(Symbol_Table* table, int name_handle, Symbol_Type symbol_type, bool* in_current_scope)
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

Symbol* symbol_table_find_symbol_of_type(Symbol_Table* table, int name_handle, Symbol_Type symbol_type)
{
    bool unused;
    return symbol_table_find_symbol_of_type_with_scope_info(table, name_handle, symbol_type, &unused);
}

Symbol* symbol_table_find_symbol_of_identifer_node(Symbol_Table* table, Symbol_Type type, AST_Parser* parser, int node_index)
{
    AST_Node* node = &parser->nodes[node_index];
    assert(node->type == AST_Node_Type::IDENTIFIER || node->type == AST_Node_Type::IDENTIFIER_PATH, "Cannot lookup non identifer code");

    if (node->type == AST_Node_Type::IDENTIFIER) {
        return symbol_table_find_symbol_of_type(table, node->name_id, type);
    }
    else {
        Symbol* module_symbol = symbol_table_find_symbol_of_type(table, node->name_id, Symbol_Type::MODULE);
        if (module_symbol == 0) return 0;
        return symbol_table_find_symbol_of_identifer_node(module_symbol->options.module_table, type, parser, node->children[0]);
    }
}

void symbol_table_append_to_string_with_parent_info(String* string, Symbol_Table* table, Lexer* lexer, bool is_parent, bool print_root)
{
    if (!print_root && table->parent == 0) return;
    if (!is_parent) {
        string_append_formated(string, "Symbols: \n");
    }
    for (int i = 0; i < table->symbols.size; i++)
    {
        Symbol* s = &table->symbols[i];
        if (is_parent) {
            string_append_formated(string, "\t");
        }
        string_append_formated(string, "\t%s ", lexer_identifer_to_string(lexer, s->name_handle).characters);
        switch (s->symbol_type)
        {
        case Symbol_Type::VARIABLE:
            string_append_formated(string, "Variable");
            type_signature_append_to_string(string, ir_data_access_get_type(&s->options.variable_access));
            break;
        case Symbol_Type::TYPE:
            string_append_formated(string, "Type");
            type_signature_append_to_string(string, s->options.data_type);
            break;
        case Symbol_Type::FUNCTION: 
            string_append_formated(string, "Function"); break;
            type_signature_append_to_string(string, s->options.function->function_type);
        case Symbol_Type::HARDCODED_FUNCTION: 
            string_append_formated(string, "Hardcoded Function"); break;
            type_signature_append_to_string(string, s->options.data_type);
        case Symbol_Type::MODULE: 
            string_append_formated(string, "Module"); break;
        }

        string_append_formated(string, "\n");
    }
    if (table->parent != 0) {
        symbol_table_append_to_string_with_parent_info(string, table->parent, lexer, true, print_root);
    }
}

void symbol_table_append_to_string(String* string, Symbol_Table* table, Lexer* lexer, bool print_root) {
    symbol_table_append_to_string_with_parent_info(string, table, lexer, false, print_root);
}

void semantic_analyser_log_error(Semantic_Analyser* analyser, const char* msg, int node_index);
void symbol_table_define_symbol(Symbol_Table* table, Semantic_Analyser* analyser, Symbol symbol)
{
    if (symbol_table_find_symbol(table, symbol.name_handle) != 0) {
        semantic_analyser_log_error(analyser, "Symbol already defined", symbol.definition_node_index);
        return;
    }
    else {
        dynamic_array_push_back(&table->symbols, symbol);
    }
}



/*
    IR PROGRAM
*/

Type_Signature* ir_data_access_get_type(IR_Data_Access* access)
{
    Type_Signature* sig = 0;
    switch (access->type)
    {
    case IR_Data_Access_Type::GLOBAL_DATA:
        sig = access->option.program->globals[access->index];
        break;
    case IR_Data_Access_Type::CONSTANT:
        sig = access->option.program->constant_pool.constants[access->index].type;
        break;
    case IR_Data_Access_Type::REGISTER:
        sig = access->option.definition_block->registers[access->index];
        break;
    case IR_Data_Access_Type::PARAMETER:
        sig = access->option.definition_block->function->function_type->parameter_types[access->index];
        break;
    default: panic("Hey!");
    }
    if (access->is_memory_access) {
        return sig->child_type;
    }
    return sig;
}

void ir_code_block_destroy(IR_Code_Block* block);
void ir_instruction_destroy(IR_Instruction* instruction)
{
    switch (instruction->type)
    {
    case IR_Instruction_Type::FUNCTION_CALL: {
        dynamic_array_destroy(&instruction->options.call.arguments);
        break;
    }
    case IR_Instruction_Type::IF: {
        ir_code_block_destroy(instruction->options.if_instr.true_branch);
        ir_code_block_destroy(instruction->options.if_instr.false_branch);
        break;
    }
    case IR_Instruction_Type::WHILE: {
        ir_code_block_destroy(instruction->options.while_instr.code);
        break;
    }
    case IR_Instruction_Type::BLOCK: {
        ir_code_block_destroy(instruction->options.block);
        break;
    }
    case IR_Instruction_Type::BREAK:
    case IR_Instruction_Type::CONTINUE:
    case IR_Instruction_Type::RETURN:
    case IR_Instruction_Type::MOVE:
    case IR_Instruction_Type::CAST:
    case IR_Instruction_Type::ADDRESS_OF:
    case IR_Instruction_Type::UNARY_OP:
    case IR_Instruction_Type::BINARY_OP:
        break;
    default: panic("Lul");
    }
}

IR_Code_Block* ir_code_block_create(IR_Function* function)
{
    IR_Code_Block* block = new IR_Code_Block();
    block->function = function;
    block->instructions = dynamic_array_create_empty<IR_Instruction>(64);
    block->registers = dynamic_array_create_empty<Type_Signature*>(32);
    return block;
}

void ir_code_block_destroy(IR_Code_Block* block)
{
    for (int i = 0; i < block->instructions.size; i++) {
        ir_instruction_destroy(&block->instructions[i]);
    }
    dynamic_array_destroy(&block->instructions);
    dynamic_array_destroy(&block->registers);
    delete block;
}

IR_Function* ir_function_create(IR_Program* program, Type_Signature* signature)
{
    IR_Function* function = new IR_Function();
    function->code = ir_code_block_create(function);
    function->function_type = signature;
    function->program = program;
    return function;
}

void ir_function_destroy(IR_Function* function)
{
    ir_code_block_destroy(function->code);
    delete function;
}

IR_Program* ir_program_create(Type_System* type_system)
{
    IR_Program* result = new IR_Program();
    result->constant_pool.constants = dynamic_array_create_empty<IR_Constant>(128);
    result->constant_pool.constant_memory = dynamic_array_create_empty<byte>(2048);
    result->entry_function = 0;
    result->functions = dynamic_array_create_empty<IR_Function*>(64);
    result->globals = dynamic_array_create_empty<Type_Signature*>(64);

    result->hardcoded_functions = dynamic_array_create_empty<IR_Hardcoded_Function*>((int)IR_Hardcoded_Function_Type::HARDCODED_FUNCTION_COUNT);
    for (int i = 0; i < (int)IR_Hardcoded_Function_Type::HARDCODED_FUNCTION_COUNT; i++)
    {
        IR_Hardcoded_Function* function = new IR_Hardcoded_Function();
        IR_Hardcoded_Function_Type type = (IR_Hardcoded_Function_Type)i;
        function->type = type;

        Dynamic_Array<Type_Signature*> parameter_types = dynamic_array_create_empty<Type_Signature*>(1);
        Type_Signature* return_type = type_system->void_type;
        switch (type)
        {
        case IR_Hardcoded_Function_Type::PRINT_I32:
            dynamic_array_push_back(&parameter_types, type_system->i32_type);
            break;
        case IR_Hardcoded_Function_Type::PRINT_F32:
            dynamic_array_push_back(&parameter_types, type_system->f32_type);
            break;
        case IR_Hardcoded_Function_Type::PRINT_BOOL:
            dynamic_array_push_back(&parameter_types, type_system->bool_type);
            break;
        case IR_Hardcoded_Function_Type::PRINT_STRING:
            dynamic_array_push_back(&parameter_types, type_system->string_type);
            break;
        case IR_Hardcoded_Function_Type::PRINT_LINE:
            break;
        case IR_Hardcoded_Function_Type::READ_I32:
            return_type = type_system->i32_type;
            break;
        case IR_Hardcoded_Function_Type::READ_F32:
            return_type = type_system->f32_type;
            break;
        case IR_Hardcoded_Function_Type::READ_BOOL:
            return_type = type_system->bool_type;
            break;
        case IR_Hardcoded_Function_Type::RANDOM_I32:
            return_type = type_system->i32_type;
            break;
        case IR_Hardcoded_Function_Type::FREE_POINTER:
            dynamic_array_push_back(&parameter_types, type_system->void_ptr_type);
            return_type = type_system->void_type;
            break;
        case IR_Hardcoded_Function_Type::MALLOC_SIZE_I32:
            dynamic_array_push_back(&parameter_types, type_system->i32_type);
            return_type = type_system->void_ptr_type;
            break;
        default:
            panic("What");
        }
        function->signature = type_system_make_function(type_system, parameter_types, return_type);
        dynamic_array_push_back(&result->hardcoded_functions, function);
    }

    return result;
}

void ir_program_destroy(IR_Program* program) 
{
    dynamic_array_destroy(&program->constant_pool.constants);
    dynamic_array_destroy(&program->constant_pool.constant_memory);
    dynamic_array_destroy(&program->globals);
    for (int i = 0; i < program->functions.size; i++) {
        ir_function_destroy(program->functions[i]);
    }
    for (int i = 0; i < program->hardcoded_functions.size; i++) {
        delete program->hardcoded_functions[i];
    }
    dynamic_array_destroy(&program->hardcoded_functions);
    dynamic_array_destroy(&program->functions);
    delete program;
}




/*
    SEMANTIC ANALYSER
*/

enum class Statement_Analysis_Result
{
    NO_RETURN,
    RETURN,
    CONTINUE,
    BREAK
};

struct Expression_Analysis_Result
{
    Type_Signature* type;
    bool has_memory_address;
    bool error_occured;
};

void semantic_analyser_log_error(Semantic_Analyser* analyser, const char* msg, int node_index)
{
    Compiler_Error error;
    error.message = msg;
    error.range = analyser->compiler->parser.token_mapping[node_index];
    dynamic_array_push_back(&analyser->errors, error);
}

void semantic_analyser_log_error(Semantic_Analyser* analyser, const char* msg, int node_start_index, int node_end_index)
{
    Compiler_Error error;
    error.message = msg;
    error.range.start_index = analyser->compiler->parser.token_mapping[node_start_index].start_index;
    error.range.end_index = analyser->compiler->parser.token_mapping[node_end_index].end_index;
    dynamic_array_push_back(&analyser->errors, error);
}

Symbol_Table* semantic_analyser_create_symbol_table(Semantic_Analyser* analyser, Symbol_Table* parent, int node_index)
{
    Symbol_Table* table = new Symbol_Table();
    table->parent = parent;
    table->symbols = dynamic_array_create_empty<Symbol>(8);
    table->ast_node_index = node_index;
    dynamic_array_push_back(&analyser->symbol_tables, table);
    hashtable_insert_element(&analyser->ast_to_symbol_table, node_index, table);
    return table;
}

void semantic_analyser_define_type(Semantic_Analyser* analyser, Symbol_Table* table, int name_id, Type_Signature* type, int definition_node_index)
{
    Symbol* sym = symbol_table_find_symbol_of_type(table, name_id, Symbol_Type::TYPE);
    if (sym != 0) {
        semantic_analyser_log_error(analyser, "Type already defined!", definition_node_index);
        return;
    }

    Symbol s;
    s.symbol_type = Symbol_Type::TYPE;
    s.options.data_type = type;
    s.name_handle = name_id;
    s.definition_node_index = definition_node_index;
    dynamic_array_push_back(&table->symbols, s);
}

void semantic_analyser_define_module(Semantic_Analyser* analyser, Symbol_Table* table, int name_id, Symbol_Table* module_table, int definition_node_index)
{
    Symbol* sym = symbol_table_find_symbol_of_type(table, name_id, Symbol_Type::MODULE);
    if (sym != 0) {
        semantic_analyser_log_error(analyser, "Module already defined!", definition_node_index);
        return;
    }

    Symbol s;
    s.symbol_type = Symbol_Type::MODULE;
    s.options.module_table = module_table;
    s.name_handle = name_id;
    s.definition_node_index = definition_node_index;
    dynamic_array_push_back(&table->symbols, s);
}

Type_Signature* semantic_analyser_analyse_type(Semantic_Analyser* analyser, Symbol_Table* table, int type_node_index)
{
    AST_Node* type_node = &analyser->compiler->parser.nodes[type_node_index];
    switch (type_node->type)
    {
    case AST_Node_Type::TYPE_IDENTIFIER:
    {
        Symbol* symbol= symbol_table_find_symbol_of_identifer_node(table, Symbol_Type::TYPE, &analyser->compiler->parser, type_node->children[0]);
        if (symbol== 0) {
            semantic_analyser_log_error(analyser, "Invalid type, identifier is not a type!", type_node_index);
            return analyser->compiler->type_system.error_type;
        }
        return symbol->options.data_type;
    }
    case AST_Node_Type::TYPE_POINTER_TO: {
        return type_system_make_pointer(&analyser->compiler->type_system, semantic_analyser_analyse_type(analyser, table, type_node->children[0]));
    }
    case AST_Node_Type::TYPE_ARRAY_SIZED:
    {
        // TODO: check if expression is compile time known, currently only literal value is supported
        int index_node_array_size = type_node->children[0];
        AST_Node* node_array_size = &analyser->compiler->parser.nodes[index_node_array_size];
        if (node_array_size->type != AST_Node_Type::EXPRESSION_LITERAL) {
            semantic_analyser_log_error(analyser, "Array size is not a expression literal, currently not evaluable", index_node_array_size);
            return analyser->compiler->type_system.error_type;
        }
        Token literal_token = analyser->compiler->lexer.tokens[analyser->compiler->parser.token_mapping[index_node_array_size].start_index];
        if (literal_token.type != Token_Type::INTEGER_LITERAL) {
            semantic_analyser_log_error(analyser, "Array size is not an integer literal, currently not evaluable", index_node_array_size);
            return analyser->compiler->type_system.error_type;
        }

        Type_Signature* element_type = semantic_analyser_analyse_type(analyser, table, type_node->children[1]);
        if (element_type == analyser->compiler->type_system.void_type) {
            semantic_analyser_log_error(analyser, "Cannot have array of void type!", index_node_array_size);
            return analyser->compiler->type_system.error_type;
        }

        return type_system_make_array_sized(
            &analyser->compiler->type_system,
            element_type,
            literal_token.attribute.integer_value
        );
    }
    case AST_Node_Type::TYPE_ARRAY_UNSIZED: {
        Type_Signature* element_type = semantic_analyser_analyse_type(analyser, table, type_node->children[0]);
        if (element_type == analyser->compiler->type_system.void_type) {
            semantic_analyser_log_error(analyser, "Cannot have array of void type!", type_node->children[0]);
            return analyser->compiler->type_system.error_type;
        }
        return type_system_make_array_unsized(&analyser->compiler->type_system, element_type);
    }
    case AST_Node_Type::TYPE_FUNCTION_POINTER: 
    {
        AST_Node* parameter_block = &analyser->compiler->parser.nodes[type_node->children[0]];
        Dynamic_Array<Type_Signature*> parameter_types = dynamic_array_create_empty<Type_Signature*>(parameter_block->children.size);
        for (int i = 0; i < parameter_block->children.size; i++) {
            int param_type_index = parameter_block->children[i];
            dynamic_array_push_back(&parameter_types, semantic_analyser_analyse_type(analyser, table, param_type_index));
        }

        Type_Signature* return_type;
        if (type_node->children.size == 2) {
            return_type = semantic_analyser_analyse_type(analyser, table, type_node->children[1]);
        }
        else {
            return_type = analyser->compiler->type_system.void_type;
        }
        Type_Signature* function_type = type_system_make_function(&analyser->compiler->type_system, parameter_types, return_type);
        return type_system_make_pointer(&analyser->compiler->type_system, function_type);
    }
    }

    panic("This should not happen, this means that the child was not a type!\n");
    return analyser->compiler->type_system.error_type;
}

Expression_Analysis_Result expression_analysis_result_make(Type_Signature* expression_result, bool has_memory_address)
{
    Expression_Analysis_Result result;
    result.type = expression_result;
    result.has_memory_address = has_memory_address;
    result.error_occured = false;
    return result;
}

Expression_Analysis_Result expression_analysis_result_make_error()
{
    Expression_Analysis_Result result;
    result.error_occured = true;
    return result;
}

bool semantic_analyser_cast_implicit_if_possible(Semantic_Analyser* analyser, IR_Code_Block* block, IR_Data_Access source, IR_Data_Access destination)
{
    Type_Signature* source_type = ir_data_access_get_type(&source);
    Type_Signature* destination_type = ir_data_access_get_type(&destination);

    IR_Instruction cast_instr;
    cast_instr.type = IR_Instruction_Type::CAST;
    cast_instr.options.cast.source = source;
    cast_instr.options.cast.destination = destination;

    // Pointer casting
    if (source_type->type == Signature_Type::POINTER && destination_type->type == Signature_Type::POINTER) {
        if (source_type == analyser->compiler->type_system.void_ptr_type || destination_type == analyser->compiler->type_system.void_ptr_type) {
            cast_instr.options.cast.type = IR_Instruction_Cast_Type::POINTERS;
            dynamic_array_push_back(&block->instructions, cast_instr);
            return true;
        }
        return false;
    }
    // Primitive Casting:
    if (source_type->type == Signature_Type::PRIMITIVE && destination_type->type == Signature_Type::PRIMITIVE)
    {
        bool cast_valid = false;
        if (primitive_type_is_integer(source_type->primitive_type) && primitive_type_is_integer(destination_type->primitive_type)) {
            cast_valid = primitive_type_is_signed(source_type->primitive_type) == primitive_type_is_signed(destination_type->primitive_type);
        }
        if (!cast_valid) {
            if (primitive_type_is_float(destination_type->primitive_type) && primitive_type_is_integer(source_type->primitive_type)) {
                cast_valid = true;
            }
        }
        if (!cast_valid) {
            if (primitive_type_is_float(destination_type->primitive_type) && primitive_type_is_float(source_type->primitive_type)) {
                cast_valid = destination_type->size_in_bytes > source_type->size_in_bytes;
            }
        }
        if (!cast_valid) {
            if (source_type->primitive_type == Primitive_Type::BOOLEAN || destination_type->primitive_type == Primitive_Type::BOOLEAN) {
                return false;
            }
        }

        if (cast_valid) {
            cast_instr.options.cast.type = IR_Instruction_Cast_Type::PRIMITIVE_TYPES;
            dynamic_array_push_back(&block->instructions, cast_instr);
            return true;
        }
        else {
            return false;
        }
    }
    // Array casting
    if (source_type->type == Signature_Type::ARRAY_SIZED && destination_type->type == Signature_Type::ARRAY_UNSIZED) {
        if (source_type->child_type == destination_type->child_type) {
            cast_instr.options.cast.type = IR_Instruction_Cast_Type::ARRAY_SIZED_TO_UNSIZED;
            dynamic_array_push_back(&block->instructions, cast_instr);
            return true;
        }
    }
    return false;
}

Semantic_Analyser semantic_analyser_create()
{
    Semantic_Analyser result;
    result.symbol_tables = dynamic_array_create_empty<Symbol_Table*>(64);
    result.location_functions = dynamic_array_create_empty<AST_Top_Level_Node_Location>(64);
    result.location_globals = dynamic_array_create_empty<AST_Top_Level_Node_Location>(64);
    result.location_structs = dynamic_array_create_empty<AST_Top_Level_Node_Location>(64);
    result.errors = dynamic_array_create_empty<Compiler_Error>(64);
    result.ast_to_symbol_table = hashtable_create_empty<int, Symbol_Table*>(256, &hash_i32, &equals_i32);
    result.program = 0;
    return result;
}

void semantic_analyser_destroy(Semantic_Analyser* analyser)
{
    for (int i = 0; i < analyser->symbol_tables.size; i++) {
        Symbol_Table* table = analyser->symbol_tables[i];
        dynamic_array_destroy(&table->symbols);
        delete analyser->symbol_tables[i];
    }
    dynamic_array_destroy(&analyser->symbol_tables);
    dynamic_array_destroy(&analyser->location_functions);
    dynamic_array_destroy(&analyser->location_structs);
    dynamic_array_destroy(&analyser->location_globals);
    hashtable_destroy(&analyser->ast_to_symbol_table);
    dynamic_array_destroy(&analyser->errors);
}

Expression_Analysis_Result semantic_analyser_analyse_expression
(Semantic_Analyser* analyser, Symbol_Table* symbol_table, int expression_index, IR_Code_Block* code_block, bool write_to_access, IR_Data_Access* access);

void semantic_analyser_analyse_variable_creation_statements(Semantic_Analyser* analyser, Symbol_Table* symbol_table, int statement_index, IR_Code_Block* code_block)
{
    AST_Node* statement = &analyser->compiler->parser.nodes[statement_index];
    IR_Code_Block* global_initialize_block = analyser->program->entry_function->code;
    switch (statement->type)
    {
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION:
    {
        Type_Signature* var_type = semantic_analyser_analyse_type(analyser, symbol_table, statement->children[0]);
        if (var_type == analyser->compiler->type_system.void_type) {
            semantic_analyser_log_error(analyser, "Cannot create variable of void type", statement_index);
            var_type = analyser->compiler->type_system.error_type;
        }

        Symbol var_symbol;
        var_symbol.symbol_type = Symbol_Type::VARIABLE;
        var_symbol.name_handle = statement->name_id;
        var_symbol.definition_node_index = statement_index;
        if (code_block == 0) {
            dynamic_array_push_back(&analyser->program->globals, var_type);
            var_symbol.options.variable_access.index = analyser->program->globals.size - 1;
            var_symbol.options.variable_access.is_memory_access = false;
            var_symbol.options.variable_access.type = IR_Data_Access_Type::GLOBAL_DATA;
            var_symbol.options.variable_access.option.program = analyser->program;
        }
        else {
            dynamic_array_push_back(&code_block->registers, var_type);
            var_symbol.options.variable_access.index = code_block->registers.size - 1;
            var_symbol.options.variable_access.is_memory_access = false;
            var_symbol.options.variable_access.type = IR_Data_Access_Type::REGISTER;
            var_symbol.options.variable_access.option.definition_block = code_block;
        }
        symbol_table_define_symbol(symbol_table, analyser, var_symbol);
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
    {
        Type_Signature* var_type = semantic_analyser_analyse_type(analyser, symbol_table, statement->children[0]);
        if (var_type == analyser->compiler->type_system.void_type) {
            semantic_analyser_log_error(analyser, "Cannot create variable of void type", statement_index);
            var_type = analyser->compiler->type_system.error_type;
        }

        Symbol var_symbol;
        var_symbol.symbol_type = Symbol_Type::VARIABLE;
        var_symbol.name_handle = statement->name_id;
        var_symbol.definition_node_index = statement_index;
        IR_Code_Block* definition_block = 0;
        if (code_block == 0) {
            definition_block = global_initialize_block;
            dynamic_array_push_back(&analyser->program->globals, var_type);
            var_symbol.options.variable_access.index = analyser->program->globals.size - 1;
            var_symbol.options.variable_access.is_memory_access = false;
            var_symbol.options.variable_access.type = IR_Data_Access_Type::GLOBAL_DATA;
            var_symbol.options.variable_access.option.program = analyser->program;
        }
        else {
            definition_block = code_block;
            dynamic_array_push_back(&code_block->registers, var_type);
            var_symbol.options.variable_access.index = code_block->registers.size - 1;
            var_symbol.options.variable_access.is_memory_access = false;
            var_symbol.options.variable_access.type = IR_Data_Access_Type::REGISTER;
            var_symbol.options.variable_access.option.definition_block = code_block;
        }

        Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, statement->children[1], definition_block, true, &var_symbol.options.variable_access
        );
        if (!expr_result.error_occured) {
            if (expr_result.type != var_type) {
                semantic_analyser_log_error(analyser, "Variable type does not match", statement_index);
                return;
            }
        }
        symbol_table_define_symbol(symbol_table, analyser, var_symbol);
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER:
    {
        Symbol var_symbol;
        var_symbol.symbol_type = Symbol_Type::VARIABLE;
        var_symbol.name_handle = statement->name_id;
        var_symbol.definition_node_index = statement_index;
        IR_Code_Block* definition_block = 0;
        if (code_block == 0) {
            definition_block = global_initialize_block;
            dynamic_array_push_back(&analyser->program->globals, analyser->compiler->type_system.error_type);
            var_symbol.options.variable_access.index = analyser->program->globals.size - 1;
            var_symbol.options.variable_access.is_memory_access = false;
            var_symbol.options.variable_access.type = IR_Data_Access_Type::GLOBAL_DATA;
            var_symbol.options.variable_access.option.program = analyser->program;
        }
        else {
            definition_block = code_block;
            dynamic_array_push_back(&code_block->registers, analyser->compiler->type_system.error_type);
            var_symbol.options.variable_access.index = code_block->registers.size - 1;
            var_symbol.options.variable_access.is_memory_access = false;
            var_symbol.options.variable_access.type = IR_Data_Access_Type::REGISTER;
            var_symbol.options.variable_access.option.definition_block = code_block;
        }

        Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, statement->children[0], definition_block, true, &var_symbol.options.variable_access
        );
        Type_Signature* var_type = analyser->compiler->type_system.error_type;
        if (!expr_result.error_occured) {
            if (var_type == analyser->compiler->type_system.void_type) {
                semantic_analyser_log_error(analyser, "Trying to create variable as void type", statement_index);
            }
        }

        if (code_block == 0) {
            analyser->program->globals[analyser->program->globals.size - 1] = var_type;
        }
        else {
            code_block->registers[code_block->registers.size - 1] = var_type;
        }
        symbol_table_define_symbol(symbol_table, analyser, var_symbol);
        break;
    }
    default:
        panic("Should not happen!");
    }
    return;
}

void semantic_analyser_find_definitions(Semantic_Analyser* analyser, Symbol_Table* parent_table, int node_index)
{
    AST_Node* module_node = &analyser->compiler->parser.nodes[node_index];

    Symbol_Table* module_table;
    if (parent_table->parent != 0) {
        module_table = semantic_analyser_create_symbol_table(analyser, parent_table, node_index);
        semantic_analyser_define_module(analyser, parent_table, module_node->name_id, module_table, node_index);
    }
    else {
        module_table = parent_table;
    }

    for (int i = 0; i < module_node->children.size; i++)
    {
        int child_index = module_node->children[i];
        AST_Node* top_level_node = &analyser->compiler->parser.nodes[child_index];
        switch (top_level_node->type)
        {
        case AST_Node_Type::MODULE:
            semantic_analyser_find_definitions(analyser, module_table, child_index);
            break;
        case AST_Node_Type::FUNCTION: {
            AST_Top_Level_Node_Location loc;
            loc.node_index = child_index;
            loc.table = module_table;
            dynamic_array_push_back(&analyser->location_functions, loc);
            break;
        }
        case AST_Node_Type::STRUCT: {
            AST_Top_Level_Node_Location loc;
            loc.node_index = child_index;
            loc.table = module_table;
            dynamic_array_push_back(&analyser->location_structs, loc);
            break;
        }
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER:
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION: {
            AST_Top_Level_Node_Location loc;
            loc.node_index = child_index;
            loc.table = module_table;
            dynamic_array_push_back(&analyser->location_globals, loc);
            break;
        }
        }
    }
}

void semantic_analyser_calculate_struct_size(
    Semantic_Analyser* analyser, Type_Signature* struct_signature, Hashset<Type_Signature*>* visited, Hashset<Type_Signature*>* finished,
    int struct_node_index)
{
    bool in_visited = hashset_contains(visited, struct_signature);
    bool in_finished = hashset_contains(finished, struct_signature);
    if (in_finished) {
        return;
    }
    if (in_visited) {
        semantic_analyser_log_error(analyser, "Cyclic struct referencing, should not happen!", struct_node_index);
        return;
    }
    hashset_insert_element(visited, struct_signature);

    int offset = 0;
    int alignment = 0;
    for (int j = 0; j < struct_signature->member_types.size; j++)
    {
        Struct_Member* member = &struct_signature->member_types[j];
        Type_Signature* member_type = member->type;
        switch (member_type->type)
        {
        case Signature_Type::STRUCT:
            semantic_analyser_calculate_struct_size(analyser, member_type, visited, finished, struct_node_index);
            break;
        case Signature_Type::ARRAY_SIZED:
            if (member_type->child_type->type == Signature_Type::STRUCT) {
                semantic_analyser_calculate_struct_size(analyser, member_type->child_type, visited, finished, struct_node_index);
                member_type->alignment_in_bytes = member_type->child_type->alignment_in_bytes;
                member_type->size_in_bytes = math_round_next_multiple(member_type->child_type->size_in_bytes,
                    member_type->child_type->alignment_in_bytes) * member_type->array_element_count;
            }
            break;
        case Signature_Type::ARRAY_UNSIZED:
        case Signature_Type::ERROR_TYPE:
        case Signature_Type::FUNCTION:
        case Signature_Type::POINTER:
        case Signature_Type::PRIMITIVE:
        case Signature_Type::VOID_TYPE:
            // Do nothing
            break;
        default: panic("Hey watch out");
        }
        alignment = math_maximum(alignment, member->type->alignment_in_bytes);
        offset = math_round_next_multiple(offset, member->type->alignment_in_bytes);
        member->offset = offset;
        offset += member->type->size_in_bytes;
    }

    struct_signature->size_in_bytes = offset;
    struct_signature->alignment_in_bytes = alignment;
    hashset_insert_element(finished, struct_signature);
}

IR_Data_Access ir_data_access_create_intermediate(IR_Code_Block* block, Type_Signature* signature)
{
    IR_Data_Access access;
    access.is_memory_access = false;
    access.type = IR_Data_Access_Type::REGISTER;
    access.option.definition_block = block;
    dynamic_array_push_back(&block->registers, signature);
    access.index = block->registers.size - 1;
    return access;
}

IR_Data_Access ir_data_access_create_constant_access(IR_Program* program, Type_Signature* signature, Array<byte> bytes)
{
    dynamic_array_reserve(&program->constant_pool.constant_memory,
        program->constant_pool.constant_memory.size + signature->alignment_in_bytes + signature->size_in_bytes);
    while (program->constant_pool.constant_memory.size % signature->alignment_in_bytes != 0) {
        dynamic_array_push_back(&program->constant_pool.constant_memory, (byte)0);
    }

    IR_Constant constant;
    constant.type = signature;
    constant.offset = program->constant_pool.constant_memory.size;
    dynamic_array_push_back(&program->constant_pool.constants, constant);

    IR_Data_Access access;
    access.type = IR_Data_Access_Type::CONSTANT;
    access.index = program->constant_pool.constants.size - 1;
    access.is_memory_access = false;
    access.option.program = program;
    return access;
}

IR_Data_Access ir_data_access_create_constant_i32(Semantic_Analyser* analyser, i32 value) {
    return ir_data_access_create_constant_access(analyser->program, analyser->compiler->type_system.i32_type, array_create_static<byte>((byte*)&value, sizeof(i32)));
}

void ir_code_block_change_last_register_type(IR_Code_Block* block, Type_Signature* signature) {
    block->registers[block->registers.size - 1] = signature;
}

IR_Data_Access ir_data_access_dereference_pointer(IR_Code_Block* block, IR_Data_Access pointer_access)
{
    Type_Signature* pointer_sig = ir_data_access_get_type(&pointer_access);
    if (pointer_sig->type != Signature_Type::POINTER) {
        panic("Hey, this should not happen!");
    }

    if (pointer_access.is_memory_access) {
        IR_Data_Access loaded_ptr_access = ir_data_access_create_intermediate(block, pointer_sig);
        IR_Instruction move_instr;
        move_instr.type = IR_Instruction_Type::MOVE;
        move_instr.options.move.destination = loaded_ptr_access;
        move_instr.options.move.source = pointer_access;
        dynamic_array_push_back(&block->instructions, move_instr);

        IR_Data_Access result = loaded_ptr_access;
        result.is_memory_access = true;
        return result;
    }
    else {
        IR_Data_Access result = pointer_access;
        result.is_memory_access = true;
        return result;
    }
}

Expression_Analysis_Result semantic_analyser_analyse_expression
(Semantic_Analyser* analyser, Symbol_Table* symbol_table, int expression_index, IR_Code_Block* code_block, bool write_to_access, IR_Data_Access* access)
{
    AST_Node* expression_node = &analyser->compiler->parser.nodes[expression_index];
    Type_System* type_system = &analyser->compiler->type_system;

    bool is_binary_op = false;
    IR_Instruction_Binary_OP_Type binary_op_type;

    switch (expression_node->type)
    {
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL:
    {
        IR_Instruction call_instruction;
        call_instruction.type = IR_Instruction_Type::FUNCTION_CALL;

        Type_Signature* signature = 0;
        {
            {
                Symbol* var_symbol = symbol_table_find_symbol_of_identifer_node(
                    symbol_table, Symbol_Type::VARIABLE, &analyser->compiler->parser, expression_node->children[0]);
                if (var_symbol != 0)
                {
                    Type_Signature* var_type = ir_data_access_get_type(&var_symbol->options.variable_access);
                    if (var_type->type != Signature_Type::POINTER) {
                        semantic_analyser_log_error(analyser, "Call to variable is only allowed if it is a function pointer", expression_index);
                        return expression_analysis_result_make_error();
                    }
                    if (var_type->child_type->type != Signature_Type::FUNCTION) {
                        semantic_analyser_log_error(analyser, "Call to variable is only allowed if it is a function pointer", expression_index);
                        return expression_analysis_result_make_error();
                    }
                    signature = var_type->child_type;
                    call_instruction.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_POINTER_CALL;
                    call_instruction.options.call.options.pointer_access = var_symbol->options.variable_access;
                }
            }
            if (signature == 0) {
                Symbol* func_symbol = symbol_table_find_symbol_of_identifer_node(
                    symbol_table, Symbol_Type::FUNCTION, &analyser->compiler->parser, expression_node->children[0]);
                if (func_symbol != 0) {
                    if (func_symbol->options.function == analyser->program->entry_function) {
                        semantic_analyser_log_error(analyser, "One cannot call the main function again!", expression_index);
                        return expression_analysis_result_make_error();
                    }

                    signature = func_symbol->options.function->function_type;
                    call_instruction.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
                    call_instruction.options.call.options.function = func_symbol->options.function;
                }
            }
            if (signature == 0) {
                Symbol* hardcoded_symbol = symbol_table_find_symbol_of_identifer_node(
                    symbol_table, Symbol_Type::HARDCODED_FUNCTION, &analyser->compiler->parser, expression_node->children[0]);
                if (hardcoded_symbol != 0) {
                    signature = hardcoded_symbol->options.hardcoded_function->signature;
                    call_instruction.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
                    call_instruction.options.call.options.hardcoded = hardcoded_symbol->options.hardcoded_function;
                }
            }
            if (signature == 0) {
                semantic_analyser_log_error(analyser, "Call to undefined symbol", expression_index);
                return expression_analysis_result_make_error();
            }
        }

        if (!write_to_access) {
            *access = ir_data_access_create_intermediate(code_block, signature->return_type);
            call_instruction.options.call.destination = *access;
        }
        call_instruction.options.call.destination = *access;

        if (expression_node->children.size != signature->parameter_types.size) {
            semantic_analyser_log_error(analyser, "Argument size does not match function parameter size!", expression_index);
            return expression_analysis_result_make(signature->return_type, false);
        }

        bool error_occured = false;
        int arguments_node_index = expression_node->children[1];
        AST_Node* arguments_node = &analyser->compiler->parser.nodes[arguments_node_index];
        call_instruction.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(arguments_node->children.size);
        for (int i = 0; i < signature->parameter_types.size && i < arguments_node->children.size; i++)
        {
            IR_Data_Access argument_access = ir_data_access_create_intermediate(code_block, analyser->compiler->type_system.error_type);
            Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(
                analyser, symbol_table, arguments_node->children[i], code_block, true, &argument_access
            );
            if (expr_result.error_occured) {
                error_occured = true;
            }
            else if (expr_result.type != signature->parameter_types[i])
            {
                ir_code_block_change_last_register_type(code_block, expr_result.type);
                IR_Data_Access casted_argument = ir_data_access_create_intermediate(code_block, expr_result.type);
                if (semantic_analyser_cast_implicit_if_possible(analyser, code_block, argument_access, casted_argument)) {
                    dynamic_array_push_back(&call_instruction.options.call.arguments, casted_argument);
                }
                else if (!error_occured) {
                    error_occured = true;
                    semantic_analyser_log_error(analyser, "Argument type does not match function parameter type", expression_index);
                }
            }
        }

        dynamic_array_push_back(&code_block->instructions, call_instruction);
        return expression_analysis_result_make(signature->return_type, false);
    }
    case AST_Node_Type::EXPRESSION_VARIABLE_READ:
    {
        Symbol* symbol = symbol_table_find_symbol_of_identifer_node(symbol_table, Symbol_Type::VARIABLE, &analyser->compiler->parser, expression_node->children[0]);
        if (symbol == 0) {
            semantic_analyser_log_error(analyser, "Identifier for variable not found!", expression_index);
            return expression_analysis_result_make_error();
        }

        if (!write_to_access) {
            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.destination = *access;
            move_instr.options.move.source = symbol->options.variable_access;
            dynamic_array_push_back(&code_block->instructions, move_instr);
        }
        else {
            *access = symbol->options.variable_access;
        }

        return expression_analysis_result_make(ir_data_access_get_type(&symbol->options.variable_access), true);
    }
    case AST_Node_Type::EXPRESSION_CAST:
    {
        Type_Signature* cast_destination_type = semantic_analyser_analyse_type(analyser, symbol_table, expression_node->children[0]);
        if (cast_destination_type == analyser->compiler->type_system.error_type) {
            return expression_analysis_result_make_error();
        }

        IR_Data_Access source_access;
        Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[1], code_block, false, &source_access
        );
        if (expr_result.error_occured) {
            return expression_analysis_result_make_error();
        }

        Type_Signature* cast_source_type = expr_result.type;
        if (cast_source_type == analyser->compiler->type_system.error_type) {
            return expression_analysis_result_make(analyser->compiler->type_system.error_type, true);
        }

        bool cast_valid = false;
        IR_Instruction_Cast_Type cast_type;
        // Pointer casting
        if (cast_source_type->type == Signature_Type::POINTER && cast_destination_type->type == Signature_Type::POINTER) {
            cast_valid = true;
            cast_type = IR_Instruction_Cast_Type::POINTERS;
        }
        // U64 to Pointer
        if (cast_source_type == analyser->compiler->type_system.u64_type && cast_destination_type->type == Signature_Type::POINTER) {
            cast_valid = true;
            cast_type = IR_Instruction_Cast_Type::U64_TO_POINTER;
        }
        // Pointer to U64
        if (cast_source_type->type == Signature_Type::POINTER && cast_destination_type == analyser->compiler->type_system.u64_type) {
            cast_valid = true;
            cast_type = IR_Instruction_Cast_Type::POINTER_TO_U64;
        }
        // Primitive Casting:
        if (cast_source_type->type == Signature_Type::PRIMITIVE && cast_destination_type->type == Signature_Type::PRIMITIVE) {
            cast_valid = true;
            cast_type = IR_Instruction_Cast_Type::PRIMITIVE_TYPES;
            if (cast_source_type->primitive_type == Primitive_Type::BOOLEAN || cast_destination_type->primitive_type == Primitive_Type::BOOLEAN) {
                cast_valid = false;
            }
        }
        // Array casting
        if (cast_source_type->type == Signature_Type::ARRAY_SIZED && cast_destination_type->type == Signature_Type::ARRAY_UNSIZED) {
            if (cast_source_type->child_type == cast_destination_type->child_type) {
                cast_type = IR_Instruction_Cast_Type::ARRAY_SIZED_TO_UNSIZED;
                cast_valid = true;
            }
        }

        if (cast_valid)
        {
            IR_Instruction cast_instr;
            cast_instr.type = IR_Instruction_Type::CAST;
            cast_instr.options.cast.source = source_access;
            if (!write_to_access) {
                *access = ir_data_access_create_intermediate(code_block, cast_destination_type);
            }
            cast_instr.options.cast.destination = *access;
            cast_instr.options.cast.type = cast_type;
            dynamic_array_push_back(&code_block->instructions, cast_instr);
        }
        else {
            semantic_analyser_log_error(analyser, "Invalid cast!", expression_index);
        }
        return expression_analysis_result_make(cast_destination_type, false);
    }
    case AST_Node_Type::EXPRESSION_LITERAL:
    {
        Token* token = &analyser->compiler->lexer.tokens[analyser->compiler->parser.token_mapping[expression_index].start_index];
        IR_Data_Access literal_access;
        Type_System* type_system = &analyser->compiler->type_system;
        if (token->type == Token_Type::BOOLEAN_LITERAL) {
            byte value = token->attribute.bool_value == 0 ? 0 : 1;
            literal_access = ir_data_access_create_constant_access(
                analyser->program, type_system->bool_type, array_create_static<byte>(&value, sizeof(bool)));
        }
        else if (token->type == Token_Type::INTEGER_LITERAL) {
            int value = token->attribute.integer_value;
            literal_access = ir_data_access_create_constant_access(
                analyser->program, type_system->i32_type, array_create_static<byte>((byte*)&value, sizeof(int)));
        }
        else if (token->type == Token_Type::FLOAT_LITERAL) {
            float value = token->attribute.float_value;
            literal_access = ir_data_access_create_constant_access(
                analyser->program, type_system->f32_type, array_create_static<byte>((byte*)&value, sizeof(float)));
        }
        else if (token->type == Token_Type::NULLPTR) {
            void* value = nullptr;
            literal_access = ir_data_access_create_constant_access(
                analyser->program, type_system->void_ptr_type, array_create_static<byte>((byte*)&value, sizeof(void*)));
        }
        else if (token->type == Token_Type::STRING_LITERAL)
        {
            // TODO: Check this
            String string = lexer_identifer_to_string(&analyser->compiler->lexer, token->attribute.identifier_number);
            byte string_data[20];
            char** character_buffer_data_ptr = (char**)&string_data[0];
            int* character_buffer_size_ptr = (int*)&string_data[8];
            int* string_size_ptr = (int*)&string_data[16];
            *character_buffer_data_ptr = string.characters;
            *character_buffer_size_ptr = string.capacity;
            *string_size_ptr = string.size;

            literal_access = ir_data_access_create_constant_access(
                analyser->program, type_system->string_type, array_create_static<byte>(string_data, 20));
        }
        else {
            panic("Should not happen!");
        }

        if (!write_to_access) {
            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.destination = *access;
            move_instr.options.move.source = literal_access;
            dynamic_array_push_back(&code_block->instructions, move_instr);
        }
        else {
            *access = literal_access;
        }

        return expression_analysis_result_make(ir_data_access_get_type(&literal_access), false);
    }
    case AST_Node_Type::EXPRESSION_NEW:
    {
        Type_Signature* new_type = semantic_analyser_analyse_type(analyser, symbol_table, expression_node->children[0]);
        if (new_type == analyser->compiler->type_system.error_type) {
            return expression_analysis_result_make_error();
        }
        if (new_type == analyser->compiler->type_system.void_type) {
            semantic_analyser_log_error(analyser, "Cannot apply new to void type!", expression_index);
            return expression_analysis_result_make_error();
        }
        Type_Signature* result_type = type_system_make_pointer(&analyser->compiler->type_system, new_type);

        IR_Instruction instruction;
        instruction.type = IR_Instruction_Type::FUNCTION_CALL;
        instruction.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
        instruction.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(1);
        dynamic_array_push_back(&instruction.options.call.arguments, ir_data_access_create_constant_i32(analyser, result_type->size_in_bytes));
        if (!write_to_access) {
            *access = ir_data_access_create_intermediate(code_block, result_type);
            instruction.options.call.destination = *access;
        }
        instruction.options.call.destination = *access;
        instruction.options.call.options.hardcoded = analyser->program->hardcoded_functions[(int)IR_Hardcoded_Function_Type::MALLOC_SIZE_I32];
        dynamic_array_push_back(&code_block->instructions, instruction);

        return expression_analysis_result_make(result_type, false);
    }
    case AST_Node_Type::EXPRESSION_NEW_ARRAY:
    {
        Type_Signature* element_type = semantic_analyser_analyse_type(analyser, symbol_table, expression_node->children[1]);
        Type_Signature* array_type = type_system_make_array_unsized(&analyser->compiler->type_system, element_type);
        if (element_type == analyser->compiler->type_system.error_type) {
            return expression_analysis_result_make_error();
        }
        if (element_type == analyser->compiler->type_system.void_type) {
            semantic_analyser_log_error(analyser, "Cannot apply new to void type!", expression_index);
            return expression_analysis_result_make_error();
        }
        IR_Data_Access index_access;
        Expression_Analysis_Result index_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, true, &index_access);
        if (index_result.error_occured) {
            return expression_analysis_result_make(array_type, false);
        }
        if (index_result.type != analyser->compiler->type_system.i32_type) {
            semantic_analyser_log_error(analyser, "Array size in new must be of type i32", expression_index);
            return expression_analysis_result_make(array_type, false);
        }

        IR_Instruction size_calculation_instr;
        size_calculation_instr.type = IR_Instruction_Type::BINARY_OP;
        size_calculation_instr.options.binary_op.operand_left = index_access;
        int element_in_array_size = math_round_next_multiple(element_type->size_in_bytes, element_type->alignment_in_bytes);
        size_calculation_instr.options.binary_op.operand_right = ir_data_access_create_constant_i32(analyser, element_in_array_size);
        IR_Data_Access array_memory_size_access = ir_data_access_create_intermediate(code_block, analyser->compiler->type_system.i32_type);
        size_calculation_instr.options.binary_op.destination = array_memory_size_access;
        dynamic_array_push_back(&code_block->instructions, size_calculation_instr);

        IR_Instruction instruction;
        instruction.type = IR_Instruction_Type::FUNCTION_CALL;
        instruction.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
        instruction.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(1);
        dynamic_array_push_back(&instruction.options.call.arguments, array_memory_size_access);
        if (!write_to_access) {
            *access = ir_data_access_create_intermediate(code_block, array_type);
            instruction.options.call.destination = *access;
        }
        instruction.options.call.destination = *access;
        instruction.options.call.options.hardcoded = analyser->program->hardcoded_functions[(int)IR_Hardcoded_Function_Type::MALLOC_SIZE_I32];
        dynamic_array_push_back(&code_block->instructions, instruction);

        return expression_analysis_result_make(array_type, false);
    }
    case AST_Node_Type::EXPRESSION_ARRAY_ACCESS: {
        IR_Data_Access array_expr_access;
        Expression_Analysis_Result array_access_expr = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, true, &array_expr_access);
        if (array_access_expr.error_occured) {
            return expression_analysis_result_make_error();
        }
        Type_Signature* access_signature = array_access_expr.type;
        if (access_signature->type != Signature_Type::ARRAY_SIZED && access_signature->type != Signature_Type::ARRAY_UNSIZED) {
            semantic_analyser_log_error(analyser, "Expression is not an array, cannot access with []!", expression_node->children[0]);
            return expression_analysis_result_make_error();
        }

        IR_Data_Access index_access;
        Expression_Analysis_Result index_expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[1], code_block, true, &index_access);
        if (array_access_expr.error_occured) {
            return expression_analysis_result_make(access_signature->child_type, true);
        }
        if (index_expr_result.type != analyser->compiler->type_system.i32_type) {
            semantic_analyser_log_error(analyser, "Array index must be integer!", expression_node->children[1]);
            return expression_analysis_result_make(access_signature->child_type, true);
        }

        IR_Instruction instruction;
        instruction.type = IR_Instruction_Type::ADDRESS_OF;
        instruction.options.address_of.type = IR_Instruction_Address_Of_Type::ARRAY_ELEMENT;
        instruction.options.address_of.source = array_expr_access;
        instruction.options.address_of.options.index_access = index_access;
        instruction.options.address_of.destination = ir_data_access_create_intermediate(code_block,
            type_system_make_pointer(&analyser->compiler->type_system, access_signature->child_type)
        );
        dynamic_array_push_back(&code_block->instructions, instruction);

        if (write_to_access) {
            *access = instruction.options.address_of.destination;
            access->is_memory_access = true;
        }
        else {
            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.source = instruction.options.address_of.destination;
            move_instr.options.move.destination = *access;
            dynamic_array_push_back(&code_block->instructions, move_instr);
        }

        return expression_analysis_result_make(access_signature->child_type, true);
    }
    case AST_Node_Type::EXPRESSION_MEMBER_ACCESS:
    {
        IR_Data_Access expr_access;
        Expression_Analysis_Result access_expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, false, &expr_access
        );
        if (access_expr_result.error_occured) {
            return expression_analysis_result_make_error();
        }

        IR_Instruction access_instr;
        access_instr.type = IR_Instruction_Type::ADDRESS_OF;
        access_instr.options.address_of.type = IR_Instruction_Address_Of_Type::STRUCT_MEMBER;
        Type_Signature* type_signature = access_expr_result.type;
        if (type_signature->type == Signature_Type::POINTER) {
            if (type_signature->child_type->type == Signature_Type::STRUCT) {
                type_signature = type_signature->child_type;
                access_instr.options.address_of.source = ir_data_access_dereference_pointer(code_block, expr_access);
            }
        }
        else {
            access_instr.options.address_of.source = expr_access;
        }

        Type_Signature* member_type = 0;
        if (type_signature->type == Signature_Type::STRUCT)
        {
            Struct_Member* found = 0;
            for (int i = 0; i < type_signature->member_types.size; i++) {
                Struct_Member* member = &type_signature->member_types[i];
                if (member->name_handle == expression_node->name_id) {
                    found = member;
                }
            }
            if (found == 0) {
                semantic_analyser_log_error(analyser, "Struct does not contain this member name", expression_index);
                return expression_analysis_result_make_error();
            }

            access_instr.options.address_of.options.member = *found;
            member_type = found->type;
        }
        else if (type_signature->type == Signature_Type::ARRAY_SIZED || type_signature->type == Signature_Type::ARRAY_UNSIZED)
        {
            if (expression_node->name_id != analyser->token_index_size && expression_node->name_id != analyser->token_index_data) {
                semantic_analyser_log_error(analyser, "Arrays only have .size or .data as member!", expression_index);
                return expression_analysis_result_make_error();
            }
            if (type_signature->type == Signature_Type::ARRAY_UNSIZED)
            {
                if (expression_node->name_id == analyser->token_index_size) {
                    member_type = analyser->compiler->type_system.i32_type;
                    access_instr.options.address_of.options.member.name_handle = expression_node->name_id;
                    access_instr.options.address_of.options.member.offset = 8;
                    access_instr.options.address_of.options.member.type = member_type;
                }
                else {
                    member_type = type_system_make_pointer(&analyser->compiler->type_system, type_signature->child_type);
                    access_instr.options.address_of.options.member.name_handle = expression_node->name_id;
                    access_instr.options.address_of.options.member.offset = 0;
                    access_instr.options.address_of.options.member.type = member_type;
                }
            }
            else // Array_Sized
            {
                if (expression_node->name_id == analyser->token_index_size)
                {
                    IR_Instruction move_instr;
                    move_instr.type = IR_Instruction_Type::MOVE;
                    move_instr.options.move.source = ir_data_access_create_constant_i32(analyser, type_signature->array_element_count);
                    if (write_to_access) {
                        move_instr.options.move.destination = *access;
                    }
                    else {
                        move_instr.options.move.destination = ir_data_access_create_intermediate(code_block, analyser->compiler->type_system.i32_type);
                        *access = move_instr.options.move.destination;
                    }
                    dynamic_array_push_back(&code_block->instructions, move_instr);
                    return expression_analysis_result_make(analyser->compiler->type_system.i32_type, false);
                }
                else
                {
                    member_type = type_system_make_pointer(&analyser->compiler->type_system, type_signature->child_type);
                    IR_Instruction address_of_instr;
                    address_of_instr.type = IR_Instruction_Type::ADDRESS_OF;
                    address_of_instr.options.address_of.type = IR_Instruction_Address_Of_Type::DATA;
                    address_of_instr.options.address_of.source = access_instr.options.address_of.source;
                    if (write_to_access) {
                        address_of_instr.options.address_of.destination = *access;
                    }
                    else {
                        address_of_instr.options.address_of.destination = ir_data_access_create_intermediate(code_block, member_type);
                        *access = address_of_instr.options.address_of.destination;
                    }
                    dynamic_array_push_back(&code_block->instructions, address_of_instr);
                    return expression_analysis_result_make(member_type, false);
                }
            }
        }
        else
        {
            semantic_analyser_log_error(analyser, "Member access is only allowed on arrays or structs", expression_index);
            return expression_analysis_result_make_error();
        }

        access_instr.options.address_of.destination = ir_data_access_create_intermediate(code_block, type_system_make_pointer(type_system, member_type));
        dynamic_array_push_back(&code_block->instructions, access_instr);

        if (write_to_access) {
            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.source = access_instr.options.address_of.destination;
            move_instr.options.move.source.is_memory_access = true;
            move_instr.options.move.destination = *access;
            dynamic_array_push_back(&code_block->instructions, move_instr);
        }
        else {
            *access = access_instr.options.address_of.destination;
            access->is_memory_access = true;
        }

        return expression_analysis_result_make(member_type, true);
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT:
    {
        IR_Data_Access operand_access;
        Expression_Analysis_Result operand_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, false, &operand_access
        );
        if (operand_result.error_occured) {
            return expression_analysis_result_make(type_system->bool_type, false);
        }
        if (operand_result.type != type_system->bool_type) {
            semantic_analyser_log_error(analyser, "Not only works on boolean type", expression_index);
            return expression_analysis_result_make(type_system->bool_type, false);
        }
        IR_Instruction not_instr;
        not_instr.type = IR_Instruction_Type::UNARY_OP;
        not_instr.options.unary_op.source = operand_access;
        not_instr.options.unary_op.type = IR_Instruction_Unary_OP_Type::NOT;
        if (write_to_access) {
            not_instr.options.unary_op.destination = *access;
        }
        else {
            not_instr.options.unary_op.destination = ir_data_access_create_intermediate(code_block, type_system->bool_type);
            *access = not_instr.options.unary_op.destination;
        }
        dynamic_array_push_back(&code_block->instructions, not_instr);
        return expression_analysis_result_make(type_system->bool_type, false);
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE:
    {
        IR_Data_Access operand_access;
        Expression_Analysis_Result operand_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, false, &operand_access
        );
        if (operand_result.error_occured) {
            return expression_analysis_result_make_error();
        }
        if (operand_result.type->type != Signature_Type::PRIMITIVE) {
            semantic_analyser_log_error(analyser, "Negate only works on primitive types", expression_index);
            return expression_analysis_result_make_error();
        }
        if (!primitive_type_is_float(operand_result.type->primitive_type))
        {
            if (!primitive_type_is_integer(operand_result.type->primitive_type)) {
                semantic_analyser_log_error(analyser, "Negate only works on integers or floats", expression_index);
                return expression_analysis_result_make_error();
            }
            else {
                if (!primitive_type_is_signed(operand_result.type->primitive_type)) {
                    semantic_analyser_log_error(analyser, "Negate cannot be used on unsigned types", expression_index);
                    return expression_analysis_result_make_error();
                }
            }
        }

        IR_Instruction negate_instr;
        negate_instr.type = IR_Instruction_Type::UNARY_OP;
        negate_instr.options.unary_op.source = operand_access;
        negate_instr.options.unary_op.type = IR_Instruction_Unary_OP_Type::NEGATE;
        if (write_to_access) {
            negate_instr.options.unary_op.destination = *access;
        }
        else {
            negate_instr.options.unary_op.destination = ir_data_access_create_intermediate(code_block, operand_result.type);
            *access = negate_instr.options.unary_op.destination;
        }
        dynamic_array_push_back(&code_block->instructions, negate_instr);
        return expression_analysis_result_make(operand_result.type, false);
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF:
    {
        IR_Data_Access expr_access;
        Expression_Analysis_Result result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, false, &expr_access
        );
        if (result.error_occured) {
            return expression_analysis_result_make_error();
        }

        Type_Signature* pointer_type = type_system_make_pointer(type_system, result.type);
        if (!result.has_memory_address) {
            semantic_analyser_log_error(analyser, "Expression does not have a memory address!", expression_node->children[0]);
            return expression_analysis_result_make(pointer_type, false);
        }

        IR_Instruction address_of_instr;
        address_of_instr.type = IR_Instruction_Type::ADDRESS_OF;
        address_of_instr.options.address_of.type = IR_Instruction_Address_Of_Type::DATA;
        address_of_instr.options.address_of.source = expr_access;
        if (write_to_access) {
            address_of_instr.options.address_of.destination = *access;
        }
        else {
            *access = ir_data_access_create_intermediate(code_block, pointer_type);
            address_of_instr.options.address_of.destination = *access;
        }
        dynamic_array_push_back(&code_block->instructions, address_of_instr);
        return expression_analysis_result_make(pointer_type, false);
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE:
    {
        IR_Data_Access pointer_access;
        Expression_Analysis_Result result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, false, &pointer_access
        );
        if (result.error_occured) {
            return expression_analysis_result_make_error();
        }

        Type_Signature* signature = result.type;
        if (signature->type != Signature_Type::POINTER) {
            semantic_analyser_log_error(analyser, "Cannot dereference non-pointer type", expression_node->children[0]);
            return expression_analysis_result_make_error();
        }

        IR_Data_Access result_access = ir_data_access_dereference_pointer(code_block, pointer_access);;
        if (write_to_access) {
            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.destination = *access;
            move_instr.options.move.source = result_access;
            dynamic_array_push_back(&code_block->instructions, move_instr);
        }
        else {
            *access = result_access;
        }

        return expression_analysis_result_make(signature->child_type, true);
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::ADDITION;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::SUBTRACTION;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::DIVISION;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::MULTIPLICATION;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::GREATER_THAN;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::GREATER_EQUAL;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::LESS_THAN;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::LESS_EQUAL;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::MODULO;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::AND;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::OR;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::EQUAL;
        break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL:
        is_binary_op = true;
        binary_op_type = IR_Instruction_Binary_OP_Type::NOT_EQUAL;
        break;
    default: {
        panic("Not all expression covered!\n");
        break;
    }
    }

    if (is_binary_op)
    {
        IR_Data_Access left_access;
        IR_Data_Access right_access;
        Expression_Analysis_Result left_expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[0], code_block, false, &left_access
        );
        Expression_Analysis_Result right_expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, expression_node->children[1], code_block, false, &right_access
        );
        if (left_expr_result.error_occured || right_expr_result.error_occured) {
            return expression_analysis_result_make_error();
        }

        // Try implicit casting if types dont match
        Type_Signature* operand_type = left_expr_result.type;
        if (left_expr_result.type != right_expr_result.type)
        {
            bool cast_possible = false;
            switch (expression_node->type)
            {
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION:
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION:
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION:
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION:
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO:
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL:
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL:
                cast_possible = true;
                break;
            default: cast_possible = false;
            }

            if (!cast_possible) {
                semantic_analyser_log_error(analyser, "Left and right of binary operation do not match", expression_index);
                return expression_analysis_result_make_error();
            }
            IR_Data_Access casted_access = ir_data_access_create_intermediate(code_block, right_expr_result.type);
            bool left_to_right_worked = false;
            if (semantic_analyser_cast_implicit_if_possible(analyser, code_block, left_access, casted_access)) {
                operand_type = right_expr_result.type;
                left_access = casted_access;
                left_to_right_worked = true;
            }
            bool right_to_left_worked = false;
            if (!left_to_right_worked)
            {
                code_block->registers[casted_access.index] = left_expr_result.type;
                if (semantic_analyser_cast_implicit_if_possible(analyser, code_block, right_access, casted_access)) {
                    operand_type = left_expr_result.type;
                    right_access = casted_access;
                    right_to_left_worked = true;
                }
            }
            if (!right_to_left_worked && !left_to_right_worked) {
                semantic_analyser_log_error(analyser, "Left and right of binary operation do not match and cannot be cast", expression_index);
                return expression_analysis_result_make_error();
            }
        }

        // Determine what operands are valid
        bool int_valid = false;
        bool float_valid = false;
        bool bool_valid = false;
        bool ptr_valid = false;
        Type_Signature* result_type = operand_type;
        switch (binary_op_type)
        {
        case IR_Instruction_Binary_OP_Type::ADDITION:
        case IR_Instruction_Binary_OP_Type::SUBTRACTION:
        case IR_Instruction_Binary_OP_Type::MULTIPLICATION:
        case IR_Instruction_Binary_OP_Type::DIVISION:
            result_type = operand_type;
            float_valid = true;
            int_valid = true;
            break;
        case IR_Instruction_Binary_OP_Type::GREATER_THAN:
        case IR_Instruction_Binary_OP_Type::GREATER_EQUAL:
        case IR_Instruction_Binary_OP_Type::LESS_THAN:
        case IR_Instruction_Binary_OP_Type::LESS_EQUAL:
            float_valid = true;
            int_valid = true;
            result_type = type_system->bool_type;
            break;
        case IR_Instruction_Binary_OP_Type::MODULO:
            int_valid = true;
            result_type = operand_type;
            break;
        case IR_Instruction_Binary_OP_Type::EQUAL:
        case IR_Instruction_Binary_OP_Type::NOT_EQUAL:
            float_valid = true;
            int_valid = true;
            bool_valid = true;
            ptr_valid = true;
            result_type = type_system->bool_type;
            break;
        case IR_Instruction_Binary_OP_Type::AND:
        case IR_Instruction_Binary_OP_Type::OR:
            bool_valid = true;
            result_type = type_system->bool_type;
            break;
        }

        if (operand_type->type == Signature_Type::POINTER && !ptr_valid) {
            semantic_analyser_log_error(analyser, "Pointer not valid for this type of operation", expression_index);
            return expression_analysis_result_make_error();
        }
        if (operand_type->type != Signature_Type::PRIMITIVE) {
            semantic_analyser_log_error(analyser, "Non primitve type not valid for binary op", expression_index);
            return expression_analysis_result_make_error();
        }
        if (primitive_type_is_integer(operand_type->primitive_type) && !int_valid) {
            semantic_analyser_log_error(analyser, "Operands cannot be integers", expression_index);
            return expression_analysis_result_make_error();
        }
        if (primitive_type_is_float(operand_type->primitive_type) && !float_valid) {
            semantic_analyser_log_error(analyser, "Operands cannot be floats", expression_index);
            return expression_analysis_result_make_error();
        }
        if (operand_type->primitive_type == Primitive_Type::BOOLEAN && !bool_valid) {
            semantic_analyser_log_error(analyser, "Operands cannot be bools", expression_index);
            return expression_analysis_result_make_error();
        }

        IR_Instruction binary_op_instr;
        binary_op_instr.type = IR_Instruction_Type::BINARY_OP;
        binary_op_instr.options.binary_op.type = binary_op_type;
        binary_op_instr.options.binary_op.operand_left = left_access;
        binary_op_instr.options.binary_op.operand_right = right_access;
        if (write_to_access) {
            binary_op_instr.options.binary_op.destination = *access;
        }
        else {
            *access = ir_data_access_create_intermediate(code_block, result_type);
            binary_op_instr.options.binary_op.destination = *access;
        }
        dynamic_array_push_back(&code_block->instructions, binary_op_instr);

        return expression_analysis_result_make(result_type, false);
    }

    panic("Should not happen");
    return expression_analysis_result_make_error();
}

Statement_Analysis_Result semantic_analyser_analyse_statement_block
(Semantic_Analyser* analyser, Symbol_Table* parent_table, int block_index, IR_Code_Block* code_block);

Statement_Analysis_Result semantic_analyser_analyse_statement(
    Semantic_Analyser* analyser, Symbol_Table* symbol_table, int statement_index, IR_Code_Block* code_block)
{
    AST_Node* statement_node = &analyser->compiler->parser.nodes[statement_index];
    switch (statement_node->type)
    {
    case AST_Node_Type::STATEMENT_RETURN:
    {
        IR_Instruction return_instr;
        return_instr.type = IR_Instruction_Type::RETURN;
        Type_Signature* return_type;
        if (code_block->function == analyser->program->entry_function) {
            return_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
            return_instr.options.return_instr.options.exit_code = Exit_Code::SUCCESS;
            return_type = analyser->compiler->type_system.void_type;
        }
        else
        {
            if (statement_node->children.size == 0) {
                return_type = analyser->compiler->type_system.void_type;
                return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_EMPTY;
            }
            else
            {
                return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_DATA;
                Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(
                    analyser, symbol_table, statement_node->children[0], code_block, false, &return_instr.options.return_instr.options.return_value
                );
                if (!expr_result.error_occured) {
                    if (expr_result.type == analyser->compiler->type_system.void_type) {
                        semantic_analyser_log_error(analyser, "Cannot return void type", statement_index);
                        return Statement_Analysis_Result::RETURN;
                    }
                }
                return_type = expr_result.type;
            }
        }

        if (return_type != code_block->function->function_type->return_type) {
            semantic_analyser_log_error(analyser, "Return type does not match function return type", statement_index);
        }
        dynamic_array_push_back(&code_block->instructions, return_instr);
        return Statement_Analysis_Result::RETURN;
    }
    case AST_Node_Type::STATEMENT_BREAK: {
        if (analyser->loop_depth == 0) {
            semantic_analyser_log_error(analyser, "Break not inside loop!", statement_index);
            return Statement_Analysis_Result::NO_RETURN;
        }
        return Statement_Analysis_Result::BREAK;
    }
    case AST_Node_Type::STATEMENT_CONTINUE: {
        if (analyser->loop_depth == 0) {
            semantic_analyser_log_error(analyser, "Continue not inside loop!", statement_index);
            return Statement_Analysis_Result::NO_RETURN;
        }
        return Statement_Analysis_Result::CONTINUE;
    }
    case AST_Node_Type::STATEMENT_DEFER: {
        semantic_analyser_log_error(analyser, "Defer not supported yet!", statement_index);
        return Statement_Analysis_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_EXPRESSION:
    {
        AST_Node* expression_node = &analyser->compiler->parser.nodes[statement_node->children[0]];
        if (expression_node->type != AST_Node_Type::EXPRESSION_FUNCTION_CALL) {
            semantic_analyser_log_error(analyser, "Expression statement must be function call!", statement_index);
            return Statement_Analysis_Result::NO_RETURN;
        }
        IR_Data_Access temp;
        semantic_analyser_analyse_expression(analyser, symbol_table, statement_node->children[0], code_block, false, &temp);
        return Statement_Analysis_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_BLOCK: {
        IR_Instruction block_instruction;
        block_instruction.type = IR_Instruction_Type::BLOCK;
        block_instruction.options.block = ir_code_block_create(code_block->function);
        Statement_Analysis_Result result = semantic_analyser_analyse_statement_block(
            analyser, symbol_table, statement_index, block_instruction.options.block
        );
        dynamic_array_push_back(&code_block->instructions, block_instruction);
        return result;
    }
    case AST_Node_Type::STATEMENT_IF:
    {
        IR_Instruction if_instruction;
        if_instruction.type = IR_Instruction_Type::IF;
        Expression_Analysis_Result expression_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, statement_node->children[0], code_block, false, &if_instruction.options.if_instr.condition
        );
        if (!expression_result.error_occured) {
            if (expression_result.type != analyser->compiler->type_system.bool_type) {
                semantic_analyser_log_error(analyser, "If condition must be boolean value", statement_index);
            }
        }

        if_instruction.options.if_instr.true_branch = ir_code_block_create(code_block->function);
        if_instruction.options.if_instr.false_branch = ir_code_block_create(code_block->function);
        Statement_Analysis_Result true_branch_result = semantic_analyser_analyse_statement_block(
            analyser, symbol_table, statement_node->children[0], if_instruction.options.if_instr.true_branch
        );
        dynamic_array_push_back(&code_block->instructions, if_instruction);
        return true_branch_result;
    }
    case AST_Node_Type::STATEMENT_IF_ELSE:
    {
        IR_Instruction if_instruction;
        if_instruction.type = IR_Instruction_Type::IF;
        Expression_Analysis_Result expression_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, statement_node->children[0], code_block, false, &if_instruction.options.if_instr.condition
        );
        if (!expression_result.error_occured) {
            if (expression_result.type != analyser->compiler->type_system.bool_type) {
                semantic_analyser_log_error(analyser, "If condition must be boolean value", statement_index);
            }
        }

        if_instruction.options.if_instr.true_branch = ir_code_block_create(code_block->function);
        if_instruction.options.if_instr.false_branch = ir_code_block_create(code_block->function);
        Statement_Analysis_Result true_branch_result = semantic_analyser_analyse_statement_block(
            analyser, symbol_table, statement_node->children[1], if_instruction.options.if_instr.true_branch
        );
        Statement_Analysis_Result false_branch_result = semantic_analyser_analyse_statement_block(
            analyser, symbol_table, statement_node->children[2], if_instruction.options.if_instr.false_branch
        );
        dynamic_array_push_back(&code_block->instructions, if_instruction);

        if (true_branch_result == false_branch_result) return true_branch_result;
        return Statement_Analysis_Result::NO_RETURN; // Maybe i need to do something different here, but I dont think so
    }
    case AST_Node_Type::STATEMENT_WHILE:
    {
        IR_Instruction while_instruction;
        while_instruction.type = IR_Instruction_Type::WHILE;
        Expression_Analysis_Result expression_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, statement_node->children[0], code_block, false, &while_instruction.options.while_instr.condition
        );
        if (!expression_result.error_occured) {
            if (expression_result.type != analyser->compiler->type_system.bool_type) {
                semantic_analyser_log_error(analyser, "If condition must be boolean value", statement_index);
            }
        }

        while_instruction.options.while_instr.code = ir_code_block_create(code_block->function);
        analyser->loop_depth++;
        Statement_Analysis_Result code_result = semantic_analyser_analyse_statement_block(
            analyser, symbol_table, statement_node->children[1], while_instruction.options.while_instr.code
        );
        analyser->loop_depth--;
        dynamic_array_push_back(&code_block->instructions, while_instruction);

        if (code_result == Statement_Analysis_Result::RETURN) {
            semantic_analyser_log_error(analyser, "While loop never runs more than once, since it always returns!", statement_index);
        }
        else if (code_result == Statement_Analysis_Result::CONTINUE) {
            semantic_analyser_log_error(analyser, "While loop always continues!", statement_index);
        }
        else if (code_result == Statement_Analysis_Result::BREAK) {
            semantic_analyser_log_error(analyser, "While loop never runs more than once, since it always breaks!", statement_index);
        }
        return Statement_Analysis_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_DELETE:
    {
        IR_Data_Access delete_access;
        Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, statement_node->children[0], code_block, false, &delete_access
        );
        if (expr_result.error_occured) {
            return Statement_Analysis_Result::NO_RETURN;
        }
        Type_Signature* delete_type = expr_result.type;
        if (delete_type->type != Signature_Type::POINTER && delete_type->type != Signature_Type::ARRAY_UNSIZED) {
            semantic_analyser_log_error(analyser, "Delete must be called on either an pointer or an unsized array", statement_index);
            return Statement_Analysis_Result::NO_RETURN;
        }

        IR_Instruction delete_instr;
        delete_instr.type = IR_Instruction_Type::FUNCTION_CALL;
        delete_instr.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(1);
        delete_instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
        delete_instr.options.call.destination = {};
        delete_instr.options.call.options.hardcoded = analyser->program->hardcoded_functions[(int)IR_Hardcoded_Function_Type::FREE_POINTER];
        if (delete_type->type == Signature_Type::ARRAY_UNSIZED) {
            IR_Instruction address_instr;
            address_instr.type = IR_Instruction_Type::ADDRESS_OF;
            address_instr.options.address_of.type = IR_Instruction_Address_Of_Type::STRUCT_MEMBER;
            address_instr.options.address_of.source = delete_access;
            Type_Signature* pointer_sig = type_system_make_pointer(&analyser->compiler->type_system, expr_result.type->child_type);
            address_instr.options.address_of.destination = ir_data_access_create_intermediate(code_block, pointer_sig);
            address_instr.options.address_of.options.member.name_handle = analyser->token_index_data;
            address_instr.options.address_of.options.member.offset = 0;
            address_instr.options.address_of.options.member.type = pointer_sig;
            dynamic_array_push_back(&code_block->instructions, address_instr);
            dynamic_array_push_back(&delete_instr.options.call.arguments, address_instr.options.address_of.destination);
        }
        else {
            dynamic_array_push_back(&delete_instr.options.call.arguments, delete_access);
        }
        dynamic_array_push_back(&code_block->instructions, delete_instr);
        return Statement_Analysis_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_ASSIGNMENT:
    {
        IR_Data_Access left_access;
        Expression_Analysis_Result left_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, statement_node->children[0], code_block, false, &left_access
        );
        if (left_result.error_occured) {
            return Statement_Analysis_Result::NO_RETURN;
        }

        IR_Data_Access right_access;
        Expression_Analysis_Result right_result = semantic_analyser_analyse_expression(
            analyser, symbol_table, statement_node->children[1], code_block, false, &right_access
        );
        if (right_result.error_occured) {
            return Statement_Analysis_Result::NO_RETURN;
        }

        if (right_result.type == analyser->compiler->type_system.void_type) {
            semantic_analyser_log_error(analyser, "Cannot assign void type to anything", statement_index);
            return Statement_Analysis_Result::NO_RETURN;
        }
        if (!left_result.has_memory_address) {
            semantic_analyser_log_error(analyser, "Left side of assignment cannot be assigned to, does not have a memory address", statement_index);
            return Statement_Analysis_Result::NO_RETURN;
        }
        if (left_result.type != right_result.type) {
            if (!semantic_analyser_cast_implicit_if_possible(analyser, code_block, right_access, left_access)) {
                semantic_analyser_log_error(analyser, "Cannot assign, types are incompatible", statement_index);
            }
        }
        return Statement_Analysis_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION:
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER: {
        semantic_analyser_analyse_variable_creation_statements(analyser, symbol_table, statement_index, code_block);
        return Statement_Analysis_Result::NO_RETURN;
    }
    default: {
        panic("Should be covered!\n");
        break;
    }
    }

    return Statement_Analysis_Result::NO_RETURN;
}

Statement_Analysis_Result semantic_analyser_analyse_statement_block
(Semantic_Analyser* analyser, Symbol_Table* parent_table, int block_index, IR_Code_Block* code_block)
{
    Symbol_Table* block_table = semantic_analyser_create_symbol_table(analyser, parent_table, block_index);

    bool unreachable = false;
    Statement_Analysis_Result result = Statement_Analysis_Result::NO_RETURN;
    AST_Node* block_node = &analyser->compiler->parser.nodes[block_index];
    for (int i = 0; i < block_node->children.size; i++)
    {
        Statement_Analysis_Result statement_result = semantic_analyser_analyse_statement(analyser, block_table, block_node->children[i], code_block);
        switch (statement_result)
        {
        case Statement_Analysis_Result::BREAK:
        case Statement_Analysis_Result::CONTINUE: {
            if (!unreachable)
            {
                result = Statement_Analysis_Result::NO_RETURN;
                if (i != block_node->children.size - 1) {
                    // This should probably be a warning
                    semantic_analyser_log_error(analyser, "Code will never be reached, break or continue before prevents that!",
                        block_node->children[i + 1], block_node->children[block_node->children.size - 1]);
                }
                unreachable = true;
            }
            break;
        }
        case Statement_Analysis_Result::RETURN:
            if (!unreachable)
            {
                result = Statement_Analysis_Result::RETURN;
                if (i != block_node->children.size - 1) {
                    // This should probably be a warning
                    semantic_analyser_log_error(analyser, "Code will never be reached, return before prevents that!",
                        block_node->children[i + 1], block_node->children[block_node->children.size - 1]);
                }
                unreachable = true;
            }
            break;
        case Statement_Analysis_Result::NO_RETURN:
            break;
        }
    }

    return result;
}

void semantic_analyser_analyse(Semantic_Analyser* analyser, Compiler* compiler)
{
    analyser->compiler = compiler;
    for (int i = 0; i < analyser->symbol_tables.size; i++) {
        Symbol_Table* table = analyser->symbol_tables[i];
        dynamic_array_destroy(&table->symbols);
        delete analyser->symbol_tables[i];
    }
    type_system_reset_all(&analyser->compiler->type_system, &analyser->compiler->lexer);
    dynamic_array_reset(&analyser->symbol_tables);
    dynamic_array_reset(&analyser->errors);
    dynamic_array_reset(&analyser->location_functions);
    dynamic_array_reset(&analyser->location_globals);
    dynamic_array_reset(&analyser->location_structs);
    hashtable_reset(&analyser->ast_to_symbol_table);

    analyser->root_table = semantic_analyser_create_symbol_table(analyser, nullptr, 0);
    if (analyser->program != 0) {
        ir_program_destroy(analyser->program);
    }
    analyser->program = ir_program_create(&analyser->compiler->type_system);

    // Add symbols for basic datatypes
    {
        int int_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("int"));
        int bool_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("bool"));
        int float_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("float"));
        int u8_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("u8"));
        int u16_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("u16"));
        int u32_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("u32"));
        int u64_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("u64"));
        int i8_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("i8"));
        int i16_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("i16"));
        int i32_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("i32"));
        int i64_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("i64"));
        int f64_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("f64"));
        int f32_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("f32"));
        int byte_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("byte"));
        int void_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("void"));
        int string_token_index = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("String"));

        semantic_analyser_define_type(analyser, analyser->root_table, int_token_index, analyser->compiler->type_system.i32_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, bool_token_index, analyser->compiler->type_system.bool_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, float_token_index, analyser->compiler->type_system.f32_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, f32_token_index, analyser->compiler->type_system.f32_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, f64_token_index, analyser->compiler->type_system.f64_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, u8_token_index, analyser->compiler->type_system.u8_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, byte_token_index, analyser->compiler->type_system.u8_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, u16_token_index, analyser->compiler->type_system.u16_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, u32_token_index, analyser->compiler->type_system.u32_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, u64_token_index, analyser->compiler->type_system.u64_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, i8_token_index, analyser->compiler->type_system.i8_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, i16_token_index, analyser->compiler->type_system.i16_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, i32_token_index, analyser->compiler->type_system.i32_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, i64_token_index, analyser->compiler->type_system.i64_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, void_token_index, analyser->compiler->type_system.void_type, -1);
        semantic_analyser_define_type(analyser, analyser->root_table, string_token_index, analyser->compiler->type_system.string_type, -1);

        analyser->token_index_size = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("size"));
        analyser->token_index_data = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("data"));
        analyser->token_index_main = lexer_add_or_find_identifier_by_string(compiler->parser.lexer, string_create_static("main"));
    }

    // Initialize hardcoded_function types and symbols
    for (int i = 0; i < analyser->program->hardcoded_functions.size; i++)
    {
        IR_Hardcoded_Function* hardcoded = analyser->program->hardcoded_functions[i];
        Symbol symbol;
        symbol.definition_node_index = -1;
        symbol.options.hardcoded_function = hardcoded;
        symbol.symbol_type = Symbol_Type::HARDCODED_FUNCTION;
        switch (hardcoded->type)
        {
        case IR_Hardcoded_Function_Type::PRINT_I32: {
            symbol.name_handle = lexer_add_or_find_identifier_by_string(analyser->compiler->parser.lexer, string_create_static("print_i32"));
            break;
        }
        case IR_Hardcoded_Function_Type::PRINT_F32: {
            symbol.name_handle = lexer_add_or_find_identifier_by_string(analyser->compiler->parser.lexer, string_create_static("print_f32"));
            break;
        }
        case IR_Hardcoded_Function_Type::PRINT_BOOL: {
            symbol.name_handle = lexer_add_or_find_identifier_by_string(analyser->compiler->parser.lexer, string_create_static("print_bool"));
            break;
        }
        case IR_Hardcoded_Function_Type::PRINT_STRING: {
            symbol.name_handle = lexer_add_or_find_identifier_by_string(analyser->compiler->parser.lexer, string_create_static("print_string"));
            break;
        }
        case IR_Hardcoded_Function_Type::PRINT_LINE: {
            symbol.name_handle = lexer_add_or_find_identifier_by_string(analyser->compiler->parser.lexer, string_create_static("print_line"));
            break;
        }
        case IR_Hardcoded_Function_Type::READ_I32: {
            symbol.name_handle = lexer_add_or_find_identifier_by_string(analyser->compiler->parser.lexer, string_create_static("read_i32"));
            break;
        }
        case IR_Hardcoded_Function_Type::READ_F32: {
            symbol.name_handle = lexer_add_or_find_identifier_by_string(analyser->compiler->parser.lexer, string_create_static("read_f32"));
            break;
        }
        case IR_Hardcoded_Function_Type::READ_BOOL: {
            symbol.name_handle = lexer_add_or_find_identifier_by_string(analyser->compiler->parser.lexer, string_create_static("read_bool"));
            break;
        }
        case IR_Hardcoded_Function_Type::RANDOM_I32: {
            symbol.name_handle = lexer_add_or_find_identifier_by_string(analyser->compiler->parser.lexer, string_create_static("random_i32"));
            break;
        }
        case IR_Hardcoded_Function_Type::MALLOC_SIZE_I32:
        case IR_Hardcoded_Function_Type::FREE_POINTER:
            continue;
        default:
            panic("What");
        }
        symbol_table_define_symbol(analyser->root_table, analyser, symbol);
    }

    semantic_analyser_find_definitions(analyser, analyser->root_table, 0);

    // First analyse structs, then function headers, then globals, then function code
    Dynamic_Array<AST_Node>* nodes = &analyser->compiler->parser.nodes;
    // Analyse Structs
    {
        Dynamic_Array<Type_Signature*> struct_types = dynamic_array_create_empty<Type_Signature*>(32);
        SCOPE_EXIT(dynamic_array_destroy(&struct_types));
        for (int i = 0; i < analyser->location_structs.size; i++)
        {
            AST_Top_Level_Node_Location struct_loc = analyser->location_structs[i];
            AST_Node* struct_node = &nodes->data[struct_loc.node_index];

            Type_Signature* signature = new Type_Signature();
            signature->type = Signature_Type::STRUCT;
            signature->member_types = dynamic_array_create_empty<Struct_Member>(4);
            signature->alignment_in_bytes = 0;
            signature->size_in_bytes = 0;
            type_system_register_type(&analyser->compiler->type_system, signature);
            dynamic_array_push_back(&struct_types, signature);
            semantic_analyser_define_type(analyser, struct_loc.table, struct_node->name_id, signature, struct_loc.node_index);

            if (struct_node->children.size == 0) {
                semantic_analyser_log_error(analyser, "Struct cannot have 0 members", struct_loc.node_index);
            }
        }

        // Create members
        for (int i = 0; i < struct_types.size; i++)
        {
            Type_Signature* struct_type = struct_types[i];
            AST_Top_Level_Node_Location struct_loc = analyser->location_structs[i];
            AST_Node* struct_node = &nodes->data[struct_loc.node_index];

            for (int j = 0; j < struct_node->children.size; j++)
            {
                AST_Node* child_node = &nodes->data[struct_node->children[j]];
                Struct_Member member;
                member.name_handle = child_node->name_id;
                member.type = semantic_analyser_analyse_type(analyser, struct_loc.table, child_node->children[0]);
                member.offset = 0;
                dynamic_array_push_back(&struct_type->member_types, member);
            }
        }

        // Calculate struct size and member offset
        Hashset<Type_Signature*> visited_structs = hashset_create_empty<Type_Signature*>(64,
            [](Type_Signature** t) -> u64 { return hash_pointer((void**)t); },
            [](Type_Signature** a, Type_Signature** b) -> bool { return *a == *b; }
        );
        Hashset<Type_Signature*> finished_structs = hashset_create_empty<Type_Signature*>(64,
            [](Type_Signature** t) -> u64 { return hash_pointer((void**)t); },
            [](Type_Signature** a, Type_Signature** b) -> bool { return *a == *b; }
        );
        SCOPE_EXIT(hashset_destroy(&visited_structs));
        SCOPE_EXIT(hashset_destroy(&finished_structs));
        for (int i = 0; i < struct_types.size; i++)
        {
            Type_Signature* struct_type = struct_types[i];
            semantic_analyser_calculate_struct_size(analyser, struct_type, &visited_structs, &finished_structs, analyser->location_structs[i].node_index);
        }

        // Recalculate sized array sizes
        for (int j = 0; j < compiler->type_system.types.size; j++) {
            Type_Signature* sig = compiler->type_system.types[j];
            if (sig->type == Signature_Type::ARRAY_SIZED) {
                if (sig->child_type->type == Signature_Type::STRUCT) {
                    sig->alignment_in_bytes = sig->child_type->alignment_in_bytes;
                    sig->size_in_bytes = math_round_next_multiple(sig->child_type->size_in_bytes,
                        sig->child_type->alignment_in_bytes) * sig->array_element_count;
                }
            }
        }
    }

    struct Queued_Function
    {
        AST_Node_Index node_index;
        IR_Function* function;
        Symbol_Table* function_symbol_table;
    };
    Dynamic_Array<Queued_Function> queued_functions = dynamic_array_create_empty<Queued_Function>(64);
    SCOPE_EXIT(dynamic_array_destroy(&queued_functions));

    // Analyse function headers
    {
        analyser->program->entry_function = 0;
        for (int i = 0; i < analyser->location_functions.size; i++)
        {
            AST_Top_Level_Node_Location loc = analyser->location_functions[i];
            int function_name = analyser->compiler->parser.nodes[loc.node_index].name_id;
            AST_Node* function_node = &nodes->data[loc.node_index];
            AST_Node* signature_node = &nodes->data[function_node->children[0]];
            AST_Node* parameter_block = &nodes->data[signature_node->children[0]];

            // Create function signature
            Type_Signature* function_type;
            {
                Dynamic_Array<Type_Signature*> parameter_types = dynamic_array_create_empty<Type_Signature*>(parameter_block->children.size);
                for (int i = 0; i < parameter_block->children.size; i++)
                {
                    int parameter_index = parameter_block->children[i];
                    AST_Node* parameter = &analyser->compiler->parser.nodes[parameter_index];
                    dynamic_array_push_back(&parameter_types, semantic_analyser_analyse_type(analyser, loc.table, parameter->children[0]));
                }

                Type_Signature* return_type;
                if (signature_node->children.size == 2) {
                    return_type = semantic_analyser_analyse_type(analyser, loc.table, signature_node->children[1]);
                }
                else {
                    return_type = analyser->compiler->type_system.void_type;
                }
                function_type = type_system_make_function(&analyser->compiler->type_system, parameter_types, return_type);
            }

            // Create function
            IR_Function* function = ir_function_create(analyser->program, function_type);
            Symbol function_symbol;
            function_symbol.definition_node_index = loc.node_index;
            function_symbol.name_handle = function_node->name_id;
            function_symbol.options.function = function;
            function_symbol.symbol_type = Symbol_Type::FUNCTION;
            symbol_table_define_symbol(loc.table, analyser, function_symbol);
            if (function_node->name_id == analyser->token_index_main) {
                analyser->program->entry_function = function;
            }

            Symbol_Table* function_table = semantic_analyser_create_symbol_table(analyser, loc.table, loc.node_index);
            // Define parameters
            for (int i = 0; i < parameter_block->children.size; i++)
            {
                int parameter_index = parameter_block->children[i];
                AST_Node* parameter = &analyser->compiler->parser.nodes[parameter_index];

                Symbol symbol;
                symbol.definition_node_index = parameter_index;
                symbol.name_handle = parameter->name_id;
                symbol.symbol_type = Symbol_Type::VARIABLE;
                symbol.options.variable_access.index = i;
                symbol.options.variable_access.type = IR_Data_Access_Type::PARAMETER;
                symbol.options.variable_access.is_memory_access = false;
                symbol.options.variable_access.option.function = function;
                symbol_table_define_symbol(function_table, analyser, symbol);
            }

            Queued_Function queue_item;
            queue_item.function = function;
            queue_item.function_symbol_table = function_table;
            queue_item.node_index = loc.node_index;
            dynamic_array_push_back(&queued_functions, queue_item);
        }

        if (analyser->program->entry_function == 0) {
            semantic_analyser_log_error(analyser, "Main function not found!", 0);
        }
    }

    // Analyse Globals
    for (int i = 0; i < analyser->location_globals.size; i++)
    {
        AST_Top_Level_Node_Location location = analyser->location_globals[i];
        AST_Node* node = &nodes->data[location.node_index];
        semantic_analyser_analyse_variable_creation_statements(analyser, location.table, location.node_index, 0);
    }

    // Create function code
    for (int i = 0; i < queued_functions.size; i++)
    {
        Queued_Function item = queued_functions[i];
        analyser->loop_depth = 0;
        AST_Node* function_node = &nodes->data[item.node_index];
        semantic_analyser_analyse_statement_block(
            analyser, item.function_symbol_table, function_node->children[1], item.function->code
        );
    }
}
