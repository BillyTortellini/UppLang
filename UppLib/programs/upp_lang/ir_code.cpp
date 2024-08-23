#include "ir_code.hpp"
#include "compiler.hpp"
#include "bytecode_generator.hpp"
#include "ast.hpp"
#include "symbol_table.hpp"

void ir_generator_generate_block(IR_Code_Block* ir_block, AST::Code_Block* ast_block);
IR_Data_Access ir_generator_generate_expression(IR_Code_Block* ir_block, AST::Expression* expression);
void ir_generator_generate_statement(AST::Statement* statement, IR_Code_Block* ir_block);



// IR Program
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
        ir_code_block_destroy(instruction->options.while_instr.condition_code);
        break;
    }
    case IR_Instruction_Type::BLOCK: {
        ir_code_block_destroy(instruction->options.block);
        break;
    }
    case IR_Instruction_Type::SWITCH: {
        for (int i = 0; i < instruction->options.switch_instr.cases.size; i++) {
            ir_code_block_destroy(instruction->options.switch_instr.cases[i].block);
        }
        dynamic_array_destroy(&instruction->options.switch_instr.cases);
        ir_code_block_destroy(instruction->options.switch_instr.default_block);
        break;
    }
    case IR_Instruction_Type::RETURN:
    case IR_Instruction_Type::MOVE:
    case IR_Instruction_Type::CAST:
    case IR_Instruction_Type::ADDRESS_OF:
    case IR_Instruction_Type::UNARY_OP:
    case IR_Instruction_Type::BINARY_OP:
    case IR_Instruction_Type::LABEL:
    case IR_Instruction_Type::GOTO:
        break;
    default: panic("Lul");
    }
}

IR_Code_Block* ir_code_block_create(IR_Function* function)
{
    IR_Code_Block* block = new IR_Code_Block();
    block->function = function;
    block->instructions = dynamic_array_create<IR_Instruction>(64);
    block->registers = dynamic_array_create<Datatype*>(32);
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

IR_Function* ir_function_create(Datatype_Function* signature, ModTree_Function* origin_func)
{
    IR_Function* function = new IR_Function();
    function->code = ir_code_block_create(function);
    function->function_type = signature;
    function->program = ir_generator.program;
    function->origin = origin_func;
    dynamic_array_push_back(&ir_generator.program->functions, function);

    if (origin_func != 0) {
        auto worked = hashtable_insert_element(&ir_generator.function_mapping, origin_func, function);
        assert(worked, "");
        Function_Stub stub;
        stub.mod_func = origin_func;
        stub.ir_func = function;
        dynamic_array_push_back(&ir_generator.queue_functions, stub);
    }
    return function;
}

void ir_function_destroy(IR_Function* function)
{
    ir_code_block_destroy(function->code);
    delete function;
}

IR_Program* ir_program_create(Type_System* type_system)
{
    IR_Program* result = new IR_Program;
    result->entry_function = 0;
    result->functions = dynamic_array_create<IR_Function*>(32);
    return result;
}

void ir_program_destroy(IR_Program* program)
{
    for (int i = 0; i < program->functions.size; i++) {
        auto function = program->functions[i];
        ir_function_destroy(function);
    }
    dynamic_array_destroy(&program->functions);
    delete program;
}



// To_String
void ir_data_access_append_to_string(IR_Data_Access* access, String* string, IR_Code_Block* current_block)
{
    switch (access->type)
    {
    case IR_Data_Access_Type::CONSTANT: {
        Upp_Constant* constant = &compiler.constant_pool.constants[access->index];
        string_append_formated(string, "Constant #%d ", access->index);
        datatype_append_to_string(string, constant->type);
        string_append_formated(string, " ", access->index);
        datatype_append_value_to_string(constant->type, constant->memory, string);
        break;
    }
    case IR_Data_Access_Type::GLOBAL_DATA: {
        string_append_formated(string, "Global #%d, type: ", access->index);
        datatype_append_to_string(string, ir_data_access_get_type(access));
        break;
    }
    case IR_Data_Access_Type::PARAMETER: {
        Datatype* sig = access->option.function->function_type->parameters[access->index].type;
        string_append_formated(string, "Param #%d, type: ", access->index);
        datatype_append_to_string(string, sig);
        break;
    }
    case IR_Data_Access_Type::REGISTER: {
        Datatype* sig = access->option.definition_block->registers[access->index];
        string_append_formated(string, "Register #%d, type: ", access->index);
        datatype_append_to_string(string, sig);
        if (access->option.definition_block != current_block) {
            string_append_formated(string, " (Not local)", access->index);
        }
        break;
    }
    }

    if (access->is_memory_access) {
        string_append_formated(string, " MEMORY_ACCESS");
    }
}

void indent_string(String* string, int indentation) {
    for (int i = 0; i < indentation; i++) {
        string_append_formated(string, "    ");
    }
}

void ir_code_block_append_to_string(IR_Code_Block* code_block, String* string, int indentation);
void ir_instruction_append_to_string(IR_Instruction* instruction, String* string, int indentation, IR_Code_Block* code_block)
{
    indent_string(string, indentation);
    switch (instruction->type)
    {
    case IR_Instruction_Type::ADDRESS_OF:
    {
        IR_Instruction_Address_Of* address_of = &instruction->options.address_of;
        string_append_formated(string, "ADDRESS_OF\n");
        indent_string(string, indentation + 1);
        if (address_of->type != IR_Instruction_Address_Of_Type::FUNCTION) {
            string_append_formated(string, "src: ");
            ir_data_access_append_to_string(&address_of->source, string, code_block);
            string_append_formated(string, "\n");
            indent_string(string, indentation + 1);
        }
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(&address_of->destination, string, code_block);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "type: ");
        switch (address_of->type)
        {
        case IR_Instruction_Address_Of_Type::ARRAY_ELEMENT:
            string_append_formated(string, "ARRAY_ELEMENT index: ");
            ir_data_access_append_to_string(&address_of->options.index_access, string, code_block);
            break;
        case IR_Instruction_Address_Of_Type::DATA:
            string_append_formated(string, "DATA");
            break;
        case IR_Instruction_Address_Of_Type::FUNCTION:
            string_append_formated(string, "FUNCTION");
            break;
        case IR_Instruction_Address_Of_Type::STRUCT_MEMBER:
            string_append_formated(string, "STRUCT_MEMBER, offset: %d, type: ", address_of->options.member.offset);
            datatype_append_to_string(string, address_of->options.member.type);
            break;
        }
        break;
    }
    case IR_Instruction_Type::BINARY_OP:
    {
        string_append_formated(string, "BINARY_OP ");
        switch (instruction->options.binary_op.type)
        {
        case AST::Binop::ADDITION:
            string_append_formated(string, "ADDITION");
            break;
        case AST::Binop::AND:
            string_append_formated(string, "AND");
            break;
        case AST::Binop::DIVISION:
            string_append_formated(string, "DIVISION");
            break;
        case AST::Binop::EQUAL:
            string_append_formated(string, "EQUAL");
            break;
        case AST::Binop::GREATER:
            string_append_formated(string, "GREATER");
            break;
        case AST::Binop::GREATER_OR_EQUAL:
            string_append_formated(string, "GREATER_OR_EQUAL");
            break;
        case AST::Binop::LESS:
            string_append_formated(string, "LESS");
            break;
        case AST::Binop::LESS_OR_EQUAL:
            string_append_formated(string, "LESS_OR_EQUAL");
            break;
        case AST::Binop::MODULO:
            string_append_formated(string, "MODULO");
            break;
        case AST::Binop::MULTIPLICATION:
            string_append_formated(string, "MULTIPLICATION ");
            break;
        case AST::Binop::NOT_EQUAL:
            string_append_formated(string, "NOT_EQUAL");
            break;
        case AST::Binop::OR:
            string_append_formated(string, "OR ");
            break;
        case AST::Binop::SUBTRACTION:
            string_append_formated(string, "SUBTRACTION");
            break;
        case AST::Binop::POINTER_EQUAL:
            string_append_formated(string, "POINTER EQUAL");
            break;
        case AST::Binop::POINTER_NOT_EQUAL:
            string_append_formated(string, "POINTER NOT_EQUAL");
            break;
        default: panic("");
        }

        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "left: ");
        ir_data_access_append_to_string(&instruction->options.binary_op.operand_left, string, code_block);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "right: ");
        ir_data_access_append_to_string(&instruction->options.binary_op.operand_right, string, code_block);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(&instruction->options.binary_op.destination, string, code_block);
        break;
    }
    case IR_Instruction_Type::BLOCK: {
        string_append_formated(string, "BLOCK\n");
        ir_code_block_append_to_string(instruction->options.block, string, indentation + 1);
        break;
    }
    case IR_Instruction_Type::GOTO: {
        string_append_formated(string, "GOTO %d", instruction->options.label_index);
        break;
    }
    case IR_Instruction_Type::LABEL: {
        string_append_formated(string, "LABEL %d", instruction->options.label_index);
        break;
    }
    case IR_Instruction_Type::CAST:
    {
        IR_Instruction_Cast* cast = &instruction->options.cast;
        string_append_formated(string, "CAST ");
        switch (cast->type)
        {
        case IR_Cast_Type::FLOATS:
            string_append_formated(string, "FLOATS");
            break;
        case IR_Cast_Type::FLOAT_TO_INT:
            string_append_formated(string, "FLOAT_TO_INT");
            break;
        case IR_Cast_Type::INT_TO_FLOAT:
            string_append_formated(string, "INT_TO_FLOAT");
            break;
        case IR_Cast_Type::INTEGERS:
            string_append_formated(string, "INTEGERS");
            break;
        case IR_Cast_Type::POINTERS:
            string_append_formated(string, "POINTERS");
            break;
        case IR_Cast_Type::POINTER_TO_U64:
            string_append_formated(string, "POINTER_TO_U64");
            break;
        case IR_Cast_Type::U64_TO_POINTER:
            string_append_formated(string, "U64_TO_POINTER");
            break;
        case IR_Cast_Type::ENUM_TO_INT:
            string_append_formated(string, "ENUM_TO_INT");
            break;
        case IR_Cast_Type::INT_TO_ENUM:
            string_append_formated(string, "INT_TO_ENUM");
            break;
        default: panic("HEY");
        }

        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "src: ");
        ir_data_access_append_to_string(&cast->source, string, code_block);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(&cast->destination, string, code_block);
        break;
    }
    case IR_Instruction_Type::FUNCTION_CALL:
    {
        IR_Instruction_Call* call = &instruction->options.call;
        string_append_formated(string, "FUNCTION_CALL\n");
        indent_string(string, indentation + 1);

        Datatype_Function* function_sig;
        switch (call->call_type)
        {
        case IR_Instruction_Call_Type::FUNCTION_CALL:
            function_sig = call->options.function->function_type;
            break;
        case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL: {
            auto type = ir_data_access_get_type(&call->options.pointer_access);
            assert(type->type == Datatype_Type::FUNCTION, "Function pointer call must be of function type!");
            function_sig = downcast<Datatype_Function>(type);
            break;
        }
        case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
            function_sig = 0;
            break;
        case IR_Instruction_Call_Type::EXTERN_FUNCTION_CALL: {
            auto type = call->options.extern_function.function_signature;
            assert(type->type == Datatype_Type::FUNCTION, "External function type must be of function type!");
            function_sig = downcast<Datatype_Function>(type);
            break;
        }
        default:
            panic("Hey");
            return;
        }
        if (function_sig != 0) {
            if (function_sig->return_type.available) {
                string_append_formated(string, "dst: ");
                ir_data_access_append_to_string(&call->destination, string, code_block);
                string_append_formated(string, "\n");
                indent_string(string, indentation + 1);
            }
        }
        string_append_formated(string, "args: (%d)\n", call->arguments.size);
        for (int i = 0; i < call->arguments.size; i++) {
            indent_string(string, indentation + 2);
            ir_data_access_append_to_string(&call->arguments[i], string, code_block);
            string_append_formated(string, "\n");
        }

        indent_string(string, indentation + 1);
        string_append_formated(string, "Call-Type: ");
        switch (call->call_type)
        {
        case IR_Instruction_Call_Type::FUNCTION_CALL:
            string_append_formated(string, "FUNCTION (later)");
            break;
        case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL:
            string_append_formated(string, "FUNCTION_POINTER_CALL, access: ");
            ir_data_access_append_to_string(&call->options.pointer_access, string, code_block);
            break;
        case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
            string_append_formated(string, "HARDCODED_FUNCTION_CALL, type: ");
            hardcoded_type_append_to_string(string, call->options.hardcoded.type);
            break;
        case IR_Instruction_Call_Type::EXTERN_FUNCTION_CALL:
            string_append_formated(string, "EXTERN_FUNCTION_CALL, type: ");
            datatype_append_to_string(string, call->options.extern_function.function_signature);
            break;
        }
        break;
    }
    case IR_Instruction_Type::IF: {
        string_append_formated(string, "IF ");
        ir_data_access_append_to_string(&instruction->options.if_instr.condition, string, code_block);
        string_append_formated(string, "\n");
        ir_code_block_append_to_string(instruction->options.if_instr.true_branch, string, indentation + 1);
        indent_string(string, indentation);
        string_append_formated(string, "ELSE\n");
        ir_code_block_append_to_string(instruction->options.if_instr.false_branch, string, indentation + 1);
        break;
    }
    case IR_Instruction_Type::MOVE: {
        string_append_formated(string, "MOVE\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "src: ");
        ir_data_access_append_to_string(&instruction->options.move.source, string, code_block);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(&instruction->options.move.destination, string, code_block);
        break;
    }
    case IR_Instruction_Type::SWITCH: 
    {
        Datatype* value_type = ir_data_access_get_type(&instruction->options.switch_instr.condition_access);
        Datatype_Enum* enum_type = 0;
        if (value_type->type == Datatype_Type::STRUCT) {
            auto structure = downcast<Datatype_Struct>(value_type);
            assert(structure->struct_type == AST::Structure_Type::UNION, "");
            enum_type = downcast<Datatype_Enum>(structure->tag_member.type);
        }
        else {
            assert(value_type->type == Datatype_Type::ENUM, "If not union, this must be an enum");
            enum_type = downcast<Datatype_Enum>(value_type);
        }
        string_append_formated(string, "SWITCH\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "Condition access: ");
        ir_data_access_append_to_string(&instruction->options.switch_instr.condition_access, string, code_block);
        string_append_formated(string, "\n");
        for (int i = 0; i < instruction->options.switch_instr.cases.size; i++) {
            IR_Switch_Case* switch_case = &instruction->options.switch_instr.cases[i];
            indent_string(string, indentation + 1);
            Optional<Enum_Member> member = enum_type_find_member_by_value(enum_type, switch_case->value);
            assert(member.available, "");
            string_append_formated(string, "Case %s: \n", member.value.name->characters);
            ir_code_block_append_to_string(switch_case->block, string, indentation + 2);
        }
        indent_string(string, indentation + 1);
        string_append_formated(string, "Default case: \n");
        ir_code_block_append_to_string(instruction->options.switch_instr.default_block, string, indentation + 2);
        break;
    }
    case IR_Instruction_Type::WHILE: {
        string_append_formated(string, "WHILE\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "Condition code: \n");
        ir_code_block_append_to_string(instruction->options.while_instr.condition_code, string, indentation + 2);
        indent_string(string, indentation + 1);
        string_append_formated(string, "Condition access: ");
        ir_data_access_append_to_string(&instruction->options.while_instr.condition_access, string, code_block);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "Body: \n");
        ir_code_block_append_to_string(instruction->options.while_instr.code, string, indentation + 2);
        break;
    }
    case IR_Instruction_Type::RETURN: {
        IR_Instruction_Return* return_instr = &instruction->options.return_instr;
        switch (return_instr->type)
        {
        case IR_Instruction_Return_Type::EXIT:
            string_append_formated(string, "EXIT ");
            exit_code_append_to_string(string, return_instr->options.exit_code);
            break;
        case IR_Instruction_Return_Type::RETURN_DATA:
            string_append_formated(string, "RETURN ");
            ir_data_access_append_to_string(&return_instr->options.return_value, string, code_block);
            break;
        case IR_Instruction_Return_Type::RETURN_EMPTY:
            string_append_formated(string, "RETURN");
            break;
        }
        break;
    }
    case IR_Instruction_Type::UNARY_OP:
    {
        string_append_formated(string, "Unary_OP ");
        switch (instruction->options.unary_op.type)
        {
        case IR_Instruction_Unary_OP_Type::NEGATE:
            string_append_formated(string, "NEGATE");
            break;
        case IR_Instruction_Unary_OP_Type::NOT:
            string_append_formated(string, "NOT");
            break;
        }

        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(&instruction->options.unary_op.destination, string, code_block);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "operand: ");
        ir_data_access_append_to_string(&instruction->options.unary_op.source, string, code_block);
        break;
    }
    default: panic("What");
    }
}

