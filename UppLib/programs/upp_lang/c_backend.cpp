#include "c_backend.hpp"

#include "../../utility/file_io.hpp"
#include <cstdlib>
#include <Windows.h>
#include "../../win32/windows_helper_functions.hpp"

C_Generator c_generator_create()
{
    C_Generator result;
    result.output_string = string_create_empty(4096);
    result.array_index_stack = dynamic_array_create_empty<int>(16);
    return result;
}

void c_generator_destroy(C_Generator* generator)
{
    string_destroy(&generator->output_string);
    dynamic_array_destroy(&generator->array_index_stack);
}

const char* c_generator_id_to_string(C_Generator* generator, int name_handle) {
    return lexer_identifer_to_string(generator->im_generator->analyser->parser->lexer, name_handle).characters;
}

void c_generator_generate_type_definition(C_Generator* generator, Type_Signature* signature, bool is_pointer)
{
    switch (signature->type)
    {
    case Signature_Type::VOID_TYPE: {
        string_append_formated(&generator->output_string, "void");
        break;
    }
    case Signature_Type::FUNCTION: {
        panic("Should not happen yet, only when we have function pointers!\n");
        break;
    }
    case Signature_Type::POINTER: {
        c_generator_generate_type_definition(generator, signature->child_type, true);
        string_append_formated(&generator->output_string, "*");
        break;
    }
    case Signature_Type::STRUCT: {
        string_append_formated(&generator->output_string, "%s", c_generator_id_to_string(generator, signature->struct_name_handle));
        break;
    }
    case Signature_Type::ARRAY_UNSIZED: {
        string_append_formated(&generator->output_string, "Unsized_Array");
        break;
    }
    case Signature_Type::ARRAY_SIZED: 
    {
        if (is_pointer) {
            c_generator_generate_type_definition(generator, signature->child_type, true);
        }
        else 
        {
            Type_Signature* child = signature->child_type;
            int element_count = signature->array_element_count;
            while (child->type == Signature_Type::ARRAY_SIZED) {
                element_count = element_count * child->array_element_count;
                child = child->child_type;
            }
            dynamic_array_push_back(&generator->array_index_stack, element_count);
            c_generator_generate_type_definition(generator, signature->child_type, true);
        }
        break;
    }
    case Signature_Type::PRIMITIVE:
    {
        switch (signature->primitive_type)
        {
        case Primitive_Type::BOOLEAN:  string_append_formated(&generator->output_string, "bool"); break;
        case Primitive_Type::FLOAT_32:  string_append_formated(&generator->output_string, "f32"); break;
        case Primitive_Type::FLOAT_64:  string_append_formated(&generator->output_string, "f64"); break;
        case Primitive_Type::SIGNED_INT_8:  string_append_formated(&generator->output_string, "i8"); break;
        case Primitive_Type::SIGNED_INT_16:  string_append_formated(&generator->output_string, "i16"); break;
        case Primitive_Type::SIGNED_INT_32:  string_append_formated(&generator->output_string, "i32"); break;
        case Primitive_Type::SIGNED_INT_64:  string_append_formated(&generator->output_string, "i64"); break;
        case Primitive_Type::UNSIGNED_INT_8:  string_append_formated(&generator->output_string, "u8"); break;
        case Primitive_Type::UNSIGNED_INT_16:  string_append_formated(&generator->output_string, "u16"); break;
        case Primitive_Type::UNSIGNED_INT_32:  string_append_formated(&generator->output_string, "u32"); break;
        case Primitive_Type::UNSIGNED_INT_64:  string_append_formated(&generator->output_string, "u64"); break;
        default: panic("Should not happen");
        }
        break;
    }
    }
}

void c_generator_generate_register_name(C_Generator* generator, int register_index)
{
    /*
    Intermediate_Register* reg = &generator->im_generator->functions[generator->current_function_index].registers[register_index];
    if (reg->type == Intermediate_Register_Type::VARIABLE) {
        string_append_formated(&generator->output_string, "%s_var%d", c_generator_id_to_string(generator, reg->name_id), register_index);
    }
    else if (reg->type == Intermediate_Register_Type::PARAMETER) {
        string_append_formated(&generator->output_string, "%s_param_%d", c_generator_id_to_string(generator, reg->name_id), register_index);
    }
    else if (reg->type == Intermediate_Register_Type::EXPRESSION_RESULT) {
        string_append_formated(&generator->output_string, "_upp_expr_%d", register_index);
    }
    */
}

