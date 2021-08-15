#include "c_backend.hpp"

#include "compiler.hpp"
#include "../../utility/file_io.hpp"
#include <cstdlib>
#include <Windows.h>
#include "../../win32/windows_helper_functions.hpp"
#include "../../utility/hash_functions.hpp"
#include "semantic_analyser.hpp"

C_Generator c_generator_create()
{
    C_Generator result;
    result.section_struct_prototypes = string_create_empty(4096);
    result.section_struct_implementations = string_create_empty(4096);
    result.section_function_prototypes = string_create_empty(4096);
    result.section_function_implementations = string_create_empty(4096);
    result.section_type_declarations = string_create_empty(4096);
    result.section_string_data = string_create_empty(4096);
    result.section_globals = string_create_empty(4096);
    result.array_index_stack = dynamic_array_create_empty<int>(16);
    result.translation_type_to_name = hashtable_create_pointer_empty<Type_Signature*, String>(128);
    result.translation_function_to_name = hashtable_create_pointer_empty<IR_Function*, String>(128);
    result.translation_code_block_to_name = hashtable_create_pointer_empty<IR_Code_Block*, String>(128);
    result.translation_string_data_to_name = hashtable_create_empty<int, String>(128, hash_i32, equals_i32);
    result.name_counter = 0;
    return result;
}

void delete_code_block_names(IR_Code_Block** signature, String* value)
{
    string_destroy(value);
}

void delete_type_names(Type_Signature** signature, String* value)
{
    string_destroy(value);
}

void delete_function_names(IR_Function** function, String* value)
{
    string_destroy(value);
}

void delete_string_data(int* integer, String* value)
{
    string_destroy(value);
}

void string_add_indentation(String* str, int indentation)
{
    for (int i = 0; i < indentation; i++) {
        string_append_formated(str, "    ");
    }
}

void c_generator_destroy(C_Generator* generator)
{
    string_destroy(&generator->section_string_data);
    string_destroy(&generator->section_function_implementations);
    string_destroy(&generator->section_function_prototypes);
    string_destroy(&generator->section_type_declarations);
    string_destroy(&generator->section_struct_prototypes);
    string_destroy(&generator->section_struct_implementations);
    string_destroy(&generator->section_globals);
    dynamic_array_destroy(&generator->array_index_stack);
    hashtable_for_each(&generator->translation_type_to_name, delete_type_names);
    hashtable_destroy(&generator->translation_type_to_name);
    hashtable_for_each(&generator->translation_function_to_name, delete_function_names);
    hashtable_destroy(&generator->translation_function_to_name);
    hashtable_for_each(&generator->translation_code_block_to_name, delete_code_block_names);
    hashtable_destroy(&generator->translation_code_block_to_name);
    hashtable_for_each(&generator->translation_string_data_to_name, delete_string_data);
    hashtable_destroy(&generator->translation_string_data_to_name);
}

const char* c_generator_id_to_string(C_Generator* generator, int name_handle) {
    return lexer_identifer_to_string(&generator->compiler->lexer, name_handle).characters;
}