void ir_code_block_append_to_string(IR_Code_Block* code_block, String* string, int indentation)
{
    indent_string(string, indentation);
    string_append_formated(string, "Registers:\n");
    for (int i = 0; i < code_block->registers.size; i++) {
        indent_string(string, indentation + 1);
        string_append_formated(string, "#%d: ", i);
        datatype_append_to_string(string, code_block->registers[i]);
        string_append_formated(string, "\n");
    }
    indent_string(string, indentation);
    string_append_formated(string, "Instructions:\n");
    for (int i = 0; i < code_block->instructions.size; i++) {
        ir_instruction_append_to_string(&code_block->instructions[i], string, indentation + 1, code_block);
        string_append_formated(string, "\n");
    }
}

void ir_function_append_to_string(IR_Function* function, String* string, int indentation)
{
    indent_string(string, indentation);
    string_append_formated(string, "Function-Type:");
    datatype_append_to_string(string, upcast(function->function_type));
    string_append_formated(string, "\n");
    ir_code_block_append_to_string(function->code, string, indentation);
}

void ir_program_append_to_string(IR_Program* program, String* string)
{
    string_append_formated(string, "Program Dump:\n-----------------\n");
    for (int i = 0; i < program->functions.size; i++)
    {
        string_append_formated(string, "Function #%d ", i);
        ir_function_append_to_string(program->functions[i], string, 0);
        string_append_formated(string, "\n");
    }
}



// Data Access
Datatype* ir_data_access_get_type(IR_Data_Access* access)
{
    Datatype* sig = 0;
    switch (access->type)
    {
    case IR_Data_Access_Type::GLOBAL_DATA:
        sig = ir_generator.modtree->globals[access->index]->type;
        break;
    case IR_Data_Access_Type::CONSTANT:
        sig = compiler.constant_pool.constants[access->index].type;
        break;
    case IR_Data_Access_Type::REGISTER:
        sig = access->option.definition_block->registers[access->index];
        break;
    case IR_Data_Access_Type::PARAMETER:
        sig = access->option.function->function_type->parameters[access->index].type;
        break;
    case IR_Data_Access_Type::NOTHING:
        sig = upcast(compiler.type_system.predefined_types.u8_type);
        break;
    default: panic("Hey!");
    }
    if (access->is_memory_access) {
        assert(datatype_get_non_const_type(sig)->type == Datatype_Type::POINTER, "");
        return downcast<Datatype_Pointer>(datatype_get_non_const_type(sig))->element_type;
    }
    return datatype_get_non_const_type(sig);
}

IR_Data_Access ir_data_access_create_nothing()
{
    IR_Data_Access access;
    access.index = 0;
    access.is_memory_access = false;
    access.type = IR_Data_Access_Type::NOTHING;
    return access;
}

IR_Data_Access ir_data_access_create_global(ModTree_Global* global)
{
    IR_Data_Access access;
    access.type = IR_Data_Access_Type::GLOBAL_DATA;
    access.index = global->index;
    access.is_memory_access = false;
    return access;
}

IR_Data_Access ir_data_access_create_intermediate(IR_Code_Block* block, Datatype* signature)
{
    assert(block != 0, "");
    IR_Data_Access access;
    access.is_memory_access = false;
    access.type = IR_Data_Access_Type::REGISTER;
    access.option.definition_block = block;
    assert(signature->type != Datatype_Type::UNKNOWN_TYPE, "Cannot have register with unknown type");
    assert(!type_size_is_unfinished(signature), "Cannot have register with 0 size!");
    dynamic_array_push_back(&block->registers, signature);
    access.index = block->registers.size - 1;
    return access;
}

IR_Data_Access ir_data_access_create_dereference(IR_Code_Block* block, IR_Data_Access access)
{
    if ((int)access.type < 0 || (int)access.type > 6) {
        panic("HEY");
    }
    if (!access.is_memory_access) {
        access.is_memory_access = true;
        return access;
    }
    Datatype* ptr_type = datatype_get_non_const_type(ir_data_access_get_type(&access));
    assert(ptr_type->type == Datatype_Type::POINTER, "");
    IR_Data_Access ptr_access = ir_data_access_create_intermediate(block, ptr_type);
    IR_Instruction instr;
    instr.type = IR_Instruction_Type::MOVE;
    instr.options.move.destination = ptr_access;
    instr.options.move.source = access;
    dynamic_array_push_back(&block->instructions, instr);

    ptr_access.is_memory_access = true;
    return ptr_access;
}

IR_Data_Access ir_data_access_create_address_of(IR_Code_Block* block, IR_Data_Access access)
{
    if (access.is_memory_access) {
        access.is_memory_access = false;
        return access;
    }
    IR_Instruction instr;
    instr.type = IR_Instruction_Type::ADDRESS_OF;
    instr.options.address_of.type = IR_Instruction_Address_Of_Type::DATA;
    instr.options.address_of.destination = ir_data_access_create_intermediate(block, upcast(type_system_make_pointer(ir_data_access_get_type(&access))));
    instr.options.address_of.source = access;
    dynamic_array_push_back(&block->instructions, instr);
    return instr.options.binary_op.destination;
}

IR_Data_Access ir_data_access_create_member(IR_Code_Block* block, IR_Data_Access struct_access, Struct_Member member)
{
    IR_Instruction member_instr;
    member_instr.type = IR_Instruction_Type::ADDRESS_OF;
    member_instr.options.address_of.type = IR_Instruction_Address_Of_Type::STRUCT_MEMBER;
    member_instr.options.address_of.options.member = member;
    member_instr.options.address_of.source = struct_access;
    member_instr.options.address_of.destination = ir_data_access_create_intermediate(block, upcast(type_system_make_pointer(member.type)));
    dynamic_array_push_back(&block->instructions, member_instr);

    member_instr.options.address_of.destination.is_memory_access = true;
    return member_instr.options.address_of.destination;
}

IR_Data_Access ir_data_access_create_constant(Datatype* signature, Array<byte> bytes)
{
    IR_Data_Access access;
    access.is_memory_access = false;
    access.type = IR_Data_Access_Type::CONSTANT;
    auto result = constant_pool_add_constant(signature, bytes);
    assert(result.success, "Must always work");
    access.index = result.constant.constant_index;
    return access;
}

IR_Data_Access ir_data_access_create_constant(Upp_Constant constant)
{
    IR_Data_Access access;
    access.index = constant.constant_index;
    access.type = IR_Data_Access_Type::CONSTANT;
    access.is_memory_access = false;
    return access;
}

IR_Data_Access ir_data_access_create_constant_i32(i32 value) {
    return ir_data_access_create_constant(
        upcast(compiler.type_system.predefined_types.i32_type),
        array_create_static((byte*)&value, sizeof(i32))
    );
}



// Code Gen
Expression_Info* get_info(AST::Expression* node) {
    return pass_get_node_info(ir_generator.current_pass, node, Info_Query::READ_NOT_NULL);
}

Statement_Info* get_info(AST::Statement* node) {
    return pass_get_node_info(ir_generator.current_pass, node, Info_Query::READ_NOT_NULL);
}