void c_generator_generate_data_access(C_Generator* generator, Data_Access access)
{
    /*
    if (access.type == Data_Access_Type::MEMORY_ACCESS) {
        string_append_formated(&generator->output_string, "*");
    }
    c_generator_generate_register_name(generator, access.register_index);
    */
}

/* C arrays suck a little, because:
    x: [5]int       -->     int x[5];
    x: [5][3]int    -->     int x[15];
    x: *[5]int      -->     int* x;
    x: [5]*int      -->     int* x[5];

    The question is how I handle pointers and arrays
    Unsized arrays should be ezy

    The problem is that C passes arrays ALWAYS as a pointer, so I would need a memcpy after passing the parameter
*/
void c_generator_generate_variable_definition_with_name_handle(C_Generator* generator, int name_handle, Type_Signature* signature, bool semicolon)
{
    dynamic_array_reset(&generator->array_index_stack);
    c_generator_generate_type_definition(generator, signature, false);
    string_append_formated(&generator->output_string, " %s", c_generator_id_to_string(generator, name_handle));
    for (int i = 0; i < generator->array_index_stack.size; i++) {
        string_append_formated(&generator->output_string, "[%d]", generator->array_index_stack[i]);
    }
    dynamic_array_reset(&generator->array_index_stack);
    if (semicolon) {
        string_append_formated(&generator->output_string, ";");
    }
}

void c_generator_generate_variable_definition_with_register_index(C_Generator* generator, int register_index, bool semicolon)
{
    /*
    Intermediate_Register* reg = &generator->im_generator->functions[generator->current_function_index].registers[register_index];
    dynamic_array_reset(&generator->array_index_stack);
    c_generator_generate_type_definition(generator, reg->type_signature, false);
    string_append_formated(&generator->output_string, " ");
    c_generator_generate_register_name(generator, register_index);
    if (generator->array_index_stack.size > 0)
    {
        for (int i = 0; i < generator->array_index_stack.size; i++) {
            string_append_formated(&generator->output_string, "[%d]", generator->array_index_stack[i]);
        }
    }
    dynamic_array_reset(&generator->array_index_stack);
    if (semicolon) {
        string_append_formated(&generator->output_string, ";");
    }
    */
}

void c_generator_generate_function_header(C_Generator* generator, int function_index)
{
    AST_Node* function_node = &generator->im_generator->analyser->parser->nodes[generator->im_generator->function_to_ast_node_mapping[function_index]];
    AST_Node* parameter_block_node = &generator->im_generator->analyser->parser->nodes[function_node->children[0]];
    Intermediate_Function* function = &generator->im_generator->functions[function_index];
    Type_Signature* signature = function->function_type;

    c_generator_generate_type_definition(generator, signature->return_type, false);
    if (function_index == generator->im_generator->main_function_index) {
        string_append_formated(&generator->output_string, " _upp_main(");
    }
    else {
        string_append_formated(&generator->output_string, " %s(", c_generator_id_to_string(generator, function->name_handle));
    }

    // I dont remember why i removed this, but I hope the reasons were bad...
    /*
    for (int i = 0; i < function->registers.size; i++)
    {
        Intermediate_Register* reg = &function->registers[i];
        if (reg->type != Intermediate_Register_Type::PARAMETER) continue;
        if (i != 0) {
            string_append_formated(&generator->output_string, ", ");
        }
        c_generator_generate_variable_definition_with_register_index(generator, i, false);
    }
    */
    /*
    for (int i = 0; i < signature->parameter_types.size; i++)
    {
        Type_Signature* param_type = signature->parameter_types[i];
        int param_name_id = generator->im_generator->analyser->parser->nodes[parameter_block_node->children[i]].name_id;
        c_generator_generate_variable_definition(generator, param_name_id, param_type, false);
        if (i != signature->parameter_types.size - 1) {
            string_append_formated(&generator->output_string, ",");
        }
    }
    */
    string_append_formated(&generator->output_string, ")");
}

