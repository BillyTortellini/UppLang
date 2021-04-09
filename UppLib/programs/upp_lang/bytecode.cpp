#include "bytecode.hpp"

Bytecode_Generator bytecode_generator_create()
{
    Bytecode_Generator result;
    result.instructions = dynamic_array_create_empty<Bytecode_Instruction>(64);
    result.variable_locations = dynamic_array_create_empty<Variable_Location>(64);
    result.break_instructions_to_fill_out = dynamic_array_create_empty<int>(64);
    result.continue_instructions_to_fill_out = dynamic_array_create_empty<int>(64);
    return result;
}

void bytecode_generator_destroy(Bytecode_Generator* generator)
{
    dynamic_array_destroy(&generator->instructions);
    dynamic_array_destroy(&generator->variable_locations);
    dynamic_array_destroy(&generator->break_instructions_to_fill_out);
    dynamic_array_destroy(&generator->continue_instructions_to_fill_out);
} 

Bytecode_Instruction instruction_make(Instruction_Type::ENUM type) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    return instr;
}

Bytecode_Instruction instruction_make_1(Instruction_Type::ENUM type, int src_1) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    instr.source_operand_1 = src_1;
    return instr;
}

Bytecode_Instruction instruction_make_unary(Instruction_Type::ENUM type, int source_reg, int dest_reg) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    instr.source_operand_1 = source_reg;
    instr.destination_operand = dest_reg;
    return instr;
}

Bytecode_Instruction instruction_make_bin_op(Instruction_Type::ENUM type, int source_reg1, int source_reg2, int dest_reg) {
    Bytecode_Instruction instr;
    instr.instruction_type = type;
    instr.source_operand_1 = source_reg1;
    instr.source_operand_2 = source_reg2;
    instr.destination_operand = dest_reg;
    return instr;
}

int bytecode_generator_generate_expression(Bytecode_Generator* generator, AST_Node_Index expression_index) {

    return -1;
}

Variable_Location* bytecode_generator_get_variable_loc(Bytecode_Generator* generator, int name_id)
{
    for (int i = generator->variable_locations.size - 1; i <= 0; i--) {
        Variable_Location* loc = &generator->variable_locations[i];
        if (loc->variable_name == name_id)
            return loc;
    }
    panic("Should not happen after semantic analysis");
    return 0;
}

