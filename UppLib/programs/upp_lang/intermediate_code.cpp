#include "intermediate_code.hpp"

void intermediate_generator_generate_function_code(Intermediate_Generator* generator, int function_node_index)
{
    Intermediate_Function* im_function = &generator->functions[function_node_index];
    int function_node_index = generator->function_to_ast_node_mapping[function_node_index];
    AST_Node* function = &generator->analyser->parser->nodes[function_node_index];
    Symbol_Table* function_table = generator->analyser->symbol_tables[generator->analyser->semantic_information[function_node_index].symbol_table_index];
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
}

Intermediate_Generator intermediate_generator_create()
{
    Intermediate_Generator result;
    result.functions = dynamic_array_create_empty<Intermediate_Function>(64);
    result.variable_mappings = dynamic_array_create_empty<Variable_Mapping>(64);
    return result;
}

void intermediate_generator_destroy(Intermediate_Generator* generator)
{
    for (int i = 0; i < generator->functions.size; i++) {
        intermediate_function_destroy(&generator->functions[i]);
    }
    dynamic_array_destroy(&generator->functions);
    dynamic_array_destroy(&generator->variable_mappings);
}

void intermediate_generator_generate(Intermediate_Generator* generator, Semantic_Analyser* analyser)
{
    generator->analyser = analyser;
    // TODO: Do reset better
    intermediate_generator_destroy(generator);
    *generator = intermediate_generator_create();

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