void c_generator_output_type_reference(C_Generator* generator, String* output, Type_Signature* type)
{
    {
        String* str = hashtable_find_element(&generator->translation_type_to_name, type);
        if (str != 0) {
            string_append_formated(output, str->characters);
            return;
        }
    }

    String tmp = string_create_empty(1024);
    SCOPE_EXIT(string_destroy(&tmp));
    String type_name = string_create_empty(16);
    switch (type->type)
    {
    case Signature_Type::ARRAY_SIZED: {
        string_append_formated(&type_name, "Array_Sized_%d", generator->name_counter);
        generator->name_counter++;
        string_append_formated(&generator->section_struct_prototypes, "struct %s;\n", type_name.characters);

        string_append_formated(&tmp, "struct %s {\n    ", type_name.characters);
        c_generator_output_type_reference(generator, &tmp, type->child_type);
        string_append_formated(&tmp, " data[%d];\n};\n\n", type->array_element_count);
        string_append_formated(&generator->section_struct_implementations, tmp.characters);
        break;
    }
    case Signature_Type::ARRAY_UNSIZED: {
        string_append_formated(&type_name, "Array_Unsized_%d", generator->name_counter);
        generator->name_counter++;
        string_append_formated(&generator->section_struct_prototypes, "struct %s;\n", type_name.characters);
        string_append_formated(&tmp, "struct %s {\n    ", type_name.characters);
        c_generator_output_type_reference(generator, &tmp, type->child_type);
        string_append_formated(&tmp, "* data;\n    i32 size; i32 padding;\n};\n\n");
        string_append_formated(&generator->section_struct_implementations, tmp.characters);
        break;
    }
    case Signature_Type::ERROR_TYPE:
        panic("Should not happen in c_backend!");
        break;
    case Signature_Type::FUNCTION: {
        // This means we need a function prototype?
        string_append_formated(&type_name, "function_ptr_type_%d", generator->name_counter);
        generator->name_counter++;
        string_append_formated(&tmp, "typedef ");
        c_generator_output_type_reference(generator, &tmp, type->return_type);
        string_append_formated(&tmp, " (*%s)(", type_name.characters);
        for (int i = 0; i < type->parameter_types.size; i++) {
            c_generator_output_type_reference(generator, &tmp, type->parameter_types[i]);
            if (i != type->parameter_types.size - 1) {
                string_append_formated(&tmp, ", ");
            }
        }
        string_append_formated(&tmp, ");\n\n");
        string_append_formated(&generator->section_type_declarations, tmp.characters);
        break;
    }
    case Signature_Type::POINTER: {
        c_generator_output_type_reference(generator, &type_name, type->child_type);
        string_append_formated(&type_name, "*");
        break;
    }
    case Signature_Type::PRIMITIVE: {
        switch (type->primitive_type)
        {
        case Primitive_Type::BOOLEAN:
            string_append_formated(&type_name, "bool");
            break;
        case Primitive_Type::FLOAT_32:
            string_append_formated(&type_name, "f32");
            break;
        case Primitive_Type::FLOAT_64:
            string_append_formated(&type_name, "f64");
            break;
        case Primitive_Type::SIGNED_INT_8:
            string_append_formated(&type_name, "i8");
            break;
        case Primitive_Type::SIGNED_INT_16:
            string_append_formated(&type_name, "i16");
            break;
        case Primitive_Type::SIGNED_INT_32:
            string_append_formated(&type_name, "i32");
            break;
        case Primitive_Type::SIGNED_INT_64:
            string_append_formated(&type_name, "i64");
            break;
        case Primitive_Type::UNSIGNED_INT_8:
            string_append_formated(&type_name, "u8");
            break;
        case Primitive_Type::UNSIGNED_INT_16:
            string_append_formated(&type_name, "u16");
            break;
        case Primitive_Type::UNSIGNED_INT_32:
            string_append_formated(&type_name, "u32");
            break;
        case Primitive_Type::UNSIGNED_INT_64:
            string_append_formated(&type_name, "u64");
            break;
        default: panic("What");
        }
        break;
    }
    case Signature_Type::STRUCT: {
        string_append_formated(&type_name, "struct_%d", generator->name_counter);
        generator->name_counter++;
        string_append_formated(&generator->section_struct_prototypes, "struct %s;\n", type_name.characters);
        string_append_formated(&tmp, "struct %s {\n", type_name.characters);
        for (int i = 0; i < type->member_types.size; i++) {
            Struct_Member* member = &type->member_types[i];
            string_add_indentation(&tmp, 1);
            c_generator_output_type_reference(generator, &tmp, member->type);
            string_append_formated(&tmp, " %s;\n", lexer_identifer_to_string(&generator->compiler->lexer, member->name_handle).characters);
        }
        string_append_formated(&tmp, "};\n\n");
        string_append_formated(&generator->section_struct_implementations, tmp.characters);
        break;
    }
    case Signature_Type::VOID_TYPE:
        string_append_formated(&type_name, "void");
        break;
    case Signature_Type::TEMPLATE_TYPE:
        //string_append_formated(&type_name, "%s", lexer_identifer_to_string(&generator->compiler->lexer, type->template_name).characters);
        string_append_formated(&type_name, "TEMPLATE_TYPE");
        break;
    default: panic("Hey");
    }

    hashtable_insert_element(&generator->translation_type_to_name, type, type_name);
    string_append_formated(output, type_name.characters);
}