Argument_Info* get_info(AST::Argument* node) {
    return pass_get_node_info(ir_generator.current_pass, node, Info_Query::READ_NOT_NULL);
}

Case_Info* get_info(AST::Switch_Case* node) {
    return pass_get_node_info(ir_generator.current_pass, node, Info_Query::READ_NOT_NULL);
}

Symbol* get_info(AST::Path_Lookup* node) {
    return pass_get_node_info(ir_generator.current_pass, node, Info_Query::READ_NOT_NULL)->symbol;
}

Symbol* get_info(AST::Definition_Symbol* node) {
    return pass_get_node_info(ir_generator.current_pass, node, Info_Query::READ_NOT_NULL)->symbol;
}

IR_Data_Access ir_generator_generate_expression_no_cast(IR_Code_Block* ir_block, AST::Expression* expression)
{
    auto info = get_info(expression);
    auto result_type = expression_info_get_type(info, true);
    auto type_system = &compiler.type_system;
    auto& types = type_system->predefined_types;
    assert(!info->contains_errors, "Cannot contain errors!"); 

    // Handle different expression results
    switch (info->result_type)
    {
    case Expression_Result_Type::CONSTANT:
        return ir_data_access_create_constant(info->options.constant);
    case Expression_Result_Type::TYPE:
        return ir_data_access_create_constant(upcast(types.type_handle), array_create_static_as_bytes(&result_type->type_handle, 1));
    case Expression_Result_Type::FUNCTION: {
        // Function pointer read
        auto access = ir_data_access_create_intermediate(ir_block, result_type);
        IR_Instruction load_instr;
        load_instr.type = IR_Instruction_Type::ADDRESS_OF;
        load_instr.options.address_of.type = IR_Instruction_Address_Of_Type::FUNCTION;
        load_instr.options.address_of.options.function = *hashtable_find_element(&ir_generator.function_mapping, info->options.function);
        load_instr.options.address_of.destination = access;
        dynamic_array_push_back(&ir_block->instructions, load_instr);
        return access;
    }
    case Expression_Result_Type::HARDCODED_FUNCTION:
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    case Expression_Result_Type::POLYMORPHIC_STRUCT:
    case Expression_Result_Type::DOT_CALL:
        panic("must not happen");
    case Expression_Result_Type::VALUE:
        break; // Rest of this function
    default: panic("");
    }

    // Handle expression value
    switch (expression->type)
    {
    case AST::Expression_Type::BINARY_OPERATION:
    {
        auto& binop = expression->options.binop;

        // Handle overloads
        if (info->specifics.overload.function != 0) {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::FUNCTION_CALL;
            instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            instr.options.call.arguments = dynamic_array_create<IR_Data_Access>(2);
            auto left = ir_generator_generate_expression(ir_block, binop.left);
            auto right = ir_generator_generate_expression(ir_block, binop.right);
            if (info->specifics.overload.switch_left_and_right) {
                dynamic_array_push_back(&instr.options.call.arguments, right);
                dynamic_array_push_back(&instr.options.call.arguments, left);
            }
            else {
                dynamic_array_push_back(&instr.options.call.arguments, left);
                dynamic_array_push_back(&instr.options.call.arguments, right);
            }
            instr.options.call.destination = ir_data_access_create_intermediate(ir_block, info->options.value_type);
            instr.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, info->specifics.overload.function);
            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.call.destination;
        }

        IR_Instruction instr;
        instr.type = IR_Instruction_Type::BINARY_OP;
        instr.options.binary_op.type = binop.type;
        if (binop.type == AST::Binop::POINTER_EQUAL) {
            instr.options.binary_op.type = AST::Binop::EQUAL;
        }
        else if (binop.type == AST::Binop::POINTER_NOT_EQUAL) {
            instr.options.binary_op.type = AST::Binop::NOT_EQUAL;
        }
        instr.options.binary_op.operand_left = ir_generator_generate_expression(ir_block, binop.left);
        instr.options.binary_op.operand_right = ir_generator_generate_expression(ir_block, binop.right);
        instr.options.binary_op.destination = ir_data_access_create_intermediate(ir_block, result_type);
        dynamic_array_push_back(&ir_block->instructions, instr);
        return instr.options.binary_op.destination;
    }
    case AST::Expression_Type::UNARY_OPERATION:
    {
        auto& unop = expression->options.unop;
        IR_Data_Access access = ir_generator_generate_expression(ir_block, expression->options.unop.expr);

        if (info->specifics.overload.function != 0) {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::FUNCTION_CALL;
            instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            instr.options.call.arguments = dynamic_array_create<IR_Data_Access>(1);
            dynamic_array_push_back(&instr.options.call.arguments, access);
            instr.options.call.destination = ir_data_access_create_intermediate(ir_block, info->options.value_type);
            instr.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, info->specifics.overload.function);
            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.call.destination;
        }

        switch (expression->options.unop.type)
        {
        case AST::Unop::POINTER: {
            return ir_data_access_create_address_of(ir_block, access);
        }
        case AST::Unop::DEREFERENCE: {
            return ir_data_access_create_dereference(ir_block, access);
        }
        case AST::Unop::NOT: {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::UNARY_OP;
            instr.options.unary_op.destination = ir_data_access_create_intermediate(ir_block, result_type);
            instr.options.unary_op.type = IR_Instruction_Unary_OP_Type::NOT;
            instr.options.unary_op.source = access;
            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.unary_op.destination;
        }
        case AST::Unop::NEGATE: {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::UNARY_OP;
            instr.options.unary_op.destination = ir_data_access_create_intermediate(ir_block, result_type);
            instr.options.unary_op.type = IR_Instruction_Unary_OP_Type::NEGATE;
            instr.options.unary_op.source = access;
            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.unary_op.destination;
        }
        default: panic("HEY");
        }
        panic("HEY");
        break;
    }
    case AST::Expression_Type::TEMPLATE_PARAMETER: {
        panic("Shouldn't happen!");
    }
    case AST::Expression_Type::FUNCTION_CALL:
    {
        auto& call = expression->options.call;
        auto call_info = get_info(call.expr);

        IR_Instruction call_instr;
        call_instr.type = IR_Instruction_Type::FUNCTION_CALL;
        Datatype_Function* signature = nullptr;
        switch (call_info->result_type)
        {
        case Expression_Result_Type::FUNCTION: {
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            call_instr.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, call_info->options.function);
            signature = call_instr.options.call.options.function->function_type;
            break;
        }
        case Expression_Result_Type::VALUE: {
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_POINTER_CALL;
            call_instr.options.call.options.pointer_access = ir_generator_generate_expression(ir_block, call.expr);
            signature = downcast<Datatype_Function>(call_info->cast_info.result_type->base_type);
            break;
        }
        case Expression_Result_Type::CONSTANT: {
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_POINTER_CALL;
            call_instr.options.call.options.pointer_access = ir_data_access_create_constant(call_info->options.constant);
            signature = downcast<Datatype_Function>(call_info->options.constant.type->base_type);
            break;
        }
        case Expression_Result_Type::HARDCODED_FUNCTION:
        {
            auto& hardcoded = call_info->options.hardcoded;
            switch (hardcoded)
            {
            case Hardcoded_Type::ASSERT_FN:
            {
                IR_Instruction if_instr;
                if_instr.type = IR_Instruction_Type::IF;
                if_instr.options.if_instr.condition = ir_generator_generate_expression(ir_block, call.arguments[0]->value);
                if_instr.options.if_instr.true_branch = ir_code_block_create(ir_block->function);
                if_instr.options.if_instr.false_branch = ir_code_block_create(ir_block->function);
                dynamic_array_push_back(&ir_block->instructions, if_instr);

                IR_Instruction exit_instr;
                exit_instr.type = IR_Instruction_Type::RETURN;
                exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
                exit_instr.options.return_instr.options.exit_code = Exit_Code::ASSERTION_FAILED;
                dynamic_array_push_back(&if_instr.options.if_instr.false_branch->instructions, exit_instr);

                call_instr.options.call.destination = ir_data_access_create_nothing();
                return call_instr.options.call.destination;
            }
            case Hardcoded_Type::TYPE_OF: {
                panic("Should be handled in semantic analyser");
                break;
            }
            }
            call_instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
            call_instr.options.call.options.hardcoded.type = hardcoded;
            signature = hardcoded_type_to_signature(hardcoded);
            call_instr.options.call.options.hardcoded.signature = signature;
            break;
        }
        case Expression_Result_Type::POLYMORPHIC_FUNCTION: 
        {
            auto function = call_info->options.polymorphic_function.instance_fn;
            assert(function != nullptr, "Must be instanciated at ir_code");
            assert(function->is_runnable, "Instances that reach ir-generator must be runnable!");
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            call_instr.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, function);
            signature = function->signature;
            break;
        }
        case Expression_Result_Type::DOT_CALL: {
            auto& dot_call = call_info->options.dot_call;
            if (dot_call.is_polymorphic)
            {
                auto function = call_info->options.dot_call.options.polymorphic.instance;
                assert(function != nullptr, "Must be instanciated at ir_code");
                assert(function->is_runnable, "Instances that reach ir-generator must be runnable!");
                call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
                call_instr.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, function);
                signature = function->signature;
            }
            else
            {
                call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
                call_instr.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, dot_call.options.function);
                signature = dot_call.options.function->signature;
            }
            break;
        }
        case Expression_Result_Type::POLYMORPHIC_STRUCT:
        case Expression_Result_Type::TYPE: {
            panic("Must not happen after semantic analysis!");
            break;
        }
        default: panic("");
        }

        // Generate return value
        assert(signature != nullptr, "");
        if (signature->return_type.available) {
            call_instr.options.call.destination = ir_data_access_create_intermediate(ir_block, signature->return_type.value);
        }
        else {
            call_instr.options.call.destination = ir_data_access_create_nothing();
        }

        // Generate arguments 
        call_instr.options.call.arguments = dynamic_array_create<IR_Data_Access>(call.arguments.size);
        auto function_signature = info->specifics.function_call_signature;

        // Add default/dummy arguments
        for (int i = 0; i < function_signature->parameters.size; i++) {
            auto& param = function_signature->parameters[i];
            if (param.default_value_exists) {
                // Initialize all default arguments with their default value, if they are supplied, this will be overwritten
                assert(param.default_value_opt.available, "Must be, otherwise we shouldn't get to this point");
                dynamic_array_push_back(&call_instr.options.call.arguments, ir_data_access_create_constant(param.default_value_opt.value));
            }
            else {
                dynamic_array_push_back_dummy(&call_instr.options.call.arguments);
            }
        }

        if (call_info->result_type == Expression_Result_Type::DOT_CALL) {
            assert(call.expr->type == AST::Expression_Type::MEMBER_ACCESS, "Must be true for dot call!");
            call_instr.options.call.arguments[0] = ir_generator_generate_expression(ir_block, call.expr->options.member_access.expr);
        }

        // Generate code for arguments
        for (int j = 0; j < call.arguments.size; j++) {
            auto info = get_info(call.arguments[j]);
            if (!info->ignore_during_code_generation) { // Skip polymorphic_function arguments
                call_instr.options.call.arguments[info->parameter_index] = ir_generator_generate_expression(ir_block, call.arguments[j]->value);
            }
        }

        dynamic_array_push_back(&ir_block->instructions, call_instr);
        return call_instr.options.call.destination;
    }
    case AST::Expression_Type::PATH_LOOKUP:
    {
        auto symbol = get_info(expression->options.path_lookup);
        switch (symbol->type)
        {
        case Symbol_Type::GLOBAL: {
            return ir_data_access_create_global(symbol->options.global);
        }
        case Symbol_Type::VARIABLE: {
            return *hashtable_find_element(&ir_generator.variable_mapping, AST::downcast<AST::Definition_Symbol>(symbol->definition_node));
        }
        case Symbol_Type::PARAMETER: {
            IR_Data_Access access;
            access.type = IR_Data_Access_Type::PARAMETER;
            access.is_memory_access = false;
            access.index = symbol->options.parameter.index_in_non_polymorphic_signature;
            access.option.function = ir_block->function;
            return access;
        }
        default: panic("Other Symbol-cases must be handled by analyser or in this function above!");
        }
        return ir_data_access_create_constant_i32(-1);
    }
    case AST::Expression_Type::STRUCT_INITIALIZER:
    {
        IR_Data_Access struct_access = ir_data_access_create_intermediate(ir_block, result_type);
        auto struct_type = downcast<Datatype_Struct>(ir_data_access_get_type(&struct_access));
        auto& members = struct_type->members;

        auto struct_init = expression->options.struct_initializer;
        for (int i = 0; i < struct_init.arguments.size; i++)
        {
            auto arg = struct_init.arguments[i];
            auto arg_info = get_info(arg);
            
            assert(result_type->type == Datatype_Type::STRUCT, "");

            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.destination = ir_data_access_create_member(ir_block, struct_access, members[arg_info->parameter_index]);
            move_instr.options.move.source = ir_generator_generate_expression(ir_block, arg->value);
            dynamic_array_push_back(&ir_block->instructions, move_instr);
        }

        // Initialize union tag
        if (struct_type->struct_type == AST::Structure_Type::UNION)
        {
            assert(struct_init.arguments.size == 1, "");
            int member_index = get_info(struct_init.arguments[0])->parameter_index;

            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.destination = ir_data_access_create_member(ir_block, struct_access, struct_type->tag_member);
            move_instr.options.move.source = ir_data_access_create_constant_i32(member_index + 1); // There's a reason this is plus 1, but i forgot...
            dynamic_array_push_back(&ir_block->instructions, move_instr);
        }
        return struct_access;
    }
    case AST::Expression_Type::ARRAY_INITIALIZER:
    {
        auto& array_init = expression->options.array_initializer;
        IR_Data_Access array_access = ir_data_access_create_intermediate(ir_block, result_type);
        if (result_type->type == Datatype_Type::SLICE)
        {
            assert(array_init.values.size == 0, "");
            Upp_Slice_Base slice_base;
            slice_base.data = 0;
            slice_base.size = 0;
            auto slice_constant = constant_pool_add_constant(result_type, array_create_static_as_bytes(&slice_base, 1));
            assert(slice_constant.success, "Empty slice must succeed!");

            IR_Instruction move;
            move.type = IR_Instruction_Type::MOVE;
            move.options.move.destination = array_access;
            move.options.move.source = ir_data_access_create_constant(slice_constant.constant);
            dynamic_array_push_back(&ir_block->instructions, move);
        }
        else
        {
            assert(result_type->type == Datatype_Type::ARRAY, "");
            auto array = downcast<Datatype_Array>(result_type);
            assert(array->element_count == array_init.values.size, "");
            for (int i = 0; i < array_init.values.size; i++)
            {
                auto init_expr = array_init.values[i];

                // Calculate array member pointer
                IR_Instruction element_instr;
                element_instr.type = IR_Instruction_Type::ADDRESS_OF;
                element_instr.options.address_of.type = IR_Instruction_Address_Of_Type::ARRAY_ELEMENT;
                element_instr.options.address_of.destination = ir_data_access_create_intermediate(
                    ir_block, upcast(type_system_make_pointer(array->element_type))
                );
                element_instr.options.address_of.source = array_access;
                element_instr.options.address_of.options.index_access = ir_data_access_create_constant_i32(i);
                dynamic_array_push_back(&ir_block->instructions, element_instr);

                IR_Data_Access member_access = element_instr.options.address_of.destination;
                member_access.is_memory_access = true;
                IR_Instruction move_instr;
                move_instr.type = IR_Instruction_Type::MOVE;
                move_instr.options.move.destination = member_access;
                move_instr.options.move.source = ir_generator_generate_expression(ir_block, init_expr);
                dynamic_array_push_back(&ir_block->instructions, move_instr);
            }
        }
        return array_access;
    }
    case AST::Expression_Type::ARRAY_ACCESS:
    {
        if (info->specifics.overload.function != 0) 
        {
            auto& access = expression->options.array_access;
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::FUNCTION_CALL;
            instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            instr.options.call.arguments = dynamic_array_create<IR_Data_Access>(2);
            dynamic_array_push_back(&instr.options.call.arguments, ir_generator_generate_expression(ir_block, access.array_expr));
            dynamic_array_push_back(&instr.options.call.arguments, ir_generator_generate_expression(ir_block, access.index_expr));
            instr.options.call.destination = ir_data_access_create_intermediate(ir_block, info->options.value_type);
            instr.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, info->specifics.overload.function);
            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.call.destination;
        }

        IR_Instruction instr;
        instr.type = IR_Instruction_Type::ADDRESS_OF;
        instr.options.address_of.destination = ir_data_access_create_intermediate(ir_block, upcast(type_system_make_pointer(result_type)));
        instr.options.address_of.type = IR_Instruction_Address_Of_Type::ARRAY_ELEMENT;
        instr.options.address_of.source = ir_generator_generate_expression(ir_block, expression->options.array_access.array_expr);
        instr.options.address_of.options.index_access = ir_generator_generate_expression(ir_block, expression->options.array_access.index_expr);
        dynamic_array_push_back(&ir_block->instructions, instr);

        instr.options.address_of.destination.is_memory_access = true;
        return instr.options.address_of.destination;
    }
    case AST::Expression_Type::MEMBER_ACCESS:
    {
        auto mem_access = expression->options.member_access;
        auto source = ir_generator_generate_expression(ir_block, mem_access.expr);

        // Handle custom member accesses
        if (info->specifics.member_access.type == Member_Access_Type::DOT_CALL_AS_MEMBER) {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::FUNCTION_CALL;
            instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            instr.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, info->specifics.member_access.dot_call_function);
            instr.options.call.destination = ir_data_access_create_intermediate(ir_block, result_type);
            instr.options.call.arguments = dynamic_array_create<IR_Data_Access>(1);
            dynamic_array_push_back(&instr.options.call.arguments, source);

            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.call.destination;
        }

        // Handle special case of array.data, which basically becomes an address of
        auto src_type = get_info(mem_access.expr)->cast_info.result_type;
        if (src_type->type == Datatype_Type::ARRAY)
        {
            assert(mem_access.name == compiler.predefined_ids.data, "Member access on array must be data or handled elsewhere!");
            if (source.is_memory_access) {
                source.is_memory_access = false;
                return source;
            }

            IR_Instruction instr;
            instr.type = IR_Instruction_Type::ADDRESS_OF;
            instr.options.address_of.destination = ir_data_access_create_intermediate(ir_block, upcast(type_system_make_pointer(result_type)));
            instr.options.address_of.type = IR_Instruction_Address_Of_Type::DATA;
            instr.options.address_of.source = source;
            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.address_of.destination;
        }

        // Handle normal member access
        auto member_result = type_signature_find_member_by_id(src_type, mem_access.name);
        assert(member_result.available, "Must be availabe");

        return ir_data_access_create_member(ir_block, source, member_result.value);
    }
    case AST::Expression_Type::NEW_EXPR:
    {
        // FUTURE: At some point this will access the Context struct for the alloc function, and then call it
        auto& new_expr = expression->options.new_expr;
        auto type_info = get_info(new_expr.type_expr);
        assert(type_info->result_type == Expression_Result_Type::TYPE, "Hey");
        assert(type_info->options.type->memory_info.available, "");
        int element_size = type_info->options.type->memory_info.value.size;
        if (new_expr.count_expr.available)
        {
            assert(result_type->type == Datatype_Type::SLICE, "HEY");
            auto slice_type = downcast<Datatype_Slice>(result_type);
            IR_Data_Access array_access = ir_data_access_create_intermediate(ir_block, result_type); // Array
            IR_Data_Access array_data_access = ir_data_access_create_member(ir_block, array_access, slice_type->data_member);
            IR_Data_Access array_size_access = ir_data_access_create_member(ir_block, array_access, slice_type->size_member);

            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.source = ir_generator_generate_expression(ir_block, new_expr.count_expr.value);
            move_instr.options.move.destination = array_size_access;
            dynamic_array_push_back(&ir_block->instructions, move_instr);

            IR_Instruction mult_instr;
            mult_instr.type = IR_Instruction_Type::BINARY_OP;
            mult_instr.options.binary_op.type = AST::Binop::MULTIPLICATION;
            mult_instr.options.binary_op.destination = ir_data_access_create_intermediate(ir_block, upcast(types.i32_type));
            mult_instr.options.binary_op.operand_left = ir_data_access_create_constant_i32(element_size);
            mult_instr.options.binary_op.operand_right = array_size_access;
            dynamic_array_push_back(&ir_block->instructions, mult_instr);

            IR_Instruction alloc_instr;
            alloc_instr.type = IR_Instruction_Type::FUNCTION_CALL;
            alloc_instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
            alloc_instr.options.call.options.hardcoded.type = Hardcoded_Type::MALLOC_SIZE_I32;
            alloc_instr.options.call.options.hardcoded.signature = hardcoded_type_to_signature(Hardcoded_Type::MALLOC_SIZE_I32);
            alloc_instr.options.call.destination = array_data_access;
            alloc_instr.options.call.arguments = dynamic_array_create<IR_Data_Access>(1);
            dynamic_array_push_back(&alloc_instr.options.call.arguments, mult_instr.options.binary_op.destination);
            dynamic_array_push_back(&ir_block->instructions, alloc_instr);
            return array_access;
        }
        else
        {
            IR_Instruction alloc_instr;
            alloc_instr.type = IR_Instruction_Type::FUNCTION_CALL;
            alloc_instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
            alloc_instr.options.call.options.hardcoded.type = Hardcoded_Type::MALLOC_SIZE_I32;
            alloc_instr.options.call.options.hardcoded.signature = hardcoded_type_to_signature(Hardcoded_Type::MALLOC_SIZE_I32);
            alloc_instr.options.call.destination = ir_data_access_create_intermediate(ir_block, result_type);
            alloc_instr.options.call.arguments = dynamic_array_create<IR_Data_Access>(1);
            dynamic_array_push_back(&alloc_instr.options.call.arguments, ir_data_access_create_constant_i32(element_size));
            dynamic_array_push_back(&ir_block->instructions, alloc_instr);
            return alloc_instr.options.call.destination;
        }

        panic("Unreachable");
        break;
    }
    case AST::Expression_Type::CAST: {
        return ir_generator_generate_expression(ir_block, expression->options.cast.operand);
    }
    case AST::Expression_Type::BAKE_BLOCK:
    case AST::Expression_Type::BAKE_EXPR:
    case AST::Expression_Type::ENUM_TYPE:
    case AST::Expression_Type::ERROR_EXPR:
    case AST::Expression_Type::FUNCTION:
    case AST::Expression_Type::FUNCTION_SIGNATURE:
    case AST::Expression_Type::LITERAL_READ:
    case AST::Expression_Type::MODULE:
    case AST::Expression_Type::STRUCTURE_TYPE:
    case AST::Expression_Type::AUTO_ENUM:
    case AST::Expression_Type::ARRAY_TYPE: {
        panic("A different path should have handled this before!");
    }
    default: panic("HEY");
    }

    panic("HEY");
    IR_Data_Access unused;
    unused.type = IR_Data_Access_Type::REGISTER; // Just to get rid of error message
    return unused;
}

