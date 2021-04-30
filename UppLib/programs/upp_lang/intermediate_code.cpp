#include "intermediate_code.hpp"

int intermediate_generator_find_variable_register_by_name(Intermediate_Generator* generator, int name_id)
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];
    for (int i = generator->variable_mappings.size - 1; i >= 0; i--) {
        if (generator->variable_mappings[i].name = name_id) {
            return generator->variable_mappings[i].register_index;
        }
    }
    panic("Cannot happen after semantic analysis");
    return -1;
}

int intermediate_generator_create_intermediate_register(Intermediate_Generator* generator, int type_index, bool read_pointer_on_access)
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];

    Intermediate_Register reg;
    reg.type_index = type_index;
    reg.type = Intermediate_Register_Type::EXPRESSION_RESULT;
    reg.read_pointer_on_access = read_pointer_on_access;
    dynamic_array_push_back(&function->registers, reg);
    return function->registers.size - 1;
}

void intermediate_generator_create_parameter_register(Intermediate_Generator* generator, int name_id, int type_index, int parameter_index)
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];

    Intermediate_Register reg;
    reg.parameter_index = parameter_index;
    reg.type_index = type_index;
    reg.type = Intermediate_Register_Type::PARAMETER;
    reg.read_pointer_on_access = false;
    dynamic_array_push_back(&function->registers, reg);

    Variable_Mapping m;
    m.name = name_id;
    m.register_index = function->registers.size - 1;
    dynamic_array_push_back(&generator->variable_mappings, m);
}

void intermediate_generator_create_variable_register(Intermediate_Generator* generator, int name_id, int type_index)
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];

    Intermediate_Register reg;
    reg.type_index = type_index;
    reg.type = Intermediate_Register_Type::VARIABLE;
    reg.name_id = name_id;
    reg.read_pointer_on_access = false;
    dynamic_array_push_back(&function->registers, reg);

    Variable_Mapping m;
    m.name = name_id;
    m.register_index = function->registers.size - 1;
    dynamic_array_push_back(&generator->variable_mappings, m);
}

int intermediate_generator_find_function_by_name(Intermediate_Generator* generator, int name_id)
{
    for (int i = 0; i < generator->function_to_ast_node_mapping.size; i++) {
        int name = generator->analyser->parser->nodes[generator->function_to_ast_node_mapping[i]].name_id;
        if (name == name_id) {
            return i;
        }
    }
    panic("Should not happen!");
    return 0;
}

int intermediate_generator_access_register(Intermediate_Generator* generator, int register_index) 
{
    Intermediate_Function* function = &generator->functions[generator->current_function_index];
    Intermediate_Register* access_reg = &function->registers[register_index];
    if (access_reg->read_pointer_on_access) {
        Type_Signature* s = type_system_get_type(&generator->analyser->type_system, access_reg->type_index);
        if (s == 0 || s->type != Signature_Type::POINTER) 
            panic("should also not happen!");
        int result_reg = intermediate_generator_create_intermediate_register(generator, s->child_type_index, false);
        Intermediate_Instruction instr;
        instr.type = Intermediate_Instruction_Type::READ_FROM_MEMORY;
        instr.destination_register = result_reg;
        instr.source_register = register_index;
        dynamic_array_push_back(&function->instructions, instr);
        return result_reg;
    }
    return register_index;
}

int intermediate_instruction_binary_operation_get_result_type(Intermediate_Instruction_Type instr_type, Intermediate_Generator* generator)
{
    switch (instr_type)
    {
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32: 
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_I32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_I32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_I32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_I32:
    case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32:
        return generator->analyser->i32_type_index;
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F32: 
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_F32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_F32:
    case Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_F32:
    case Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32:
        return generator->analyser->f32_type_index;
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
        return generator->analyser->bool_type_index;
    }
    panic("Sheit\n");
    return 0;
}

