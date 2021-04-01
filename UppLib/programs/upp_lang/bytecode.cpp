#include "bytecode.hpp"

Bytecode bytecode_create()
{
    Bytecode result;
    result.entry_point_index = -1;
    result.instructions = dynamic_array_create_empty<Bytecode_Instruction>(256);
    return result;
}

void bytecode_destroy(Bytecode* bytecode) {
    dynamic_array_destroy(&bytecode->instructions);
}

struct Variable_Location
{
    int variable_name;
    Variable_Type::ENUM symbol_type;
    bool in_register;
    bool on_stack;
    int register_index; 
    int stack_base_offset;
};

struct Bytecode_Generator
{
    Bytecode bytecode;
    Lexer* Lexer;
    DynamicArray<Variable_Location> symbol_table;
};

Bytecode_Generator bytecode_generator_create(Lexer* Lexer) 
{
    Bytecode_Generator result;
    result.bytecode = bytecode_create();
    result.Lexer = Lexer;
    result.symbol_table = dynamic_array_create_empty<Variable_Location>(64);
    return result;
}


void bytecode_generator_destroy(Bytecode_Generator* generator) {
    dynamic_array_destroy(&generator->symbol_table);
}

Variable_Location* bytecode_generator_find_variable(Bytecode_Generator* generator, int variable_name)
{
    for (int i = generator->symbol_table.size - 1; i >= 0; i--) {
        if (generator->symbol_table[i].variable_name == variable_name) {
            return &generator->symbol_table[i];
        }
    }
    return 0;
}

void bytecode_generator_define_variable(Bytecode_Generator* generator, Variable_Location location)
{
    if (bytecode_generator_find_variable(generator, location.variable_name) != 0) {
        panic("Variable already defined, should be catched by semantic analysis :)");
        return;
    }
    dynamic_array_push_back(&generator->symbol_table, location);
}

void bytecode_generator_parse_function(Bytecode_Generator* generator, Ast_Node_Function* function)
{
    // Stack grows in positive direction
    // If i knew the types of all this shit (Semantic analysis, I would not need to think to much about all this shit...)
    for (int i = 0; i < function->parameters.size; i++) {
        Variable_Location loc;
        loc.variable_name = function->parameters[i].name_id;
    }
}

Bytecode bytecode_create_from_ast(Ast_Node_Root* root, Lexer* Lexer)
{
    Bytecode_Generator generator = bytecode_generator_create(Lexer);
    SCOPE_EXIT(bytecode_generator_destroy(&generator));
    
    for (int i = 0; i < root->functions.size; i++)
    {
        Ast_Node_Function* function = &root->functions[i];
        bytecode_generator_parse_function(&generator, function);
    }

    return generator.bytecode;
}