IR_Data_Access ir_generator_generate_expression(IR_Code_Block* ir_block, AST::Expression* expression)
{
    // Generate expression
    auto source = ir_generator_generate_expression_no_cast(ir_block, expression);

    // Generate cast
    auto info = get_info(expression);
    auto& cast_info = info->cast_info;
    auto cast_type = cast_info.cast_type;

    // Auto operations
    if (cast_info.deref_count < 0) {
        for (int i = 0; i < -cast_info.deref_count; i++) {
            source = ir_data_access_create_address_of(ir_block, source);
        }
    }
    else
    {
        for (int i = 0; i < cast_info.deref_count; i++) {
            source = ir_data_access_create_dereference(ir_block, source);
        }
    }
    if (cast_type == Cast_Type::NO_CAST) return source;

    auto type_system = &compiler.type_system;
    auto& types = type_system->predefined_types;
    auto source_type = ir_data_access_get_type(&source);
    auto make_simple_cast = [](IR_Code_Block* ir_block, IR_Data_Access source, Datatype* result_type, IR_Cast_Type cast_type) -> IR_Data_Access
    {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::CAST;
        instr.options.cast.type = cast_type;
        instr.options.cast.source = source;
        instr.options.cast.destination = ir_data_access_create_intermediate(ir_block, result_type);
        dynamic_array_push_back(&ir_block->instructions, instr);
        return instr.options.cast.destination;
    };

    switch (cast_type)
    {
    case Cast_Type::INVALID:
    case Cast_Type::NO_CAST:
        panic("Should have been handled elsewhere!");
    case Cast_Type::INTEGERS: {
        return make_simple_cast(ir_block, source, cast_info.result_type, IR_Cast_Type::INTEGERS);
    }
    case Cast_Type::FLOATS: {
        return make_simple_cast(ir_block, source, cast_info.result_type, IR_Cast_Type::FLOATS);
    }
    case Cast_Type::FLOAT_TO_INT: {
        return make_simple_cast(ir_block, source, cast_info.result_type, IR_Cast_Type::FLOAT_TO_INT);
    }
    case Cast_Type::INT_TO_FLOAT: {
        return make_simple_cast(ir_block, source, cast_info.result_type, IR_Cast_Type::INT_TO_FLOAT);
    }
    case Cast_Type::POINTERS: {
        return make_simple_cast(ir_block, source, cast_info.result_type, IR_Cast_Type::POINTERS);
    }
    case Cast_Type::POINTER_TO_U64: {
        return make_simple_cast(ir_block, source, cast_info.result_type, IR_Cast_Type::POINTER_TO_U64);
    }
    case Cast_Type::U64_TO_POINTER: {
        return make_simple_cast(ir_block, source, cast_info.result_type, IR_Cast_Type::U64_TO_POINTER);
    }
    case Cast_Type::ENUM_TO_INT: {
        return make_simple_cast(ir_block, source, cast_info.result_type, IR_Cast_Type::ENUM_TO_INT);
    }
    case Cast_Type::INT_TO_ENUM: {
        return make_simple_cast(ir_block, source, cast_info.result_type, IR_Cast_Type::INT_TO_ENUM);
    }
    case Cast_Type::ARRAY_TO_SLICE:
    {
        Datatype_Slice* slice_type = downcast<Datatype_Slice>(datatype_get_non_const_type(cast_info.result_type));
        Datatype_Array* array_type = downcast<Datatype_Array>(datatype_get_non_const_type(source_type));
        IR_Data_Access slice_access = ir_data_access_create_intermediate(ir_block, upcast(slice_type));
        // Set size
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::MOVE;
            instr.options.move.destination = ir_data_access_create_member(ir_block, slice_access, slice_type->size_member);
            assert(array_type->count_known, "");
            instr.options.move.source = ir_data_access_create_constant_i32(array_type->element_count);
            dynamic_array_push_back(&ir_block->instructions, instr);
        }
        // Set data
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::ADDRESS_OF;
            instr.options.address_of.type = IR_Instruction_Address_Of_Type::DATA;
            instr.options.address_of.source = source;
            instr.options.address_of.destination = ir_data_access_create_member(ir_block, slice_access, slice_type->data_member);
            dynamic_array_push_back(&ir_block->instructions, instr);
        }
        return slice_access;
    }
    case Cast_Type::CUSTOM_CAST: {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::FUNCTION_CALL;
        instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
        instr.options.call.arguments = dynamic_array_create<IR_Data_Access>(1);
        dynamic_array_push_back(&instr.options.call.arguments, source);
        instr.options.call.destination = ir_data_access_create_intermediate(ir_block, cast_info.result_type);
        instr.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, cast_info.options.custom_cast_function);
        dynamic_array_push_back(&ir_block->instructions, instr);
        return instr.options.call.destination;
    }
    case Cast_Type::POINTER_NULL_CHECK: {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::BINARY_OP;
        instr.options.binary_op.type = AST::Binop::NOT_EQUAL; // Note: Pointer equals would be more correct, but currently only equals is used as overload
        instr.options.binary_op.operand_left = source;
        void* null_ptr = nullptr;
        instr.options.binary_op.operand_right = ir_data_access_create_constant(types.byte_pointer, array_create_static_as_bytes(&null_ptr, 1));
        instr.options.binary_op.destination = ir_data_access_create_intermediate(ir_block, upcast(types.bool_type));
        dynamic_array_push_back(&ir_block->instructions, instr);
        return instr.options.binary_op.destination;
    }
    case Cast_Type::FROM_ANY:
    {
        // Check if type matches given type, if not error out
        IR_Data_Access access_operand = source;
        IR_Data_Access access_valid_cast = ir_data_access_create_intermediate(ir_block, upcast(types.bool_type));
        IR_Data_Access access_result = ir_data_access_create_intermediate(ir_block, cast_info.result_type);
        {
            IR_Instruction cmp_instr;
            cmp_instr.type = IR_Instruction_Type::BINARY_OP;
            cmp_instr.options.binary_op.type = AST::Binop::EQUAL;
            cmp_instr.options.binary_op.operand_left = ir_data_access_create_constant(
                types.type_handle,
                array_create_static_as_bytes<u32>(&cast_info.result_type->type_handle.index, 1)
            );
            cmp_instr.options.binary_op.operand_right = ir_data_access_create_member(ir_block, access_operand, types.any_type->members[1]);
            cmp_instr.options.binary_op.destination = access_valid_cast;
            dynamic_array_push_back(&ir_block->instructions, cmp_instr);
        }
        IR_Code_Block* branch_valid = ir_code_block_create(ir_block->function);
        IR_Code_Block* branch_invalid = ir_code_block_create(ir_block->function);
        {
            IR_Instruction if_instr;
            if_instr.type = IR_Instruction_Type::IF;
            if_instr.options.if_instr.condition = access_valid_cast;
            if_instr.options.if_instr.false_branch = branch_invalid;
            if_instr.options.if_instr.true_branch = branch_valid;
            dynamic_array_push_back(&ir_block->instructions, if_instr);
        }
        {
            // True_Branch
            IR_Data_Access any_data_access = ir_data_access_create_member(branch_valid, access_operand, types.any_type->members[0]);
            {
                IR_Instruction cast_instr;
                cast_instr.type = IR_Instruction_Type::CAST;
                cast_instr.options.cast.type = IR_Cast_Type::POINTERS;
                cast_instr.options.cast.source = any_data_access;
                cast_instr.options.cast.destination = ir_data_access_create_intermediate(
                    branch_valid, upcast(type_system_make_pointer(cast_info.result_type))
                );
                dynamic_array_push_back(&branch_valid->instructions, cast_instr);
                any_data_access = cast_instr.options.cast.destination;
            }
            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.destination = access_result;
            move_instr.options.move.source = ir_data_access_create_dereference(branch_valid, any_data_access);
            dynamic_array_push_back(&branch_valid->instructions, move_instr);
        }
        {
            // False branch
            IR_Instruction exit_instr;
            exit_instr.type = IR_Instruction_Type::RETURN;
            exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
            exit_instr.options.return_instr.options.exit_code = Exit_Code::ANY_CAST_INVALID;
            dynamic_array_push_back(&branch_invalid->instructions, exit_instr);
        }
        return access_result;
    }
    case Cast_Type::TO_ANY:
    {
        IR_Data_Access operand_access = source;
        if (operand_access.type == IR_Data_Access_Type::CONSTANT && !operand_access.is_memory_access)
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::MOVE;
            instr.options.move.destination = ir_data_access_create_intermediate(ir_block, source_type);
            instr.options.move.source = operand_access;
            dynamic_array_push_back(&ir_block->instructions, instr);
            operand_access = instr.options.move.destination;
        }

        IR_Data_Access any_access = ir_data_access_create_intermediate(ir_block, upcast(types.any_type));
        // Set data
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::ADDRESS_OF;
            instr.options.address_of.type = IR_Instruction_Address_Of_Type::DATA;
            instr.options.address_of.source = operand_access;
            // In theory a cast from pointer to voidptr would be better, but I think I can ignore it
            instr.options.address_of.destination = ir_data_access_create_member(
                ir_block, any_access, types.any_type->members[0]
            );
            dynamic_array_push_back(&ir_block->instructions, instr);
        }
        // Set type
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::MOVE;
            instr.options.move.destination = ir_data_access_create_member(
                ir_block, any_access, types.any_type->members[1]
            );
            instr.options.move.source = ir_data_access_create_constant(
                upcast(types.type_handle),
                array_create_static_as_bytes(&source_type->type_handle, 1)
            );
            dynamic_array_push_back(&ir_block->instructions, instr);
        }
        return any_access;
    }
    default: panic("");
    }

    panic("");
    return ir_data_access_create_intermediate(ir_block, upcast(types.bool_type)); // Just random code
}