void c_generator_output_data_access(C_Generator* generator, String* output, IR_Data_Access access) 
{
    if (access.is_memory_access) {
        string_append_formated(output, "(*(");
    }

    switch (access.type)
    {
    case IR_Data_Access_Type::REGISTER: 
    {
        {
            String* name = hashtable_find_element(&generator->translation_code_block_to_name, access.option.definition_block);
            if (name != 0) {
                string_append_formated(output, "%s_%d", name->characters, access.index);
                break;
            }
        }
        String new_name = string_create_empty(16);
        string_append_formated(&new_name, "code_block_%d", generator->name_counter);
        generator->name_counter++;
        hashtable_insert_element(&generator->translation_code_block_to_name, access.option.definition_block, new_name);
        string_append_formated(output, "%s_%d", new_name.characters, access.index);
        break;
    }
    case IR_Data_Access_Type::PARAMETER:
    {
        string_append_formated(output, "param_%d", access.index);
        break;
    }
    case IR_Data_Access_Type::GLOBAL_DATA:
    {
        string_append_formated(output, "global_%d", access.index);
        break;
    }
    case IR_Data_Access_Type::CONSTANT:
    {
        IR_Constant* constant = &generator->program->constant_pool.constants[access.index];
        Type_Signature* signature = constant->type;
        void* raw_data = &generator->program->constant_pool.constant_memory[constant->offset];
        if (signature->type == Signature_Type::PRIMITIVE) 
        {
            switch (signature->primitive_type)
            {
            case Primitive_Type::BOOLEAN: {
                if (*(bool*)raw_data) {
                    string_append_formated(output, "true");
                }
                else {
                    string_append_formated(output, "false");
                }
                break;
            }
            case Primitive_Type::FLOAT_32: {
                string_append_formated(output, "%f", *(f32*)raw_data);
                break;
            }
            case Primitive_Type::FLOAT_64:{
                string_append_formated(output, "%f", *(f64*)raw_data);
                break;
            }
            case Primitive_Type::SIGNED_INT_8: {
                string_append_formated(output, "%d", *(i8*)raw_data);
                break;
            }
            case Primitive_Type::SIGNED_INT_16: {
                string_append_formated(output, "%d", *(i16*)raw_data);
                break;
            }
            case Primitive_Type::SIGNED_INT_32: {
                string_append_formated(output, "%d", *(i32*)raw_data);
                break;
            }
            case Primitive_Type::SIGNED_INT_64: {
                string_append_formated(output, "%d", *(i64*)raw_data);
                break;
            }
            case Primitive_Type::UNSIGNED_INT_8: {
                string_append_formated(output, "%d", *(u8*)raw_data);
                break;
            }
            case Primitive_Type::UNSIGNED_INT_16: {
                string_append_formated(output, "%d", *(u16*)raw_data);
                break;
            }
            case Primitive_Type::UNSIGNED_INT_32: {
                string_append_formated(output, "%d", *(u32*)raw_data);
                break;
            }
            case Primitive_Type::UNSIGNED_INT_64: {
                string_append_formated(output, "%d", *(u64*)raw_data);
                break;
            }
            default: panic("HEy");
            }
        }
        else if (signature == generator->compiler->type_system.string_type) {
            String* string_access_str = hashtable_find_element(&generator->translation_string_data_to_name, access.index);
            assert(string_access_str != 0, "Should not happen");
            string_append_formated(output, string_access_str->characters);
        }
        else if (signature->type == Signature_Type::POINTER && signature->child_type->type == Signature_Type::VOID_TYPE) {
            string_append_formated(output, "nullptr");
        }
        else {
            panic("Cannot load constants that are no strings or primitives!");
        }
        break;
    }
    }
    if (access.is_memory_access) {
        string_append_formated(output, "))");
    }

    return;
}