void bytecode_generator_generate_statement_block(Bytecode_Generator* generator, AST_Node_Index block_index);
void bytecode_generator_generate_statement(Bytecode_Generator* generator, AST_Node_Index statement_index)
{
    AST_Node* statement = &generator->analyser->parser->nodes[statement_index];
    switch (statement->type)
    {
    case AST_Node_Type::STATEMENT_BLOCK: {
        bytecode_generator_generate_statement_block(generator, statement->children[0]);
        break;
    }
    case AST_Node_Type::STATEMENT_BREAK: 
    {
        dynamic_array_push_back(&generator->break_instructions_to_fill_out, generator->instructions.size);
        dynamic_array_push_back(&generator->instructions, instruction_make(Instruction_Type::JUMP));
        break;
    }
    case AST_Node_Type::STATEMENT_CONTINUE: 
    {
        dynamic_array_push_back(&generator->continue_instructions_to_fill_out, generator->instructions.size);
        dynamic_array_push_back(&generator->instructions, instruction_make(Instruction_Type::JUMP));
        break;
    }
    case AST_Node_Type::STATEMENT_RETURN: {
        dynamic_array_push_back(&generator->instructions, instruction_make(Instruction_Type::RETURN));
        break;
    }
    case AST_Node_Type::STATEMENT_IF: 
    {
        int cond_reg = bytecode_generator_generate_expression(generator, statement->children[0]);
        int jump_cond_reg = generator->next_free_register++;
        dynamic_array_push_back(&generator->instructions, instruction_make_unary(Instruction_Type::BOOLEAN_NOT, cond_reg, jump_cond_reg));
        int fill_out_later = generator->instructions.size;
        dynamic_array_push_back(&generator->instructions, instruction_make_1(Instruction_Type::JUMP_CONDITIONAL, jump_cond_reg));
        bytecode_generator_generate_statement_block(generator, statement->children[1]);
        generator->instructions[fill_out_later].source_operand_2 = generator->instructions.size;
        break;
    }
    case AST_Node_Type::STATEMENT_IF_ELSE: 
    {
        int cond_reg = bytecode_generator_generate_expression(generator, statement->children[0]);
        int jump_cond_reg = generator->next_free_register++;
        dynamic_array_push_back(&generator->instructions, instruction_make_unary(Instruction_Type::BOOLEAN_NOT, cond_reg, jump_cond_reg));
        int jump_after_if_block = generator->instructions.size;
        dynamic_array_push_back(&generator->instructions, instruction_make_1(Instruction_Type::JUMP_CONDITIONAL, jump_cond_reg));
        bytecode_generator_generate_statement_block(generator, statement->children[1]);
        int jump_after_else = generator->instructions.size;
        dynamic_array_push_back(&generator->instructions, instruction_make(Instruction_Type::JUMP));
        generator->instructions[jump_after_if_block].source_operand_2 = generator->instructions.size;
        bytecode_generator_generate_statement_block(generator, statement->children[2]);
        generator->instructions[jump_after_else].source_operand_1 = generator->instructions.size;
        break;
    }
    case AST_Node_Type::STATEMENT_WHILE: 
    {
        int cond_instruction_index = generator->instructions.size;
        int cond_reg = bytecode_generator_generate_expression(generator, statement->children[0]);
        int jump_cond_reg = generator->next_free_register++;
        dynamic_array_push_back(&generator->instructions, instruction_make_unary(Instruction_Type::BOOLEAN_NOT, cond_reg, jump_cond_reg));
        int jump_after_while = generator->instructions.size;
        dynamic_array_push_back(&generator->instructions, instruction_make_1(Instruction_Type::JUMP_CONDITIONAL, jump_cond_reg));
        bytecode_generator_generate_statement_block(generator, statement->children[1]);
        dynamic_array_push_back(&generator->instructions, instruction_make_1(Instruction_Type::JUMP, cond_instruction_index));
        generator->instructions[jump_after_while].source_operand_2 = generator->instructions.size;

        for (int i = 0; i < generator->break_instructions_to_fill_out.size; i++) {
            int brk_index = generator->break_instructions_to_fill_out[i];
            generator->instructions[brk_index].source_operand_1 = jump_after_while;
        }
        dynamic_array_reset(&generator->break_instructions_to_fill_out);
        for (int i = 0; i < generator->continue_instructions_to_fill_out.size; i++) {
            int cnd_index = generator->continue_instructions_to_fill_out[i];
            generator->instructions[cnd_index].source_operand_1 = cond_instruction_index;
        }
        dynamic_array_reset(&generator->continue_instructions_to_fill_out);
        break;
    }
    case AST_Node_Type::STATEMENT_EXPRESSION: {
        bytecode_generator_generate_expression(generator, statement->children[0]);
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_ASSIGNMENT: 
    {
        Variable_Location* loc = bytecode_generator_get_variable_loc(generator, statement->name_id);
        int variable_register;
        if (loc->in_register) variable_register = loc->register_index;
        else {
            variable_register = generator->next_free_register++;
            loc->in_register = true;
            loc->on_stack = false;
            loc->register_index = variable_register;
            dynamic_array_push_back(&generator->instructions, 
                instruction_make_unary(Instruction_Type::LOAD_FROM_STACK, loc->stack_base_offset, variable_register));
        }
        dynamic_array_push_back(&generator->instructions,
            instruction_make_unary(Instruction_Type::LOAD_FROM_STACK, loc->stack_base_offset, variable_register));
        int result_reg = bytecode_generator_generate_expression(generator, statement->children[0]);
        dynamic_array_push_back(&generator->instructions,
            instruction_make_unary(Instruction_Type::MOVE, result_reg, variable_register));
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN: {
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER: {
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION: {
        break; // This is all :)
    }
    }
}

void bytecode_generator_generate_statement_block(Bytecode_Generator* generator, AST_Node_Index block_index)
{
    AST_Node* block = &generator->analyser->parser->nodes[block_index];
    int size_rollback = generator->variable_locations.size;
    for (int i = 0; i < block->children.size; i++)
    {
        dynamic_array_rollback_to_size(&generator->variable_locations, size_rollback);
        bytecode_generator_generate_statement(generator, block->children[i]);
    }
}

void bytecode_generator_generate_function_code(Bytecode_Generator* generator, AST_Node_Index function_index)
{
    AST_Node* function = &generator->analyser->parser->nodes[function_index];
    AST_Node* parameter_block = &generator->analyser->parser->nodes[function->children[0]];
    Symbol_Table* table = generator->analyser->symbol_tables[generator->analyser->node_to_table_mappings[function_index]];
    for (int i = 0; i < parameter_block->children.size; i++) 
    {
        // Maybe do something here if we have globals section in bytecode
        dynamic_array_reset(&generator->variable_locations);
        // HACK: All parameters are the first symbols in my current symbol_table design
        Symbol s = table->symbols[i];
        if (s.symbol_type != Symbol_Type::VARIABLE) {
            panic("Hack does not work anymore, now do something smarter here!");
        }
        Variable_Location loc;
        loc.in_register = false;
        loc.on_stack = true;
        loc.stack_base_offset = parameter_block->children.size - i - 1;
        loc.variable_name = s.name;
        loc.variable_type = s.variable_type;
        dynamic_array_push_back(&generator->variable_locations, loc);
        generator->stack_base_offset += 1;
    }
    generator->next_free_register = 1; // Register 0 is for return values
    bytecode_generator_generate_statement_block(generator, function->children[1]);
}

void bytecode_generator_generate(Bytecode_Generator* generator, Semantic_Analyser* analyser)
{
    generator->analyser = analyser;
    generator->stack_base_offset = 0;
    generator->next_free_register = 0;
    dynamic_array_reset(&generator->instructions);
    dynamic_array_reset(&generator->variable_locations);
    dynamic_array_reset(&generator->break_instructions_to_fill_out);
    dynamic_array_reset(&generator->continue_instructions_to_fill_out);

    // Generate code for all functions
    for (int i = 0; i < analyser->parser->nodes[0].children.size; i++) 
    {
        AST_Node* function = &analyser->parser->nodes[analyser->parser->nodes[0].children[i]];
    }
}