Intermediate_Instruction_Type binary_operation_get_instruction_type(Intermediate_Generator* generator, AST_Node_Type::ENUM op_type, int operand_types)
{
    switch (op_type)
    {
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION:
        if (operand_types == generator->analyser->i32_type_index) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32;
        if (operand_types == generator->analyser->f32_type_index) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F32;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION:
        if (operand_types == generator->analyser->i32_type_index) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_I32;
        if (operand_types == generator->analyser->f32_type_index) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_F32;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION:
        if (operand_types == generator->analyser->i32_type_index) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_I32;
        if (operand_types == generator->analyser->f32_type_index) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_F32;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION:
        if (operand_types == generator->analyser->i32_type_index) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_I32;
        if (operand_types == generator->analyser->f32_type_index) return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_F32;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO:
        return Intermediate_Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_I32;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND:
        return Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_AND;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR:
        return Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_OR;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL:
        if (operand_types == generator->analyser->i32_type_index) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_I32;
        if (operand_types == generator->analyser->f32_type_index) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_F32;
        if (operand_types == generator->analyser->bool_type_index) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_EQUAL_BOOL;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL:
        if (operand_types == generator->analyser->i32_type_index) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_I32;
        if (operand_types == generator->analyser->f32_type_index) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_F32;
        if (operand_types == generator->analyser->bool_type_index) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_BOOL;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
        if (operand_types == generator->analyser->i32_type_index) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_I32;
        if (operand_types == generator->analyser->f32_type_index) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_F32;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL:
        if (operand_types == generator->analyser->i32_type_index) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_I32;
        if (operand_types == generator->analyser->f32_type_index) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_F32;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
        if (operand_types == generator->analyser->i32_type_index) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_I32;
        if (operand_types == generator->analyser->f32_type_index) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_F32;
        panic("Not valid, should have been caught!");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL:
        if (operand_types == generator->analyser->i32_type_index) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_I32;
        if (operand_types == generator->analyser->f32_type_index) return Intermediate_Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_F32;
        panic("Not valid, should have been caught!");
    }
    panic("This should not happen :)\n");
    return Intermediate_Instruction_Type::BINARY_OP_BOOLEAN_AND;
}

