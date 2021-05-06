#include "intermediate_code.hpp"

int intermediate_generator_find_variable_register_by_name(Intermediate_Generator* generator, int name_id)
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];
    for (int i = generator->variable_mappings.size - 1; i >= 0; i--) {
        if (generator->variable_mappings[i].name_handle == name_id) {
            return generator->variable_mappings[i].register_index;
        }
    }
    panic("Cannot happen after semantic analysis");
    return -1;
}

Data_Access intermediate_generator_create_intermediate_register(Intermediate_Generator* generator, Type_Signature* type_signature)
{
    if (type_signature == generator->analyser->type_system.void_type) panic("Should not happen");
    Intermediate_Function* function = &generator->functions[generator->current_function_index];

    Intermediate_Register reg;
    reg.type_signature = type_signature;
    reg.type = Intermediate_Register_Type::EXPRESSION_RESULT;
    dynamic_array_push_back(&function->registers, reg);

    Data_Access result;
    result.type = Data_Access_Type::REGISTER_ACCESS;
    result.register_index = function->registers.size - 1;

    return result;
}

void intermediate_generator_create_parameter_register(Intermediate_Generator* generator, int name_id, Type_Signature* type_signature, int parameter_index)
{
    if (type_signature == generator->analyser->type_system.void_type) panic("Should not happen");
    Intermediate_Function* function = &generator->functions[generator->current_function_index];

    Intermediate_Register reg;
    reg.parameter_index = parameter_index;
    reg.type_signature = type_signature;
    reg.type = Intermediate_Register_Type::PARAMETER;
    dynamic_array_push_back(&function->registers, reg);

    Variable_Mapping m;
    m.name_handle = name_id;
    m.register_index = function->registers.size - 1;
    dynamic_array_push_back(&generator->variable_mappings, m);
}

void intermediate_generator_create_variable_register(Intermediate_Generator* generator, int name_id, Type_Signature* type_signature)
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];

    Intermediate_Register reg;
    reg.type_signature = type_signature;
    reg.type = Intermediate_Register_Type::VARIABLE;
    reg.name_id = name_id;
    dynamic_array_push_back(&function->registers, reg);

    Variable_Mapping m;
    m.name_handle = name_id;
    m.register_index = function->registers.size - 1;
    dynamic_array_push_back(&generator->variable_mappings, m);
}

int intermediate_generator_find_function_by_name(Intermediate_Generator* generator, int name_id)
{
    for (int i = 0; i < generator->function_to_ast_node_mapping.size; i++) {
        int name_handle = generator->analyser->parser->nodes[generator->function_to_ast_node_mapping[i]].name_id;
        if (name_handle == name_id) {
            return i;
        }
    }
    panic("Should not happen!");
    return 0;
}

Type_Signature* intermediate_instruction_binary_operation_get_result_type(Intermediate_Instruction_Type instr_type, Intermediate_Generator* generator)
{
    switch (instr_type)
    {
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32: 
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_I32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_I32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_I32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_I32:
    case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32:
        return generator->analyser->type_system.i32_type;
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F32: 
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_F32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_F32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_F32:
    case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32:
        return generator->analyser->type_system.f32_type;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_I32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_I32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_I32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_I32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_I32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_I32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_F32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_F32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_F32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_F32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_F32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_F32:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_BOOL:
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_BOOL:
    case Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_AND:
    case Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_OR:
    case Intermediate_Instruction_Type::UNARY_OP_BOOLEAN_NOT:
        return generator->analyser->type_system.bool_type;
    }
    panic("Sheit\n");
    return 0;
}

