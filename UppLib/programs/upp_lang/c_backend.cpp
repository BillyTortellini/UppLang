#include "c_backend.hpp"

#include "../../utility/file_io.hpp"

C_Generator c_generator_create()
{
    C_Generator result;
    result.output_string = string_create_empty(4096);
    return result;
}

void c_generator_destroy(C_Generator* generator)
{
    string_destroy(&generator->output_string);
}

void c_generator_generate_function(C_Generator* generator, int function_index)
{
    Intermediate_Function* function = &generator->im_generator->functions[function_index];
}

const char* c_generator_id_to_string(C_Generator* generator, int name_handle) {
    return lexer_identifer_to_string(generator->im_generator->analyser->parser->lexer, name_handle).characters;
}

void c_generator_generate_type_definition(C_Generator* generator, Type_Signature* signature)
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
        c_generator_generate_type_definition(generator, signature->child_type);
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
    case Signature_Type::ARRAY_SIZED: {
        c_generator_generate_type_definition(generator, signature->child_type);
        string_append_formated(&generator->output_string, "[%d]", signature->array_element_count);
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

void c_generator_generate_variable_definition(C_Generator* generator, int name_handle, Type_Signature* signature, bool semicolon)
{
    c_generator_generate_type_definition(generator, signature);
    string_append_formated(&generator->output_string, " %s", c_generator_id_to_string(generator, name_handle));
    if (semicolon) {
        string_append_formated(&generator->output_string, ";");
    }
}

void c_generator_generate_function_header(C_Generator* generator, int function_index)
{
    AST_Node* function_node = &generator->im_generator->analyser->parser->nodes[generator->im_generator->function_to_ast_node_mapping[function_index]];
    AST_Node* parameter_block_node = &generator->im_generator->analyser->parser->nodes[function_node->children[0]];
    Intermediate_Function* function = &generator->im_generator->functions[function_index];
    Type_Signature* signature = function->function_type;

    c_generator_generate_type_definition(generator, signature->return_type);
    if (function_index == generator->im_generator->main_function_index) {
        string_append_formated(&generator->output_string, " _upp_main(");
    }
    else {
        string_append_formated(&generator->output_string, " %s(", c_generator_id_to_string(generator, function->name_handle));
    }
    for (int i = 0; i < signature->parameter_types.size; i++) 
    {
        Type_Signature* param_type = signature->parameter_types[i];
        int param_name_id = generator->im_generator->analyser->parser->nodes[parameter_block_node->children[i]].name_id;
        c_generator_generate_variable_definition(generator, param_name_id, param_type, false);
        if (i != signature->parameter_types.size - 1) {
            string_append_formated(&generator->output_string, ",");
        }
    }
    string_append_formated(&generator->output_string, ")");
}

void c_generator_generate_register_access(C_Generator* generator, Data_Access access)
{
    Intermediate_Function* function = &generator->im_generator->functions[generator->current_function_index];
    if (access.type == Data_Access_Type::MEMORY_ACCESS) {
        string_append_formated(&generator->output_string, "*");
    }
    Intermediate_Register* reg = &function->registers[access.register_index];
    if (reg->type == Intermediate_Register_Type::VARIABLE || reg->type == Intermediate_Register_Type::PARAMETER) {
        string_append_formated(&generator->output_string, "%s", c_generator_id_to_string(generator, reg->name_id));
    }
    else if (reg->type == Intermediate_Register_Type::EXPRESSION_RESULT) {
        string_append_formated(&generator->output_string, "_upp_int_expr%d", access.register_index);
    }
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
            c_generator_generate_register_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = ");
            c_generator_generate_register_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ";\n");
            break;
        }
        case Intermediate_Instruction_Type::ADDRESS_OF:
        {
            c_generator_generate_register_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = &");
            c_generator_generate_register_access(generator, instr->source1);
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
            if (instr->type == Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION) {
                Hardcoded_Function* hardcoded = &generator->im_generator->analyser->hardcoded_functions[(u32)instr->hardcoded_function_type];
                Type_Signature* function_type = hardcoded->function_type;
                return_type = function_type->return_type;
                function_str = c_generator_id_to_string(generator, hardcoded->name_handle);
            }
            else {
                Type_Signature* function_type = generator->im_generator->functions[instr->intermediate_function_index].function_type;
                return_type = function_type->return_type;
                function_str = c_generator_id_to_string(generator, generator->im_generator->functions[instr->intermediate_function_index].name_handle);
            }

            if (return_type != generator->im_generator->analyser->type_system.void_type) {
                c_generator_generate_register_access(generator, instr->destination);
                string_append_formated(&generator->output_string, " = ");
            }
            string_append_formated(&generator->output_string, "%s(", function_str);
            for (int i = 0; i < instr->arguments.size; i++) {
                c_generator_generate_register_access(generator, instr->arguments[i]);
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
            c_generator_generate_register_access(generator, instr->source1);
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
            c_generator_generate_register_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = %s;\n", instr->constant_bool_value ? "true" : "false");
            break;
        }
        case Intermediate_Instruction_Type::LOAD_CONSTANT_F32:
        {
            c_generator_generate_register_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = %f;\n", instr->constant_f32_value);
            break;
        }
        case Intermediate_Instruction_Type::LOAD_CONSTANT_I32:
        {
            c_generator_generate_register_access(generator, instr->destination);
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
            c_generator_generate_register_access(generator, instr->source1);
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
            string_append_formated(&generator->output_string, "if (");
            c_generator_generate_register_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ") break;\n");

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
            c_generator_generate_register_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = &((");
            c_generator_generate_register_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ")[");
            c_generator_generate_register_access(generator, instr->source2);
            string_append_formated(&generator->output_string, "]);\n");
            break;
        }
        case Intermediate_Instruction_Type::CALCULATE_MEMBER_ACCESS_POINTER:
        {
            c_generator_generate_register_access(generator, instr->destination);
            Type_Signature* result_type = function->registers[instr->destination.register_index].type_signature;
            string_append_formated(&generator->output_string, " = (");
            c_generator_generate_type_definition(generator, result_type);
            string_append_formated(&generator->output_string, " = ) ((u8*)(&");
            c_generator_generate_register_access(generator, instr->source1);
            string_append_formated(&generator->output_string, " + %d));\n", instr->constant_i32_value);
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
            c_generator_generate_register_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = (");
            c_generator_generate_register_access(generator, instr->source1);
            string_append_formated(&generator->output_string, ") %s (", operation_str);
            c_generator_generate_register_access(generator, instr->source2);
            string_append_formated(&generator->output_string, ");\n");
        }
        else if (is_unary_op) {
            c_generator_generate_register_access(generator, instr->destination);
            string_append_formated(&generator->output_string, " = %s(", operation_str);
            c_generator_generate_register_access(generator, instr->source1);
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
            c_generator_generate_variable_definition(generator, member->name_handle, member->type, true);
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
        for (int j = 0; j < im_generator->functions[i].registers.size; j++)
        {
            Intermediate_Register* reg = &im_generator->functions[i].registers[j];
            if (reg->type == Intermediate_Register_Type::VARIABLE || reg->type == Intermediate_Register_Type::EXPRESSION_RESULT) {
                string_append_formated(&generator->output_string, "    ");
                c_generator_generate_type_definition(generator, reg->type_signature);
                string_append_formated(&generator->output_string, " ");
                Data_Access access;
                access.type = Data_Access_Type::REGISTER_ACCESS;
                access.register_index = j;
                c_generator_generate_register_access(generator, access);
                string_append_formated(&generator->output_string, ";\n");
            }
        }
        c_generator_generate_function_instruction_slice(generator, 1, true, 0, im_generator->functions[i].instructions.size);
        string_append_formated(&generator->output_string, "\n}\n");
    }

    // Create real main function
    string_append_formated(&generator->output_string, "\n int main(int argc, const char** argv) {\n    _upp_main();\n    return 0;\n}");

    file_io_write_file("backend/main.cpp", array_create_static((byte*)generator->output_string.characters, generator->output_string.size));
}
