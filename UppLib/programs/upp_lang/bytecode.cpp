#include "bytecode.hpp"

Bytecode_Generator bytecode_generator_create()
{
    Bytecode_Generator result;
    result.instructions = dynamic_array_create_empty<Bytecode_Instruction>(64);
    result.variable_locations = dynamic_array_create_empty<Variable_Location>(64);
    result.break_instructions_to_fill_out = dynamic_array_create_empty<int>(64);
    result.continue_instructions_to_fill_out = dynamic_array_create_empty<int>(64);
    result.function_locations = dynamic_array_create_empty<Function_Location>(64);
    result.function_calls = dynamic_array_create_empty<Function_Call_Location>(64);
    result.maximum_function_stack_depth = 0;
    return result;
}

void bytecode_generator_destroy(Bytecode_Generator* generator)
{
    dynamic_array_destroy(&generator->instructions);
    dynamic_array_destroy(&generator->variable_locations);
    dynamic_array_destroy(&generator->break_instructions_to_fill_out);
    dynamic_array_destroy(&generator->continue_instructions_to_fill_out);
    dynamic_array_destroy(&generator->function_calls);
    dynamic_array_destroy(&generator->function_locations);
} 

Bytecode_Instruction instruction_make_0(Instruction_Type::ENUM type) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    return instr;
}

Bytecode_Instruction instruction_make_1(Instruction_Type::ENUM type, int src_1) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    instr.op1 = src_1;
    return instr;
}

Bytecode_Instruction instruction_make_2(Instruction_Type::ENUM type, int src_1, int src_2) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    instr.op1 = src_1;
    instr.op2 = src_2;
    return instr;
}

Bytecode_Instruction instruction_make_3(Instruction_Type::ENUM type, int src_1, int src_2, int src_3) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    instr.op1 = src_1;
    instr.op2 = src_2;
    instr.op3 = src_3;
    return instr;
}

int bytecode_generator_generate_register(Bytecode_Generator* generator)
{
    int result = generator->stack_base_offset;
    generator->stack_base_offset++;
    if (generator->stack_base_offset > generator->max_stack_base_offset) {
        generator->max_stack_base_offset = generator->stack_base_offset;
    }
    return result;
}

Variable_Location* bytecode_generator_get_variable_loc(Bytecode_Generator* generator, int name_id)
{
    for (int i = generator->variable_locations.size - 1; i >= 0; i--) {
        Variable_Location* loc = &generator->variable_locations[i];
        if (loc->variable_name == name_id)
            return loc;
    }
    panic("Should not happen after semantic analysis");
    return 0;
}

int bytecode_generator_add_instruction(Bytecode_Generator* generator, Bytecode_Instruction instruction) {
    dynamic_array_push_back(&generator->instructions, instruction);
    return generator->instructions.size - 1;
}