Intermediate_Instruction_Type binary_operation_get_instruction_type(Intermediate_Generator* generator, AST_Node_Type::ENUM op_type, Type_Signature* operand_types)
{
    switch (op_type)
    {
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION:
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F32;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION:
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_I32;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_F32;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION:
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_I32;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_F32;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION:
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_I32;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_F32;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO:
        return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_I32;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND:
        return Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_AND;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR:
        return Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_OR;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL:
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_I32;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_F32;
        if (operand_types == generator->analyser->type_system.bool_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_BOOL;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL:
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_I32;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_F32;
        if (operand_types == generator->analyser->type_system.bool_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_BOOL;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_I32;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_F32;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL:
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_I32;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_F32;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_I32;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_F32;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL:
        if (operand_types == generator->analyser->type_system.i32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_I32;
        if (operand_types == generator->analyser->type_system.f32_type) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_F32;
        panic("Not valid, should have been caught!");
    }
    panic("This should not happen :)\n");
    return Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_AND;
}

Data_Access data_access_make_empty() {
    Data_Access d;
    d.register_index = 0;
    d.type = Data_Access_Type::MEMORY_ACCESS;
    return d;
}

void intermediate_geneartor_add_instruction_unary(Intermediate_Function* function, Intermediate_Instruction_Type type, Data_Access destination,
    Data_Access source1)
{
    Intermediate_Instruction instr;
    instr.type = type;
    instr.destination = destination;
    instr.source1 = source1;
    dynamic_array_push_back(&function->instructions, instr);
}

// Returns expression result register
Data_Access intermediate_generator_generate_expression(Intermediate_Generator* generator, int expression_index,
    bool force_destination, Data_Access destination)
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];
    AST_Node* expression = &generator->analyser->parser->nodes[expression_index];
    Symbol_Table* table = generator->analyser->symbol_tables[generator->analyser->semantic_information[expression_index].symbol_table_index];

    switch (expression->type)
    {
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL:
    {
        Symbol* function_symbol = symbol_table_find_symbol_of_type(
            generator->analyser->symbol_tables[generator->analyser->semantic_information[expression_index].symbol_table_index],
            expression->name_id, Symbol_Type::FUNCTION);
        if (function_symbol == 0) panic("Should not happen, maybe semantic information isnt complete yet!");

        Intermediate_Instruction instr;
        // Check if its an hardcoded function
        bool is_hardcoded = false;
        for (int i = 0; i < generator->analyser->hardcoded_functions.size; i++) {
            if (expression->name_id == generator->analyser->hardcoded_functions[i].name_handle) {
                instr.type = Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION;
                instr.hardcoded_function_type = generator->analyser->hardcoded_functions[i].type;
                is_hardcoded = true;
            }
        }
        if (!is_hardcoded)
        {
            instr.type = Intermediate_Instruction_Type::CALL_FUNCTION;
            instr.intermediate_function_index = intermediate_generator_find_function_by_name(generator, expression->name_id);
        }
        instr.arguments = dynamic_array_create_empty<Data_Access>(expression->children.size);

        // Generate argument Expressions
        for (int i = 0; i < expression->children.size; i++) {
            Data_Access argument = intermediate_generator_generate_expression(generator, expression->children[i], false, data_access_make_empty());
            dynamic_array_push_back(&instr.arguments, argument);
        }
        // Generate destination
        if (generator->analyser->semantic_information[expression_index].expression_result_type != generator->analyser->type_system.void_type)
        {
            if (force_destination) {
                instr.destination = destination;
            }
            else {
                instr.destination = intermediate_generator_create_intermediate_register(
                    generator,
                    generator->analyser->semantic_information[expression_index].expression_result_type
                );
            }
        }
        else {
            instr.destination = data_access_make_empty();
        }
        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination;
    }
    case AST_Node_Type::EXPRESSION_LITERAL:
    {
        Intermediate_Instruction instr;
        if (force_destination) {
            instr.destination = destination;
        }
        else {
            instr.destination = intermediate_generator_create_intermediate_register(
                generator,
                generator->analyser->semantic_information[expression_index].expression_result_type
            );
        }

        Token& token = generator->analyser->parser->lexer->tokens[generator->analyser->parser->token_mapping[expression_index].start_index];
        if (token.type == Token_Type::FLOAT_LITERAL) {
            instr.type = Intermediate_Instruction_Type::LOAD_CONSTANT_F32;
            instr.constant_f32_value = token.attribute.float_value;
        }
        else if (token.type == Token_Type::INTEGER_LITERAL) {
            instr.type = Intermediate_Instruction_Type::LOAD_CONSTANT_I32;
            instr.constant_i32_value = token.attribute.integer_value;
        }
        else if (token.type == Token_Type::BOOLEAN_LITERAL) {
            instr.type = Intermediate_Instruction_Type::LOAD_CONSTANT_BOOL;
            instr.constant_bool_value = token.attribute.bool_value;
        }
        else panic("what");

        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination;
    }
    case AST_Node_Type::EXPRESSION_VARIABLE_READ:
    {
        Data_Access access;
        access.type = Data_Access_Type::REGISTER_ACCESS;
        access.register_index = intermediate_generator_find_variable_register_by_name(generator, expression->name_id);

        if (force_destination) {
            Intermediate_Instruction instr;
            instr.type = Intermediate_Instruction_Type::MOVE_DATA;
            instr.destination = destination;
            instr.source1 = access;
            dynamic_array_push_back(&function->instructions, instr);
            return destination;
        }
        return access;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF:
    {
        Data_Access access = intermediate_generator_generate_expression(generator, expression->children[0], false, data_access_make_empty());
        if (access.type == Data_Access_Type::MEMORY_ACCESS)
        {
            access.type = Data_Access_Type::REGISTER_ACCESS;
            if (force_destination) {
                Intermediate_Instruction instr;
                instr.type = Intermediate_Instruction_Type::MOVE_DATA;
                instr.destination = destination;
                instr.source1 = access;
                dynamic_array_push_back(&function->instructions, instr);
                return destination;
            }
            return access;
        }

        Intermediate_Instruction instr;
        instr.type = Intermediate_Instruction_Type::ADDRESS_OF;
        instr.source1 = access;
        if (force_destination) {
            instr.destination = destination;
        }
        else {
            instr.destination = intermediate_generator_create_intermediate_register(
                generator,
                generator->analyser->semantic_information[expression_index].expression_result_type
            );
        }
        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE:
    {
        Data_Access pointer_access = intermediate_generator_generate_expression(generator, expression->children[0], false, data_access_make_empty());
        Data_Access result_access;
        if (pointer_access.type == Data_Access_Type::REGISTER_ACCESS)
        {
            result_access = pointer_access;
            result_access.type = Data_Access_Type::MEMORY_ACCESS;
        }
        else if (pointer_access.type == Data_Access_Type::MEMORY_ACCESS)
        {
            // This is the case for multiple dereferences
            result_access = intermediate_generator_create_intermediate_register(generator,
                generator->analyser->semantic_information[expression->children[0]].expression_result_type);
            Intermediate_Instruction instr;
            instr.type = Intermediate_Instruction_Type::MOVE_DATA;
            instr.destination = result_access;
            instr.source1 = pointer_access;
            dynamic_array_push_back(&function->instructions, instr);

            result_access.type = Data_Access_Type::MEMORY_ACCESS;
        }
        else {
            result_access = data_access_make_empty();
            panic("Should not happen!");
        }

        if (force_destination) {
            Intermediate_Instruction instr;
            instr.type = Intermediate_Instruction_Type::MOVE_DATA;
            instr.destination = destination;
            instr.source1 = result_access;
            dynamic_array_push_back(&function->instructions, instr);
            return instr.destination;
        }
        else {
            return result_access;
        }
    }
    case AST_Node_Type::EXPRESSION_MEMBER_ACCESS:
    {
        Data_Access structure_data = intermediate_generator_generate_expression(generator, expression->children[0], false, data_access_make_empty());
        Type_Signature* accessor_signature = function->registers[structure_data.register_index].type_signature;

        int member_offset;
        if (expression->name_id == generator->analyser->size_token_index) {
            member_offset = 8;
        }
        else if (expression->name_id == generator->analyser->data_token_index) {
            member_offset = 0;
        }
        else panic("Shit");

        if (accessor_signature->type == Signature_Type::ARRAY_SIZED)
        {
            if (expression->name_id == generator->analyser->data_token_index)
            {
                Intermediate_Instruction instr;
                instr.type = Intermediate_Instruction_Type::ADDRESS_OF;
                instr.source1 = structure_data;
                if (force_destination) {
                    instr.destination = destination;
                }
                else {
                    instr.destination = intermediate_generator_create_intermediate_register(generator, generator->analyser->type_system.i32_type);
                }
                dynamic_array_push_back(&function->instructions, instr);
                return instr.destination;
            }
            else if (expression->name_id == generator->analyser->size_token_index)
            {
                Intermediate_Instruction instr;
                instr.type = Intermediate_Instruction_Type::LOAD_CONSTANT_I32;
                if (force_destination) {
                    instr.destination = destination;
                }
                else {
                    instr.destination = intermediate_generator_create_intermediate_register(generator, generator->analyser->type_system.i32_type);
                }
                instr.constant_i32_value = accessor_signature->array_element_count;
                dynamic_array_push_back(&function->instructions, instr);
                return instr.destination;
            }
            else panic("Should not happen, really!");
        }

        Intermediate_Instruction instr;
        instr.type = Intermediate_Instruction_Type::CALCULATE_MEMBER_ACCESS_POINTER;
        instr.constant_i32_value = member_offset;
        instr.destination = intermediate_generator_create_intermediate_register(
            generator,
            type_system_make_pointer(
                &generator->analyser->type_system,
                generator->analyser->semantic_information[expression_index].expression_result_type
            )
        );
        instr.source1 = structure_data;
        dynamic_array_push_back(&function->instructions, instr);
        instr.destination.type = Data_Access_Type::MEMORY_ACCESS;

        if (force_destination) {
            Intermediate_Instruction move_instr;
            move_instr.type = Intermediate_Instruction_Type::MOVE_DATA;
            move_instr.source1 = instr.destination;
            move_instr.destination = destination;
            dynamic_array_push_back(&function->instructions, move_instr);
            return move_instr.destination;
        }
        else {
            return instr.destination;
        }
    }
    case AST_Node_Type::EXPRESSION_ARRAY_ACCESS:
    {
        Type_Signature* array_type_signature = generator->analyser->semantic_information[expression->children[0]].expression_result_type;
        Type_Signature* element_type_signature = array_type_signature->child_type;
        Type_Signature* element_pointer_type = type_system_make_pointer(&generator->analyser->type_system, array_type_signature->child_type);

        Data_Access array_data = intermediate_generator_generate_expression(generator, expression->children[0], false, data_access_make_empty());
        Data_Access index_data = intermediate_generator_generate_expression(generator, expression->children[1], false, data_access_make_empty());

        Data_Access result_access;
        if (array_type_signature->type == Signature_Type::ARRAY_UNSIZED)
        {
            // Load pointer if array_data is a memory access, otherwise set it to memory access
            // This is the case for multiple dereferences
            if (array_data.type == Data_Access_Type::REGISTER_ACCESS)
            {
                result_access = array_data;
                result_access.type = Data_Access_Type::MEMORY_ACCESS;
            }
            else if (array_data.type == Data_Access_Type::MEMORY_ACCESS)
            {
                result_access = intermediate_generator_create_intermediate_register(generator, element_pointer_type);
                Intermediate_Instruction instr;
                instr.type = Intermediate_Instruction_Type::MOVE_DATA;
                instr.destination = result_access;
                instr.source1 = array_data;
                dynamic_array_push_back(&function->instructions, instr);

                result_access.type = Data_Access_Type::MEMORY_ACCESS;
            }
            else { result_access = data_access_make_empty(); panic("Lol"); };
        }
        else {
            result_access = array_data;
        }

        // Array bounds check
        {
            Data_Access size_data;
            if (array_type_signature->type == Signature_Type::ARRAY_SIZED)
            {
                // If its an sized array, i need to compare the index data with the constant size of something 
                Intermediate_Instruction load_size_instr;
                load_size_instr.type = Intermediate_Instruction_Type::LOAD_CONSTANT_I32;
                load_size_instr.destination = intermediate_generator_create_intermediate_register(generator, generator->analyser->type_system.i32_type);
                load_size_instr.constant_i32_value = array_type_signature->array_element_count;
                dynamic_array_push_back(&function->instructions, load_size_instr);
                size_data = load_size_instr.destination;
            }
            else
            {
                // If its a unsized array, i need to compare the index data with the size stored in the unsized array
                Type_Signature* int_ptr_type = type_system_make_pointer(&generator->analyser->type_system, generator->analyser->type_system.i32_type);

                Intermediate_Instruction offset_instr;
                offset_instr.type = Intermediate_Instruction_Type::CALCULATE_MEMBER_ACCESS_POINTER;
                offset_instr.destination = intermediate_generator_create_intermediate_register(generator, int_ptr_type);
                offset_instr.source1 = array_data;
                offset_instr.constant_i32_value = 8;
                dynamic_array_push_back(&function->instructions, offset_instr);
                size_data = offset_instr.destination;
                size_data.type = Data_Access_Type::MEMORY_ACCESS;
            }

            // Generate an if instruction, an condition register and an error message
            Intermediate_Instruction if_out_of_bounds_instr;
            if_out_of_bounds_instr.type = Intermediate_Instruction_Type::IF_BLOCK;
            if_out_of_bounds_instr.source1 = intermediate_generator_create_intermediate_register(generator, generator->analyser->type_system.bool_type);
            if_out_of_bounds_instr.condition_calculation_instruction_start = function->instructions.size + 1;
            if_out_of_bounds_instr.condition_calculation_instruction_end_exclusive = function->instructions.size + 2;
            if_out_of_bounds_instr.true_branch_instruction_start = function->instructions.size + 2;
            if_out_of_bounds_instr.true_branch_instruction_end_exclusive = function->instructions.size + 3;
            if_out_of_bounds_instr.false_branch_instruction_start = function->instructions.size + 3;
            if_out_of_bounds_instr.false_branch_instruction_end_exclusive = function->instructions.size + 3;
            dynamic_array_push_back(&function->instructions, if_out_of_bounds_instr);

            // Condition instruction
            Intermediate_Instruction condition_instr;
            condition_instr.type = Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_I32;
            condition_instr.destination = if_out_of_bounds_instr.source1;
            condition_instr.source1 = index_data;
            condition_instr.source2 = size_data;
            dynamic_array_push_back(&function->instructions, condition_instr);

            // Error Instruciton
            Intermediate_Instruction error_instr;
            error_instr.source1 = index_data;
            error_instr.type = Intermediate_Instruction_Type::EXIT;
            error_instr.exit_code = Exit_Code::OUT_OF_BOUNDS;
            dynamic_array_push_back(&function->instructions, error_instr);
        }

        Intermediate_Instruction instr;
        instr.type = Intermediate_Instruction_Type::CALCULATE_ARRAY_ACCESS_POINTER;
        instr.constant_i32_value = element_type_signature->size_in_bytes;
        instr.destination = intermediate_generator_create_intermediate_register(
            generator,
            element_pointer_type
        );
        instr.source1 = result_access;
        instr.source2 = index_data;
        dynamic_array_push_back(&function->instructions, instr);
        instr.destination.type = Data_Access_Type::MEMORY_ACCESS;

        if (force_destination) {
            Intermediate_Instruction move_instr;
            move_instr.type = Intermediate_Instruction_Type::MOVE_DATA;
            move_instr.source1 = instr.destination;
            move_instr.destination = destination;
            dynamic_array_push_back(&function->instructions, move_instr);
            return move_instr.destination;
        }
        else {
            return instr.destination;
        }
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL:
    {
        Type_Signature* left_type = generator->analyser->semantic_information[expression->children[0]].expression_result_type;
        Intermediate_Instruction instr;
        instr.type = binary_operation_get_instruction_type(generator, expression->type, left_type);
        instr.source1 = intermediate_generator_generate_expression(generator, expression->children[0], false, data_access_make_empty());
        instr.source2 = intermediate_generator_generate_expression(generator, expression->children[1], false, data_access_make_empty());
        if (force_destination) {
            instr.destination = destination;
        }
        else {
            instr.destination = intermediate_generator_create_intermediate_register(
                generator,
                generator->analyser->semantic_information[expression_index].expression_result_type
            );
        }
        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE:
    {
        Intermediate_Instruction_Type instr_type;
        Type_Signature* operand_type = generator->analyser->semantic_information[expression->children[0]].expression_result_type;
        if (operand_type == generator->analyser->type_system.f32_type) {
            instr_type = Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32;
        }
        else if (operand_type == generator->analyser->type_system.i32_type) {
            instr_type = Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32;
        }
        else panic("Should not happen");

        Intermediate_Instruction instr;
        instr.type = instr_type;
        instr.source1 = intermediate_generator_generate_expression(generator, expression->children[0], false, data_access_make_empty());
        if (force_destination) {
            instr.destination = destination;
        }
        else {
            instr.destination = intermediate_generator_create_intermediate_register(
                generator,
                generator->analyser->semantic_information[expression_index].expression_result_type
            );
        }
        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT:
    {
        Intermediate_Instruction instr;
        instr.type = Intermediate_Instruction_Type::UNARY_OP_BOOLEAN_NOT;
        instr.source1 = intermediate_generator_generate_expression(generator, expression->children[0], false, data_access_make_empty());
        if (force_destination) {
            instr.destination = destination;
        }
        else {
            instr.destination = intermediate_generator_create_intermediate_register(
                generator,
                generator->analyser->semantic_information[expression_index].expression_result_type
            );
        }
        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination;
    }
    }

    panic("Shit this is not something that should happen!\n");
    return data_access_make_empty();
}

void intermediate_generator_generate_statement_block(Intermediate_Generator* generator, int block_index);
void intermediate_generator_generate_statement(Intermediate_Generator* generator, int statement_index)
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];

    AST_Node* statement = &generator->analyser->parser->nodes[statement_index];
    switch (statement->type)
    {
    case AST_Node_Type::STATEMENT_BLOCK: {
        intermediate_generator_generate_statement_block(generator, statement->children[0]);
        break;
    }
    case AST_Node_Type::STATEMENT_BREAK:
    {
        Intermediate_Instruction i;
        i.type = Intermediate_Instruction_Type::BREAK;
        dynamic_array_push_back(&function->instructions, i);
        break;
    }
    case AST_Node_Type::STATEMENT_CONTINUE:
    {
        Intermediate_Instruction i;
        i.type = Intermediate_Instruction_Type::CONTINUE;
        dynamic_array_push_back(&function->instructions, i);
        break;
    }
    case AST_Node_Type::STATEMENT_RETURN: {
        Intermediate_Instruction i;
        i.type = Intermediate_Instruction_Type::RETURN;
        if (generator->current_function_index == generator->main_function_index) {
            i.type = Intermediate_Instruction_Type::EXIT;
            i.exit_code = Exit_Code::SUCCESS;
        }
        i.return_has_value = false;
        if (function->function_type->return_type != generator->analyser->type_system.void_type) {
            i.return_has_value = true;
            i.source1 = intermediate_generator_generate_expression(generator, statement->children[0], false, data_access_make_empty());
        }
        dynamic_array_push_back(&function->instructions, i);
        break;
    }
    case AST_Node_Type::STATEMENT_IF:
    case AST_Node_Type::STATEMENT_IF_ELSE:
    case AST_Node_Type::STATEMENT_WHILE:
    {
        Intermediate_Instruction i;
        bool generate_else_path;
        if (statement->type == AST_Node_Type::STATEMENT_IF) {
            i.type = Intermediate_Instruction_Type::IF_BLOCK;
            generate_else_path = false;
        }
        else if (statement->type == AST_Node_Type::STATEMENT_IF_ELSE) {
            i.type = Intermediate_Instruction_Type::IF_BLOCK;
            generate_else_path = true;
        }
        else if (statement->type == AST_Node_Type::STATEMENT_WHILE) {
            i.type = Intermediate_Instruction_Type::WHILE_BLOCK;
            generate_else_path = false;
        }
        else panic("Cannot happen");

        int branch_instruction_index = function->instructions.size;
        i.condition_calculation_instruction_start = function->instructions.size + 1;
        dynamic_array_push_back(&function->instructions, i);
        Data_Access condition_access = intermediate_generator_generate_expression(generator, statement->children[0], false, data_access_make_empty());

        // Generate Code
        int condition_end = function->instructions.size;
        intermediate_generator_generate_statement_block(generator, statement->children[1]);
        int false_branch_start_index = function->instructions.size;
        if (generate_else_path) {
            intermediate_generator_generate_statement_block(generator, statement->children[2]);
        }
        int false_branch_end = function->instructions.size;

        function->instructions[branch_instruction_index].condition_calculation_instruction_end_exclusive = condition_end;
        function->instructions[branch_instruction_index].source1 = condition_access;
        function->instructions[branch_instruction_index].true_branch_instruction_start = condition_end;
        function->instructions[branch_instruction_index].true_branch_instruction_end_exclusive = false_branch_start_index;
        function->instructions[branch_instruction_index].false_branch_instruction_start = false_branch_start_index;
        function->instructions[branch_instruction_index].false_branch_instruction_end_exclusive = false_branch_end;
        break;
    }
    case AST_Node_Type::STATEMENT_EXPRESSION: {
        intermediate_generator_generate_expression(generator, statement->children[0], false, data_access_make_empty());
        break;
    }
    case AST_Node_Type::STATEMENT_ASSIGNMENT:
    {
        Data_Access destination_register = intermediate_generator_generate_expression(generator, statement->children[0], false, data_access_make_empty());
        intermediate_generator_generate_expression(generator, statement->children[1], true, destination_register);
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN: {
        Data_Access variable_access;
        variable_access.register_index = intermediate_generator_find_variable_register_by_name(generator, statement->name_id);
        variable_access.type = Data_Access_Type::REGISTER_ACCESS;
        intermediate_generator_generate_expression(generator, statement->children[1], true, variable_access);
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER: {
        Data_Access variable_access;
        variable_access.register_index = intermediate_generator_find_variable_register_by_name(generator, statement->name_id);
        variable_access.type = Data_Access_Type::REGISTER_ACCESS;
        intermediate_generator_generate_expression(generator, statement->children[0], true, variable_access);
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION: {
        break; // This is all :)
    }
    }

    return;
}

void intermediate_generator_generate_statement_block(Intermediate_Generator* generator, int block_index)
{
    AST_Node* block = &generator->analyser->parser->nodes[block_index];
    int size_rollback = generator->variable_mappings.size;

    // Generate Variable Registers
    Symbol_Table* table = generator->analyser->symbol_tables[generator->analyser->semantic_information[block_index].symbol_table_index];
    for (int i = 0; i < table->symbols.size; i++)
    {
        Symbol s = table->symbols[i];
        if (s.symbol_type != Symbol_Type::VARIABLE) {
            panic("Shouldnt happen now, only when we have inline functions and types\n");
            continue;
        }
        intermediate_generator_create_variable_register(generator, s.name_handle, s.type);
    }
    for (int i = 0; i < block->children.size; i++) {
        intermediate_generator_generate_statement(generator, block->children[i]);
    }
    dynamic_array_rollback_to_size(&generator->variable_mappings, size_rollback);
}

void intermediate_generator_generate_function_code(Intermediate_Generator* generator, int function_index)
{
    generator->current_function_index = function_index;
    Intermediate_Function* im_function = &generator->functions[function_index];
    int function_node_index = generator->function_to_ast_node_mapping[function_index];
    AST_Node* function = &generator->analyser->parser->nodes[function_node_index];
    Symbol_Table* function_table = generator->analyser->symbol_tables[generator->analyser->semantic_information[function_node_index].symbol_table_index];

    // Generate Parameter Registers
    for (int i = 0; i < function_table->symbols.size; i++) {
        Symbol* s = &function_table->symbols[i];
        Intermediate_Register reg;
        reg.parameter_index = i;
        reg.type_signature = s->type;
        reg.type = Intermediate_Register_Type::PARAMETER;
        dynamic_array_push_back(&im_function->registers, reg);
        Variable_Mapping m;
        m.name_handle = function_table->symbols[i].name_handle;
        m.register_index = im_function->registers.size - 1;
        dynamic_array_push_back(&generator->variable_mappings, m);
    }

    // Generate function code
    intermediate_generator_generate_statement_block(generator, function->children[2]);

    dynamic_array_reset(&generator->variable_mappings);
}

void intermediate_instruction_destroy(Intermediate_Instruction* instruction)
{
    switch (instruction->type)
    {
    case Intermediate_Instruction_Type::CALL_FUNCTION: {
        dynamic_array_destroy(&instruction->arguments);
        return;
    }
    }
}

Intermediate_Function intermediate_function_create(int name_handle, Type_Signature* function_signature)
{
    Intermediate_Function result;
    result.instructions = dynamic_array_create_empty<Intermediate_Instruction>(64);
    result.instruction_to_ast_node_mapping = dynamic_array_create_empty<int>(64);
    result.registers = dynamic_array_create_empty<Intermediate_Register>(64);
    result.register_to_ast_mapping = dynamic_array_create_empty<int>(64);
    result.name_handle = name_handle;
    result.function_type = function_signature;
    return result;
}

void intermediate_function_destroy(Intermediate_Function* function)
{
    for (int i = 0; i < function->instructions.size; i++) {
        intermediate_instruction_destroy(&function->instructions[i]);
    }
    dynamic_array_destroy(&function->instructions);
    dynamic_array_destroy(&function->instruction_to_ast_node_mapping);
    dynamic_array_destroy(&function->registers);
    dynamic_array_destroy(&function->register_to_ast_mapping);
}

Intermediate_Generator intermediate_generator_create()
{
    Intermediate_Generator result;
    result.functions = dynamic_array_create_empty<Intermediate_Function>(64);
    result.variable_mappings = dynamic_array_create_empty<Variable_Mapping>(64);
    result.function_to_ast_node_mapping = dynamic_array_create_empty<int>(16);
    return result;
}

void intermediate_generator_destroy(Intermediate_Generator* generator)
{
    for (int i = 0; i < generator->functions.size; i++) {
        intermediate_function_destroy(&generator->functions[i]);
    }
    dynamic_array_destroy(&generator->functions);
    dynamic_array_destroy(&generator->function_to_ast_node_mapping);
    dynamic_array_destroy(&generator->variable_mappings);
}

void intermediate_generator_generate(Intermediate_Generator* generator, Semantic_Analyser* analyser)
{
    // TODO: Do reset better, kinda tricky because of intermediate functions
    intermediate_generator_destroy(generator);
    *generator = intermediate_generator_create();

    generator->analyser = analyser;
    generator->main_function_index = -1;

    // Generate all (empty) functions
    for (int i = 0; i < analyser->parser->nodes[0].children.size; i++)
    {
        AST_Node_Index function_node_index = analyser->parser->nodes[0].children[i];
        AST_Node* function_node = &analyser->parser->nodes[function_node_index];
        dynamic_array_push_back(
            &generator->functions,
            intermediate_function_create(function_node->name_id, analyser->semantic_information[function_node_index].function_signature)
        );
        dynamic_array_push_back(&generator->function_to_ast_node_mapping, function_node_index);
        if (analyser->parser->nodes[function_node_index].name_id == analyser->main_token_index) {
            generator->main_function_index = generator->functions.size - 1;
        }
    }

    // Now generate all functions
    for (int i = 0; i < generator->functions.size; i++) {
        intermediate_generator_generate_function_code(generator, i);
    }
}

void intermediate_register_append_to_string(String* string, Intermediate_Function* function, int register_index, Intermediate_Generator* generator)
{
    switch (function->registers[register_index].type)
    {
    case Intermediate_Register_Type::EXPRESSION_RESULT:
        string_append_formated(string, "Reg #%d (Expr)", register_index);
        break;
    case Intermediate_Register_Type::PARAMETER:
        string_append_formated(string, "Reg #%d (Param)", register_index);
        break;
    case Intermediate_Register_Type::VARIABLE:
        string_append_formated(string, "Reg #%d (Var %s)", register_index,
            lexer_identifer_to_string(generator->analyser->parser->lexer, function->registers[register_index].name_id).characters);
        break;
    }
    string_append_formated(string, " <");
    type_signature_append_to_string(string, function->registers[register_index].type_signature);
    string_append_formated(string, ">");
}

void data_access_append_to_string(String* string, Data_Access access, Intermediate_Function* function, Intermediate_Generator* generator)
{
    if (access.type == Data_Access_Type::MEMORY_ACCESS) {
        string_append_formated(string, "MEMORY_ACCESS, pointer register: ");
        intermediate_register_append_to_string(string, function, access.register_index, generator);
    }
    else if (access.type == Data_Access_Type::REGISTER_ACCESS) {
        string_append_formated(string, "REGISTER_ACCESS, register: ");
        intermediate_register_append_to_string(string, function, access.register_index, generator);
    }
    else {
        string_append_formated(string, "SHOULD NOT HAPPEN!!!!");
    }
}

void exit_code_append_to_string(String* string, Exit_Code code)
{
    switch (code)
    {
    case Exit_Code::SUCCESS:
        string_append_formated(string, "OUT OF BOUNDS");
        break;
    case Exit_Code::OUT_OF_BOUNDS:
        string_append_formated(string, "OUT OF BOUNDS");
        break;
    case Exit_Code::STACK_OVERFLOW:
        string_append_formated(string, "STACK_OVERFLOW");
        break;
    case Exit_Code::RETURN_VALUE_OVERFLOW:
        string_append_formated(string, "STACK_OVERFLOW");
        break;
    default: panic("");
    }
}

void intermediate_instruction_append_to_string(String* string, Intermediate_Instruction* instruction, Intermediate_Function* function, Intermediate_Generator* generator)
{
    bool append_source_destination = false;
    bool append_binary = false;
    bool append_destination = false;
    bool append_src_1 = false;
    switch (instruction->type)
    {
    case Intermediate_Instruction_Type::ADDRESS_OF:
        string_append_formated(string, "ADDRESS_OF");
        append_source_destination = true;
        break;
    case Intermediate_Instruction_Type::IF_BLOCK:
        string_append_formated(string, "IF_BLOCK, \n\t\tcond_start: %d, cond_end: %d\n\t\ttrue_start: %d, true_end: %d\n\t\t, false_start: %d, false_end: %d",
            instruction->condition_calculation_instruction_start, instruction->condition_calculation_instruction_end_exclusive,
            instruction->true_branch_instruction_start, instruction->true_branch_instruction_end_exclusive,
            instruction->false_branch_instruction_start, instruction->false_branch_instruction_end_exclusive
        );
        string_append_formated(string, "\n\t\tcondition: ");
        data_access_append_to_string(string, instruction->source1, function, generator);
        break;
    case Intermediate_Instruction_Type::WHILE_BLOCK:
        string_append_formated(string, "WHILE_BLOCK, \n\t\tcond_start: %d, cond_end: %d\n\t\ttrue_start: %d, true_end: %d",
            instruction->condition_calculation_instruction_start, instruction->condition_calculation_instruction_end_exclusive,
            instruction->true_branch_instruction_start, instruction->true_branch_instruction_end_exclusive
        );
        string_append_formated(string, "\n\t\tcondition: ");
        data_access_append_to_string(string, instruction->source1, function, generator);
        break;
    case Intermediate_Instruction_Type::CALL_FUNCTION:
        string_append_formated(string, "CALL_FUNCTION, function_index: %d, \n\t\treturn_data: ", instruction->intermediate_function_index);
        if (generator->functions[instruction->intermediate_function_index].function_type->return_type != generator->analyser->type_system.void_type) {
            data_access_append_to_string(string, instruction->destination, function, generator);
        }
        else {
            string_append_formated(string, "void");
        }
        for (int i = 0; i < instruction->arguments.size; i++) {
            string_append_formated(string, "\n\t\t#%d: ", i);
            data_access_append_to_string(string, instruction->arguments[i], function, generator);
        }
        break;
    case Intermediate_Instruction_Type::CALL_HARDCODED_FUNCTION:
        string_append_formated(string, "CALL_HARDCODED_FUNCTION, function_id: %d, \n\t\treturn_data: ", (i32)instruction->hardcoded_function_type);
        data_access_append_to_string(string, instruction->destination, function, generator);
        for (int i = 0; i < instruction->arguments.size; i++) {
            string_append_formated(string, "\n\t\t#%d: ", i);
            data_access_append_to_string(string, instruction->arguments[i], function, generator);
        }
        break;
    case Intermediate_Instruction_Type::RETURN:
        string_append_formated(string, "RETURN, return_data: ");
        if (instruction->return_has_value)
            append_src_1 = true;
        else {
            string_append_formated(string, "void");
        }
        break;
    case Intermediate_Instruction_Type::EXIT:
        string_append_formated(string, "EXIT ");
        if (instruction->exit_code == Exit_Code::SUCCESS && instruction->return_has_value)
            append_src_1 = true;
        exit_code_append_to_string(string, instruction->exit_code);
        break;
    case Intermediate_Instruction_Type::BREAK:
        string_append_formated(string, "BREAK");
        break;
    case Intermediate_Instruction_Type::CONTINUE:
        string_append_formated(string, "CONTINUE");
        break;
    case Intermediate_Instruction_Type::CALCULATE_ARRAY_ACCESS_POINTER:
        string_append_formated(string, "CALCULATE_ARRAY_ACCESS_POINTER, type_size: %d,  ", instruction->constant_i32_value);
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::CALCULATE_MEMBER_ACCESS_POINTER:
        string_append_formated(string, "CALCULATE_MEMBER_ACCESS_POINTER, offset: %d ", instruction->constant_i32_value);
        append_source_destination = true;
        break;
    case Intermediate_Instruction_Type::MOVE_DATA:
        string_append_formated(string, "MOVE_DATA");
        append_source_destination = true;
        break;
    case Intermediate_Instruction_Type::LOAD_CONSTANT_F32:
        string_append_formated(string, "LOAD_CONSTANT_F32, value: %3.2f ", instruction->constant_f32_value);
        append_destination = true;
        break;
    case Intermediate_Instruction_Type::LOAD_CONSTANT_I32:
        string_append_formated(string, "LOAD_CONSTANT_I32, value: %d ", instruction->constant_i32_value);
        append_destination = true;
        break;
    case Intermediate_Instruction_Type::LOAD_CONSTANT_BOOL:
        string_append_formated(string, "LOAD_CONSTANT_BOOL, value: %s ", instruction->constant_bool_value ? "TRUE" : "FALSE");
        append_destination = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32:
        string_append_formated(string, "BINARY_OP_ARITHMETIC_ADDITION_I32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_I32:
        string_append_formated(string, "BINARY_OP_ARITHMETIC_SUBTRACTION_I32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_I32:
        string_append_formated(string, "BINARY_OP_ARITHMETIC_MULTIPLICATION_I32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_I32:
        string_append_formated(string, "BINARY_OP_ARITHMETIC_DIVISION_I32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_I32:
        string_append_formated(string, "BINARY_OP_ARITHMETIC_MODULO_I32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_I32:
        string_append_formated(string, "BINARY_OP_COMPARISON_EQUAL_I32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_I32:
        string_append_formated(string, "BINARY_OP_COMPARISON_NOT_EQUAL_I32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_I32:
        string_append_formated(string, "BINARY_OP_COMPARISON_GREATER_THAN_I32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_I32:
        string_append_formated(string, "BINARY_OP_COMPARISON_GREATER_EQUAL_I32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_I32:
        string_append_formated(string, "BINARY_OP_COMPARISON_LESS_THAN_I32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_I32:
        string_append_formated(string, "BINARY_OP_COMPARISON_LESS_EQUAL_I32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32:
        string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_I32 ");
        append_source_destination = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F32:
        string_append_formated(string, "BINARY_OP_ARITHMETIC_ADDITION_F32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_F32:
        string_append_formated(string, "BINARY_OP_ARITHMETIC_SUBTRACTION_F32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_F32:
        string_append_formated(string, "BINARY_OP_ARITHMETIC_MULTIPLICATION_F32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_F32:
        string_append_formated(string, "BINARY_OP_ARITHMETIC_DIVISION_F32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_F32:
        string_append_formated(string, "BINARY_OP_COMPARISON_EQUAL_F32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_F32:
        string_append_formated(string, "BINARY_OP_COMPARISON_NOT_EQUAL_F32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_F32:
        string_append_formated(string, "BINARY_OP_COMPARISON_GREATER_THAN_F32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_F32:
        string_append_formated(string, "BINARY_OP_COMPARISON_GREATER_EQUAL_F32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_F32:
        string_append_formated(string, "BINARY_OP_COMPARISON_LESS_THAN_F32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_F32:
        string_append_formated(string, "BINARY_OP_COMPARISON_LESS_EQUAL_F32 ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32:
        string_append_formated(string, "UNARY_OP_ARITHMETIC_NEGATE_F32 ");
        append_source_destination = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_BOOL:
        string_append_formated(string, "BINARY_OP_COMPARISON_EQUAL_BOOL ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_BOOL:
        string_append_formated(string, "BINARY_OP_COMPARISON_NOT_EQUAL_BOOL ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_AND:
        string_append_formated(string, "BINARY_OP_BOOLEAN_AND ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_OR:
        string_append_formated(string, "BINARY_OP_BOOLEAN_OR ");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::UNARY_OP_BOOLEAN_NOT:
        string_append_formated(string, "UNARY_OP_BOOLEAN_NOT ");
        append_source_destination = true;
        break;
    default:
        logg("Should not fucking happen!");
        break;
    }

    if (append_binary) {
        string_append_formated(string, "\n\t\tleft = ");
        data_access_append_to_string(string, instruction->source1, function, generator);
        string_append_formated(string, "\n\t\tright = ");
        data_access_append_to_string(string, instruction->source2, function, generator);
        string_append_formated(string, "\n\t\tdest = ");
        data_access_append_to_string(string, instruction->destination, function, generator);
    }
    if (append_source_destination) {
        string_append_formated(string, "\n\t\tsrc = ");
        data_access_append_to_string(string, instruction->source1, function, generator);
        string_append_formated(string, "\n\t\tdest = ");
        data_access_append_to_string(string, instruction->destination, function, generator);
    }
    if (append_destination) {
        string_append_formated(string, "\n\t\tdest = ");
        data_access_append_to_string(string, instruction->destination, function, generator);
    }
    if (append_src_1) {
        string_append_formated(string, "\n\t\tsrc = ");
        data_access_append_to_string(string, instruction->source1, function, generator);
    }
}

void intermediate_function_append_to_string(String* string, Intermediate_Generator* generator, int index)
{
    Intermediate_Function* function = &generator->functions[index];
    string_append_formated(string, "Function #%d: %s\n", index,
        lexer_identifer_to_string(generator->analyser->parser->lexer,
            generator->analyser->parser->nodes[generator->function_to_ast_node_mapping[index]].name_id).characters);
    string_append_formated(string, "Instructions:\n");
    for (int i = 0; i < function->instructions.size; i++) {
        string_append_formated(string, "\t#%d: ", i);
        intermediate_instruction_append_to_string(string, &function->instructions[i], function, generator);
        string_append_formated(string, "\n");
    }
}

void intermediate_generator_append_to_string(String* string, Intermediate_Generator* generator)
{
    string_append_formated(string, "Function count: %d\n\n", generator->functions.size);
    for (int i = 0; i < generator->functions.size; i++) {
        intermediate_function_append_to_string(string, generator, i);
    }
}