#include "ir_code.hpp"
#include "compiler.hpp"

Type_Signature* ir_data_access_get_type(IR_Data_Access* access)
{
    Type_Signature* sig = 0;
    switch (access->type)
    {
    case IR_Data_Access_Type::GLOBAL_DATA:
        sig = access->option.program->globals[access->index];
        break;
    case IR_Data_Access_Type::CONSTANT:
        sig = access->option.constant_pool->constants[access->index].type;
        break;
    case IR_Data_Access_Type::REGISTER:
        sig = access->option.definition_block->registers[access->index];
        break;
    case IR_Data_Access_Type::PARAMETER:
        sig = access->option.function->function_type->options.function.parameter_types[access->index];
        break;
    default: panic("Hey!");
    }
    if (access->is_memory_access) {
        return sig->options.pointer_child;
    }
    return sig;
}

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

IR_Function* ir_function_create(IR_Program* program, Type_Signature* signature)
{
    IR_Function* function = new IR_Function();
    function->code = ir_code_block_create(function);
    function->function_type = signature;
    function->program = program;
    dynamic_array_push_back(&program->functions, function);
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
    result->globals = dynamic_array_create_empty<Type_Signature*>(32);
    return result;
}

void ir_program_destroy(IR_Program* program)
{
    dynamic_array_destroy(&program->globals);
    for (int i = 0; i < program->functions.size; i++) {
        ir_function_destroy(program->functions[i]);
    }
    dynamic_array_destroy(&program->functions);
    delete program;
}