void c_generator_generate_function_instruction_slice(
    C_Generator* generator, int indentation_level, bool indent_first, int instr_start_index, int instr_end_index_exclusive)
{
    Intermediate_Function* function = &generator->im_generator->functions[generator->current_function_index];
    for (int instruction_index = instr_start_index;
        instruction_index < function->instructions.size && instruction_index < instr_end_index_exclusive;
        instruction_index++)
    {
        for (int i = 0; i < indentation_level; i++) {
            if (instruction_index == instr_start_index && !indent_first) {
                break;
            }
            string_append_formated(&generator->output_string, "    ");
        }
        Intermediate_Instruction* instr = &function->instructions[instruction_index];
        bool is_binary_op = false;
        bool is_unary_op = false;
        const char* operation_str = "";
        switch (instr->type)
        {
        case Intermediate_Instruction_Type::MOVE_DATA:
        {
            c_generator_generate_data_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = ");
            c_generator_generate_data_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ";\n");
            break;
        }
        case Intermediate_Instruction_Type::ADDRESS_OF:
        {
            c_generator_generate_data_access(generator, instr->destination);
            /*
            Intermediate_Register* reg = &function->registers[instr->source1.register_index];
            if (reg->type_signature->type == Signature_Type::ARRAY_SIZED && instr->source1.type == Data_Access_Type::REGISTER_ACCESS) {
                string_append_formated(&generator->output_string, " = ");
                c_generator_generate_data_access(generator, instr->source1);
                string_append_formated(&generator->output_string, ";\n");
                break;
            }
            */
            string_append_formated(&generator->output_string, " = &");
            c_generator_generate_data_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ";\n");
            break;
        }
        case Intermediate_Instruction_Type::BREAK:
        {
            string_append_formated(&generator->output_string, "break;\n");
            break;
        }
        case Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION:
        case Intermediate_Instruction_Type::CALL_FUNCTION:
        {
            Type_Signature* return_type;
            const char* function_str = "error";
            bool cast_to_type = false;
            if (instr->type == Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION)
            {
                Hardcoded_Function* hardcoded = &generator->im_generator->analyser->hardcoded_functions[(u32)instr->hardcoded_function_type];
                Type_Signature* function_type = hardcoded->function_type;
                return_type = function_type->return_type;
                if (instr->hardcoded_function_type == Hardcoded_Function_Type::FREE_POINTER) {
                    function_str = "free_pointer";
                }
                else if (instr->hardcoded_function_type == Hardcoded_Function_Type::MALLOC_SIZE_I32) {
                    function_str = "malloc_size_i32";
                    cast_to_type = true;
                }
                else {
                    function_str = c_generator_id_to_string(generator, hardcoded->name_handle);
                }
            }
            else {
                Type_Signature* function_type = generator->im_generator->functions[instr->intermediate_function_index].function_type;
                return_type = function_type->return_type;
                function_str = c_generator_id_to_string(generator, generator->im_generator->functions[instr->intermediate_function_index].name_handle);
            }

            if (return_type != generator->im_generator->analyser->type_system.void_type)
            {
                c_generator_generate_data_access(generator, instr->destination);
                string_append_formated(&generator->output_string, " = ");
                if (cast_to_type) {
                    string_append_formated(&generator->output_string, "(", function_str);
                    c_generator_generate_type_definition(generator, return_type, true);
                    string_append_formated(&generator->output_string, ")", function_str);
                }
            }
            string_append_formated(&generator->output_string, "%s(", function_str);
            for (int i = 0; i < instr->arguments.size; i++) {
                c_generator_generate_data_access(generator, instr->arguments[i]);
                if (i != instr->arguments.size - 1) {
                    string_append_formated(&generator->output_string, ", ");
                }
            }
            string_append_formated(&generator->output_string, ");\n");
            break;
        }
        case Intermediate_Instruction_Type::CONTINUE:
        {
            string_append_formated(&generator->output_string, "continue;\n");
            break;
        }
        case Intermediate_Instruction_Type::EXIT:
        {
            int code = 0;
            switch (instr->exit_code)
            {
            case Exit_Code::OUT_OF_BOUNDS: {
                code = -1;
                break;
            }
            case Exit_Code::RETURN_VALUE_OVERFLOW: {
                code = -2;
                break;
            }
            case Exit_Code::STACK_OVERFLOW: {
                code = -3;
                break;
            }
            case Exit_Code::SUCCESS: {
                code = 0;
                break;
            }
            }
            string_append_formated(&generator->output_string, "exit(%d);\n", code);
            break;
        }
        case Intermediate_Instruction_Type::IF_BLOCK:
        {
            c_generator_generate_function_instruction_slice(generator, indentation_level, false,
                instr->condition_calculation_instruction_start,
                instr->condition_calculation_instruction_end_exclusive
            );
            for (int i = 0; i < indentation_level; i++) {
                string_append_formated(&generator->output_string, "    ");
            }
            string_append_formated(&generator->output_string, "if (");
            c_generator_generate_data_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ") {\n");
            c_generator_generate_function_instruction_slice(generator, indentation_level + 1, true,
                instr->true_branch_instruction_start,
                instr->true_branch_instruction_end_exclusive
            );
            for (int i = 0; i < indentation_level; i++) {
                string_append_formated(&generator->output_string, "    ");
            }
            string_append_formated(&generator->output_string, "}\n");
            instruction_index = instr->false_branch_instruction_end_exclusive - 1;
            if (instr->false_branch_instruction_start != instr->false_branch_instruction_end_exclusive)
            {
                instruction_index = instr->false_branch_instruction_end_exclusive - 1;
                for (int i = 0; i < indentation_level; i++) {
                    string_append_formated(&generator->output_string, "    ");
                }
                string_append_formated(&generator->output_string, "else {\n");
                c_generator_generate_function_instruction_slice(generator, indentation_level + 1, true,
                    instr->false_branch_instruction_start,
                    instr->false_branch_instruction_end_exclusive
                );
                for (int i = 0; i < indentation_level; i++) {
                    string_append_formated(&generator->output_string, "    ");
                }
                string_append_formated(&generator->output_string, "}\n");

            }
            break;
        }
        case Intermediate_Instruction_Type::LOAD_CONSTANT_BOOL:
        {
            c_generator_generate_data_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = %s;\n", instr->constant_bool_value ? "true" : "false");
            break;
        }
        case Intermediate_Instruction_Type::LOAD_CONSTANT_F32:
        {
            c_generator_generate_data_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = %f;\n", instr->constant_f32_value);
            break;
        }
        case Intermediate_Instruction_Type::LOAD_CONSTANT_I32:
        {
            c_generator_generate_data_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = %d;\n", instr->constant_i32_value);
            break;
        }
        case Intermediate_Instruction_Type::RETURN:
        {
            if (!instr->return_has_value) {
                string_append_formated(&generator->output_string, "return;\n");
                break;
            }
            string_append_formated(&generator->output_string, "return ");
            c_generator_generate_data_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ";\n");
            break;
        }
        case Intermediate_Instruction_Type::WHILE_BLOCK:
        {
            string_append_formated(&generator->output_string, "while (true) {\n");
            c_generator_generate_function_instruction_slice(generator, indentation_level + 1, true,
                instr->condition_calculation_instruction_start,
                instr->condition_calculation_instruction_end_exclusive
            );
            for (int i = 0; i < indentation_level + 1; i++) {
                string_append_formated(&generator->output_string, "    ");
            }
            string_append_formated(&generator->output_string, "if (!(");
            c_generator_generate_data_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ")) break;\n");

            c_generator_generate_function_instruction_slice(generator, indentation_level + 1, true,
                instr->true_branch_instruction_start,
                instr->true_branch_instruction_end_exclusive
            );
            for (int i = 0; i < indentation_level; i++) {
                string_append_formated(&generator->output_string, "    ");
            }
            string_append_formated(&generator->output_string, "}\n");
            instruction_index = instr->true_branch_instruction_end_exclusive - 1;
            break;
        }
        case Intermediate_Instruction_Type::CALCULATE_ARRAY_ACCESS_POINTER:
        {
            // Either we have a base_pointer as source 1, or we have an unsized array I think
            /*
            c_generator_generate_data_access(generator, instr->destination);
            Type_Signature* result_type = function->registers[instr->destination.register_index].type_signature;
            string_append_formated(&generator->output_string, " = (");
            c_generator_generate_type_definition(generator, result_type, false);
            string_append_formated(&generator->output_string, ")");

            Intermediate_Register* base_reg = &function->registers[instr->source1.register_index];
            if (base_reg->type_signature->type == Signature_Type::ARRAY_SIZED) 
            {
                if (instr->source1.type == Data_Access_Type::MEMORY_ACCESS) {

                }
                else {

                }
            }
            */
            string_append_formated(&generator->output_string, "((u8*)(");
            c_generator_generate_data_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ") + ");
            c_generator_generate_data_access(generator, instr->source2);
            string_append_formated(&generator->output_string, " * %d);\n", instr->constant_i32_value);

            /*
            c_generator_generate_data_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = &((");
            c_generator_generate_data_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ")[");
            c_generator_generate_data_access(generator, instr->source2);
            string_append_formated(&generator->output_string, "]);\n");
            */
            break;
        }
        case Intermediate_Instruction_Type::CALCULATE_MEMBER_ACCESS_POINTER:
        {
            c_generator_generate_data_access(generator, instr->destination);
            /*
            Type_Signature* result_type = function->registers[instr->destination.register_index].type_signature;
            string_append_formated(&generator->output_string, " = (");
            c_generator_generate_type_definition(generator, result_type, false);
            string_append_formated(&generator->output_string, ") ((u8*)(&");
            c_generator_generate_data_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ") + %d);\n", instr->constant_i32_value);
            */
            break;

            /* TODO: It would be nicer if I would use member access of C instead of pointer manipulation
            Type_Signature* struct_signature = function->registers[instr->source1.register_index].type_signature;
            if (struct_signature->type == Signature_Type::POINTER) struct_signature = struct_signature->child_type;
            Struct_Member* found = nullptr;
            for (int i = 0; i < struct_signature->member_types.size; i++) {
                Struct_Member* member = &struct_signature->member_types[i];
                if (member->offset == instr->constant_i32_value) {
                    found = member;
                }
            }
            if (found == nullptr) { break; } // TODO: Handle member access of arrays (.data and .size)

            c_generator_generate_register_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = &((");
            c_generator_generate_register_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ").%s);\n", c_generator_id_to_string(generator, found->name_handle));
            break;
            */
        }
        case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F32:
        case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32: is_binary_op = true; operation_str = "+"; break;
        case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_F32:
        case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_I32:  is_binary_op = true; operation_str = "-"; break;
        case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_F32:
        case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_I32:  is_binary_op = true; operation_str = "*"; break;
        case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_F32:
        case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_I32:  is_binary_op = true; operation_str = "/"; break;
        case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_I32:  is_binary_op = true; operation_str = "%"; break;

        case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_F32:
        case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_BOOL:
        case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_I32:  is_binary_op = true; operation_str = "=="; break;
        case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_BOOL:
        case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_F32:
        case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_I32:  is_binary_op = true; operation_str = "!="; break;
        case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_F32:
        case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_I32:  is_binary_op = true; operation_str = ">"; break;
        case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_F32:
        case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_I32:  is_binary_op = true; operation_str = ">="; break;
        case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_F32:
        case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_I32:  is_binary_op = true; operation_str = "<"; break;
        case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_F32:
        case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_I32:  is_binary_op = true; operation_str = "<="; break;

        case Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_AND:  is_binary_op = true; operation_str = "&&"; break;
        case Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_OR:  is_binary_op = true; operation_str = "||"; break;

        case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32:
        case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32:   is_unary_op = true; operation_str = "-"; break;
        case Intermediate_Instruction_Type::UNARY_OP_BOOLEAN_NOT:   is_unary_op = true; operation_str = "!"; break;
        default: {
            string_append_formated(&generator->output_string, "Not implemented yet!;\n");
        }
        }

        if (is_binary_op) {
            c_generator_generate_data_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = (");
            c_generator_generate_data_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ") %s (", operation_str);
            c_generator_generate_data_access(generator, instr->source2);
            string_append_formated(&generator->output_string, ");\n");
        }
        else if (is_unary_op) {
            c_generator_generate_data_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = %s(", operation_str);
            c_generator_generate_data_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ");\n", operation_str);
        }
    }
}