void ir_generator_work_through_defers(IR_Code_Block* ir_block, int defer_to_index, bool rewind_stack)
{
    auto& defers = ir_generator.defer_stack;
    for (int i = defers.size - 1; i >= defer_to_index; i--)
    {
        auto block = defers[i];
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::BLOCK;
        instr.options.block = ir_code_block_create(ir_block->function);
        ir_generator_generate_block(instr.options.block, block);
        dynamic_array_push_back(&ir_block->instructions, instr);
    }
    if (rewind_stack) {
        dynamic_array_rollback_to_size(&defers, defer_to_index);
    }
}

void ir_generator_generate_block_loop_increment(IR_Code_Block* ir_block, AST::Code_Block* loop_block)
{
    Loop_Increment* loop_increment = hashtable_find_element(&ir_generator.loop_increment_instructions, loop_block);
    if (loop_increment == nullptr) {
        assert(loop_block->base.parent->type == AST::Node_Type::STATEMENT, "Must be while block");
        assert(downcast<AST::Statement>(loop_block->base.parent)->type == AST::Statement_Type::WHILE_STATEMENT, "");
        return; // While loop does not have increment instructions
    }

    if (loop_increment->type == Loop_Type::FOR_LOOP) {
        ir_generator_generate_statement(loop_increment->options.increment_statement, ir_block);
    }
    else
    {
        auto& foreach = loop_increment->options.foreach_loop;

        // Increment index access
        IR_Instruction increment;
        increment.type = IR_Instruction_Type::BINARY_OP;
        increment.options.binary_op.type = AST::Binop::ADDITION;
        increment.options.binary_op.destination = foreach.index_access;
        increment.options.binary_op.operand_left = foreach.index_access;
        increment.options.binary_op.operand_right = ir_data_access_create_constant_i32(1);
        dynamic_array_push_back(&ir_block->instructions, increment);

        // Update pointer
        if (foreach.is_custom_iterator)
        {
            IR_Instruction next_call;
            next_call.type = IR_Instruction_Type::FUNCTION_CALL;
            next_call.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            next_call.options.call.destination = foreach.loop_variable_access;
            next_call.options.call.options.function = foreach.next_function;
            next_call.options.call.arguments = dynamic_array_create<IR_Data_Access>(1);
            IR_Data_Access argument_access = foreach.iterator_access;
            for (int i = 0; i < foreach.iterator_deref_value; i++) {
                argument_access = ir_data_access_create_dereference(ir_block, argument_access);
            }
            if (foreach.iterator_deref_value == -1) {
                argument_access = ir_data_access_create_address_of(ir_block, foreach.iterator_access);
            }
            dynamic_array_push_back(&next_call.options.call.arguments, argument_access);
            dynamic_array_push_back(&ir_block->instructions, next_call);
        }
        else {
            IR_Instruction element_instr;
            element_instr.type = IR_Instruction_Type::ADDRESS_OF;
            element_instr.options.address_of.type = IR_Instruction_Address_Of_Type::ARRAY_ELEMENT;
            element_instr.options.address_of.destination = foreach.loop_variable_access;
            element_instr.options.address_of.source = foreach.iterable_access;
            element_instr.options.address_of.options.index_access = foreach.index_access;
            dynamic_array_push_back(&ir_block->instructions, element_instr);
        }
    }
}