void c_generator_output_code_block(C_Generator* generator, String* output, IR_Code_Block* code_block, int indentation_level, bool registers_in_same_scope)
{
    if (!registers_in_same_scope)
    {
        string_add_indentation(output, indentation_level);
        string_append_formated(output, "{\n");
    }
    // Create Register variables
    for (int i = 0; i < code_block->registers.size; i++) {
        if (registers_in_same_scope) {
            string_add_indentation(output, indentation_level);
        }
        else {
            string_add_indentation(output, indentation_level + 1);
        }
        Type_Signature* sig = code_block->registers[i];
        c_generator_output_type_reference(generator, output, sig);
        string_append_formated(output, " ");
        IR_Data_Access access;
        access.index = i;
        access.is_memory_access = false;
        access.type = IR_Data_Access_Type::REGISTER;
        access.option.definition_block = code_block;
        c_generator_output_data_access(generator, output, access);
        string_append_formated(output, ";\n");
    }
    if (registers_in_same_scope) {
        string_add_indentation(output, indentation_level);
        string_append_formated(output, "{\n");
    }

    // Output code
    for (int i = 0; i < code_block->instructions.size; i++)
    {
        string_add_indentation(output, indentation_level + 1);
        IR_Instruction* instr = &code_block->instructions[i];
        switch (instr->type)
        {
        case IR_Instruction_Type::FUNCTION_CALL:
        {
            IR_Instruction_Call* call = &instr->options.call;
            Type_Signature* function_sig = 0;
            switch (call->call_type) {
            case IR_Instruction_Call_Type::FUNCTION_CALL:
                function_sig = call->options.function->function_type;
                break;
            case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL:
                function_sig = ir_data_access_get_type(&call->options.pointer_access)->child_type;
                break;
            case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
                function_sig = call->options.hardcoded->signature;
                break;
            }
            if (function_sig->return_type != generator->compiler->type_system.void_type) {
                c_generator_output_data_access(generator, output, call->destination);
                string_append_formated(output, " = ");
            }

            switch (call->call_type)
            {
            case IR_Instruction_Call_Type::FUNCTION_CALL: {
                String* fn_name = hashtable_find_element(&generator->translation_function_to_name, call->options.function);
                assert(fn_name != 0, "Hey");
                string_append_formated(output, fn_name->characters);
                break;
            }
            case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL: {
                c_generator_output_data_access(generator, output, call->options.pointer_access);
                break;
            }
            case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL: {
                switch (call->options.hardcoded->type)
                {
                case IR_Hardcoded_Function_Type::PRINT_I32:
                    string_append_formated(output, "print_i32");
                    break;
                case IR_Hardcoded_Function_Type::PRINT_F32:
                    string_append_formated(output, "print_f32");
                    break;
                case IR_Hardcoded_Function_Type::PRINT_BOOL:
                    string_append_formated(output, "print_bool");
                    break;
                case IR_Hardcoded_Function_Type::PRINT_LINE:
                    string_append_formated(output, "print_line");
                    break;
                case IR_Hardcoded_Function_Type::PRINT_STRING:
                    string_append_formated(output, "print_string");
                    break;
                case IR_Hardcoded_Function_Type::READ_I32:
                    string_append_formated(output, "read_i32");
                    break;
                case IR_Hardcoded_Function_Type::READ_F32:
                    string_append_formated(output, "read_f32");
                    break;
                case IR_Hardcoded_Function_Type::READ_BOOL:
                    string_append_formated(output, "read_bool");
                    break;
                case IR_Hardcoded_Function_Type::RANDOM_I32:
                    string_append_formated(output, "random_i32");
                    break;
                case IR_Hardcoded_Function_Type::MALLOC_SIZE_I32:
                    string_append_formated(output, "malloc_size_i32");
                    break;
                case IR_Hardcoded_Function_Type::FREE_POINTER:
                    string_append_formated(output, "free_pointer");
                    break;
                default: panic("What");
                }
                break;
            }
            default: panic("What");
            }
            string_append_formated(output, "(");
            for (int j = 0; j < call->arguments.size; j++) {
                c_generator_output_data_access(generator, output, call->arguments[j]);
                if (j != call->arguments.size - 1) {
                    string_append_formated(output, ", ");
                }
            }
            string_append_formated(output, ");\n");
            break;
        }
        case IR_Instruction_Type::IF:
        {
            IR_Instruction_If* if_instr = &instr->options.if_instr;
            string_append_formated(output, "if (");
            c_generator_output_data_access(generator, output, if_instr->condition);
            string_append_formated(output, ")\n");
            c_generator_output_code_block(generator, output, if_instr->true_branch, indentation_level + 1, false);
            string_add_indentation(output, indentation_level + 1);
            string_append_formated(output, "else\n");
            c_generator_output_code_block(generator, output, if_instr->false_branch, indentation_level + 1, false);
            break;
        }
        case IR_Instruction_Type::WHILE:
        {
            IR_Instruction_While* while_instr = &instr->options.while_instr;
            string_append_formated(output, "while(true){\n");
            c_generator_output_code_block(generator, output, while_instr->condition_code, indentation_level + 2, true);
            string_add_indentation(output, indentation_level + 2);
            string_append_formated(output, "if(!(");
            c_generator_output_data_access(generator, output, while_instr->condition_access);
            string_append_formated(output, ")) break;\n");
            c_generator_output_code_block(generator, output, while_instr->code, indentation_level + 2, false);
            string_add_indentation(output, indentation_level + 1);
            string_append_formated(output, "}\n");
            break;
        }
        case IR_Instruction_Type::BLOCK:
        {
            c_generator_output_code_block(generator, output, instr->options.block, indentation_level + 1, false);
            break;
        }
        case IR_Instruction_Type::BREAK: {
            string_append_formated(output, "break;\n");
            break;
        }
        case IR_Instruction_Type::CONTINUE: {
            string_append_formated(output, "continue;\n");
            break;
        }
        case IR_Instruction_Type::RETURN: {
            IR_Instruction_Return* return_instr = &instr->options.return_instr;
            switch (return_instr->type)
            {
            case IR_Instruction_Return_Type::EXIT: {
                string_append_formated(output, "exit(%d);\n", (i32)return_instr->options.exit_code);
                break;
            }
            case IR_Instruction_Return_Type::RETURN_DATA: {
                string_append_formated(output, "return ");
                c_generator_output_data_access(generator, output, return_instr->options.return_value);
                string_append_formated(output, ";\n");
                break;
            }
            case IR_Instruction_Return_Type::RETURN_EMPTY: {
                string_append_formated(output, "return;\n");
                break;
            }
            default: panic("What");
            }
            break;
        }
        case IR_Instruction_Type::MOVE: {
            c_generator_output_data_access(generator, output, instr->options.move.destination);
            string_append_formated(output, " = ");
            c_generator_output_data_access(generator, output, instr->options.move.source);
            string_append_formated(output, ";\n");
            break;
        }
        case IR_Instruction_Type::CAST: 
        {
            IR_Instruction_Cast* cast = &instr->options.cast;
            switch (cast->type)
            {
            case IR_Instruction_Cast_Type::ARRAY_SIZED_TO_UNSIZED: {
                c_generator_output_data_access(generator, output, cast->destination);
                string_append_formated(output, ".data = &(");
                c_generator_output_data_access(generator, output, cast->source);
                string_append_formated(output, ".data = );\n");
                string_add_indentation(output, indentation_level + 1);
                c_generator_output_data_access(generator, output, cast->destination);
                assert(ir_data_access_get_type(&cast->source)->type == Signature_Type::ARRAY_SIZED, "HEy");
                string_append_formated(output, ".size = %d;\n", ir_data_access_get_type(&cast->source)->size_in_bytes);
                break;
            }
            case IR_Instruction_Cast_Type::PRIMITIVE_TYPES:
            case IR_Instruction_Cast_Type::POINTER_TO_U64:
            case IR_Instruction_Cast_Type::U64_TO_POINTER:
            case IR_Instruction_Cast_Type::POINTERS: {
                c_generator_output_data_access(generator, output, cast->destination);
                string_append_formated(output, " = (");
                c_generator_output_type_reference(generator, output, ir_data_access_get_type(&cast->destination));
                string_append_formated(output, ")");
                c_generator_output_data_access(generator, output, cast->source);
                string_append_formated(output, ";\n");
                break;
            }
            default: panic("Wat");
            }
            break;
        }
        case IR_Instruction_Type::ADDRESS_OF: {
            IR_Instruction_Address_Of* addr_of = &instr->options.address_of;
            c_generator_output_data_access(generator, output, addr_of->destination);
            string_append_formated(output, " = ");
            switch (addr_of->type)
            {
            case IR_Instruction_Address_Of_Type::ARRAY_ELEMENT: {
                string_append_formated(output, "&(");
                c_generator_output_data_access(generator, output, addr_of->source);
                string_append_formated(output, ").data[");
                c_generator_output_data_access(generator, output, addr_of->options.index_access);
                string_append_formated(output, "];\n");
                break;
            }
            case IR_Instruction_Address_Of_Type::DATA: {
                string_append_formated(output, "&(");
                c_generator_output_data_access(generator, output, addr_of->source);
                string_append_formated(output, ");\n");
                break;
            }
            case IR_Instruction_Address_Of_Type::FUNCTION: {
                String* fn_name = hashtable_find_element(&generator->translation_function_to_name, addr_of->options.function);
                assert(fn_name != 0, "HEY");
                string_append_formated(output, "&%s;\n", fn_name->characters);
                break;
            }
            case IR_Instruction_Address_Of_Type::STRUCT_MEMBER: {
                string_append_formated(output, "&(");
                c_generator_output_data_access(generator, output, addr_of->source);
                string_append_formated(output, ").%s;\n", lexer_identifer_to_string(&generator->compiler->lexer, addr_of->options.member.name_handle).characters);
                break;
            }
            default: panic("What");
            }
            break;
        }
        case IR_Instruction_Type::UNARY_OP: {
            IR_Instruction_Unary_OP* unary = &instr->options.unary_op;
            c_generator_output_data_access(generator, output, unary->destination);
            string_append_formated(output, " = ");
            switch (unary->type)
            {
            case IR_Instruction_Unary_OP_Type::NEGATE: {
                string_append_formated(output, "-");
                break;
            }
            case IR_Instruction_Unary_OP_Type::NOT: {
                string_append_formated(output, "!");
                break;
            }
            default: panic("Hey");
            }
            string_append_formated(output, "(");
            c_generator_output_data_access(generator, output, unary->source);
            string_append_formated(output, ");\n");
            break;
        }
        case IR_Instruction_Type::BINARY_OP:
        {
            IR_Instruction_Binary_OP* binary = &instr->options.binary_op;
            c_generator_output_data_access(generator, output, binary->destination);
            string_append_formated(output, " = ");
            c_generator_output_data_access(generator, output, binary->operand_left);
            string_append_formated(output, " ");
            const char* binary_str = "";
            switch (binary->type)
            {
            case IR_Instruction_Binary_OP_Type::ADDITION: binary_str = "+"; break;
            case IR_Instruction_Binary_OP_Type::AND: binary_str = "&&"; break;
            case IR_Instruction_Binary_OP_Type::DIVISION: binary_str = "/"; break;
            case IR_Instruction_Binary_OP_Type::EQUAL: binary_str = "=="; break;
            case IR_Instruction_Binary_OP_Type::GREATER_EQUAL: binary_str = ">="; break;
            case IR_Instruction_Binary_OP_Type::GREATER_THAN: binary_str = ">"; break;
            case IR_Instruction_Binary_OP_Type::LESS_EQUAL: binary_str = "<="; break;
            case IR_Instruction_Binary_OP_Type::LESS_THAN: binary_str = "<"; break;
            case IR_Instruction_Binary_OP_Type::MODULO: binary_str = "%"; break;
            case IR_Instruction_Binary_OP_Type::MULTIPLICATION: binary_str = "*"; break;
            case IR_Instruction_Binary_OP_Type::NOT_EQUAL: binary_str = "!="; break;
            case IR_Instruction_Binary_OP_Type::OR: binary_str = "||"; break;
            case IR_Instruction_Binary_OP_Type::SUBTRACTION: binary_str = "-"; break;
            default: panic("Hey");
            }
            string_append_formated(output, "%s ", binary_str);
            c_generator_output_data_access(generator, output, binary->operand_right);
            string_append_formated(output, ";\n");
            break;
        }
        default: panic("Hey");
        }
    }

    string_add_indentation(output, indentation_level);
    string_append_formated(output, "}\n");
}

