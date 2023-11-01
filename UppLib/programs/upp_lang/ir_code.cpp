#include "ir_code.hpp"
#include "compiler.hpp"
#include "bytecode_generator.hpp"
#include "ast.hpp"
#include "symbol_table.hpp"

void ir_generator_generate_block(IR_Code_Block* ir_block, AST::Code_Block* ast_block);
IR_Data_Access ir_generator_generate_expression(IR_Code_Block* ir_block, AST::Expression* expression);



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
    block->instructions = dynamic_array_create_empty<IR_Instruction>(64);
    block->registers = dynamic_array_create_empty<Type_Signature*>(32);
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

IR_Function* ir_function_create(Type_Signature* signature, ModTree_Function* origin_func)
{
    IR_Function* function = new IR_Function();
    function->code = ir_code_block_create(function);
    function->function_type = signature;
    function->program = ir_generator.program;
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
    result->functions = dynamic_array_create_empty<IR_Function*>(32);
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
        type_signature_append_to_string(string, constant->type);
        string_append_formated(string, " ", access->index);
        type_signature_append_value_to_string(constant->type, &compiler.constant_pool.buffer[constant->offset], string);
        break;
    }
    case IR_Data_Access_Type::GLOBAL_DATA: {
        string_append_formated(string, "Global #%d, type: ", access->index);
        type_signature_append_to_string(string, ir_data_access_get_type(access));
        break;
    }
    case IR_Data_Access_Type::PARAMETER: {
        Type_Signature* sig = access->option.function->function_type->options.function.parameters[access->index].type;
        string_append_formated(string, "Param #%d, type: ", access->index);
        type_signature_append_to_string(string, sig);
        break;
    }
    case IR_Data_Access_Type::REGISTER: {
        Type_Signature* sig = access->option.definition_block->registers[access->index];
        string_append_formated(string, "Register #%d, type: ", access->index);
        type_signature_append_to_string(string, sig);
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
            type_signature_append_to_string(string, address_of->options.member.type);
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

        Type_Signature* function_sig;
        switch (call->call_type)
        {
        case IR_Instruction_Call_Type::FUNCTION_CALL:
            function_sig = call->options.function->function_type;
            break;
        case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL:
            function_sig = ir_data_access_get_type(&call->options.pointer_access);
            break;
        case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
            function_sig = 0;
            break;
        case IR_Instruction_Call_Type::EXTERN_FUNCTION_CALL:
            function_sig = call->options.extern_function.function_signature;
            break;
        default:
            panic("Hey");
            return;
        }
        if (function_sig != 0) {
            if (function_sig->options.function.return_type->type != Signature_Type::VOID_TYPE) {
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
            type_signature_append_to_string(string, call->options.extern_function.function_signature);
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
        Type_Signature* enum_type = ir_data_access_get_type(&instruction->options.switch_instr.condition_access);
        if (enum_type->type == Signature_Type::STRUCT) {
            assert(enum_type->options.structure.struct_type == AST::Structure_Type::UNION, "");
            enum_type = enum_type->options.structure.tag_member.type;
        }
        string_append_formated(string, "SWITCH\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "Condition access: ");
        ir_data_access_append_to_string(&instruction->options.switch_instr.condition_access, string, code_block);
        string_append_formated(string, "\n");
        for (int i = 0; i < instruction->options.switch_instr.cases.size; i++) {
            IR_Switch_Case* switch_case = &instruction->options.switch_instr.cases[i];
            indent_string(string, indentation + 1);
            Optional<Enum_Item> member = enum_type_find_member_by_value(enum_type, switch_case->value);
            assert(member.available, "");
            string_append_formated(string, "Case %s: \n", member.value.id->characters);
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
        type_signature_append_to_string(string, code_block->registers[i]);
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
    type_signature_append_to_string(string, function->function_type);
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
Type_Signature* ir_data_access_get_type(IR_Data_Access* access)
{
    Type_Signature* sig = 0;
    switch (access->type)
    {
    case IR_Data_Access_Type::GLOBAL_DATA:
        return ir_generator.modtree->globals[access->index]->type;
        break;
    case IR_Data_Access_Type::CONSTANT:
        sig = compiler.constant_pool.constants[access->index].type;
        break;
    case IR_Data_Access_Type::REGISTER:
        sig = access->option.definition_block->registers[access->index];
        break;
    case IR_Data_Access_Type::PARAMETER:
        sig = access->option.function->function_type->options.function.parameters[access->index].type;
        break;
    default: panic("Hey!");
    }
    if (access->is_memory_access) {
        assert(sig->type == Signature_Type::POINTER, "");
        return sig->options.pointer_child;
    }
    return sig;
}

IR_Data_Access ir_data_access_create_global(ModTree_Global* global)
{
    IR_Data_Access access;
    access.type = IR_Data_Access_Type::GLOBAL_DATA;
    access.index = global->index;
    access.is_memory_access = false;
    return access;
}

IR_Data_Access ir_data_access_create_intermediate(IR_Code_Block* block, Type_Signature* signature)
{
    assert(block != 0, "");
    IR_Data_Access access;
    if (signature->type == Signature_Type::VOID_TYPE) {
        access.is_memory_access = false;
        access.type = IR_Data_Access_Type::GLOBAL_DATA;
        access.index = 0;
        return access;
    }
    access.is_memory_access = false;
    access.type = IR_Data_Access_Type::REGISTER;
    access.option.definition_block = block;
    assert(signature->type != Signature_Type::UNKNOWN_TYPE, "Cannot have register with unknown type");
    assert(!(signature->size == 0), "Cannot have register with 0 size!");
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
    Type_Signature* ptr_type = ir_data_access_get_type(&access);
    assert(ptr_type->type == Signature_Type::POINTER, "");
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
    instr.options.address_of.destination = ir_data_access_create_intermediate(block, type_system_make_pointer(&compiler.type_system, ir_data_access_get_type(&access)));
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
    member_instr.options.address_of.destination = ir_data_access_create_intermediate(block, type_system_make_pointer(&compiler.type_system, member.type));
    dynamic_array_push_back(&block->instructions, member_instr);

    member_instr.options.address_of.destination.is_memory_access = true;
    return member_instr.options.address_of.destination;
}

IR_Data_Access ir_data_access_create_constant(Type_Signature* signature, Array<byte> bytes)
{
    IR_Data_Access access;
    access.is_memory_access = false;
    access.type = IR_Data_Access_Type::CONSTANT;
    Constant_Result result = constant_pool_add_constant(&compiler.constant_pool, signature, bytes);
    assert(result.status == Constant_Status::SUCCESS, "Must always work");
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
        compiler.type_system.predefined_types.i32_type,
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

IR_Data_Access ir_generator_generate_cast(IR_Code_Block* ir_block, IR_Data_Access source, Type_Signature* result_type, Info_Cast_Type cast_type)
{
    if (cast_type == Info_Cast_Type::NO_CAST) return source;
    auto type_system = &compiler.type_system;
    auto& types = type_system->predefined_types;
    auto source_type = ir_data_access_get_type(&source);
    auto make_simple_cast = [](IR_Code_Block* ir_block, IR_Data_Access source, Type_Signature* result_type, IR_Cast_Type cast_type) -> IR_Data_Access
    {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::CAST;
        instr.options.cast.type = cast_type;
        instr.options.cast.source = source;
        instr.options.cast.destination = ir_data_access_create_intermediate(ir_block, result_type);
        dynamic_array_push_back(&ir_block->instructions, instr);
        return instr.options.cast.destination;
    };

    IR_Data_Access result_access;
    switch (cast_type)
    {
    case Info_Cast_Type::INVALID:
    case Info_Cast_Type::NO_CAST:
        panic("Should have been handled elsewhere!");
    case Info_Cast_Type::INTEGERS: {
        result_access = make_simple_cast(ir_block, source, result_type, IR_Cast_Type::INTEGERS);
        break;
    }
    case Info_Cast_Type::FLOATS: {
        result_access = make_simple_cast(ir_block, source, result_type, IR_Cast_Type::FLOATS);
        break;
    }
    case Info_Cast_Type::FLOAT_TO_INT: {
        result_access = make_simple_cast(ir_block, source, result_type, IR_Cast_Type::FLOAT_TO_INT);
        break;
    }
    case Info_Cast_Type::INT_TO_FLOAT: {
        result_access = make_simple_cast(ir_block, source, result_type, IR_Cast_Type::INT_TO_FLOAT);
        break;
    }
    case Info_Cast_Type::POINTERS: {
        result_access = make_simple_cast(ir_block, source, result_type, IR_Cast_Type::POINTERS);
        break;
    }
    case Info_Cast_Type::POINTER_TO_U64: {
        result_access = make_simple_cast(ir_block, source, result_type, IR_Cast_Type::POINTER_TO_U64);
        break;
    }
    case Info_Cast_Type::U64_TO_POINTER: {
        result_access = make_simple_cast(ir_block, source, result_type, IR_Cast_Type::U64_TO_POINTER);
        break;
    }
    case Info_Cast_Type::ENUM_TO_INT: {
        result_access = make_simple_cast(ir_block, source, result_type, IR_Cast_Type::ENUM_TO_INT);
        break;
    }
    case Info_Cast_Type::INT_TO_ENUM: {
        result_access = make_simple_cast(ir_block, source, result_type, IR_Cast_Type::INT_TO_ENUM);
        break;
    }
    case Info_Cast_Type::ARRAY_TO_SLICE:
    {
        Type_Signature* slice_type = result_type;
        Type_Signature* array_type = source_type;
        assert(slice_type->type == Signature_Type::SLICE, "");
        assert(array_type->type == Signature_Type::ARRAY, "");
        IR_Data_Access slice_access = ir_data_access_create_intermediate(ir_block, slice_type);
        // Set size
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::MOVE;
            instr.options.move.destination = ir_data_access_create_member(ir_block, slice_access, slice_type->options.slice.size_member);
            instr.options.move.source = ir_data_access_create_constant_i32(array_type->options.array.element_count);
            dynamic_array_push_back(&ir_block->instructions, instr);
        }
        // Set data
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::ADDRESS_OF;
            instr.options.address_of.type = IR_Instruction_Address_Of_Type::DATA;
            instr.options.address_of.source = source;
            instr.options.address_of.destination = ir_data_access_create_member(ir_block, slice_access, slice_type->options.slice.data_member);
            dynamic_array_push_back(&ir_block->instructions, instr);
        }
        return slice_access;
    }
    case Info_Cast_Type::FROM_ANY:
    {
        // Check if type matches given type, if not error out
        IR_Data_Access access_operand = source;
        IR_Data_Access access_valid_cast = ir_data_access_create_intermediate(ir_block, types.bool_type);
        IR_Data_Access access_result = ir_data_access_create_intermediate(ir_block, result_type);
        {
            IR_Instruction cmp_instr;
            cmp_instr.type = IR_Instruction_Type::BINARY_OP;
            cmp_instr.options.binary_op.type = AST::Binop::EQUAL;
            cmp_instr.options.binary_op.operand_left = ir_data_access_create_constant(
                types.type_type,
                array_create_static_as_bytes<u64>(&result_type->internal_index, 1)
            );
            cmp_instr.options.binary_op.operand_right = ir_data_access_create_member(ir_block, access_operand,
                types.any_type->options.structure.members[1]
            );
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
            IR_Data_Access any_data_access = ir_data_access_create_member(branch_valid, access_operand,
                types.any_type->options.structure.members[0]
            );
            {
                IR_Instruction cast_instr;
                cast_instr.type = IR_Instruction_Type::CAST;
                cast_instr.options.cast.type = IR_Cast_Type::POINTERS;
                cast_instr.options.cast.source = any_data_access;
                cast_instr.options.cast.destination = ir_data_access_create_intermediate(
                    branch_valid, type_system_make_pointer(&compiler.type_system, result_type)
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
    case Info_Cast_Type::TO_ANY:
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

        IR_Data_Access any_access = ir_data_access_create_intermediate(ir_block, types.any_type);
        Type_Signature* any_type = types.any_type;
        // Set data
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::ADDRESS_OF;
            instr.options.address_of.type = IR_Instruction_Address_Of_Type::DATA;
            instr.options.address_of.source = operand_access;
            // In theory a cast from pointer to voidptr would be better, but I think I can ignore it
            instr.options.address_of.destination = ir_data_access_create_member(
                ir_block, any_access, any_type->options.structure.members[0]
            );
            dynamic_array_push_back(&ir_block->instructions, instr);
        }
        // Set type
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::MOVE;
            instr.options.move.destination = ir_data_access_create_member(
                ir_block, any_access, any_type->options.structure.members[1]
            );
            instr.options.move.source = ir_data_access_create_constant(
                types.type_type,
                array_create_static_as_bytes(&source_type->internal_index, 1)
            );
            dynamic_array_push_back(&ir_block->instructions, instr);
        }
        return any_access;
    }
    default: panic("");
    }
    return result_access;
}

IR_Data_Access ir_generator_generate_expression_no_cast(IR_Code_Block* ir_block, AST::Expression* expression)
{
    auto info = get_info(expression);
    auto result_type = expression_info_get_type(info);
    auto type_system = &compiler.type_system;
    auto& types = type_system->predefined_types;
    assert(!info->contains_errors, "Cannot contain errors!"); 

    // Handle different expression results
    switch (info->result_type)
    {
    case Expression_Result_Type::CONSTANT:
        return ir_data_access_create_constant(info->options.constant);
    case Expression_Result_Type::TYPE:
        return ir_data_access_create_constant(types.u64_type, array_create_static_as_bytes(&result_type->internal_index, 1));
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
        IR_Data_Access access = ir_generator_generate_expression(ir_block, expression->options.unop.expr);
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
    case AST::Expression_Type::FUNCTION_CALL:
    {
        auto& call = expression->options.call;
        auto call_info = get_info(call.expr);

        IR_Instruction call_instr;
        call_instr.type = IR_Instruction_Type::FUNCTION_CALL;
        call_instr.options.call.destination = ir_data_access_create_intermediate(ir_block, result_type);
        switch (call_info->result_type)
        {
        case Expression_Result_Type::FUNCTION: {
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            call_instr.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, call_info->options.function);
            break;
        }
        case Expression_Result_Type::VALUE: {
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_POINTER_CALL;
            call_instr.options.call.options.pointer_access = ir_generator_generate_expression(ir_block, call.expr);
            break;
        }
        case Expression_Result_Type::CONSTANT: {
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_POINTER_CALL;
            call_instr.options.call.options.pointer_access = ir_data_access_create_constant(call_info->options.constant);
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
                return call_instr.options.call.destination;
            }
            case Hardcoded_Type::TYPE_INFO:
            {
                IR_Data_Access index_access = ir_generator_generate_expression(ir_block, call.arguments[0]->value);
                auto index_type = ir_data_access_get_type(&index_access);
                if (type_signature_equals(index_type, types.type_type)) {
                    index_access = ir_generator_generate_cast(ir_block, index_access, types.i32_type, Info_Cast_Type::INTEGERS);
                }
                else {
                    panic("Should be type_type!");
                }

                // Array index access
                IR_Instruction instr;
                instr.type = IR_Instruction_Type::ADDRESS_OF;
                instr.options.address_of.destination = ir_data_access_create_intermediate(ir_block, type_system_make_pointer(type_system, types.type_information_type));
                instr.options.address_of.type = IR_Instruction_Address_Of_Type::ARRAY_ELEMENT;
                instr.options.address_of.source = ir_data_access_create_global(compiler.semantic_analyser->global_type_informations);
                instr.options.address_of.options.index_access = index_access;
                dynamic_array_push_back(&ir_block->instructions, instr);
                instr.options.address_of.destination.is_memory_access = true;

                return instr.options.address_of.destination;
            }
            case Hardcoded_Type::TYPE_OF: {
                panic("Should be handled in semantic analyser");
                break;
            }
            }
            call_instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
            call_instr.options.call.options.hardcoded.type = hardcoded;
            call_instr.options.call.options.hardcoded.signature = hardcoded_type_to_signature(hardcoded);
            break;
        }
        case Expression_Result_Type::POLYMORPHIC_FUNCTION: 
        {
            auto function = call_info->options.polymorphic.instance->progress->function;
            assert(function->is_runnable, "Instances that reach ir-generator must not be polymorphic!");
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            call_instr.options.call.options.function = *hashtable_find_element(&ir_generator.function_mapping, function);
            break;
        }
        case Expression_Result_Type::TYPE: {
            panic("Must not happen after semantic analysis!");
            break;
        }
        default: panic("");
        }

        // Generate arguments 
        call_instr.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(call.arguments.size);
        auto function_signature = get_info(expression)->specifics.function_call_signature;

        // Add default/dummy arguments
        for (int i = 0; i < function_signature->options.function.parameters.size; i++) {
            auto& param = function_signature->options.function.parameters[i];
            if (param.has_default_value) {
                // Initialize all default arguments with their default value, if they are supplied, this will be overwritten
                assert(param.default_value.available, "Must be, otherwise we shouldn't get to this point");
                dynamic_array_push_back(&call_instr.options.call.arguments, ir_data_access_create_constant(param.default_value.value));
            }
            else {
                dynamic_array_push_back_dummy(&call_instr.options.call.arguments);
            }
        }

        // Generate code for arguments
        for (int j = 0; j < call.arguments.size; j++) {
            auto info = get_info(call.arguments[j]);
            if (!info->is_polymorphic) { // Skip polymorphic arguments
                call_instr.options.call.arguments[info->argument_index] = ir_generator_generate_expression(ir_block, call.arguments[j]->value);
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
            access.index = symbol->options.parameter.type_index;
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
        auto struct_init = expression->options.struct_initializer;
        for (int i = 0; i < struct_init.arguments.size; i++)
        {
            auto arg = struct_init.arguments[i];
            auto arg_info = get_info(arg);
            assert(result_type->type == Signature_Type::STRUCT, "");

            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.destination = ir_data_access_create_member(ir_block, struct_access, arg_info->member);
            move_instr.options.move.source = ir_generator_generate_expression(ir_block, arg->value);
            dynamic_array_push_back(&ir_block->instructions, move_instr);
        }

        // Initialize union tag
        if (result_type->options.structure.struct_type == AST::Structure_Type::UNION)
        {
            assert(struct_init.arguments.size == 1, "");
            auto& member = get_info(struct_init.arguments[0])->member;
            int member_index = -1;
            for (int i = 0; i < result_type->options.structure.members.size; i++) {
                if (result_type->options.structure.members[i].offset == member.offset &&
                    result_type->options.structure.members[i].id == member.id &&
                    result_type->options.structure.members[i].type == member.type) {
                    member_index = i;
                    break;
                }
            }
            assert(member_index != -1, "");

            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.destination = ir_data_access_create_member(ir_block, struct_access, result_type->options.structure.tag_member);
            move_instr.options.move.source = ir_data_access_create_constant_i32(member_index + 1);
            dynamic_array_push_back(&ir_block->instructions, move_instr);
        }
        return struct_access;
    }
    case AST::Expression_Type::ARRAY_INITIALIZER:
    {
        auto& array_init = expression->options.array_initializer;
        IR_Data_Access array_access = ir_data_access_create_intermediate(ir_block, result_type);
        if (result_type->type == Signature_Type::SLICE)
        {
            assert(array_init.values.size == 0, "");
            Upp_Slice_Base slice_base;
            slice_base.data_ptr = 0;
            slice_base.size = 0;
            auto slice_constant = constant_pool_add_constant(&compiler.constant_pool, result_type, array_create_static_as_bytes(&slice_base, 1));
            assert(slice_constant.status == Constant_Status::SUCCESS, "Empty slice must succeed!");

            IR_Instruction move;
            move.type = IR_Instruction_Type::MOVE;
            move.options.move.destination = array_access;
            move.options.move.source = ir_data_access_create_constant(slice_constant.constant);
            dynamic_array_push_back(&ir_block->instructions, move);
        }
        else
        {
            assert(result_type->type == Signature_Type::ARRAY, "");
            assert(result_type->options.array.element_count == array_init.values.size, "");
            for (int i = 0; i < array_init.values.size; i++)
            {
                auto init_expr = array_init.values[i];

                // Calculate array member pointer
                IR_Instruction element_instr;
                element_instr.type = IR_Instruction_Type::ADDRESS_OF;
                element_instr.options.address_of.type = IR_Instruction_Address_Of_Type::ARRAY_ELEMENT;
                element_instr.options.address_of.destination = ir_data_access_create_intermediate(
                    ir_block, type_system_make_pointer(&compiler.type_system, result_type->options.array.element_type)
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
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::ADDRESS_OF;
        instr.options.address_of.destination = ir_data_access_create_intermediate(ir_block, type_system_make_pointer(type_system, result_type));
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
        auto src_type = get_info(mem_access.expr)->context_ops.after_cast_type;

        // Handle special case of array.data, which basically becomes an address of
        if (src_type->type == Signature_Type::ARRAY)
        {
            assert(mem_access.name == compiler.id_data, "Member access on array must be data or handled elsewhere!");
            if (source.is_memory_access) {
                source.is_memory_access = false;
                return source;
            }

            IR_Instruction instr;
            instr.type = IR_Instruction_Type::ADDRESS_OF;
            instr.options.address_of.destination = ir_data_access_create_intermediate(ir_block, type_system_make_pointer(type_system, result_type));
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
        int element_size = type_info->options.type->size;
        if (new_expr.count_expr.available)
        {
            assert(result_type->type == Signature_Type::SLICE, "HEY");
            IR_Data_Access array_access = ir_data_access_create_intermediate(ir_block, result_type); // Array
            IR_Data_Access array_data_access = ir_data_access_create_member(ir_block, array_access, result_type->options.slice.data_member);
            IR_Data_Access array_size_access = ir_data_access_create_member(ir_block, array_access, result_type->options.slice.size_member);

            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.source = ir_generator_generate_expression(ir_block, new_expr.count_expr.value);
            move_instr.options.move.destination = array_size_access;
            dynamic_array_push_back(&ir_block->instructions, move_instr);

            IR_Instruction mult_instr;
            mult_instr.type = IR_Instruction_Type::BINARY_OP;
            mult_instr.options.binary_op.type = AST::Binop::MULTIPLICATION;
            mult_instr.options.binary_op.destination = ir_data_access_create_intermediate(ir_block, types.i32_type);
            mult_instr.options.binary_op.operand_left = ir_data_access_create_constant_i32(element_size);
            mult_instr.options.binary_op.operand_right = array_size_access;
            dynamic_array_push_back(&ir_block->instructions, mult_instr);

            IR_Instruction alloc_instr;
            alloc_instr.type = IR_Instruction_Type::FUNCTION_CALL;
            alloc_instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
            alloc_instr.options.call.options.hardcoded.type = Hardcoded_Type::MALLOC_SIZE_I32;
            alloc_instr.options.call.options.hardcoded.signature = hardcoded_type_to_signature(Hardcoded_Type::MALLOC_SIZE_I32);
            alloc_instr.options.call.destination = array_data_access;
            alloc_instr.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(1);
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
            alloc_instr.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(1);
            dynamic_array_push_back(&alloc_instr.options.call.arguments, ir_data_access_create_constant_i32(element_size));
            dynamic_array_push_back(&ir_block->instructions, alloc_instr);
            return alloc_instr.options.call.destination;
        }

        panic("Unreachable");
        break;
    }
    case AST::Expression_Type::CAST:
    {
        auto source = ir_generator_generate_expression(ir_block, expression->options.cast.operand);
        return ir_generator_generate_cast(ir_block, source, expression_info_get_type(info), info->specifics.cast_type);
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
    auto info = get_info(expression);
    auto access = ir_generator_generate_expression_no_cast(ir_block, expression);

    // Apply context operations
    if (info->context_ops.take_address_of) {
        access = ir_data_access_create_address_of(ir_block, access);
    }
    else
    {
        for (int i = 0; i < info->context_ops.deref_count; i++) {
            access = ir_data_access_create_dereference(ir_block, access);
        }
    }
    return ir_generator_generate_cast(ir_block, access, info->context_ops.after_cast_type, info->context_ops.cast);
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

void ir_generator_generate_block(IR_Code_Block* ir_block, AST::Code_Block* ast_block)
{
    int defer_start_index = ir_generator.defer_stack.size;
    hashtable_insert_element(&ir_generator.block_defer_depths, ast_block, defer_start_index);

    // Generate code
    for (int i = 0; i < ast_block->statements.size; i++)
    {
        auto statement = ast_block->statements[i];
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
                continue;
            }

            IR_Data_Access broadcast_value;
            Type_Signature* broadcast_type;
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
                        assert(broadcast_type->type == Signature_Type::STRUCT, "");
                        move.options.move.source = ir_data_access_create_member(ir_block, broadcast_value, broadcast_type->options.structure.members[i]);
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
            instr.options.switch_instr.cases = dynamic_array_create_empty<IR_Switch_Case>(statement->options.switch_statement.cases.size);

            // Check for union access
            auto cond_type = ir_data_access_get_type(&instr.options.switch_instr.condition_access);
            if (cond_type->type == Signature_Type::STRUCT) {
                assert(cond_type->options.structure.struct_type == AST::Structure_Type::UNION, "");
                instr.options.switch_instr.condition_access = ir_data_access_create_member(ir_block, instr.options.switch_instr.condition_access, cond_type->options.structure.tag_member);
            }
            else {
                assert(cond_type->type == Signature_Type::ENUM, "");
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
        case AST::Statement_Type::ASSIGNMENT: 
        {
            auto& as = statement->options.assignment;
            bool is_struct_split = get_info(statement)->specifics.is_struct_split;

            IR_Data_Access broadcast_value;
            Type_Signature* broadcast_type = 0;
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
                        assert(broadcast_type->type == Signature_Type::STRUCT, "");
                        instr.options.move.source = ir_data_access_create_member(ir_block, broadcast_value, broadcast_type->options.structure.members[i]);
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
            instr.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(1);

            IR_Data_Access delete_access = ir_generator_generate_expression(ir_block, statement->options.delete_expr);
            auto delete_type = ir_data_access_get_type(&delete_access);
            if (delete_type->type == Signature_Type::SLICE) {
                delete_access = ir_data_access_create_member(ir_block, delete_access, delete_type->options.slice.data_member);
            }

            dynamic_array_push_back(&instr.options.call.arguments, delete_access);
            dynamic_array_push_back(&ir_block->instructions, instr);
            break;
        }
        default: panic("Statment type invalid!");
        }
    }

    ir_generator_work_through_defers(ir_block, defer_start_index, true);
}



// Queueing
void ir_generator_generate_queued_items(bool gen_bytecode)
{
    Timing_Task before_task = compiler.task_current;
    SCOPE_EXIT(compiler_switch_timing_task(before_task));
    compiler_switch_timing_task(Timing_Task::CODE_GEN);

    // Execute schema for partial compilation (See Bytecode_Generator.hpp)
    if (gen_bytecode) {
        bytecode_generator_update_globals(compiler.bytecode_generator);
    }

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
                ir_generator_generate_block(ir_func->code, ((Workload_Function_Body*) mod_func->code_workload)->body_node);
            }
            else {
                panic("");
            }
        }

        // Add empty return
        if (type_signature_equals(ir_func->function_type->options.function.return_type, compiler.type_system.predefined_types.void_type)) {
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
        assert((function->progress->poly_instance == 0 || function->progress->poly_instance->instance_index != 0), "Function cannot be polymorhic here!");
    }
    if (hashtable_find_element(&ir_generator.function_mapping, function) != 0) return;
    ir_function_create(function->signature, function);
}

void ir_generator_finish(bool gen_bytecode)
{
    // Generate entry function
    auto& type_system = compiler.type_system;
    auto entry_function = ir_function_create(type_system_make_function(&type_system, {}, type_system.predefined_types.void_type), 0);
    ir_generator.program->entry_function = entry_function;

    // Set type_informations global
    {
        Constant_Result result = constant_pool_add_constant(
            &compiler.constant_pool,
            type_system_make_array(
                &type_system, type_system.predefined_types.type_information_type, true, type_system.internal_type_infos.size
            ),
            array_create_static_as_bytes(type_system.internal_type_infos.data, type_system.internal_type_infos.size)
        );
        assert(result.status == Constant_Status::SUCCESS, "Type information must be valid");
        assert(type_system.types.size == type_system.internal_type_infos.size, "");

        IR_Instruction move_instr;
        move_instr.type = IR_Instruction_Type::MOVE;
        move_instr.options.move.destination = ir_data_access_create_global(compiler.semantic_analyser->global_type_informations);
        move_instr.options.move.source = ir_generator_generate_cast(
            entry_function->code, ir_data_access_create_constant(result.constant),
            ir_data_access_get_type(&move_instr.options.move.destination), Info_Cast_Type::ARRAY_TO_SLICE
        );
        dynamic_array_push_back(&entry_function->code->instructions, move_instr);
    }

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
        call_instr.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(1);
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

    // Queue entry function
    Function_Stub entry_stub;
    entry_stub.mod_func = 0;
    entry_stub.ir_func = entry_function;
    dynamic_array_push_back(&ir_generator.queue_functions, entry_stub);

    // Queue and generate all functions
    for (int i = 0; i < ir_generator.modtree->functions.size; i++) {
        ir_generator_queue_function(ir_generator.modtree->functions[i]);
    }
    ir_generator_generate_queued_items(gen_bytecode);
}

// Generator
IR_Generator ir_generator;

IR_Generator* ir_generator_initialize()
{
    ir_generator.program = 0;
    ir_generator.next_label_index = 0;

    ir_generator.function_mapping = hashtable_create_pointer_empty<ModTree_Function*, IR_Function*>(8);
    ir_generator.variable_mapping = hashtable_create_pointer_empty<AST::Definition_Symbol*, IR_Data_Access>(8);
    ir_generator.labels_break = hashtable_create_pointer_empty<AST::Code_Block*, int>(8);
    ir_generator.labels_continue = hashtable_create_pointer_empty<AST::Code_Block*, int>(8);
    ir_generator.block_defer_depths = hashtable_create_pointer_empty<AST::Code_Block*, int>(8);

    ir_generator.queue_functions = dynamic_array_create_empty<Function_Stub>(8);
    ir_generator.defer_stack = dynamic_array_create_empty<AST::Code_Block*>(8);
    ir_generator.fill_out_breaks = dynamic_array_create_empty<Unresolved_Goto>(8);
    ir_generator.fill_out_continues = dynamic_array_create_empty<Unresolved_Goto>(8);
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

    dynamic_array_destroy(&ir_generator.defer_stack);
    dynamic_array_destroy(&ir_generator.queue_functions);
    dynamic_array_destroy(&ir_generator.fill_out_breaks);
    dynamic_array_destroy(&ir_generator.fill_out_continues);
}