void ir_generator_generate_statement(AST::Statement* statement, IR_Code_Block* ir_block)
{
    auto stat_info = get_info(statement);
    switch (statement->type)
    {
    case AST::Statement_Type::DEFER:
    {
        dynamic_array_push_back(&ir_generator.defer_stack, statement->options.defer_block);
        break;
    }
    case AST::Statement_Type::DEFINITION:
    {
        auto definition = statement->options.definition;
        bool is_split = get_info(statement)->specifics.is_struct_split;
        if (definition->is_comptime) {
            // Comptime definitions should be already handled
            return;
        }

        IR_Data_Access broadcast_value;
        Datatype* broadcast_type;
        if (definition->values.size == 1) {
            broadcast_value = ir_generator_generate_expression(ir_block, definition->values[0]);
            broadcast_type = ir_data_access_get_type(&broadcast_value);
        }

        // Define all variables
        for (int i = 0; i < definition->symbols.size; i++)
        {
            auto symbol = get_info(definition->symbols[i]);
            assert(symbol->type == Symbol_Type::VARIABLE, "");
            auto var_type = symbol->options.variable_type;
            dynamic_array_push_back(&ir_block->registers, var_type);

            IR_Data_Access access;
            access.type = IR_Data_Access_Type::REGISTER;
            access.index = ir_block->registers.size - 1;
            access.is_memory_access = false;
            access.option.definition_block = ir_block;
            hashtable_insert_element(&ir_generator.variable_mapping, definition->symbols[i], access);

            if (definition->values.size == 1) {
                IR_Instruction move;
                move.type = IR_Instruction_Type::MOVE;
                move.options.move.destination = access;
                move.options.move.source = broadcast_value;
                if (is_split) {
                    assert(broadcast_type->type == Datatype_Type::STRUCT, "");
                    move.options.move.source = ir_data_access_create_member(
                        ir_block, broadcast_value, downcast<Datatype_Struct>(broadcast_type)->members[i]);
                }
                dynamic_array_push_back(&ir_block->instructions, move);
            }
            else if (definition->values.size != 0) {
                assert(i < definition->values.size, "Must be guaranteed by semantic analyser!");
                IR_Instruction move;
                move.type = IR_Instruction_Type::MOVE;
                move.options.move.destination = access;
                move.options.move.source = ir_generator_generate_expression(ir_block, definition->values[i]);
                dynamic_array_push_back(&ir_block->instructions, move);
            }
        }

        break;
    }
    case AST::Statement_Type::BLOCK:
    {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::BLOCK;
        instr.options.block = ir_code_block_create(ir_block->function);
        ir_generator_generate_block(instr.options.block, statement->options.block);
        dynamic_array_push_back(&ir_block->instructions, instr);

        IR_Instruction label;
        label.type = IR_Instruction_Type::LABEL;
        label.options.label_index = ir_generator.next_label_index;
        ir_generator.next_label_index++;
        dynamic_array_push_back(&ir_block->instructions, label);
        hashtable_insert_element(&ir_generator.labels_break, statement->options.block, label.options.label_index);
        break;
    }
    case AST::Statement_Type::IF_STATEMENT:
    {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::IF;
        instr.options.if_instr.condition = ir_generator_generate_expression(ir_block, statement->options.if_statement.condition);
        instr.options.if_instr.true_branch = ir_code_block_create(ir_block->function);
        ir_generator_generate_block(instr.options.if_instr.true_branch, statement->options.if_statement.block);
        instr.options.if_instr.false_branch = ir_code_block_create(ir_block->function);
        if (statement->options.if_statement.else_block.available) {
            ir_generator_generate_block(instr.options.if_instr.false_branch, statement->options.if_statement.else_block.value);
        }
        dynamic_array_push_back(&ir_block->instructions, instr);

        IR_Instruction label;
        label.type = IR_Instruction_Type::LABEL;
        label.options.label_index = ir_generator.next_label_index;
        ir_generator.next_label_index++;
        dynamic_array_push_back(&ir_block->instructions, label);
        bool valid = hashtable_insert_element(&ir_generator.labels_break, statement->options.if_statement.block, label.options.label_index);
        assert(valid, "");
        if (statement->options.if_statement.else_block.available) {
            valid = hashtable_insert_element(&ir_generator.labels_break, statement->options.if_statement.else_block.value, label.options.label_index);
            assert(valid, "");
        }
        break;
    }
    case AST::Statement_Type::SWITCH_STATEMENT:
    {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::SWITCH;
        instr.options.switch_instr.condition_access = ir_generator_generate_expression(ir_block, statement->options.switch_statement.condition);
        instr.options.switch_instr.cases = dynamic_array_create<IR_Switch_Case>(statement->options.switch_statement.cases.size);

        // Check for union access
        auto cond_type = ir_data_access_get_type(&instr.options.switch_instr.condition_access);
        if (cond_type->type == Datatype_Type::STRUCT) {
            auto structure = downcast<Datatype_Struct>(cond_type);
            assert(structure->struct_type == AST::Structure_Type::UNION, "");
            instr.options.switch_instr.condition_access =
                ir_data_access_create_member(ir_block, instr.options.switch_instr.condition_access, structure->tag_member);
        }
        else {
            assert(cond_type->type == Datatype_Type::ENUM, "");
        }

        AST::Switch_Case* default_case = 0;
        for (int i = 0; i < statement->options.switch_statement.cases.size; i++)
        {
            auto switch_case = statement->options.switch_statement.cases[i];
            if (!switch_case->value.available) {
                default_case = switch_case;
                continue;
            }

            auto case_info = get_info(switch_case);
            assert(case_info->is_valid, "");

            IR_Switch_Case new_case;
            new_case.value = case_info->case_value;
            new_case.block = ir_code_block_create(ir_block->function);
            ir_generator_generate_block(new_case.block, switch_case->block);
            dynamic_array_push_back(&instr.options.switch_instr.cases, new_case);
        }

        instr.options.switch_instr.default_block = ir_code_block_create(ir_block->function);
        if (default_case != 0) {
            ir_generator_generate_block(instr.options.switch_instr.default_block, default_case->block);
        }
        else {
            IR_Instruction exit_instr;
            exit_instr.type = IR_Instruction_Type::RETURN;
            exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
            exit_instr.options.return_instr.options.exit_code = Exit_Code::INVALID_SWITCH_CASE;
            dynamic_array_push_back(&instr.options.switch_instr.default_block->instructions, exit_instr);
        }
        dynamic_array_push_back(&ir_block->instructions, instr);

        IR_Instruction label;
        label.type = IR_Instruction_Type::LABEL;
        label.options.label_index = ir_generator.next_label_index;
        ir_generator.next_label_index++;
        dynamic_array_push_back(&ir_block->instructions, label);
        for (int i = 0; i < statement->options.switch_statement.cases.size; i++) {
            auto switch_case = statement->options.switch_statement.cases[i];
            hashtable_insert_element(&ir_generator.labels_break, switch_case->block, label.options.label_index);
        }
        break;
    }
    case AST::Statement_Type::FOR_LOOP:
    {
        auto& for_loop = statement->options.for_loop;
        auto& loop_info = get_info(statement)->specifics.for_loop;

        // Create + initialize loop variable
        {
            auto symbol = loop_info.loop_variable_symbol;
            assert(symbol->type == Symbol_Type::VARIABLE, "");
            IR_Data_Access access = ir_data_access_create_intermediate(ir_block, symbol->options.variable_type);
            hashtable_insert_element(&ir_generator.variable_mapping, for_loop.loop_variable_definition, access);

            IR_Instruction initialize;
            initialize.type = IR_Instruction_Type::MOVE;
            initialize.options.move.destination = access;
            initialize.options.move.source = ir_generator_generate_expression(ir_block, for_loop.initial_value);
            dynamic_array_push_back(&ir_block->instructions, initialize);
        }

        // Register loop increment method
        {
            Loop_Increment increment;
            increment.type = Loop_Type::FOR_LOOP;
            increment.options.increment_statement = for_loop.increment_statement;
            hashtable_insert_element(&ir_generator.loop_increment_instructions, for_loop.body_block, increment);
        }

        // Push Loop + continue/break labels
        IR_Instruction continue_label;
        continue_label.type = IR_Instruction_Type::LABEL;
        continue_label.options.label_index = ir_generator.next_label_index;
        ir_generator.next_label_index++;
        dynamic_array_push_back(&ir_block->instructions, continue_label);
        hashtable_insert_element(&ir_generator.labels_continue, for_loop.body_block, continue_label.options.label_index);

        IR_Instruction instr;
        instr.type = IR_Instruction_Type::WHILE;
        instr.options.while_instr.condition_code = ir_code_block_create(ir_block->function);
        instr.options.while_instr.condition_access = ir_generator_generate_expression(
            instr.options.while_instr.condition_code, for_loop.condition
        );
        instr.options.while_instr.code = ir_code_block_create(ir_block->function);
        ir_generator_generate_block(instr.options.while_instr.code, for_loop.body_block);
        dynamic_array_push_back(&ir_block->instructions, instr);

        // Push increment instruction at end of while block
        ir_generator_generate_statement(for_loop.increment_statement, instr.options.while_instr.code);

        // Push break label
        IR_Instruction break_label;
        break_label.type = IR_Instruction_Type::LABEL;
        break_label.options.label_index = ir_generator.next_label_index;
        ir_generator.next_label_index++;
        dynamic_array_push_back(&ir_block->instructions, break_label);
        hashtable_insert_element(&ir_generator.labels_break, for_loop.body_block, break_label.options.label_index);

        break;
    }
    case AST::Statement_Type::FOREACH_LOOP:
    {
        auto& foreach_loop = statement->options.foreach_loop;
        auto& loop_info = get_info(statement)->specifics.foreach_loop;
        auto& types = compiler.type_system.predefined_types;

        // Create and initialize index data-access (Always available)
        IR_Data_Access index_access = ir_data_access_create_intermediate(ir_block, upcast(types.i32_type));
        {
            // Initialize
            IR_Instruction initialize;
            initialize.type = IR_Instruction_Type::MOVE;
            initialize.options.move.destination = index_access;
            initialize.options.move.source = ir_data_access_create_constant_i32(0);
            dynamic_array_push_back(&ir_block->instructions, initialize);

            if (foreach_loop.index_variable_definition.available) {
                assert(loop_info.index_variable_symbol != 0 && loop_info.index_variable_symbol->type == Symbol_Type::VARIABLE, "");
                hashtable_insert_element(&ir_generator.variable_mapping, foreach_loop.index_variable_definition.value, index_access);
            }
        }

        // Create iterable access
        IR_Data_Access iterable_access = ir_generator_generate_expression(ir_block, foreach_loop.expression);
        auto iterable_type = ir_data_access_get_type(&iterable_access);

        // Create and initialize loop variable
        Datatype* iterator_type = nullptr;
        IR_Data_Access iterator_access; // Only valid for custom iterators
        IR_Data_Access loop_variable_access = ir_data_access_create_intermediate(ir_block, upcast(loop_info.loop_variable_symbol->options.variable_type));
        {
            hashtable_insert_element(&ir_generator.variable_mapping, foreach_loop.loop_variable_definition, loop_variable_access);

            // Initialize
            if (loop_info.is_custom_op) {
                iterator_type = loop_info.custom_op.fn_create->signature->return_type.value;
                iterator_access = ir_data_access_create_intermediate(ir_block, iterator_type);
                IR_Instruction iter_create_instr;
                iter_create_instr.type = IR_Instruction_Type::FUNCTION_CALL;
                iter_create_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
                iter_create_instr.options.call.destination = iterator_access;
                iter_create_instr.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, loop_info.custom_op.fn_create);
                iter_create_instr.options.call.arguments = dynamic_array_create<IR_Data_Access>(1);
                dynamic_array_push_back(&iter_create_instr.options.call.arguments, iterable_access);
                dynamic_array_push_back(&ir_block->instructions, iter_create_instr);
            }
            else
            {
                assert(iterable_type->type == Datatype_Type::ARRAY || iterable_type->type == Datatype_Type::SLICE, "Other types not supported currently");
                IR_Instruction element_instr;
                element_instr.type = IR_Instruction_Type::ADDRESS_OF;
                element_instr.options.address_of.type = IR_Instruction_Address_Of_Type::ARRAY_ELEMENT;
                element_instr.options.address_of.destination = loop_variable_access;
                element_instr.options.address_of.source = iterable_access;
                element_instr.options.address_of.options.index_access = index_access;
                dynamic_array_push_back(&ir_block->instructions, element_instr);
            }
        }

        // Register loop increment method
        {
            Loop_Increment increment;
            increment.type = Loop_Type::FOREACH_LOOP;
            increment.options.foreach_loop.index_access = index_access;
            increment.options.foreach_loop.iterable_access = iterable_access;
            increment.options.foreach_loop.loop_variable_access = loop_variable_access;
            increment.options.foreach_loop.is_custom_iterator = loop_info.is_custom_op;
            if (loop_info.is_custom_op) {
                increment.options.foreach_loop.iterator_access = iterator_access;
                increment.options.foreach_loop.next_function = *hashtable_find_element(&ir_generator.function_mapping, loop_info.custom_op.fn_next);
                increment.options.foreach_loop.iterator_deref_value = loop_info.custom_op.next_pointer_diff;
            }
            hashtable_insert_element(&ir_generator.loop_increment_instructions, foreach_loop.body_block, increment);
        }

        // Push loop
        {
            // Push loop_start label
            {
                IR_Instruction continue_label;
                continue_label.type = IR_Instruction_Type::LABEL;
                continue_label.options.label_index = ir_generator.next_label_index;
                ir_generator.next_label_index++;
                dynamic_array_push_back(&ir_block->instructions, continue_label);
                hashtable_insert_element(&ir_generator.labels_continue, foreach_loop.body_block, continue_label.options.label_index);
            }

            // Push loop
            {
                // Create condition code
                IR_Code_Block* condition_code = ir_code_block_create(ir_block->function);
                IR_Data_Access condition_access = ir_data_access_create_intermediate(ir_block, upcast(types.bool_type));
                if (loop_info.is_custom_op) 
                {
                    IR_Instruction has_next_call;
                    has_next_call.type = IR_Instruction_Type::FUNCTION_CALL;
                    has_next_call.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
                    has_next_call.options.call.destination = condition_access;
                    has_next_call.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, loop_info.custom_op.fn_has_next);
                    has_next_call.options.call.arguments = dynamic_array_create<IR_Data_Access>(1);
                    IR_Data_Access argument_access = iterator_access;
                    for (int i = 0; i < loop_info.custom_op.has_next_pointer_diff; i++) {
                        argument_access = ir_data_access_create_dereference(condition_code, argument_access);
                    }
                    if (loop_info.custom_op.has_next_pointer_diff == -1) {
                        argument_access = ir_data_access_create_address_of(condition_code, iterator_access);
                    }
                    dynamic_array_push_back(&has_next_call.options.call.arguments, argument_access);
                    dynamic_array_push_back(&condition_code->instructions, has_next_call);
                }
                else 
                {
                    IR_Data_Access array_size_access;
                    if (iterable_type->type == Datatype_Type::ARRAY) {
                        auto array_type = downcast<Datatype_Array>(iterable_type);
                        assert(array_type->count_known, "");
                        array_size_access = ir_data_access_create_constant_i32(array_type->element_count);
                    }
                    else if (iterable_type->type == Datatype_Type::SLICE) {
                        auto slice_type = downcast<Datatype_Slice>(iterable_type);
                        array_size_access = ir_data_access_create_member(condition_code, iterable_access, slice_type->size_member);
                    }

                    IR_Instruction comparison;
                    comparison.type = IR_Instruction_Type::BINARY_OP;
                    comparison.options.binary_op.type = AST::Binop::LESS;
                    comparison.options.binary_op.operand_left = index_access;
                    comparison.options.binary_op.operand_right = array_size_access;
                    comparison.options.binary_op.destination = condition_access;
                    dynamic_array_push_back(&condition_code->instructions, comparison);
                }

                // Push loop + create body code
                IR_Instruction instr;
                instr.type = IR_Instruction_Type::WHILE;
                instr.options.while_instr.condition_code = condition_code;
                instr.options.while_instr.condition_access = condition_access;
                instr.options.while_instr.code = ir_code_block_create(ir_block->function);

                // Create get_value call for custom operators
                if (loop_info.is_custom_op) {
                    IR_Instruction get_value_call;
                    get_value_call.type = IR_Instruction_Type::FUNCTION_CALL;
                    get_value_call.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
                    get_value_call.options.call.destination = loop_variable_access;
                    get_value_call.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, loop_info.custom_op.fn_get_value);
                    get_value_call.options.call.arguments = dynamic_array_create<IR_Data_Access>(1);
                    IR_Data_Access argument_access = iterator_access;
                    for (int i = 0; i < loop_info.custom_op.has_next_pointer_diff; i++) {
                        argument_access = ir_data_access_create_dereference(condition_code, argument_access);
                    }
                    if (loop_info.custom_op.has_next_pointer_diff == -1) {
                        argument_access = ir_data_access_create_address_of(condition_code, iterator_access);
                    }
                    dynamic_array_push_back(&get_value_call.options.call.arguments, argument_access);
                    dynamic_array_push_back(&instr.options.while_instr.code->instructions, get_value_call);
                }
                ir_generator_generate_block(instr.options.while_instr.code, foreach_loop.body_block);

                dynamic_array_push_back(&ir_block->instructions, instr);

                // Push increment instruction at end of while block
                ir_generator_generate_block_loop_increment(instr.options.while_instr.code, foreach_loop.body_block);
            }

            // Push break label
            {
                IR_Instruction break_label;
                break_label.type = IR_Instruction_Type::LABEL;
                break_label.options.label_index = ir_generator.next_label_index;
                ir_generator.next_label_index++;
                dynamic_array_push_back(&ir_block->instructions, break_label);
                hashtable_insert_element(&ir_generator.labels_break, foreach_loop.body_block, break_label.options.label_index);
            }
        }
        break;
    }
    case AST::Statement_Type::WHILE_STATEMENT:
    {
        IR_Instruction continue_label;
        continue_label.type = IR_Instruction_Type::LABEL;
        continue_label.options.label_index = ir_generator.next_label_index;
        ir_generator.next_label_index++;
        dynamic_array_push_back(&ir_block->instructions, continue_label);
        hashtable_insert_element(&ir_generator.labels_continue, statement->options.while_statement.block, continue_label.options.label_index);

        IR_Instruction instr;
        instr.type = IR_Instruction_Type::WHILE;
        instr.options.while_instr.condition_code = ir_code_block_create(ir_block->function);
        instr.options.while_instr.condition_access = ir_generator_generate_expression(
            instr.options.while_instr.condition_code, statement->options.if_statement.condition
        );
        instr.options.while_instr.code = ir_code_block_create(ir_block->function);
        ir_generator_generate_block(instr.options.while_instr.code, statement->options.while_statement.block);
        dynamic_array_push_back(&ir_block->instructions, instr);

        IR_Instruction break_label;
        break_label.type = IR_Instruction_Type::LABEL;
        break_label.options.label_index = ir_generator.next_label_index;
        ir_generator.next_label_index++;
        dynamic_array_push_back(&ir_block->instructions, break_label);
        hashtable_insert_element(&ir_generator.labels_break, statement->options.while_statement.block, break_label.options.label_index);

        break;
    }
    case AST::Statement_Type::BREAK_STATEMENT:
    case AST::Statement_Type::CONTINUE_STATEMENT:
    {
        bool is_continue = statement->type == AST::Statement_Type::CONTINUE_STATEMENT;
        auto goto_block = stat_info->specifics.block;
        ir_generator_work_through_defers(ir_block, *hashtable_find_element(&ir_generator.block_defer_depths, goto_block), false);

        // Push loop increment instructions if they are available
        if (is_continue) {
            ir_generator_generate_block_loop_increment(ir_block, goto_block);
        }

        IR_Instruction instr;
        instr.type = IR_Instruction_Type::GOTO;
        dynamic_array_push_back(&ir_block->instructions, instr);

        Unresolved_Goto fill_out;
        fill_out.block = ir_block;
        fill_out.instruction_index = ir_block->instructions.size - 1;
        fill_out.break_block = goto_block;
        dynamic_array_push_back(is_continue ? &ir_generator.fill_out_continues : &ir_generator.fill_out_breaks, fill_out);
        break;
    }
    case AST::Statement_Type::RETURN_STATEMENT:
    {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::RETURN;
        if (statement->options.return_value.available) {
            instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_DATA;
            instr.options.return_instr.options.return_value = ir_generator_generate_expression(ir_block, statement->options.return_value.value);
            if (ir_generator.defer_stack.size != 0) {
                // Copy the generated expression to another location, so defers cannot interfere
                IR_Instruction move;
                move.type = IR_Instruction_Type::MOVE;
                move.options.move.source = instr.options.return_instr.options.return_value;
                move.options.move.destination = ir_data_access_create_intermediate(ir_block, ir_data_access_get_type(&move.options.move.source));
                dynamic_array_push_back(&ir_block->instructions, move);
                instr.options.return_instr.options.return_value = move.options.move.destination;
            }
        }
        else {
            instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_EMPTY;
        }

        ir_generator_work_through_defers(ir_block, 0, false);
        dynamic_array_push_back(&ir_block->instructions, instr);
        break;
    }
    case AST::Statement_Type::IMPORT: {
        break; // Imports don't generate code
    }
    case AST::Statement_Type::EXPRESSION_STATEMENT: {
        ir_generator_generate_expression(ir_block, statement->options.expression);
        break;
    }
    case AST::Statement_Type::BINOP_ASSIGNMENT:
    {
        auto info = get_info(statement);
        auto& assign = statement->options.binop_assignment;

        IR_Data_Access left_access = ir_generator_generate_expression(ir_block, assign.left_side);
        Datatype* type = ir_data_access_get_type(&left_access);
        IR_Data_Access right_access = ir_generator_generate_expression(ir_block, assign.right_side);
        IR_Data_Access result_access = ir_data_access_create_intermediate(ir_block, type);

        IR_Instruction assignment;
        assignment.type = IR_Instruction_Type::MOVE;
        assignment.options.move.destination = left_access;
        assignment.options.move.source = result_access;

        // Handle binop
        if (info->specifics.overload.function == 0)
        {
            IR_Instruction binop;
            binop.type = IR_Instruction_Type::BINARY_OP;
            binop.options.binary_op.type = assign.binop;
            binop.options.binary_op.destination = result_access;
            binop.options.binary_op.operand_left = left_access;
            binop.options.binary_op.operand_right = right_access;
            dynamic_array_push_back(&ir_block->instructions, binop);
        }
        else
        {
            IR_Instruction call;
            call.type = IR_Instruction_Type::FUNCTION_CALL;
            call.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            call.options.call.destination = result_access;
            call.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, info->specifics.overload.function);
            call.options.call.arguments = dynamic_array_create<IR_Data_Access>(2);
            if (info->specifics.overload.switch_arguments) {
                dynamic_array_push_back(&call.options.call.arguments, right_access);
                dynamic_array_push_back(&call.options.call.arguments, left_access);
            }
            else {
                dynamic_array_push_back(&call.options.call.arguments, left_access);
                dynamic_array_push_back(&call.options.call.arguments, right_access);
            }
            dynamic_array_push_back(&ir_block->instructions, call);
        }

        // Push assignment
        dynamic_array_push_back(&ir_block->instructions, assignment);
        break;
    }
    case AST::Statement_Type::ASSIGNMENT:
    {
        auto& as = statement->options.assignment;
        bool is_struct_split = get_info(statement)->specifics.is_struct_split;

        IR_Data_Access broadcast_value;
        Datatype* broadcast_type = 0;
        if (as.right_side.size == 1) {
            broadcast_value = ir_generator_generate_expression(ir_block, as.right_side[0]);
            broadcast_type = ir_data_access_get_type(&broadcast_value);
        }

        for (int i = 0; i < as.left_side.size; i++)
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::MOVE;
            instr.options.move.destination = ir_generator_generate_expression(ir_block, as.left_side[i]);
            if (as.right_side.size == 1) {
                if (is_struct_split) {
                    assert(broadcast_type->type == Datatype_Type::STRUCT, "");
                    instr.options.move.source = ir_data_access_create_member(
                        ir_block, broadcast_value, downcast<Datatype_Struct>(broadcast_type)->members[i]);
                }
                else {
                    instr.options.move.source = broadcast_value;
                }
            }
            else {
                instr.options.move.source = ir_generator_generate_expression(ir_block, as.right_side[i]);
            }

            if ((int)instr.options.move.source.type < 0 || (int)instr.options.move.source.type > 6) {
                panic("HEY");
            }
            dynamic_array_push_back(&ir_block->instructions, instr);
        }
        break;
    }
    case AST::Statement_Type::DELETE_STATEMENT:
    {
        // FUTURE: At some point this will access the Context struct for the free func, and also source_parse the size
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::FUNCTION_CALL;
        instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
        instr.options.call.options.hardcoded.type = Hardcoded_Type::FREE_POINTER;
        instr.options.call.options.hardcoded.signature = hardcoded_type_to_signature(Hardcoded_Type::FREE_POINTER);
        instr.options.call.arguments = dynamic_array_create<IR_Data_Access>(1);

        IR_Data_Access delete_access = ir_generator_generate_expression(ir_block, statement->options.delete_expr);
        auto delete_type = ir_data_access_get_type(&delete_access);
        if (delete_type->type == Datatype_Type::SLICE) {
            delete_access = ir_data_access_create_member(
                ir_block, delete_access, downcast<Datatype_Slice>(delete_type)->data_member);
        }

        dynamic_array_push_back(&instr.options.call.arguments, delete_access);
        dynamic_array_push_back(&ir_block->instructions, instr);
        break;
    }
    default: panic("Statment type invalid!");
    }
}