void c_generator_generate(C_Generator* generator, Compiler* compiler)
{
    // Reset generator data 
    {
        generator->compiler = compiler;
        generator->program = compiler->analyser.program;
        string_reset(&generator->section_function_implementations);
        string_reset(&generator->section_function_prototypes);
        string_reset(&generator->section_type_declarations);
        string_reset(&generator->section_struct_implementations);
        string_reset(&generator->section_struct_prototypes);
        string_reset(&generator->section_string_data);
        string_reset(&generator->section_globals);
        hashtable_for_each(&generator->translation_type_to_name, delete_type_names);
        hashtable_reset(&generator->translation_type_to_name);
        hashtable_for_each(&generator->translation_function_to_name, delete_function_names);
        hashtable_reset(&generator->translation_function_to_name);
        hashtable_for_each(&generator->translation_code_block_to_name, delete_code_block_names);
        hashtable_reset(&generator->translation_code_block_to_name);
        hashtable_for_each(&generator->translation_string_data_to_name, delete_string_data);
        hashtable_reset(&generator->translation_string_data_to_name);
        generator->name_counter = 0;
    }

    // Create String Data code
    {
        String str = string_create("Unsized_Array_U8");
        Type_Signature* sig = type_system_make_array_unsized(&generator->compiler->type_system, generator->compiler->type_system.u8_type);
        hashtable_insert_element(&generator->translation_type_to_name, sig, str);
        String str_str = string_create("Upp_String");
        hashtable_insert_element(&generator->translation_type_to_name, generator->compiler->type_system.string_type, str_str);
    }
    for (int i = 0; i < generator->program->constant_pool.constants.size; i++)
    {
        IR_Constant* constant = &generator->program->constant_pool.constants[i];
        if (constant->type == generator->compiler->type_system.string_type)
        {
            void* raw_data = &generator->program->constant_pool.constant_memory[constant->offset];
            char* string_data = *(char**)raw_data;
            i32 capacity = *(i32*)((byte*)raw_data + 8);
            i32 size = *(i32*)((byte*)raw_data + 16);
            assert(strlen(string_data) == size, "Hey");

            string_append_formated(&generator->section_string_data, "u8* string_%d = (u8*) R\"Upp(%s)Upp\";\n", generator->name_counter, string_data);
            String str = string_create_empty(size);
            string_append_formated(&str, "upp_create_static_string(string_%d, %d)", generator->name_counter, size);
            hashtable_insert_element(&generator->translation_string_data_to_name, i, str);
            generator->name_counter++;
        }
    }

    // Create globals Definitions
    {
        for (int i = 0; i < generator->program->globals.size; i++) {
            Type_Signature* type = generator->program->globals[i];
            c_generator_output_type_reference(generator, &generator->section_globals, type);
            string_append_formated(&generator->section_globals, " global_%d;\n", i);
        }
    }

    // Create function prototypes
    for (int i = 0; i < generator->program->functions.size; i++)
    {
        IR_Function* function = generator->program->functions[i];
        Type_Signature* function_signature = function->function_type;
        c_generator_output_type_reference(generator, &generator->section_function_prototypes, function_signature->return_type);
        string_append_formated(&generator->section_function_prototypes, " ");
        {
            String function_name = string_create_empty(16);
            string_append_formated(&function_name, "function_%d", generator->name_counter);
            generator->name_counter++;
            hashtable_insert_element(&generator->translation_function_to_name, function, function_name);
            string_append_formated(&generator->section_function_prototypes, function_name.characters);
        }

        string_append_formated(&generator->section_function_prototypes, "(");
        for (int j = 0; j < function->function_type->parameter_types.size; j++) {
            Type_Signature* param_type = function->function_type->parameter_types[j];
            c_generator_output_type_reference(generator, &generator->section_function_prototypes, param_type);
            if (j != function->function_type->parameter_types.size - 1) {
                string_append_formated(&generator->section_function_prototypes, ", ");
            }
        }
        string_append_formated(&generator->section_function_prototypes, ");\n");
    }

    // Create function code
    for (int i = 0; i < generator->program->functions.size; i++)
    {
        IR_Function* function = generator->program->functions[i];
        Type_Signature* function_signature = function->function_type;

        c_generator_output_type_reference(generator, &generator->section_function_implementations, function_signature->return_type);
        string_append_formated(&generator->section_function_implementations, " ");
        // In C functions look like: return_type function_name ( Param_Type param_name, ...) { Body }
        {
            String* function_name = hashtable_find_element(&generator->translation_function_to_name, function);
            assert(function_name != 0, "HEY");
            string_append_formated(&generator->section_function_implementations, function_name->characters);
        }
        string_append_formated(&generator->section_function_implementations, "(");
        for (int j = 0; j < function->function_type->parameter_types.size; j++) {
            Type_Signature* param_type = function->function_type->parameter_types[j];
            c_generator_output_type_reference(generator, &generator->section_function_implementations, param_type);
            string_append_formated(&generator->section_function_implementations, " ");
            IR_Data_Access access;
            access.is_memory_access = false;
            access.type = IR_Data_Access_Type::PARAMETER;
            access.index = j;
            access.option.function = function;
            c_generator_output_data_access(generator, &generator->section_function_implementations, access);
            if (j != function->function_type->parameter_types.size - 1) {
                string_append_formated(&generator->section_function_implementations, ", ");
            }
        }
        string_append_formated(&generator->section_function_implementations, ")\n");
        c_generator_output_code_block(generator, &generator->section_function_implementations, function->code, 0, false);
        string_append_formated(&generator->section_function_implementations, "\n");
    }

    // Add entry function
    {
        String* main_fn_name = hashtable_find_element(&generator->translation_function_to_name, generator->program->entry_function);
        assert(main_fn_name != 0, "HEY");
        string_append_formated(&generator->section_function_implementations, "\nint main(int argc, char** argv) {%s(); return 0;}\n", main_fn_name->characters);
    }

    // Combine sections into one program
    String section_introduction = string_create_static("#pragma once\n#include <cstdlib>\n#include \"compiler/hardcoded_functions.h\"\n#include \"compiler/datatypes.h\"\n\n");
    String source_code = string_create_empty(4096);
    SCOPE_EXIT(string_destroy(&source_code));

    string_append_formated(&source_code, "/* INTRODUCTION\n----------------*/\n");
    string_append_string(&source_code, &section_introduction);
    string_append_formated(&source_code, "\n/* STRING_DATA\n----------------*/\n");
    string_append_string(&source_code, &generator->section_string_data);
    // TODO: The following three (Defining custom data are probably wrong, since there are dependencies that need to be fulfilled)
    string_append_formated(&source_code, "\n/* STRUCT_PROTOTYPES\n----------------*/\n");
    string_append_string(&source_code, &generator->section_struct_prototypes);
    string_append_formated(&source_code, "\n/* STRUCT_IMPLEMENTATIONS\n----------------*/\n");
    string_append_string(&source_code, &generator->section_struct_implementations);
    string_append_formated(&source_code, "\n/* TYPE_DECLARATIONS\n------------------*/\n");
    string_append_string(&source_code, &generator->section_type_declarations); // Function pointers
    // Afterwards its correct again
    string_append_formated(&source_code, "\n/* GLOBALS\n------------------*/\n");
    string_append_string(&source_code, &generator->section_globals);
    string_append_formated(&source_code, "\n/* FUNCTION PROTOTYPES\n------------------*/\n");
    string_append_string(&source_code, &generator->section_function_prototypes);
    string_append_formated(&source_code, "\n/* FUNCTION IMPLEMENTATIONS\n------------------*/\n");
    string_append_string(&source_code, &generator->section_function_implementations);

    // Write to file
    file_io_write_file("backend/main.cpp", array_create_static((byte*)source_code.characters, source_code.size));

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