Instruction_Type::ENUM binary_operation_get_instruction_type(Bytecode_Generator* generator, AST_Node_Type::ENUM op_type, int operand_types)
{
    switch (op_type)
    {
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION:
            if (operand_types == generator->analyser->float_type_index) return Instruction_Type::FLOAT_ADDITION;
            if (operand_types == generator->analyser->int_type_index) return Instruction_Type::INT_ADDITION;
            panic("Not valid, should have been caught!");
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION:
            if (operand_types == generator->analyser->float_type_index) return Instruction_Type::FLOAT_SUBTRACT;
            if (operand_types == generator->analyser->int_type_index) return Instruction_Type::INT_SUBTRACT;
            panic("Not valid, should have been caught!");
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION:
            if (operand_types == generator->analyser->float_type_index) return Instruction_Type::FLOAT_DIVISION;
            if (operand_types == generator->analyser->int_type_index) return Instruction_Type::INT_DIVISION;
            panic("Not valid, should have been caught!");
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION:
            if (operand_types == generator->analyser->float_type_index) return Instruction_Type::FLOAT_MULTIPLY;
            if (operand_types == generator->analyser->int_type_index) return Instruction_Type::INT_MULTIPLY;
            panic("Not valid, should have been caught!");
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO:
            return Instruction_Type::INT_MODULO;
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND:
            return Instruction_Type::BOOLEAN_AND;
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR:
            return Instruction_Type::BOOLEAN_OR;
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL:
            return Instruction_Type::COMPARE_REGISTERS_4BYTE_EQUAL;
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL:
            return Instruction_Type::COMPARE_REGISTERS_4BYTE_NOT_EQUAL;
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
            if (operand_types == generator->analyser->float_type_index) return Instruction_Type::COMPARE_FLOAT_LESS_THAN;
            if (operand_types == generator->analyser->int_type_index) return Instruction_Type::COMPARE_INT_LESS_THAN;
            panic("Not valid, should have been caught!");
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL:
            if (operand_types == generator->analyser->float_type_index) return Instruction_Type::COMPARE_FLOAT_LESS_EQUAL;
            if (operand_types == generator->analyser->int_type_index) return Instruction_Type::COMPARE_INT_LESS_EQUAL;
            panic("Not valid, should have been caught!");
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
            if (operand_types == generator->analyser->float_type_index) return Instruction_Type::COMPARE_FLOAT_GREATER_THAN;
            if (operand_types == generator->analyser->int_type_index) return Instruction_Type::COMPARE_INT_GREATER_THAN;
            panic("Not valid, should have been caught!");
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL:
            if (operand_types == generator->analyser->float_type_index) return Instruction_Type::COMPARE_FLOAT_GREATER_EQUAL;
            if (operand_types == generator->analyser->int_type_index) return Instruction_Type::COMPARE_INT_GREATER_EQUAL;
            panic("Not valid, should have been caught!");
    }
    panic("This should not happen :)\n");
    return Instruction_Type::BOOLEAN_AND;
}