// Returns expression result register
int intermediate_generator_generate_expression(Intermediate_Generator* generator, int expression_index)
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
        instr.type = Intermediate_Instruction_Type::CALL_FUNCTION;
        instr.intermediate_function_index = intermediate_generator_find_function_by_name(generator, expression->name_id);
        instr.argument_registers = dynamic_array_create_empty<int>(expression->children.size);
        instr.destination_register = intermediate_generator_create_intermediate_register(generator,
            type_system_get_type(&generator->analyser->type_system, function_symbol->type_index)->return_type_index,
            false
        );

        // Create expression stuff
        for (int i = 0; i < expression->children.size; i++) {
            int arg_reg = intermediate_generator_generate_expression(generator, expression->children[i]);
            dynamic_array_push_back(&instr.argument_registers, intermediate_generator_access_register(generator, arg_reg));
        }
        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination_register;
    }
    case AST_Node_Type::EXPRESSION_LITERAL:
    {
        Intermediate_Instruction instr;
        Token& token = generator->analyser->parser->lexer->tokens[generator->analyser->parser->token_mapping[expression_index].start_index];
        int result_type;
        if (token.type == Token_Type::FLOAT_LITERAL) {
            result_type = generator->analyser->f32_type_index;
            instr.type = Intermediate_Instruction_Type::MOVE_CONSTANT_F32;
            instr.constant_f32_value = token.attribute.float_value;
        }
        else if (token.type == Token_Type::INTEGER_LITERAL) {
            result_type = generator->analyser->i32_type_index;
            instr.type = Intermediate_Instruction_Type::MOVE_CONSTANT_I32;
            instr.constant_i32_value = token.attribute.integer_value;
        }
        else if (token.type == Token_Type::BOOLEAN_LITERAL) {
            result_type = generator->analyser->bool_type_index;
            instr.type = Intermediate_Instruction_Type::MOVE_CONSTANT_BOOL;
            instr.constant_i32_value = token.attribute.bool_value;
        }
        else panic("what");
        instr.destination_register = intermediate_generator_create_intermediate_register(generator, result_type, false);
        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination_register;
    }
    case AST_Node_Type::EXPRESSION_ARRAY_ACCESS:
    {
        // TODO: Include Array bounds checks here
        int index_reg = intermediate_generator_access_register(generator,
            intermediate_generator_generate_expression(generator, expression->children[1])
        );
        int base_pointer_reg = intermediate_generator_generate_expression(generator, expression->children[0]);
        if (function->registers[base_pointer_reg].read_pointer_on_access) {
            int result_reg = intermediate_generator_create_intermediate_register(generator, function->registers[base_pointer_reg].type_index, true);
            Intermediate_Instruction instr;
            instr.type = Intermediate_Instruction_Type::CALC_ARRAY_ACCESS_POINTER;
            instr.destination_register = result_reg;
            instr.left_operand_register = base_pointer_reg;
            instr.right_operand_register = index_reg;
            dynamic_array_push_back(&function->instructions, instr);
        }
        else {
            Type_Signature* signature = type_system_get_type(&generator->analyser->type_system, function->registers[base_pointer_reg].type_index);
            if (signature->type == Signature_Type::ARRAY_SIZED) {
                
            }
            else if (signature->type == Signature_Type::ARRAY_SIZED) {

            }
            else panic("What?");
        }
        __debugbreak();
        return -1;
    }
    case AST_Node_Type::EXPRESSION_MEMBER_ACCESS:
    {
        /*
        if (expression->name_id == generator->analyser->data_token_index) {
            int base_pointer_reg = intermediate_generator_generate_expression(generator, expression->children[0]);
            if (!function->registers[base_pointer_reg].read_pointer_on_access) panic("Access base should be pointer!");
            int result_reg = intermediate_generator_create_intermediate_register(generator, function->registers[base_pointer_reg].type_index, true);
        }
        else if (expression->name_id == generator->analyser->size_token_index) {

        }
        else panic("What");
        */
        //panic("Member access not implemented!\n");
        return 0;
    }
    case AST_Node_Type::EXPRESSION_VARIABLE_READ: {
        int var_reg = intermediate_generator_find_variable_register_by_name(generator, expression->name_id);
        return var_reg;
        /*
        int type_index = type_system_find_or_create_type(
            &generator->analyser->type_system,
            type_signature_make_pointer(function->registers[var_reg].type_index)
        );
        int result_reg = intermediate_generator_create_intermediate_register(generator, type_index, true);
        Intermediate_Instruction instr;
        instr.type = Intermediate_Instruction_Type::ADDRESS_OF_REGISTER;
        instr.destination_register = result_reg;
        instr.source_register = var_reg;
        dynamic_array_push_back(&function->instructions, instr);
        return result_reg;
        */
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
        int left_type = generator->analyser->semantic_information[expression->children[0]].expression_result_type_index;
        Intermediate_Instruction_Type instr_type = binary_operation_get_instruction_type(generator, expression->type, left_type);
        int left_register = intermediate_generator_generate_expression(generator, expression->children[0]);
        int right_register = intermediate_generator_generate_expression(generator, expression->children[1]);
        int result_register = intermediate_generator_create_intermediate_register(generator,
            intermediate_instruction_binary_operation_get_result_type(instr_type, generator), false);
        Intermediate_Instruction instr;
        instr.type = instr_type;
        instr.destination_register = result_register;
        instr.left_operand_register = intermediate_generator_access_register(generator, left_register);
        instr.right_operand_register = intermediate_generator_access_register(generator, right_register);
        dynamic_array_push_back(&function->instructions, instr);
        return result_register;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE:
    {
        int source_register = intermediate_generator_access_register(generator,
            intermediate_generator_generate_expression(generator, expression->children[0]));
        int result_register = intermediate_generator_create_intermediate_register(generator,
            generator->analyser->semantic_information[expression_index].expression_result_type_index, false);
        Intermediate_Instruction_Type instr_type;
        int operand_type = generator->analyser->semantic_information[expression->children[0]].expression_result_type_index;
        if (operand_type == generator->analyser->f32_type_index) {
            instr_type = Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32;
        }
        else if (operand_type == generator->analyser->i32_type_index) {
            instr_type = Intermediate_Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32;
        }
        else panic("Should not happen");

        Intermediate_Instruction instr;
        instr.type = instr_type;
        instr.source_register = source_register;
        instr.destination_register = result_register;
        dynamic_array_push_back(&function->instructions, instr);

        return result_register;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT:
    {
        int source_register = intermediate_generator_access_register(generator,
            intermediate_generator_generate_expression(generator, expression->children[0]));
        int result_register = intermediate_generator_create_intermediate_register(generator,
            generator->analyser->semantic_information[expression_index].expression_result_type_index, false);

        Intermediate_Instruction instr;
        instr.type = Intermediate_Instruction_Type::UNARY_OP_BOOLEAN_NOT;
        instr.source_register = source_register;
        instr.destination_register = result_register;
        dynamic_array_push_back(&function->instructions, instr);

        return result_register;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF:
    {
        int source_register = intermediate_generator_generate_expression(generator, expression->children[0]);
        if (function->registers[source_register].read_pointer_on_access) {
            // I dont really know if this will work how i want it to
            return source_register;
        }

        Intermediate_Instruction instr;
        instr.type = Intermediate_Instruction_Type::ADDRESS_OF_REGISTER;
        instr.source_register = source_register;
        instr.destination_register = intermediate_generator_create_intermediate_register(generator,
            generator->analyser->semantic_information[expression_index].expression_result_type_index, false);
        dynamic_array_push_back(&function->instructions, instr);
        return instr.destination_register;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE:
    {
        int pointer_register = intermediate_generator_access_register(generator,
            intermediate_generator_generate_expression(generator, expression->children[0]));

        Intermediate_Instruction instr;
        instr.type = Intermediate_Instruction_Type::READ_FROM_MEMORY;
        instr.source_register = pointer_register;
        instr.destination_register = intermediate_generator_create_intermediate_register(generator,
            generator->analyser->semantic_information[expression_index].expression_result_type_index, false);
        dynamic_array_push_back(&function->instructions, instr);

        return instr.destination_register;
    }
    }

    panic("Shit this is not something that should happen!\n");
    return generator->analyser->error_type_index;
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
        int return_value_register = intermediate_generator_generate_expression(generator, statement->children[0]);
        Intermediate_Instruction i;
        i.type = Intermediate_Instruction_Type::RETURN;
        if (generator->current_function_index == generator->main_function_index) {
            i.type = Intermediate_Instruction_Type::EXIT;
        }
        i.source_register = return_value_register;
        dynamic_array_push_back(&function->instructions, i);
        break;
    }
    case AST_Node_Type::STATEMENT_IF:
    {
        int return_value_register = intermediate_generator_generate_expression(generator, statement->children[0]);
        int if_instruction_index_fill_out = function->instructions.size;
        Intermediate_Instruction i;
        i.type = Intermediate_Instruction_Type::IF_BLOCK;
        i.source_register = return_value_register;
        dynamic_array_push_back(&function->instructions, i);

        // Generate Code
        int true_branch_start_index = function->instructions.size;
        intermediate_generator_generate_statement_block(generator, statement->children[1]);
        int true_branch_size = function->instructions.size - true_branch_start_index;
        int false_branch_start_index = function->instructions.size;
        int false_branch_size = 0;

        function->instructions[if_instruction_index_fill_out].true_branch_instruction_start = true_branch_start_index;
        function->instructions[if_instruction_index_fill_out].true_branch_instruction_size = true_branch_size;
        function->instructions[if_instruction_index_fill_out].false_branch_instruction_start = false_branch_start_index;
        function->instructions[if_instruction_index_fill_out].false_branch_instruction_size = false_branch_size;
        break;
    }
    case AST_Node_Type::STATEMENT_IF_ELSE:
    {
        int return_value_register = intermediate_generator_generate_expression(generator, statement->children[0]);
        int if_instruction_index_fill_out = function->instructions.size;
        Intermediate_Instruction i;
        i.type = Intermediate_Instruction_Type::IF_BLOCK;
        i.source_register = return_value_register;
        dynamic_array_push_back(&function->instructions, i);

        // Generate Code
        int true_branch_start_index = function->instructions.size;
        intermediate_generator_generate_statement_block(generator, statement->children[1]);
        int true_branch_size = function->instructions.size - true_branch_start_index;
        int false_branch_start_index = function->instructions.size;
        intermediate_generator_generate_statement_block(generator, statement->children[2]);
        int false_branch_size = function->instructions.size - false_branch_start_index;

        function->instructions[if_instruction_index_fill_out].true_branch_instruction_start = true_branch_start_index;
        function->instructions[if_instruction_index_fill_out].true_branch_instruction_size = true_branch_size;
        function->instructions[if_instruction_index_fill_out].false_branch_instruction_start = false_branch_start_index;
        function->instructions[if_instruction_index_fill_out].false_branch_instruction_size = false_branch_size;
        break;
    }
    case AST_Node_Type::STATEMENT_WHILE:
    {
        int return_value_register = intermediate_generator_generate_expression(generator, statement->children[0]);
        int if_instruction_index_fill_out = function->instructions.size;
        Intermediate_Instruction i;
        i.type = Intermediate_Instruction_Type::WHILE_BLOCK;
        i.source_register = return_value_register;
        dynamic_array_push_back(&function->instructions, i);

        // Generate Code
        int true_branch_start_index = function->instructions.size;
        intermediate_generator_generate_statement_block(generator, statement->children[1]);
        int true_branch_size = function->instructions.size - true_branch_start_index;
        int false_branch_start_index = function->instructions.size;
        int false_branch_size = 0;

        function->instructions[if_instruction_index_fill_out].true_branch_instruction_start = true_branch_start_index;
        function->instructions[if_instruction_index_fill_out].true_branch_instruction_size = true_branch_size;
        function->instructions[if_instruction_index_fill_out].false_branch_instruction_start = false_branch_start_index;
        function->instructions[if_instruction_index_fill_out].false_branch_instruction_size = false_branch_size;
        break;
    }
    case AST_Node_Type::STATEMENT_EXPRESSION: {
        intermediate_generator_generate_expression(generator, statement->children[0]);
        break;
    }
    case AST_Node_Type::STATEMENT_ASSIGNMENT:
    {
        int source_register = intermediate_generator_generate_expression(generator, statement->children[1]);
        int destination_register = intermediate_generator_generate_expression(generator, statement->children[0]);
        if (function->registers[destination_register].read_pointer_on_access) {
            Intermediate_Instruction instr;
            instr.type = Intermediate_Instruction_Type::WRITE_TO_MEMORY;
            instr.destination_register = destination_register;
            instr.source_register = source_register;
            dynamic_array_push_back(&function->instructions, instr);
        }
        else {
            Intermediate_Instruction instr;
            instr.type = Intermediate_Instruction_Type::MOVE_REGISTER;
            instr.destination_register = destination_register;
            instr.source_register = source_register;
            dynamic_array_push_back(&function->instructions, instr);
        }
        /*
        AST_Node* left_side_expr = &generator->analyser->parser->nodes[statement->children[0]];
        if (left_side_expr->type == AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE) {
            int memory_address_reg = intermediate_generator_generate_expression(generator, left_side_expr->children[0]);
            Intermediate_Instruction instr;
            instr.type = Intermediate_Instruction_Type::WRITE_TO_MEMORY;
            instr.destination_register = memory_address_reg;
            instr.source_register = source_register;
            dynamic_array_push_back(&function->instructions, instr);
        }
        else if (left_side_expr->type == AST_Node_Type::EXPRESSION_VARIABLE_READ) {
            int dest_register_id = intermediate_generator_find_variable_register_by_name(generator, left_side_expr->name_id);
            Intermediate_Instruction instr;
            instr.type = Intermediate_Instruction_Type::MOVE_REGISTER;
            instr.destination_register = dest_register_id;
            instr.source_register = source_register;
            dynamic_array_push_back(&function->instructions, instr);
            break;
        }
        else {
            panic("Shit, i dont know if this can happend, good question!");
        }
        */
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN: {
        int destination_register = intermediate_generator_find_variable_register_by_name(generator, statement->name_id);
        int source_register = intermediate_generator_generate_expression(generator, statement->children[1]);
        Intermediate_Instruction instr;
        instr.type = Intermediate_Instruction_Type::MOVE_REGISTER;
        instr.destination_register = destination_register;
        instr.source_register = source_register;
        dynamic_array_push_back(&function->instructions, instr);
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER: {
        int destination_register = intermediate_generator_find_variable_register_by_name(generator, statement->name_id);
        int source_register = intermediate_generator_generate_expression(generator, statement->children[0]);
        Intermediate_Instruction instr;
        instr.type = Intermediate_Instruction_Type::MOVE_REGISTER;
        instr.destination_register = destination_register;
        instr.source_register = source_register;
        dynamic_array_push_back(&function->instructions, instr);
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
        intermediate_generator_create_variable_register(generator, s.name, s.type_index);
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
        reg.type_index = s->type_index;
        reg.type = Intermediate_Register_Type::PARAMETER;
        dynamic_array_push_back(&im_function->registers, reg);
        Variable_Mapping m;
        m.name = function_table->symbols[i].name;
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
        dynamic_array_destroy(&instruction->argument_registers);
        return;
    }
    }
}

Intermediate_Function intermediate_function_create()
{
    Intermediate_Function result;
    result.instructions = dynamic_array_create_empty<Intermediate_Instruction>(64);
    result.instruction_to_ast_node_mapping = dynamic_array_create_empty<int>(64);
    result.registers = dynamic_array_create_empty<Intermediate_Register>(64);
    result.register_to_ast_mapping = dynamic_array_create_empty<int>(64);
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
    // TODO: Do reset better
    intermediate_generator_destroy(generator);
    *generator = intermediate_generator_create();

    generator->analyser = analyser;
    generator->main_function_index = -1;

    // Generate all (empty) functions
    for (int i = 0; i < analyser->parser->nodes[0].children.size; i++) {
        AST_Node_Index function_node_index = analyser->parser->nodes[0].children[i];
        dynamic_array_push_back(&generator->functions, intermediate_function_create());
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
        return;
    case Intermediate_Register_Type::PARAMETER:
        string_append_formated(string, "Reg #%d (Param)", register_index);
        return;
    case Intermediate_Register_Type::VARIABLE:
        string_append_formated(string, "Reg #%d (Var %s)", register_index,
            lexer_identifer_to_string(generator->analyser->parser->lexer, function->registers[register_index].name_id).characters);
        return;
    }
}

void intermediate_instruction_append_to_string(String* string, Intermediate_Instruction* instruction, Intermediate_Function* function, Intermediate_Generator* generator)
{
    bool append_source_reg = false;
    bool append_destination_reg = false;
    bool append_binary = false;
    switch (instruction->type)
    {
    case Intermediate_Instruction_Type::ADDRESS_OF_REGISTER:
        string_append_formated(string, "ADDRESS_OF_REGISTER");
        append_source_reg = true;
        append_destination_reg = true;
        break;
    case Intermediate_Instruction_Type::WRITE_TO_MEMORY:
        string_append_formated(string, "WRITE_TO_MEMORY");
        append_source_reg = true;
        append_destination_reg = true;
        break;
    case Intermediate_Instruction_Type::READ_FROM_MEMORY:
        string_append_formated(string, "READ_FROM_MEMORY");
        append_source_reg = true;
        append_destination_reg = true;
        break;
    case Intermediate_Instruction_Type::IF_BLOCK:
        string_append_formated(string, "IF_BLOCK, cond_reg: ");
        intermediate_register_append_to_string(string, function, instruction->condition_register, generator);
        string_append_formated(string, " true_start: %d, true_size: %d, false_start: %d, false_size: %d",
            instruction->true_branch_instruction_start, instruction->true_branch_instruction_size,
            instruction->false_branch_instruction_start, instruction->false_branch_instruction_size);
        break;
    case Intermediate_Instruction_Type::WHILE_BLOCK:
        string_append_formated(string, "WHILE_BLOCK, cond_reg: ");
        intermediate_register_append_to_string(string, function, instruction->condition_register, generator);
        string_append_formated(string, " true_start: %d, true_size: %d",
            instruction->true_branch_instruction_start, instruction->true_branch_instruction_size);
        break;
    case Intermediate_Instruction_Type::CALL_FUNCTION:
        string_append_formated(string, "CALL_FUNCTION, function_index: %d, ");
        intermediate_register_append_to_string(string, function, instruction->condition_register, generator);
        string_append_formated(string, " true_start: %d, true_size: %d ",
            instruction->true_branch_instruction_start, instruction->true_branch_instruction_size);
        break;
    case Intermediate_Instruction_Type::RETURN:
        string_append_formated(string, "RETURN ");
        append_source_reg = true;
        break;
    case Intermediate_Instruction_Type::EXIT:
        string_append_formated(string, "EXIT ");
        append_source_reg = true;
        break;
    case Intermediate_Instruction_Type::BREAK:
        string_append_formated(string, "BREAK");
        break;
    case Intermediate_Instruction_Type::CONTINUE:
        string_append_formated(string, "CONTINUE");
        break;
    case Intermediate_Instruction_Type::CALC_ARRAY_ACCESS_POINTER:
        string_append_formated(string, "CALC_ARRAY_ACCESS_POINTER");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::OFFSET_POINTER_BY_I32:
        string_append_formated(string, "OFFSET_POINTER_BY_I32");
        append_binary = true;
        break;
    case Intermediate_Instruction_Type::MOVE_REGISTER:
        string_append_formated(string, "MOVE_REGISTER");
        append_destination_reg = true;
        append_source_reg = true;
        break;
    case Intermediate_Instruction_Type::MOVE_CONSTANT_F32:
        string_append_formated(string, "MOVE_CONSTANT_F32, value: %3.2f ", instruction->constant_f32_value);
        append_destination_reg = true;
        break;
    case Intermediate_Instruction_Type::MOVE_CONSTANT_I32:
        string_append_formated(string, "MOVE_CONSTANT_I32, value: %d ", instruction->constant_i32_value);
        append_destination_reg = true;
        break;
    case Intermediate_Instruction_Type::MOVE_CONSTANT_BOOL:
        string_append_formated(string, "MOVE_CONSTANT_BOOL, value: %s ", instruction->constant_bool_value ? "TRUE" : "FALSE");
        append_destination_reg = true;
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
        append_binary = true;
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
        append_binary = true;
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
        append_binary = true;
        break;
    }

    if (append_binary) {
        string_append_formated(string, "\n\t\tdest = ");
        intermediate_register_append_to_string(string, function, instruction->destination_register, generator);
        string_append_formated(string, ", left = ");
        intermediate_register_append_to_string(string, function, instruction->left_operand_register, generator);
        string_append_formated(string, ", right = ");
        intermediate_register_append_to_string(string, function, instruction->right_operand_register, generator);
    }
    if (append_destination_reg) {
        string_append_formated(string, "\n\t\tdest = ");
        intermediate_register_append_to_string(string, function, instruction->destination_register, generator);
    }
    if (append_source_reg) {
        string_append_formated(string, "\n\t\tsrc = ");
        intermediate_register_append_to_string(string, function, instruction->source_register, generator);
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