void ir_generator_generate_block(IR_Code_Block* ir_block, AST::Code_Block* ast_block)
{
    int defer_start_index = ir_generator.defer_stack.size;
    hashtable_insert_element(&ir_generator.block_defer_depths, ast_block, defer_start_index);

    // Generate code
    for (int i = 0; i < ast_block->statements.size; i++) {
        ir_generator_generate_statement(ast_block->statements[i], ir_block);
    }

    ir_generator_work_through_defers(ir_block, defer_start_index, true);
}



// Queueing
void ir_generator_generate_queued_items(bool gen_bytecode)
{
    Timing_Task before_task = compiler.task_current;
    SCOPE_EXIT(compiler_switch_timing_task(before_task));
    compiler_switch_timing_task(Timing_Task::CODE_GEN);

    // Generate Blocks
    for (int i = 0; i < ir_generator.queue_functions.size; i++)
    {
        auto& stub = ir_generator.queue_functions[i];
        ModTree_Function* mod_func = stub.mod_func;
        IR_Function* ir_func = stub.ir_func;
        if (mod_func == 0) {
            assert(ir_generator.program->entry_function == ir_func, "Only entry function doesn't have a mod_func!");
            if (gen_bytecode) {
                bytecode_generator_compile_function(compiler.bytecode_generator, ir_func);
            }
            continue;
        }
        ir_generator.current_pass = mod_func->code_workload->current_pass;

        // Generate function code
        {
            assert(mod_func->code_workload != 0, "");
            if (mod_func->code_workload->type == Analysis_Workload_Type::BAKE_ANALYSIS)
            {
                auto bake_node = ((Workload_Bake_Analysis*)mod_func->code_workload)->bake_node;
                if (bake_node->type == AST::Expression_Type::BAKE_EXPR) {
                    IR_Instruction return_instr;
                    return_instr.type = IR_Instruction_Type::RETURN;
                    return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_DATA;
                    return_instr.options.return_instr.options.return_value = ir_generator_generate_expression(ir_func->code, bake_node->options.bake_expr);
                    dynamic_array_push_back(&ir_func->code->instructions, return_instr);
                }
                else if (bake_node->type == AST::Expression_Type::BAKE_BLOCK) {
                    ir_generator_generate_block(ir_func->code, bake_node->options.bake_block);
                }
                else {
                    panic("Shoudn't happen!");
                }
            }
            else if (mod_func->code_workload->type == Analysis_Workload_Type::FUNCTION_BODY) {
                ir_generator_generate_block(ir_func->code, ((Workload_Function_Body*)mod_func->code_workload)->body_node);
            }
            else {
                panic("");
            }
        }

        // Add empty return
        if (!ir_func->function_type->return_type.available) {
            IR_Instruction return_instr;
            return_instr.type = IR_Instruction_Type::RETURN;
            return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_EMPTY;
            dynamic_array_push_back(&ir_func->code->instructions, return_instr);
        }

        // Fill out breaks and continues
        for (int j = 0; j < ir_generator.fill_out_breaks.size; j++) {
            Unresolved_Goto fill_out = ir_generator.fill_out_breaks[j];
            int label_index = *hashtable_find_element(&ir_generator.labels_break, fill_out.break_block);
            assert(fill_out.block->instructions[fill_out.instruction_index].type == IR_Instruction_Type::GOTO, "");
            fill_out.block->instructions[fill_out.instruction_index].options.label_index = label_index;
        }
        for (int j = 0; j < ir_generator.fill_out_continues.size; j++) {
            Unresolved_Goto fill_out = ir_generator.fill_out_continues[j];
            int label_index = *hashtable_find_element(&ir_generator.labels_continue, fill_out.break_block);
            assert(fill_out.block->instructions[fill_out.instruction_index].type == IR_Instruction_Type::GOTO, "");
            fill_out.block->instructions[fill_out.instruction_index].options.label_index = label_index;
        }
        dynamic_array_reset(&ir_generator.fill_out_breaks);
        dynamic_array_reset(&ir_generator.fill_out_continues);
        hashtable_reset(&ir_generator.variable_mapping);
        hashtable_reset(&ir_generator.labels_break);
        hashtable_reset(&ir_generator.labels_continue);
        hashtable_reset(&ir_generator.block_defer_depths);
        hashtable_reset(&ir_generator.loop_increment_instructions);

        if (gen_bytecode) {
            bytecode_generator_compile_function(compiler.bytecode_generator, ir_func);
        }
    }
    dynamic_array_reset(&ir_generator.queue_functions);

    // Do bytecode stuff
    if (gen_bytecode) {
        bytecode_generator_update_references(compiler.bytecode_generator);
    }
}