// Returns register index of result
int bytecode_generator_generate_expression(Bytecode_Generator* generator, AST_Node_Index expression_index) 
{
    AST_Node* expression = &generator->analyser->parser->nodes[expression_index];
    Symbol_Table* table = generator->analyser->symbol_tables[generator->analyser->semantic_information[expression_index].symbol_table_index];
    switch (expression->type)
    {
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL: 
    {
        int argument_reg_start = bytecode_generator_generate_register(generator);
        for (int i = 0; i < expression->children.size - 1; i++) {
            bytecode_generator_generate_register(generator);
        }
        for (int i = 0; i < expression->children.size; i++) {
            int argument_index = expression->children[i];
            int arg_reg = bytecode_generator_generate_expression(generator, argument_index);
            if (arg_reg != argument_reg_start + i) {
                bytecode_generator_add_instruction(generator, instruction_make_2(Instruction_Type::MOVE, argument_reg_start + i, arg_reg));
            }
        }
        Function_Call_Location call_loc;
        call_loc.call_instruction_location = bytecode_generator_add_instruction(generator, 
            instruction_make_2(Instruction_Type::CALL, 0, argument_reg_start + expression->children.size));
        call_loc.function_name = expression->name_id;
        dynamic_array_push_back(&generator->function_calls, call_loc);
        int result_register = bytecode_generator_generate_register(generator);
        bytecode_generator_add_instruction(generator, instruction_make_1(Instruction_Type::LOAD_RETURN_VALUE, result_register));
        return result_register;
    }
    case AST_Node_Type::EXPRESSION_LITERAL: 
    {
        int return_type_index;
        Token& token = generator->analyser->parser->lexer->tokens[generator->analyser->parser->token_mapping[expression_index].start_index];
        int result_register = bytecode_generator_generate_register(generator);
        switch (token.type)
        {
        case Token_Type::FLOAT_LITERAL:
            return_type_index = generator->analyser->float_type_index;
            bytecode_generator_add_instruction(generator, instruction_make_2(
                Instruction_Type::LOAD_INTERMEDIATE_4BYTE, result_register, *((int*)((void*)(&token.attribute.float_value)))
            ));
            break;
        case Token_Type::INTEGER_LITERAL:
            return_type_index = generator->analyser->int_type_index;
            bytecode_generator_add_instruction(generator, instruction_make_2(
                Instruction_Type::LOAD_INTERMEDIATE_4BYTE, result_register, token.attribute.integer_value)
            );
            break;
        case Token_Type::BOOLEAN_LITERAL:
            return_type_index = generator->analyser->bool_type_index;
            bytecode_generator_add_instruction(generator, instruction_make_2(
                Instruction_Type::LOAD_INTERMEDIATE_4BYTE, result_register, token.attribute.bool_value ? 1 : 0)
            );
            break;
        default: 
            panic("Should not frigging happen!");
        }
        return result_register;
    }
    case AST_Node_Type::EXPRESSION_VARIABLE_READ: {
        Variable_Location* loc = bytecode_generator_get_variable_loc(generator, expression->name_id);
        return loc->stack_base_offset;
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
        Instruction_Type::ENUM instr_type = binary_operation_get_instruction_type(generator, expression->type, left_type);
        int left_register = bytecode_generator_generate_expression(generator, expression->children[0]);
        int right_register = bytecode_generator_generate_expression(generator, expression->children[1]);
        int result_register = bytecode_generator_generate_register(generator);
        bytecode_generator_add_instruction(generator, instruction_make_3(instr_type, result_register, left_register, right_register));
        return result_register;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE: 
    {
        int operand_type = generator->analyser->semantic_information[expression->children[0]].expression_result_type_index;
        int operand_register = bytecode_generator_generate_expression(generator, expression->children[0]);
        int result_register = bytecode_generator_generate_register(generator);
        if (operand_type == generator->analyser->float_type_index) {
            bytecode_generator_add_instruction(generator, instruction_make_2(Instruction_Type::FLOAT_NEGATE, result_register, operand_register));
        }
        else if (operand_type == generator->analyser->int_type_index) {
            bytecode_generator_add_instruction(generator, instruction_make_2(Instruction_Type::INT_NEGATE, result_register, operand_register));
        }
        else panic("Should not happen");
        return result_register;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT: 
    {
        int operand_register = bytecode_generator_generate_expression(generator, expression->children[0]);
        int result_register = bytecode_generator_generate_register(generator);
        bytecode_generator_add_instruction(generator, instruction_make_2(Instruction_Type::BOOLEAN_NOT, result_register, operand_register));
        return result_register;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF: 
    {
        int operand_register = bytecode_generator_generate_expression(generator, expression->children[0]);
        int result_register = bytecode_generator_generate_register(generator);
        bytecode_generator_add_instruction(generator, instruction_make_2(Instruction_Type::LOAD_REGISTER_ADDRESS, result_register, operand_register));
        return result_register;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE: 
    {
        int operand_register = bytecode_generator_generate_expression(generator, expression->children[0]);
        int result_register = bytecode_generator_generate_register(generator);
        bytecode_generator_add_instruction(generator, instruction_make_2(Instruction_Type::LOAD_FROM_ADDRESS, result_register, operand_register));
        return result_register;
    }
    }

    panic("Shit this is not something that should happen!\n");
    return generator->analyser->error_type_index;
}

void bytecode_generator_generate_statement_block(Bytecode_Generator* generator, AST_Node_Index block_index, bool generate_variables);
void bytecode_generator_generate_statement(Bytecode_Generator* generator, AST_Node_Index statement_index)
{
    AST_Node* statement = &generator->analyser->parser->nodes[statement_index];
    int register_rewind = generator->stack_base_offset;
    switch (statement->type)
    {
    case AST_Node_Type::STATEMENT_BLOCK: {
        bytecode_generator_generate_statement_block(generator, statement->children[0], true);
        break;
    }
    case AST_Node_Type::STATEMENT_BREAK:
    {
        dynamic_array_push_back(&generator->break_instructions_to_fill_out, generator->instructions.size);
        dynamic_array_push_back(&generator->instructions, instruction_make_0(Instruction_Type::JUMP));
        break;
    }
    case AST_Node_Type::STATEMENT_CONTINUE:
    {
        dynamic_array_push_back(&generator->continue_instructions_to_fill_out, generator->instructions.size);
        dynamic_array_push_back(&generator->instructions, instruction_make_0(Instruction_Type::JUMP));
        break;
    }
    case AST_Node_Type::STATEMENT_RETURN: {
        int return_value_register = bytecode_generator_generate_expression(generator, statement->children[0]);
        if (generator->in_main_function) {
            dynamic_array_push_back(&generator->instructions, instruction_make_1(Instruction_Type::EXIT, return_value_register));
        }
        else {
            dynamic_array_push_back(&generator->instructions, instruction_make_1(Instruction_Type::RETURN, return_value_register));
        }
        break;
    }
    case AST_Node_Type::STATEMENT_IF:
    {
        int condition_register = bytecode_generator_generate_expression(generator, statement->children[0]);
        int fill_out_later = bytecode_generator_add_instruction(generator, instruction_make_2(Instruction_Type::JUMP_ON_FALSE, 0, condition_register));
        bytecode_generator_generate_statement_block(generator, statement->children[1], true);
        generator->instructions[fill_out_later].op1 = generator->instructions.size;
        break;
    }
    case AST_Node_Type::STATEMENT_IF_ELSE:
    {
        int condition_register = bytecode_generator_generate_expression(generator, statement->children[0]);
        int jump_else = bytecode_generator_add_instruction(generator, instruction_make_2(Instruction_Type::JUMP_ON_FALSE, 0, condition_register));
        bytecode_generator_generate_statement_block(generator, statement->children[1], true);
        int jump_end = bytecode_generator_add_instruction(generator, instruction_make_0(Instruction_Type::JUMP));
        generator->instructions[jump_else].op1 = generator->instructions.size;
        bytecode_generator_generate_statement_block(generator, statement->children[2], true);
        generator->instructions[jump_end].op1 = generator->instructions.size;
        break;
    }
    case AST_Node_Type::STATEMENT_WHILE:
    {
        int start_index = generator->instructions.size;
        int condition_register = bytecode_generator_generate_expression(generator, statement->children[0]);
        int jump_end = bytecode_generator_add_instruction(generator, instruction_make_2(Instruction_Type::JUMP_ON_FALSE, 0, condition_register));
        bytecode_generator_generate_statement_block(generator, statement->children[1], true);
        bytecode_generator_add_instruction(generator, instruction_make_1(Instruction_Type::JUMP, start_index));
        int end_index = generator->instructions.size;
        generator->instructions[jump_end].op1 = end_index;

        for (int i = 0; i < generator->break_instructions_to_fill_out.size; i++) {
            int brk_index = generator->break_instructions_to_fill_out[i];
            generator->instructions[brk_index].op1 = end_index;
        }
        dynamic_array_reset(&generator->break_instructions_to_fill_out);
        for (int i = 0; i < generator->continue_instructions_to_fill_out.size; i++) {
            int cnd_index = generator->continue_instructions_to_fill_out[i];
            generator->instructions[cnd_index].op1 = start_index;
        }
        dynamic_array_reset(&generator->continue_instructions_to_fill_out);
        break;
    }
    case AST_Node_Type::STATEMENT_EXPRESSION: {
        bytecode_generator_generate_expression(generator, statement->children[0]);
        break;
    }
    case AST_Node_Type::STATEMENT_ASSIGNMENT: 
    {
        int right_side_result_reg = bytecode_generator_generate_expression(generator, statement->children[1]);
        AST_Node* left_side_expr = &generator->analyser->parser->nodes[statement->children[0]];
        if (left_side_expr->type == AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE) {
            int left_side_expr_reg = bytecode_generator_generate_expression(generator, left_side_expr->children[0]);
            bytecode_generator_add_instruction(generator, instruction_make_2(Instruction_Type::STORE_TO_ADDRESS, left_side_expr_reg, right_side_result_reg));
            break;
        }
        else if (left_side_expr->type == AST_Node_Type::EXPRESSION_VARIABLE_READ) {
            Variable_Location* loc = bytecode_generator_get_variable_loc(generator, left_side_expr->name_id);
            bytecode_generator_add_instruction(generator, instruction_make_2(Instruction_Type::MOVE, loc->stack_base_offset, right_side_result_reg));
            break;
        }
        else {
            panic("Shit, i dont know if this can happend, good question!");
        }
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER: {
        int right_side_result_reg = bytecode_generator_generate_expression(generator, statement->children[0]);
        Variable_Location* loc = bytecode_generator_get_variable_loc(generator, statement->name_id);
        bytecode_generator_add_instruction(generator, instruction_make_2(Instruction_Type::MOVE, loc->stack_base_offset, right_side_result_reg));
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION: {
        break; // This is all :)
    }
    }

    generator->stack_base_offset = register_rewind;
    return;
}

void bytecode_generator_generate_statement_block(Bytecode_Generator* generator, AST_Node_Index block_index, bool generate_variables)
{
    AST_Node* block = &generator->analyser->parser->nodes[block_index];
    int size_rollback = generator->variable_locations.size;
    int register_rewind = generator->stack_base_offset;
    if (generate_variables)
    {
        Symbol_Table* table = generator->analyser->symbol_tables[generator->analyser->semantic_information[block_index].symbol_table_index];
        for (int i = 0; i < table->symbols.size; i++)
        {
            Symbol s = table->symbols[i];
            if (s.symbol_type != Symbol_Type::VARIABLE) {
                continue;
            }
            Variable_Location loc;
            loc.stack_base_offset = bytecode_generator_generate_register(generator);
            loc.variable_name = s.name;
            loc.type_index = s.type_index;
            dynamic_array_push_back(&generator->variable_locations, loc);
        }
    }
    for (int i = 0; i < block->children.size; i++) {
        bytecode_generator_generate_statement(generator, block->children[i]);
    }
    dynamic_array_rollback_to_size(&generator->variable_locations, size_rollback);
    generator->stack_base_offset = register_rewind;
}

void bytecode_generator_generate_function_code(Bytecode_Generator* generator, AST_Node_Index function_index)
{
    AST_Node* function = &generator->analyser->parser->nodes[function_index];
    AST_Node* parameter_block = &generator->analyser->parser->nodes[function->children[0]];
    Symbol_Table* table = generator->analyser->symbol_tables[generator->analyser->semantic_information[function_index].symbol_table_index];

    generator->stack_base_offset = 0;
    generator->max_stack_base_offset = 0;
    generator->in_main_function = false;
    if (function->name_id == generator->main_name_id) {
        generator->in_main_function = true;
        generator->entry_point_index = generator->instructions.size;
    }

    // Dont do this when we have globals, since this would reset too much
    dynamic_array_reset(&generator->variable_locations);
    for (int i = 0; i < table->symbols.size; i++)
    {
        // HACK: All parameters are the first symbols in my current symbol_table design
        Symbol s = table->symbols[i];
        if (s.symbol_type != Symbol_Type::VARIABLE) {
            panic("Hack does not work anymore, now do something smarter here!");
        }
        Variable_Location loc;
        if (i < parameter_block->children.size) {
            loc.stack_base_offset = (i - parameter_block->children.size + 1) - 3; // - 3 since return address and old base ptr is also on the stack
        }
        else {
            loc.stack_base_offset = bytecode_generator_generate_register(generator);
        }
        loc.variable_name = s.name;
        loc.type_index = s.type_index;
        dynamic_array_push_back(&generator->variable_locations, loc);
    }

    int function_entry = generator->instructions.size;
    bytecode_generator_generate_statement_block(generator, function->children[2], false);
    if (generator->max_stack_base_offset > generator->maximum_function_stack_depth) {
        generator->maximum_function_stack_depth = generator->max_stack_base_offset;
    }

    Function_Location location;
    location.function_entry_instruction = function_entry;
    location.name_id = function->name_id;
    dynamic_array_push_back(&generator->function_locations, location);
}

void bytecode_generator_generate(Bytecode_Generator* generator, Semantic_Analyser* analyser)
{
    generator->analyser = analyser;
    generator->stack_base_offset = 0;
    dynamic_array_reset(&generator->instructions);
    dynamic_array_reset(&generator->variable_locations);
    dynamic_array_reset(&generator->break_instructions_to_fill_out);
    dynamic_array_reset(&generator->continue_instructions_to_fill_out);
    dynamic_array_reset(&generator->function_calls);
    dynamic_array_reset(&generator->function_locations);
    generator->main_name_id = lexer_add_or_find_identifier_by_string(analyser->parser->lexer, string_create_static("main"));
    generator->entry_point_index = -1;
    generator->maximum_function_stack_depth = 0;
    generator->max_stack_base_offset = 0;

    // Generate code for all functions
    for (int i = 0; i < analyser->parser->nodes[0].children.size; i++) {
        AST_Node_Index function_index = analyser->parser->nodes[0].children[i];
        bytecode_generator_generate_function_code(generator, function_index);
    }

    // Fill out all function calls
    for (int i = 0; i < generator->function_calls.size; i++) {
        Function_Call_Location& call_loc = generator->function_calls[i];
        for (int j = 0; j < generator->function_locations.size; j++) {
            Function_Location& function_loc = generator->function_locations[j];
            if (function_loc.name_id == call_loc.function_name) {
                generator->instructions[call_loc.call_instruction_location].op1 = function_loc.function_entry_instruction;
                break;
            }
        }
    }

    if (generator->entry_point_index == -1) {
        panic("main not found, fag!\n");
    }
}

void bytecode_generator_append_bytecode_to_string(Bytecode_Generator* generator, String* string)
{
    string_append_formated(string, "Functions:\n");
    for (int i = 0; i < generator->function_locations.size; i++) {
        Function_Location& loc = generator->function_locations[i];
        string_append_formated(string, "\t%s: %d\n", 
            lexer_identifer_to_string(generator->analyser->parser->lexer, loc.name_id).characters, loc.function_entry_instruction);
    }
    string_append_formated(string, "Code: \n");
    for (int i = 0; i < generator->instructions.size; i++) 
    {
        Bytecode_Instruction& instruction = generator->instructions[i];
        string_append_formated(string, "%4d: ", i);
        switch (instruction.instruction_type)
        {
        case Instruction_Type::LOAD_INTERMEDIATE_4BYTE:
            string_append_formated(string, "LOAD_INTERMEDIATE_4BYTE           dest=%d, val=%08x\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::MOVE:
            string_append_formated(string, "MOVE                              dest=%d, src=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::STORE_TO_ADDRESS:
            string_append_formated(string, "STORE_TO_ADDRESS                  address_reg=%d, value_reg=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::LOAD_REGISTER_ADDRESS:
            string_append_formated(string, "LOAD_REGISTER_ADDRESS             dest=%d, reg_id=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::LOAD_FROM_ADDRESS:
            string_append_formated(string, "LOAD_FROM_ADDRESS                 dest=%d, address_reg=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::JUMP:
            string_append_formated(string, "JUMP                              dest=%d\n", instruction.op1);
            break;
        case Instruction_Type::JUMP_ON_TRUE:
            string_append_formated(string, "JUMP_ON_TRUE                      dest=%d, cond=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::JUMP_ON_FALSE:
            string_append_formated(string, "JUMP_ON_FALSE                     dest=%d, cond=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::CALL:
            string_append_formated(string, "CALL                              dest=%d, stack_offset=%d\n", instruction.op1, instruction.op2);
            break;
        case Instruction_Type::RETURN:
            string_append_formated(string, "RETURN                            return_reg=%d\n", instruction.op1);
            break;
        case Instruction_Type::LOAD_RETURN_VALUE: 
            string_append_formated(string, "LOAD_RETURN_VALUE                 dst=%d\n", instruction.op1);
            break;
        case Instruction_Type::EXIT: 
            string_append_formated(string, "EXIT                              src=%d\n", instruction.op1);
            break;
        case Instruction_Type::INT_ADDITION:
            string_append_formated(string, "INT_ADDITION                      dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::INT_SUBTRACT:
            string_append_formated(string, "INT_SUBTRACT                      dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::INT_MULTIPLY:
            string_append_formated(string, "INT_MULTIPLY                      dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::INT_DIVISION:
            string_append_formated(string, "INT_DIVISION                      dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::INT_MODULO:
            string_append_formated(string, "INT_MODULO                        dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::INT_NEGATE:
            string_append_formated(string, "INT_NEGATE                        dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::FLOAT_ADDITION:
            string_append_formated(string, "FLOAT_ADDITION                    dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::FLOAT_SUBTRACT:
            string_append_formated(string, "FLOAT_SUBTRACT                    dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::FLOAT_MULTIPLY:
            string_append_formated(string, "FLOAT_MULTIPLY                    dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::FLOAT_DIVISION:
            string_append_formated(string, "FLOAT_DIVISION                    dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::FLOAT_NEGATE:
            string_append_formated(string, "FLOAT_NEGATE                      dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::BOOLEAN_AND:
            string_append_formated(string, "BOOLEAN_AND                       dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::BOOLEAN_OR:
            string_append_formated(string, "BOOLEAN_OR                        dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::BOOLEAN_NOT:
            string_append_formated(string, "BOOLEAN_NOT                       dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::COMPARE_INT_GREATER_THAN:
            string_append_formated(string, "COMPARE_INT_GREATER_THAN          dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::COMPARE_INT_GREATER_EQUAL:
            string_append_formated(string, "COMPARE_INT_GREATER_EQUAL         dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::COMPARE_INT_LESS_THAN:
            string_append_formated(string, "COMPARE_INT_LESS_THAN             dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::COMPARE_INT_LESS_EQUAL:
            string_append_formated(string, "COMPARE_INT_LESS_EQUAL            dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::COMPARE_FLOAT_GREATER_THAN:
            string_append_formated(string, "COMPARE_FLOAT_GREATER_THAN        dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::COMPARE_FLOAT_GREATER_EQUAL:
            string_append_formated(string, "COMPARE_FLOAT_GREATER_EQUAL       dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::COMPARE_FLOAT_LESS_THAN:
            string_append_formated(string, "COMPARE_FLOAT_LESS_THAN           dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::COMPARE_FLOAT_LESS_EQUAL:
            string_append_formated(string, "COMPARE_FLOAT_LESS_EQUAL          dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::COMPARE_REGISTERS_4BYTE_EQUAL:
            string_append_formated(string, "COMPARE_REGISTERS_4BYTE_EQUAL     dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        case Instruction_Type::COMPARE_REGISTERS_4BYTE_NOT_EQUAL:
            string_append_formated(string, "COMPARE_REGISTERS_4BYTE_NOT_EQUAL dst=%d, src1=%d, src2=%d\n", instruction.op1, instruction.op2, instruction.op3);
            break;
        }
    }
}