void ir_data_access_append_to_string(IR_Data_Access* access, String* string, IR_Code_Block* current_block)
{
    switch (access->type)
    {
    case IR_Data_Access_Type::CONSTANT: {
        Upp_Constant* constant = &access->option.constant_pool->constants[access->index];
        string_append_formated(string, "Constant #%d ", access->index);
        type_signature_append_to_string(string, constant->type);
        string_append_formated(string, " ", access->index);
        type_signature_append_value_to_string(constant->type, &access->option.constant_pool->buffer[constant->offset], string);
        break;
    }
    case IR_Data_Access_Type::GLOBAL_DATA: {
        Type_Signature* sig = access->option.program->globals[access->index];
        string_append_formated(string, "Global #%d, type: ", access->index);
        type_signature_append_to_string(string, sig);
        break;
    }
    case IR_Data_Access_Type::PARAMETER: {
        Type_Signature* sig = access->option.function->function_type->options.function.parameter_types[access->index];
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
        case ModTree_Binary_Operation_Type::ADDITION:
            string_append_formated(string, "ADDITION");
            break;
        case ModTree_Binary_Operation_Type::AND:
            string_append_formated(string, "AND");
            break;
        case ModTree_Binary_Operation_Type::DIVISION:
            string_append_formated(string, "DIVISION");
            break;
        case ModTree_Binary_Operation_Type::EQUAL:
            string_append_formated(string, "EQUAL");
            break;
        case ModTree_Binary_Operation_Type::GREATER:
            string_append_formated(string, "GREATER");
            break;
        case ModTree_Binary_Operation_Type::GREATER_OR_EQUAL:
            string_append_formated(string, "GREATER_OR_EQUAL");
            break;
        case ModTree_Binary_Operation_Type::LESS:
            string_append_formated(string, "LESS");
            break;
        case ModTree_Binary_Operation_Type::LESS_OR_EQUAL:
            string_append_formated(string, "LESS_OR_EQUAL");
            break;
        case ModTree_Binary_Operation_Type::MODULO:
            string_append_formated(string, "MODULO");
            break;
        case ModTree_Binary_Operation_Type::MULTIPLICATION:
            string_append_formated(string, "MULTIPLICATION ");
            break;
        case ModTree_Binary_Operation_Type::NOT_EQUAL:
            string_append_formated(string, "NOT_EQUAL");
            break;
        case ModTree_Binary_Operation_Type::OR:
            string_append_formated(string, "OR ");
            break;
        case ModTree_Binary_Operation_Type::SUBTRACTION:
            string_append_formated(string, "SUBTRACTION");
            break;
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
            hardcoded_function_type_append_to_string(string, call->options.hardcoded.type);
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
        ir_code_block_append_to_string(instruction->options.if_instr.true_branch, string, indentation + 1);
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

IR_Data_Access ir_data_access_create_intermediate(IR_Code_Block* block, Type_Signature* signature)
{
    IR_Data_Access access;
    if (signature->type == Signature_Type::VOID_TYPE) {
        access.is_memory_access = false;
        access.type = IR_Data_Access_Type::GLOBAL_DATA;
        access.option.program = 0;
        access.index = 0;
        return access;
    }
    access.is_memory_access = false;
    access.type = IR_Data_Access_Type::REGISTER;
    access.option.definition_block = block;
    dynamic_array_push_back(&block->registers, signature);
    access.index = block->registers.size - 1;
    return access;
}

IR_Data_Access ir_data_access_create_dereference(IR_Code_Block* block, IR_Data_Access access)
{
    if (!access.is_memory_access) {
        access.is_memory_access = true;
        return access;
    }
    Type_Signature* ptr_type = ir_data_access_get_type(&access);
    assert(ptr_type->type == Signature_Type::POINTER, "");
    IR_Data_Access ptr_access = ir_data_access_create_intermediate(block, ptr_type->options.pointer_child);
    IR_Instruction instr;
    instr.type = IR_Instruction_Type::MOVE;
    instr.options.move.destination = ptr_access;
    instr.options.move.source = access;
    dynamic_array_push_back(&block->instructions, instr);

    ptr_access.is_memory_access = true;
    return ptr_access;
}


IR_Data_Access ir_data_access_create_member(IR_Generator* generator, IR_Code_Block* block, IR_Data_Access struct_access, Struct_Member member)
{
    IR_Instruction member_instr;
    member_instr.type = IR_Instruction_Type::ADDRESS_OF;
    member_instr.options.address_of.type = IR_Instruction_Address_Of_Type::STRUCT_MEMBER;
    member_instr.options.address_of.options.member = member;
    member_instr.options.address_of.source = struct_access;
    member_instr.options.address_of.destination = ir_data_access_create_intermediate(block, type_system_make_pointer(generator->type_system, member.type));
    dynamic_array_push_back(&block->instructions, member_instr);

    member_instr.options.address_of.destination.is_memory_access = true;
    return member_instr.options.address_of.destination;
}

IR_Generator ir_generator_create()
{
    IR_Generator generator;
    generator.compiler = 0;
    generator.program = 0;
    generator.next_label_index = 0;
    generator.function_mapping = hashtable_create_pointer_empty<ModTree_Function*, IR_Function*>(32);
    generator.variable_mapping = hashtable_create_pointer_empty<ModTree_Variable*, IR_Data_Access>(32);
    generator.queue_functions = dynamic_array_create_empty<ModTree_Function*>(32);
    generator.queue_globals = dynamic_array_create_empty<ModTree_Variable*>(32);
    generator.function_stubs = dynamic_array_create_empty<ModTree_Function*>(32);
    generator.fill_out_breaks = dynamic_array_create_empty<Unresolved_Goto>(32);
    generator.fill_out_continues = dynamic_array_create_empty<Unresolved_Goto>(32);
    generator.labels_break = hashtable_create_pointer_empty<ModTree_Block*, int>(32);
    generator.labels_continue = hashtable_create_pointer_empty<ModTree_Block*, int>(32);
    return generator;
}

void ir_generator_destroy(IR_Generator* generator)
{
    if (generator->program != 0) {
        ir_program_destroy(generator->program);
    }
    hashtable_destroy(&generator->function_mapping);
    hashtable_destroy(&generator->variable_mapping);
    dynamic_array_destroy(&generator->queue_functions);
    dynamic_array_destroy(&generator->queue_globals);
    dynamic_array_destroy(&generator->function_stubs);
    dynamic_array_destroy(&generator->fill_out_breaks);
    dynamic_array_destroy(&generator->fill_out_continues);
    hashtable_destroy(&generator->labels_break);
    hashtable_destroy(&generator->labels_continue);
}

IR_Data_Access ir_data_access_create_constant(IR_Generator* generator, Type_Signature* signature, Array<byte> bytes)
{
    IR_Data_Access access;
    access.is_memory_access = false;
    access.type = IR_Data_Access_Type::CONSTANT;
    access.option.constant_pool = &generator->compiler->constant_pool;
    Constant_Result result = constant_pool_add_constant(&generator->compiler->constant_pool, signature, bytes);
    assert(result.status == Constant_Status::SUCCESS, "Must always work");
    access.index = result.constant_index;
    return access;
}

IR_Data_Access ir_data_access_create_constant_i32(IR_Generator* generator, i32 value) {
    return ir_data_access_create_constant(
        generator, generator->type_system->i32_type,
        array_create_static((byte*)&value, sizeof(i32))
    );
}

IR_Data_Access ir_generator_generate_expression(IR_Generator* generator, IR_Code_Block* ir_block, ModTree_Expression* expression)
{
    switch (expression->expression_type)
    {
    case ModTree_Expression_Type::BINARY_OPERATION:
    {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::BINARY_OP;
        instr.options.binary_op.type = expression->options.binary_operation.operation_type;
        instr.options.binary_op.operand_left = ir_generator_generate_expression(generator, ir_block, expression->options.binary_operation.left_operand);
        instr.options.binary_op.operand_right = ir_generator_generate_expression(generator, ir_block, expression->options.binary_operation.right_operand);
        instr.options.binary_op.destination = ir_data_access_create_intermediate(ir_block, expression->result_type);
        dynamic_array_push_back(&ir_block->instructions, instr);
        return instr.options.binary_op.destination;
    }
    case ModTree_Expression_Type::UNARY_OPERATION:
    {
        IR_Data_Access access = ir_generator_generate_expression(generator, ir_block, expression->options.unary_operation.operand);
        switch (expression->options.unary_operation.operation_type)
        {
        case ModTree_Unary_Operation_Type::ADDRESS_OF:
        {
            if (access.is_memory_access) {
                access.is_memory_access = false;
                return access;
            }
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::ADDRESS_OF;
            instr.options.address_of.type = IR_Instruction_Address_Of_Type::DATA;
            instr.options.address_of.destination = ir_data_access_create_intermediate(ir_block, expression->result_type);
            instr.options.address_of.source = access;
            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.binary_op.destination;
        }
        case ModTree_Unary_Operation_Type::DEREFERENCE:
        {
            if (!access.is_memory_access) {
                access.is_memory_access = true;
                return access;
            }
            IR_Data_Access ptr_access = ir_data_access_create_intermediate(ir_block, expression->options.unary_operation.operand->result_type);
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::MOVE;
            instr.options.move.destination = ptr_access;
            instr.options.move.source = access;
            if ((int)access.type < 0 || (int)access.type > 6) {
                panic("HEY");
            }
            dynamic_array_push_back(&ir_block->instructions, instr);

            ptr_access.is_memory_access = true;
            return ptr_access;
        }
        case ModTree_Unary_Operation_Type::LOGICAL_NOT: {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::UNARY_OP;
            instr.options.unary_op.destination = ir_data_access_create_intermediate(ir_block, expression->result_type);
            instr.options.unary_op.type = IR_Instruction_Unary_OP_Type::NOT;
            instr.options.unary_op.source = access;
            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.unary_op.destination;
        }
        case ModTree_Unary_Operation_Type::NEGATE: {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::UNARY_OP;
            instr.options.unary_op.destination = ir_data_access_create_intermediate(ir_block, expression->result_type);
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
    case ModTree_Expression_Type::CONSTANT_READ: {
        IR_Data_Access access;
        access.index = expression->options.literal_read.constant_index;
        access.type = IR_Data_Access_Type::CONSTANT;
        access.is_memory_access = false;
        access.option.constant_pool = &generator->compiler->constant_pool;
        return access;
    }
    case ModTree_Expression_Type::FUNCTION_CALL:
    {
        IR_Instruction call_instr;
        call_instr.type = IR_Instruction_Type::FUNCTION_CALL;
        call_instr.options.call.destination = ir_data_access_create_intermediate(ir_block, expression->result_type);
        if (expression->options.function_call.is_pointer_call) {
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_POINTER_CALL;
            call_instr.options.call.options.pointer_access = ir_generator_generate_expression(generator, ir_block, expression->options.function_call.pointer_expression);
        }
        else
        {
            switch (expression->options.function_call.function->function_type)
            {
            case ModTree_Function_Type::FUNCTION:
                call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
                call_instr.options.call.options.function = *hashtable_find_element(&generator->function_mapping, expression->options.function_call.function);
                break;
            case ModTree_Function_Type::EXTERN_FUNCTION:
                call_instr.options.call.call_type = IR_Instruction_Call_Type::EXTERN_FUNCTION_CALL;
                call_instr.options.call.options.extern_function = expression->options.function_call.function->options.extern_function;
                break;
            case ModTree_Function_Type::HARDCODED_FUNCTION:
                call_instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
                call_instr.options.call.options.hardcoded.type = expression->options.function_call.function->options.hardcoded_type;
                call_instr.options.call.options.hardcoded.signature = expression->options.function_call.function->signature;
                break;
            default: panic("HEY");
            }
        }
        call_instr.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(expression->options.function_call.arguments.size);
        for (int j = 0; j < expression->options.function_call.arguments.size; j++) {
            dynamic_array_push_back(
                &call_instr.options.call.arguments,
                ir_generator_generate_expression(generator, ir_block, expression->options.function_call.arguments[j])
            );
        }

        dynamic_array_push_back(&ir_block->instructions, call_instr);
        return call_instr.options.call.destination;
    }
    case ModTree_Expression_Type::VARIABLE_READ: {
        return *hashtable_find_element(&generator->variable_mapping, expression->options.variable_read);
    }
    case ModTree_Expression_Type::STRUCT_INITIALIZER:
    {
        IR_Data_Access struct_access = ir_data_access_create_intermediate(ir_block, expression->result_type);
        for (int i = 0; i < expression->options.struct_initializer.size; i++)
        {
            Member_Initializer* init = &expression->options.struct_initializer[i];

            // Calculate member pointer
            IR_Instruction element_instr;
            element_instr.type = IR_Instruction_Type::ADDRESS_OF;
            element_instr.options.address_of.type = IR_Instruction_Address_Of_Type::STRUCT_MEMBER;
            element_instr.options.address_of.destination = ir_data_access_create_intermediate(
                ir_block, type_system_make_pointer(&generator->compiler->type_system, init->member.type)
            );
            element_instr.options.address_of.source = struct_access;
            element_instr.options.address_of.options.member = init->member;
            dynamic_array_push_back(&ir_block->instructions, element_instr);

            IR_Data_Access member_access = element_instr.options.address_of.destination;
            member_access.is_memory_access = true;

            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.destination = member_access;
            move_instr.options.move.source = ir_generator_generate_expression(generator, ir_block, init->init_expr);
            dynamic_array_push_back(&ir_block->instructions, move_instr);
        }
        return struct_access;
    }
    case ModTree_Expression_Type::ARRAY_INITIALIZER:
    {
        IR_Data_Access array_access = ir_data_access_create_intermediate(ir_block, expression->result_type);
        for (int i = 0; i < expression->options.array_initializer.size; i++)
        {
            ModTree_Expression* init_expr = expression->options.array_initializer[i];

            // Calculate array member pointer
            IR_Instruction element_instr;
            element_instr.type = IR_Instruction_Type::ADDRESS_OF;
            element_instr.options.address_of.type = IR_Instruction_Address_Of_Type::ARRAY_ELEMENT;
            element_instr.options.address_of.destination = ir_data_access_create_intermediate(
                ir_block, type_system_make_pointer(&generator->compiler->type_system, expression->result_type->options.array.element_type)
            );
            element_instr.options.address_of.source = array_access;
            element_instr.options.address_of.options.index_access = ir_data_access_create_constant_i32(generator, i);
            dynamic_array_push_back(&ir_block->instructions, element_instr);

            IR_Data_Access member_access = element_instr.options.address_of.destination;
            member_access.is_memory_access = true;
            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.destination = member_access;
            move_instr.options.move.source = ir_generator_generate_expression(generator, ir_block, init_expr);
            dynamic_array_push_back(&ir_block->instructions, move_instr);
        }
        return array_access;
    }
    case ModTree_Expression_Type::FUNCTION_POINTER_READ: {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::ADDRESS_OF;
        instr.options.address_of.destination = ir_data_access_create_intermediate(ir_block, expression->result_type);
        if (expression->options.function_pointer_read->function_type == ModTree_Function_Type::FUNCTION) {
            instr.options.address_of.type = IR_Instruction_Address_Of_Type::FUNCTION;
            instr.options.address_of.options.function = *hashtable_find_element(&generator->function_mapping, expression->options.function_pointer_read);
        }
        else if (expression->options.function_pointer_read->function_type == ModTree_Function_Type::EXTERN_FUNCTION) {
            instr.options.address_of.type = IR_Instruction_Address_Of_Type::EXTERN_FUNCTION;
            instr.options.address_of.options.extern_function = expression->options.function_pointer_read->options.extern_function;
        }
        else panic("No hardcoded functions allowed here!");
        dynamic_array_push_back(&ir_block->instructions, instr);
        return instr.options.address_of.destination;
    }
    case ModTree_Expression_Type::ARRAY_ACCESS: {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::ADDRESS_OF;
        instr.options.address_of.destination = ir_data_access_create_intermediate(ir_block, type_system_make_pointer(generator->type_system, expression->result_type));
        instr.options.address_of.type = IR_Instruction_Address_Of_Type::ARRAY_ELEMENT;
        instr.options.address_of.source = ir_generator_generate_expression(generator, ir_block, expression->options.array_access.array_expression);
        instr.options.address_of.options.index_access = ir_generator_generate_expression(generator, ir_block, expression->options.array_access.index_expression);
        dynamic_array_push_back(&ir_block->instructions, instr);

        instr.options.address_of.destination.is_memory_access = true;
        return instr.options.address_of.destination;
    }
    case ModTree_Expression_Type::MEMBER_ACCESS: {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::ADDRESS_OF;
        instr.options.address_of.destination = ir_data_access_create_intermediate(ir_block, type_system_make_pointer(generator->type_system, expression->result_type));
        instr.options.address_of.type = IR_Instruction_Address_Of_Type::STRUCT_MEMBER;
        instr.options.address_of.source = ir_generator_generate_expression(generator, ir_block, expression->options.member_access.structure_expression);
        instr.options.address_of.options.member = expression->options.member_access.member;
        dynamic_array_push_back(&ir_block->instructions, instr);

        instr.options.address_of.destination.is_memory_access = true;
        return instr.options.address_of.destination;
    }
    case ModTree_Expression_Type::NEW_ALLOCATION:
    {
        // FUTURE: At some point this will access the Context struct for the alloc function, and then call it
        if (expression->options.new_allocation.element_count.available)
        {
            assert(expression->result_type->type == Signature_Type::SLICE, "HEY");
            IR_Data_Access array_access = ir_data_access_create_intermediate(ir_block, expression->result_type); // Array
            IR_Data_Access array_data_access = ir_data_access_create_member(generator, ir_block, array_access, expression->result_type->options.slice.data_member);
            IR_Data_Access array_size_access = ir_data_access_create_member(generator, ir_block, array_access, expression->result_type->options.slice.size_member);

            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.source = ir_generator_generate_expression(generator, ir_block, expression->options.new_allocation.element_count.value);
            move_instr.options.move.destination = array_size_access;
            dynamic_array_push_back(&ir_block->instructions, move_instr);

            IR_Instruction mult_instr;
            mult_instr.type = IR_Instruction_Type::BINARY_OP;
            mult_instr.options.binary_op.type = ModTree_Binary_Operation_Type::MULTIPLICATION;
            mult_instr.options.binary_op.destination = ir_data_access_create_intermediate(ir_block, generator->type_system->i32_type);
            mult_instr.options.binary_op.operand_left = ir_data_access_create_constant_i32(generator, expression->options.new_allocation.allocation_size);
            mult_instr.options.binary_op.operand_right = array_size_access;
            dynamic_array_push_back(&ir_block->instructions, mult_instr);

            IR_Instruction alloc_instr;
            alloc_instr.type = IR_Instruction_Type::FUNCTION_CALL;
            alloc_instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
            alloc_instr.options.call.options.hardcoded.type = Hardcoded_Function_Type::MALLOC_SIZE_I32;
            alloc_instr.options.call.options.hardcoded.signature = generator->compiler->analyser.malloc_function->signature;
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
            alloc_instr.options.call.options.hardcoded.type = Hardcoded_Function_Type::MALLOC_SIZE_I32;
            alloc_instr.options.call.options.hardcoded.signature = generator->compiler->analyser.malloc_function->signature;
            alloc_instr.options.call.destination = ir_data_access_create_intermediate(ir_block, expression->result_type);
            alloc_instr.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(1);
            dynamic_array_push_back(&alloc_instr.options.call.arguments, ir_data_access_create_constant_i32(generator, expression->options.new_allocation.allocation_size));
            dynamic_array_push_back(&ir_block->instructions, alloc_instr);
            return alloc_instr.options.call.destination;
        }

        panic("Unreachable");
        break;
    }
    case ModTree_Expression_Type::CAST:
    {
        auto make_simple_cast = [](IR_Generator* generator, IR_Code_Block* ir_block, ModTree_Expression* expression, IR_Cast_Type cast_type) -> IR_Data_Access
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::CAST;
            instr.options.cast.type = cast_type;
            instr.options.cast.source = ir_generator_generate_expression(generator, ir_block, expression->options.cast.cast_argument);
            instr.options.cast.destination = ir_data_access_create_intermediate(ir_block, expression->result_type);
            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.cast.destination;
        };
        IR_Data_Access result_access;
        switch (expression->options.cast.type)
        {
        case ModTree_Cast_Type::INTEGERS: {
            result_access = make_simple_cast(generator, ir_block, expression, IR_Cast_Type::INTEGERS);
            break;
        }
        case ModTree_Cast_Type::FLOATS: {
            result_access = make_simple_cast(generator, ir_block, expression, IR_Cast_Type::FLOATS);
            break;
        }
        case ModTree_Cast_Type::FLOAT_TO_INT: {
            result_access = make_simple_cast(generator, ir_block, expression, IR_Cast_Type::FLOAT_TO_INT);
            break;
        }
        case ModTree_Cast_Type::INT_TO_FLOAT: {
            result_access = make_simple_cast(generator, ir_block, expression, IR_Cast_Type::INT_TO_FLOAT);
            break;
        }
        case ModTree_Cast_Type::POINTERS: {
            result_access = make_simple_cast(generator, ir_block, expression, IR_Cast_Type::POINTERS);
            break;
        }
        case ModTree_Cast_Type::POINTER_TO_U64: {
            result_access = make_simple_cast(generator, ir_block, expression, IR_Cast_Type::POINTER_TO_U64);
            break;
        }
        case ModTree_Cast_Type::U64_TO_POINTER: {
            result_access = make_simple_cast(generator, ir_block, expression, IR_Cast_Type::U64_TO_POINTER);
            break;
        }
        case ModTree_Cast_Type::ENUM_TO_INT: {
            result_access = make_simple_cast(generator, ir_block, expression, IR_Cast_Type::ENUM_TO_INT);
            break;
        }
        case ModTree_Cast_Type::INT_TO_ENUM: {
            result_access = make_simple_cast(generator, ir_block, expression, IR_Cast_Type::INT_TO_ENUM);
            break;
        }
        case ModTree_Cast_Type::ARRAY_SIZED_TO_UNSIZED:
        {
            Type_Signature* slice_type = expression->result_type;
            Type_Signature* array_type = expression->options.cast.cast_argument->result_type;
            assert(slice_type->type == Signature_Type::SLICE, "");
            assert(array_type->type == Signature_Type::ARRAY, "");
            IR_Data_Access slice_access = ir_data_access_create_intermediate(ir_block, slice_type);
            // Set size
            {
                IR_Instruction instr;
                instr.type = IR_Instruction_Type::MOVE;
                instr.options.move.destination = ir_data_access_create_member(generator, ir_block, slice_access, slice_type->options.slice.size_member);
                instr.options.move.source = ir_data_access_create_constant_i32(generator, array_type->options.array.element_count);
                dynamic_array_push_back(&ir_block->instructions, instr);
            }
            // Set data
            {
                IR_Instruction instr;
                instr.type = IR_Instruction_Type::ADDRESS_OF;
                instr.options.address_of.type = IR_Instruction_Address_Of_Type::DATA;
                instr.options.address_of.source = ir_generator_generate_expression(generator, ir_block, expression->options.cast.cast_argument);
                instr.options.address_of.destination = ir_data_access_create_member(generator, ir_block, slice_access, slice_type->options.slice.data_member);
                dynamic_array_push_back(&ir_block->instructions, instr);
            }
            return slice_access;
        }
        case ModTree_Cast_Type::FROM_ANY:
        {
            // Check if type matches given type, if not error out
            IR_Data_Access access_operand = ir_generator_generate_expression(generator, ir_block, expression->options.cast.cast_argument);
            IR_Data_Access access_valid_cast = ir_data_access_create_intermediate(ir_block, generator->compiler->type_system.bool_type);
            IR_Data_Access access_result = ir_data_access_create_intermediate(ir_block, expression->result_type);
            {
                IR_Instruction cmp_instr;
                cmp_instr.type = IR_Instruction_Type::BINARY_OP;
                cmp_instr.options.binary_op.type = ModTree_Binary_Operation_Type::EQUAL;
                cmp_instr.options.binary_op.operand_left = ir_data_access_create_constant(
                    generator, generator->compiler->type_system.type_type,
                    array_create_static_as_bytes<u64>(&expression->result_type->internal_index, 1)
                );
                cmp_instr.options.binary_op.operand_right = ir_data_access_create_member(generator, ir_block, access_operand,
                    generator->compiler->type_system.any_type->options.structure.members[1]
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
                IR_Data_Access any_data_access = ir_data_access_create_member(generator, branch_valid, access_operand,
                    generator->compiler->type_system.any_type->options.structure.members[0]
                );
                {
                    IR_Instruction cast_instr;
                    cast_instr.type = IR_Instruction_Type::CAST;
                    cast_instr.options.cast.type = IR_Cast_Type::POINTERS;
                    cast_instr.options.cast.source = any_data_access;
                    cast_instr.options.cast.destination = ir_data_access_create_intermediate(
                        branch_valid, type_system_make_pointer(&generator->compiler->type_system, expression->result_type)
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
        case ModTree_Cast_Type::TO_ANY:
        {
            IR_Data_Access operand_access = ir_generator_generate_expression(generator, ir_block, expression->options.cast.cast_argument);
            if (operand_access.type == IR_Data_Access_Type::CONSTANT && !operand_access.is_memory_access)
            {
                IR_Instruction instr;
                instr.type = IR_Instruction_Type::MOVE;
                instr.options.move.destination = ir_data_access_create_intermediate(ir_block, expression->options.cast.cast_argument->result_type);
                instr.options.move.source = operand_access;
                dynamic_array_push_back(&ir_block->instructions, instr);
                operand_access = instr.options.move.destination;
            }

            IR_Data_Access any_access = ir_data_access_create_intermediate(ir_block, generator->compiler->type_system.any_type);
            Type_Signature* any_type = generator->compiler->type_system.any_type;
            // Set data
            {
                IR_Instruction instr;
                instr.type = IR_Instruction_Type::ADDRESS_OF;
                instr.options.address_of.type = IR_Instruction_Address_Of_Type::DATA;
                instr.options.address_of.source = operand_access;
                // In theory a cast from pointer to voidptr would be better, but I think I can ignore it
                instr.options.address_of.destination = ir_data_access_create_member(
                    generator, ir_block, any_access, any_type->options.structure.members[0]
                );
                dynamic_array_push_back(&ir_block->instructions, instr);
            }
            // Set type
            {
                IR_Instruction instr;
                instr.type = IR_Instruction_Type::MOVE;
                instr.options.move.destination = ir_data_access_create_member(
                    generator, ir_block, any_access, any_type->options.structure.members[1]
                );
                u64 type_val = expression->options.cast.cast_argument->result_type->internal_index;
                instr.options.move.source = ir_data_access_create_constant(
                    generator, generator->compiler->type_system.type_type,
                    array_create_static_as_bytes(&type_val, 1)
                );
                dynamic_array_push_back(&ir_block->instructions, instr);
            }
            return any_access;
        }
        default: panic("");
        }
        return result_access;
    }
    default: panic("HEY");
    }

    panic("HEY");
    IR_Data_Access unused;
    return unused;
}

void ir_generator_generate_block(IR_Generator* generator, IR_Code_Block* ir_block, ModTree_Block* mod_block)
{
    // Generate Variables
    for (int i = 0; i < mod_block->variables.size; i++) {
        dynamic_array_push_back(&ir_block->registers, mod_block->variables[i]->data_type);
        IR_Data_Access access;
        access.type = IR_Data_Access_Type::REGISTER;
        access.index = ir_block->registers.size - 1;
        access.is_memory_access = false;
        access.option.definition_block = ir_block;
        hashtable_insert_element(&generator->variable_mapping, mod_block->variables[i], access);
    }

    // Generate code
    for (int i = 0; i < mod_block->statements.size; i++)
    {
        ModTree_Statement* statement = mod_block->statements[i];
        switch (statement->type)
        {
        case ModTree_Statement_Type::BLOCK:
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::BLOCK;
            instr.options.block = ir_code_block_create(ir_block->function);
            ir_generator_generate_block(generator, instr.options.block, statement->options.block);
            dynamic_array_push_back(&ir_block->instructions, instr);

            IR_Instruction label;
            label.type = IR_Instruction_Type::LABEL;
            label.options.label_index = generator->next_label_index;
            generator->next_label_index++;
            dynamic_array_push_back(&ir_block->instructions, label);
            hashtable_insert_element(&generator->labels_break, statement->options.block, label.options.label_index);
            break;
        }
        case ModTree_Statement_Type::IF:
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::IF;
            instr.options.if_instr.condition = ir_generator_generate_expression(generator, ir_block, statement->options.if_statement.condition);
            instr.options.if_instr.true_branch = ir_code_block_create(ir_block->function);
            ir_generator_generate_block(generator, instr.options.if_instr.true_branch, statement->options.if_statement.if_block);
            instr.options.if_instr.false_branch = ir_code_block_create(ir_block->function);
            ir_generator_generate_block(generator, instr.options.if_instr.false_branch, statement->options.if_statement.else_block);
            dynamic_array_push_back(&ir_block->instructions, instr);

            IR_Instruction label;
            label.type = IR_Instruction_Type::LABEL;
            label.options.label_index = generator->next_label_index;
            generator->next_label_index++;
            dynamic_array_push_back(&ir_block->instructions, label);
            hashtable_insert_element(&generator->labels_break, statement->options.if_statement.if_block, label.options.label_index);
            hashtable_insert_element(&generator->labels_break, statement->options.if_statement.else_block, label.options.label_index);

            break;
        }
        case ModTree_Statement_Type::SWITCH:
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::SWITCH;
            instr.options.switch_instr.condition_access = ir_generator_generate_expression(generator, ir_block, statement->options.switch_statement.condition);
            instr.options.switch_instr.cases = dynamic_array_create_empty<IR_Switch_Case>(statement->options.switch_statement.cases.size);
            for (int i = 0; i < statement->options.switch_statement.cases.size; i++) {
                ModTree_Switch_Case* switch_case = &statement->options.switch_statement.cases[i];
                IR_Switch_Case new_case;
                new_case.value = switch_case->value;
                new_case.block = ir_code_block_create(ir_block->function);
                ir_generator_generate_block(generator, new_case.block, switch_case->body);
                dynamic_array_push_back(&instr.options.switch_instr.cases, new_case);
            }
            instr.options.switch_instr.default_block = ir_code_block_create(ir_block->function);
            if (statement->options.switch_statement.default_block == 0) {
                IR_Instruction exit_instr;
                exit_instr.type = IR_Instruction_Type::RETURN;
                exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
                exit_instr.options.return_instr.options.exit_code = Exit_Code::INVALID_SWITCH_CASE;
                dynamic_array_push_back(&instr.options.switch_instr.default_block->instructions, exit_instr);
            }
            else {
                ir_generator_generate_block(generator, instr.options.switch_instr.default_block, statement->options.switch_statement.default_block);
            }
            dynamic_array_push_back(&ir_block->instructions, instr);

            IR_Instruction label;
            label.type = IR_Instruction_Type::LABEL;
            label.options.label_index = generator->next_label_index;
            generator->next_label_index++;
            dynamic_array_push_back(&ir_block->instructions, label);
            for (int i = 0; i < statement->options.switch_statement.cases.size; i++) {
                ModTree_Switch_Case* switch_case = &statement->options.switch_statement.cases[i];
                hashtable_insert_element(&generator->labels_break, switch_case->body, label.options.label_index);
            }
            if (statement->options.switch_statement.default_block != 0) {
                hashtable_insert_element(&generator->labels_break, statement->options.switch_statement.default_block, label.options.label_index);
            }

            break;
        }
        case ModTree_Statement_Type::WHILE:
        {
            IR_Instruction continue_label;
            continue_label.type = IR_Instruction_Type::LABEL;
            continue_label.options.label_index = generator->next_label_index;
            generator->next_label_index++;
            dynamic_array_push_back(&ir_block->instructions, continue_label);
            hashtable_insert_element(&generator->labels_continue, statement->options.while_statement.while_block, continue_label.options.label_index);

            IR_Instruction instr;
            instr.type = IR_Instruction_Type::WHILE;
            instr.options.while_instr.condition_code = ir_code_block_create(ir_block->function);
            instr.options.while_instr.condition_access = ir_generator_generate_expression(
                generator, instr.options.while_instr.condition_code, statement->options.if_statement.condition
            );
            instr.options.while_instr.code = ir_code_block_create(ir_block->function);
            ir_generator_generate_block(generator, instr.options.while_instr.code, statement->options.while_statement.while_block);
            dynamic_array_push_back(&ir_block->instructions, instr);

            IR_Instruction break_label;
            break_label.type = IR_Instruction_Type::LABEL;
            break_label.options.label_index = generator->next_label_index;
            generator->next_label_index++;
            dynamic_array_push_back(&ir_block->instructions, break_label);
            hashtable_insert_element(&generator->labels_break, statement->options.while_statement.while_block, break_label.options.label_index);

            break;
        }
        case ModTree_Statement_Type::BREAK:
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::GOTO;
            dynamic_array_push_back(&ir_block->instructions, instr);

            Unresolved_Goto fill_out;
            fill_out.block = ir_block;
            fill_out.instruction_index = ir_block->instructions.size - 1;
            fill_out.break_block = statement->options.break_to_block;
            dynamic_array_push_back(&generator->fill_out_breaks, fill_out);
            break;
        }
        case ModTree_Statement_Type::CONTINUE:
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::GOTO;
            dynamic_array_push_back(&ir_block->instructions, instr);

            Unresolved_Goto fill_out;
            fill_out.block = ir_block;
            fill_out.instruction_index = ir_block->instructions.size - 1;
            fill_out.break_block = statement->options.continue_to_block;
            dynamic_array_push_back(&generator->fill_out_continues, fill_out);
            break;
        }
        case ModTree_Statement_Type::RETURN: {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::RETURN;
            if (statement->options.return_value.available) {
                instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_DATA;
                instr.options.return_instr.options.return_value = ir_generator_generate_expression(generator, ir_block, statement->options.return_value.value);
            }
            else {
                instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_EMPTY;
            }
            dynamic_array_push_back(&ir_block->instructions, instr);
            break;
        }
        case ModTree_Statement_Type::EXIT: {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::RETURN;
            instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
            instr.options.return_instr.options.exit_code = statement->options.exit_code;
            dynamic_array_push_back(&ir_block->instructions, instr);
            break;
        }
        case ModTree_Statement_Type::EXPRESSION: {
            ir_generator_generate_expression(generator, ir_block, statement->options.expression);
            break;
        }
        case ModTree_Statement_Type::ASSIGNMENT: {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::MOVE;
            instr.options.move.source = ir_generator_generate_expression(generator, ir_block, statement->options.assignment.source);
            instr.options.move.destination = ir_generator_generate_expression(generator, ir_block, statement->options.assignment.destination);
            if ((int)instr.options.move.source.type < 0 || (int)instr.options.move.source.type > 6) {
                panic("HEY");
            }
            dynamic_array_push_back(&ir_block->instructions, instr);
            break;
        }
        case ModTree_Statement_Type::DELETION:
        {
            // FUTURE: At some point this will access the Context struct for the free func, and also pass the size
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::FUNCTION_CALL;
            instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
            instr.options.call.options.hardcoded.type = Hardcoded_Function_Type::FREE_POINTER;
            instr.options.call.options.hardcoded.signature = generator->compiler->analyser.free_function->signature;
            instr.options.call.arguments = dynamic_array_create_empty<IR_Data_Access>(1);

            IR_Data_Access delete_access = ir_generator_generate_expression(generator, ir_block, statement->options.deletion.expression);
            if (statement->options.deletion.is_array)
            {
                assert(statement->options.deletion.expression->result_type->type == Signature_Type::SLICE, "HEY");
                Type_Signature* pointer_type = statement->options.deletion.expression->result_type->options.slice.data_member.type;
                IR_Instruction instr;
                instr.type = IR_Instruction_Type::ADDRESS_OF;
                instr.options.address_of.destination = ir_data_access_create_intermediate(ir_block, pointer_type);
                instr.options.address_of.type = IR_Instruction_Address_Of_Type::STRUCT_MEMBER;
                instr.options.address_of.source = delete_access;
                instr.options.address_of.options.member = statement->options.deletion.expression->result_type->options.slice.data_member;
                dynamic_array_push_back(&ir_block->instructions, instr);

                instr.options.address_of.destination.is_memory_access = true;
                delete_access = instr.options.address_of.destination;
            }

            dynamic_array_push_back(&instr.options.call.arguments, delete_access);
            dynamic_array_push_back(&ir_block->instructions, instr);
            break;
        }
        default: panic("Statment type invalid!");
        }
    }
}

void ir_generator_queue_module(IR_Generator* generator, ModTree_Module* module)
{
    // Queue functions
    for (int i = 0; i < module->functions.size; i++)
    {
        ModTree_Function* mod_func = module->functions[i];
        if (mod_func->function_type != ModTree_Function_Type::FUNCTION) {
            continue;
        }
        ir_generator_queue_function(generator, mod_func);
    }

    // Queue globals
    for (int i = 0; i < module->globals.size; i++) {
        ir_generator_queue_global(generator, module->globals[i]);
    }

    // Queue sub modules
    for (int i = 0; i < module->modules.size; i++) {
        ir_generator_queue_module(generator, module->modules[i]);
    }
}

void ir_generator_reset(IR_Generator* generator, Compiler* compiler)
{
    generator->compiler = compiler;
    if (generator->program != 0) {
        ir_program_destroy(generator->program);
    }
    generator->next_label_index = 0;
    generator->program = ir_program_create(&generator->compiler->type_system);
    generator->modtree = generator->compiler->analyser.program;
    generator->type_system = &generator->compiler->type_system;
    hashtable_reset(&generator->variable_mapping);
    hashtable_reset(&generator->function_mapping);
    hashtable_reset(&generator->labels_break);
    hashtable_reset(&generator->labels_continue);
    dynamic_array_reset(&generator->queue_functions);
    dynamic_array_reset(&generator->queue_globals);
    dynamic_array_reset(&generator->function_stubs);
    dynamic_array_reset(&generator->fill_out_breaks);
    dynamic_array_reset(&generator->fill_out_continues);
}

void ir_generator_generate_queued_items(IR_Generator* generator)
{
    Timing_Task before_task = generator->compiler->task_current;
    SCOPE_EXIT(compiler_switch_timing_task(generator->compiler, before_task));
    compiler_switch_timing_task(generator->compiler, Timing_Task::CODE_GEN);

    // Generate Function stubs
    for (int i = 0; i < generator->queue_functions.size; i++)
    {
        ModTree_Function* mod_func = generator->queue_functions[i];
        if (mod_func->function_type != ModTree_Function_Type::FUNCTION) {
            continue;
        }
        if (hashtable_find_element(&generator->function_mapping, mod_func) != 0) continue;

        IR_Function* ir_func = new IR_Function;
        ir_func->function_type = mod_func->signature;
        ir_func->program = generator->program;
        ir_func->code = ir_code_block_create(ir_func);
        dynamic_array_push_back(&generator->program->functions, ir_func);
        hashtable_insert_element(&generator->function_mapping, mod_func, ir_func);
        dynamic_array_push_back(&generator->function_stubs, mod_func);

        // Generate parameters vars
        for (int j = 0; j < mod_func->options.function.parameters.size; j++) {
            IR_Data_Access access;
            access.type = IR_Data_Access_Type::PARAMETER;
            access.is_memory_access = false;
            access.option.function = ir_func;
            access.index = j;
            hashtable_insert_element(&generator->variable_mapping, mod_func->options.function.parameters[j], access);
        }
    }
    dynamic_array_reset(&generator->queue_functions);

    // Generate Globals
    for (int i = 0; i < generator->queue_globals.size; i++)
    {
        ModTree_Variable* variable = generator->queue_globals[i];
        if (hashtable_find_element(&generator->variable_mapping, variable) != 0) continue;

        dynamic_array_push_back(&generator->program->globals, variable->data_type);
        IR_Data_Access access;
        access.index = generator->program->globals.size - 1;
        access.is_memory_access = false;
        access.type = IR_Data_Access_Type::GLOBAL_DATA;
        access.option.program = generator->program;
        hashtable_insert_element(&generator->variable_mapping, variable, access);
    }
    dynamic_array_reset(&generator->queue_globals);

    // Execute schema for partial compilation (See Bytecode_Generator.hpp)
    bytecode_generator_update_globals(&generator->compiler->bytecode_generator);

    // Generate Blocks
    for (int i = 0; i < generator->function_stubs.size; i++)
    {
        ModTree_Function* mod_func = generator->function_stubs[i];
        IR_Function* ir_func = *hashtable_find_element(&generator->function_mapping, mod_func);
        ir_generator_generate_block(generator, ir_func->code, mod_func->options.function.body);
        // Fill out breaks and continues
        for (int j = 0; j < generator->fill_out_breaks.size; j++) {
            Unresolved_Goto fill_out = generator->fill_out_breaks[j];
            int label_index = *hashtable_find_element(&generator->labels_break, fill_out.break_block);
            assert(fill_out.block->instructions[fill_out.instruction_index].type == IR_Instruction_Type::GOTO, "");
            fill_out.block->instructions[fill_out.instruction_index].options.label_index = label_index;
        }
        for (int j = 0; j < generator->fill_out_continues.size; j++) {
            Unresolved_Goto fill_out = generator->fill_out_continues[j];
            int label_index = *hashtable_find_element(&generator->labels_continue, fill_out.break_block);
            assert(fill_out.block->instructions[fill_out.instruction_index].type == IR_Instruction_Type::GOTO, "");
            fill_out.block->instructions[fill_out.instruction_index].options.label_index = label_index;
        }
        dynamic_array_reset(&generator->fill_out_breaks);
        dynamic_array_reset(&generator->fill_out_continues);

        bytecode_generator_compile_function(&generator->compiler->bytecode_generator, ir_func);
    }
    dynamic_array_reset(&generator->function_stubs);

    // Compile to Bytecode
    bytecode_generator_update_references(&generator->compiler->bytecode_generator);
}

void ir_generator_queue_function(IR_Generator* generator, ModTree_Function* function) {
    dynamic_array_push_back(&generator->queue_functions, function);
}

void ir_generator_queue_global(IR_Generator* generator, ModTree_Variable* variable) {
    dynamic_array_push_back(&generator->queue_globals, variable);
}

void ir_generator_queue_and_generate_all(IR_Generator* generator)
{
    ir_generator_queue_module(generator, generator->modtree->root_module);
    ir_generator_generate_queued_items(generator);
    generator->program->entry_function = *hashtable_find_element(&generator->function_mapping, generator->modtree->entry_function);
}