void c_generator_generate(C_Generator* generator, Intermediate_Generator* im_generator)
{
    generator->im_generator = im_generator;
    string_reset(&generator->output_string);

    string_append_formated(&generator->output_string, "#pragma once\n#include <cstdlib>\n#include \"compiler/hardcoded_functions.h\"\n#include \"compiler/datatypes.h\"\n\n");
    string_append_formated(&generator->output_string, "struct Unsized_Array {void* data; i32 size;};\n\n");

    // Create forward declaration for structs
    Symbol_Table* root_table = im_generator->analyser->symbol_tables[0];
    for (int i = 0; i < root_table->symbols.size; i++)
    {
        Symbol* symbol = &root_table->symbols[i];
        if (symbol->symbol_type != Symbol_Type::TYPE) continue;
        Type_Signature* signature = symbol->type;
        if (signature->type != Signature_Type::STRUCT) continue;
        string_append_formated(&generator->output_string, "struct %s;\n", c_generator_id_to_string(generator, symbol->name_handle));
    }

    // Create struct definitions
    for (int i = 0; i < root_table->symbols.size; i++)
    {
        Symbol* symbol = &root_table->symbols[i];
        if (symbol->symbol_type != Symbol_Type::TYPE) continue;
        Type_Signature* signature = symbol->type;
        if (signature->type != Signature_Type::STRUCT) continue;

        string_append_formated(&generator->output_string, "struct %s\n{\n", c_generator_id_to_string(generator, symbol->name_handle));
        for (int j = 0; j < signature->member_types.size; j++) {
            Struct_Member* member = &signature->member_types[j];
            string_append_formated(&generator->output_string, "    ");
            c_generator_generate_variable_definition_with_name_handle(generator, member->name_handle, member->type, true);
            string_append_formated(&generator->output_string, "\n");
        }
        string_append_formated(&generator->output_string, "};\n");
    }

    // Generate function headers
    for (int i = 0; i < im_generator->functions.size; i++) {
        generator->current_function_index = i;
        c_generator_generate_function_header(generator, i);
        string_append_formated(&generator->output_string, ";\n");
    }
    // Generate Function Code
    for (int i = 0; i < im_generator->functions.size; i++)
    {
        generator->current_function_index = i;
        c_generator_generate_function_header(generator, i);
        string_append_formated(&generator->output_string, "\n{\n");
        /*
        for (int j = 0; j < im_generator->functions[i].registers.size; j++)
        {
            Intermediate_Register* reg = &im_generator->functions[i].registers[j];
            if (reg->type == Intermediate_Register_Type::VARIABLE || reg->type == Intermediate_Register_Type::EXPRESSION_RESULT) {
                string_append_formated(&generator->output_string, "    ");
                c_generator_generate_variable_definition_with_register_index(generator, j, true);
                string_append_formated(&generator->output_string, "\n");
            }
        }
        c_generator_generate_function_instruction_slice(generator, 1, true, 0, im_generator->functions[i].instructions.size);
        string_append_formated(&generator->output_string, "\n}\n");
        */
    }

    // Create real main function
    string_append_formated(&generator->output_string, "\n int main(int argc, const char** argv) {\n    random_initialize();\n    _upp_main();\n    return 0;\n}");

    // Compile
    file_io_write_file("backend/main.cpp", array_create_static((byte*)generator->output_string.characters, generator->output_string.size));

    //const char* cmd_setup_compiler_vars = "\"P:\\Programme\\Visual Studio Community 2019\\VC\\Auxiliary\\Build\\vcvars64.bat\"";
    //system(cmd_setup_compiler_vars);
    //const char* cmd_compile = "cl /MTd /Fi /Zi /Wall /RTCsu /JMC backend\\main.exe backend\\main.cpp backed\\compiler\\hardcoded_functions.cpp";
    //const char* cmd_compile_simple = "cl.exe";
    //system(cmd_compile_simple);
    /*
    PROCESS_INFORMATION proc_info;
    STARTUPINFOA start_info = {};
    start_info.cb = sizeof(start_info);

    char buffer[1024];
    char* file_part;
    int ret_val = SearchPathA(NULL, "cmd", ".exe", 1024, buffer, &file_part);
    if (ret_val == 0) {
        logg("Could not find cmd.exe location");
        return;
    }

    int return_val = CreateProcessA(
        //"P:\\Programme\\Visual Studio Community 2019\\VC\\Tools\\MSVC\\14.27.29110\\bin\\Hostx64\\x64\\cl.exe",
        //"backend\\main.exe backend\\main.cpp backend\\compiler\\hardcoded_functions.cpp", 
        buffer,
        "/c compile_and_run.bat",
        NULL, NULL, FALSE,
        NULL,
        NULL, NULL,
        &start_info,
        &proc_info
    );
    if (return_val == 0) {
        helper_print_last_error();
        logg("Error!");
    }
    else
    {
        WaitForSingleObject(proc_info.hProcess, INFINITE);
        DWORD exit_code = 0;
        if (FALSE == GetExitCodeProcess(proc_info.hProcess, &exit_code)) {
            logg("Could not get exit code?\n");
        }
        CloseHandle(proc_info.hProcess);
        CloseHandle(proc_info.hThread);

        if (exit_code == 0) {
            system("backend\\main.exe");
        }
        else {
            system("There were build ERRORS\n");
        }
        logg("\n");
    }
    */
}