void ir_generator_queue_function(ModTree_Function* function) {
    if (!function->is_runnable) {
        return;
    }
    if (function->progress != 0) {
        assert((function->progress->type != Polymorphic_Analysis_Type::POLYMORPHIC_BASE), "Function cannot be polymorhic here!");
    }
    if (hashtable_find_element(&ir_generator.function_mapping, function) != 0) return;
    ir_function_create(function->signature, function);
}

void ir_generator_finish(bool gen_bytecode)
{
    // Queue and generate all functions
    for (int i = 0; i < ir_generator.modtree->functions.size; i++) {
        ir_generator_queue_function(ir_generator.modtree->functions[i]);
    }
    ir_generator_generate_queued_items(gen_bytecode);

    // Generate entry function
    {
        auto& type_system = compiler.type_system;
        auto entry_function = ir_function_create(type_system_make_function({}), 0);
        ir_generator.program->entry_function = entry_function;

        // Initialize all globals
        auto& globals = compiler.semantic_analyser->program->globals;
        for (int i = 0; i < globals.size; i++)
        {
            auto global = globals[i];
            if (!global->has_initial_value) continue;

            ir_generator.current_pass = global->definition_workload->base.current_pass;
            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.destination = ir_data_access_create_global(global);
            move_instr.options.move.source = ir_generator_generate_expression(entry_function->code, global->init_expr);
            dynamic_array_push_back(&entry_function->code->instructions, move_instr);
        }

        // Call main
        {
            IR_Instruction call_instr;
            call_instr.type = IR_Instruction_Type::FUNCTION_CALL;
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            call_instr.options.call.arguments = dynamic_array_create<IR_Data_Access>(1);
            call_instr.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, ir_generator.modtree->main_function);
            dynamic_array_push_back(&entry_function->code->instructions, call_instr);
        }

        // Exit
        {
            IR_Instruction exit_instr;
            exit_instr.type = IR_Instruction_Type::RETURN;
            exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
            exit_instr.options.return_instr.options.exit_code = Exit_Code::SUCCESS;
            dynamic_array_push_back(&entry_function->code->instructions, exit_instr);
        }

        // Queue and generate entry function
        Function_Stub entry_stub;
        entry_stub.mod_func = 0;
        entry_stub.ir_func = entry_function;
        dynamic_array_push_back(&ir_generator.queue_functions, entry_stub);
        ir_generator_generate_queued_items(gen_bytecode);
    }
}

// Generator
IR_Generator ir_generator;

IR_Generator* ir_generator_initialize()
{
    ir_generator.program = 0;
    ir_generator.next_label_index = 0;

    ir_generator.function_mapping = hashtable_create_pointer_empty<ModTree_Function*, IR_Function*>(8);
    ir_generator.loop_increment_instructions = hashtable_create_pointer_empty<AST::Code_Block*, Loop_Increment>(8);
    ir_generator.variable_mapping = hashtable_create_pointer_empty<AST::Definition_Symbol*, IR_Data_Access>(8);
    ir_generator.labels_break = hashtable_create_pointer_empty<AST::Code_Block*, int>(8);
    ir_generator.labels_continue = hashtable_create_pointer_empty<AST::Code_Block*, int>(8);
    ir_generator.block_defer_depths = hashtable_create_pointer_empty<AST::Code_Block*, int>(8);

    ir_generator.queue_functions = dynamic_array_create<Function_Stub>(8);
    ir_generator.defer_stack = dynamic_array_create<AST::Code_Block*>(8);
    ir_generator.fill_out_breaks = dynamic_array_create<Unresolved_Goto>(8);
    ir_generator.fill_out_continues = dynamic_array_create<Unresolved_Goto>(8);
    return &ir_generator;
}

void ir_generator_reset()
{
    if (ir_generator.program != 0) {
        ir_program_destroy(ir_generator.program);
    }
    ir_generator.program = ir_program_create(&compiler.type_system);
    ir_generator.next_label_index = 0;
    ir_generator.modtree = compiler.semantic_analyser->program;

    hashtable_reset(&ir_generator.variable_mapping);
    hashtable_reset(&ir_generator.function_mapping);
    hashtable_reset(&ir_generator.labels_break);
    hashtable_reset(&ir_generator.labels_continue);
    hashtable_reset(&ir_generator.block_defer_depths);
    hashtable_reset(&ir_generator.loop_increment_instructions);

    dynamic_array_reset(&ir_generator.defer_stack);
    dynamic_array_reset(&ir_generator.queue_functions);
    dynamic_array_reset(&ir_generator.fill_out_breaks);
    dynamic_array_reset(&ir_generator.fill_out_continues);
}

void ir_generator_destroy()
{
    if (ir_generator.program != 0) {
        ir_program_destroy(ir_generator.program);
    }
    hashtable_destroy(&ir_generator.function_mapping);
    hashtable_destroy(&ir_generator.variable_mapping);
    hashtable_destroy(&ir_generator.labels_break);
    hashtable_destroy(&ir_generator.labels_continue);
    hashtable_destroy(&ir_generator.block_defer_depths);
    hashtable_destroy(&ir_generator.loop_increment_instructions);

    dynamic_array_destroy(&ir_generator.defer_stack);
    dynamic_array_destroy(&ir_generator.queue_functions);
    dynamic_array_destroy(&ir_generator.fill_out_breaks);
    dynamic_array_destroy(&ir_generator.fill_out_continues);
}


