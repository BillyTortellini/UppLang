#include "ir_code.hpp"
#include "compilation_data.hpp"
#include "bytecode_generator.hpp"
#include "ast.hpp"
#include "symbol_table.hpp"
#include "memory_source.hpp"

void ir_generator_generate_block(IR_Code_Block* ir_block, AST::Code_Block* ast_block);
IR_Data_Access* ir_generator_generate_expression(AST::Expression* expression, IR_Data_Access* destination = 0);
void ir_generator_generate_statement(AST::Statement* statement, IR_Code_Block* ir_block);
IR_Data_Access* ir_data_access_create_constant_usize(u64 value);



// GLOBALS(Generator)
thread_local static IR_Generator* ir_generator;



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
    case IR_Instruction_Type::MATCH: {
        for (int i = 0; i < instruction->options.switch_instr.cases.size; i++) {
            ir_code_block_destroy(instruction->options.switch_instr.cases[i].block);
        }
        dynamic_array_destroy(&instruction->options.switch_instr.cases);
        ir_code_block_destroy(instruction->options.switch_instr.default_block);
        break;
    }
    case IR_Instruction_Type::RETURN:
    case IR_Instruction_Type::FUNCTION_ADDRESS:
    case IR_Instruction_Type::OPERATION:
    case IR_Instruction_Type::LABEL:
    case IR_Instruction_Type::GOTO:
    case IR_Instruction_Type::VARIABLE_DEFINITION:
        break;
    default: panic("Lul");
    }
}

IR_Code_Block* ir_code_block_create(Upp_Function* function = nullptr)
{
    if (function == nullptr) {
        assert(ir_generator->current_block != nullptr, "");
        function = ir_generator->current_block->function;
        assert(function != nullptr, "");
    }

    IR_Code_Block* block = new IR_Code_Block();
    block->function = function;
    block->instructions = dynamic_array_create<IR_Instruction>();
    block->registers = dynamic_array_create<IR_Register>();
    if (ir_generator->current_block == nullptr) {
        block->parent_block = nullptr;
        block->parent_instruction_index = -1;
    }
    else {
        block->parent_block = ir_generator->current_block;
        block->parent_instruction_index = ir_generator->current_block->instructions.size;
    }
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


// To_String
void ir_data_access_append_to_string(IR_Data_Access* access, String* string, IR_Code_Block* current_block, Compilation_Data* compilation_data)
{
    Type_System* type_system = compilation_data->type_system;
    switch (access->type)
    {
    case IR_Data_Access_Type::CONSTANT: {
        auto const_index = access->option.constant_index;
        Upp_Constant* constant = &compilation_data->constant_pool->constants[const_index];
        string_append_formated(string, "Constant #%d ", const_index);
        datatype_append_to_string(constant->type, string, type_system);
        string_append_formated(string, " ");
        datatype_append_value_to_string(
            constant->type, string, constant->memory, datatype_value_format_single_line(),
            0, Memory_Source(nullptr), Memory_Source(nullptr), type_system
        );
        break;
    }
    case IR_Data_Access_Type::GLOBAL_DATA: {
        string_append_formated(string, "Global #%d, type: ", access->option.global_index);
        datatype_append_to_string(access->datatype, string, type_system);
        break;
    }
    case IR_Data_Access_Type::PARAMETER: {
        auto& param_info = access->option.parameter;
        auto& param = param_info.function->signature->parameters[param_info.index];
        string_append_formated(string, "Param \"%s\", type: ", param.name->characters);
        datatype_append_to_string(param.datatype, string, type_system);
        break;
    }
    case IR_Data_Access_Type::REGISTER: {
        auto& reg_access = access->option.register_access;
        auto& reg = reg_access.definition_block->registers[reg_access.index];
        if (reg.name.available) {
            string_append_formated(string, "Register #%d \"%s\", type: ", reg_access.index, reg.name.value->characters);
        }
        else {
            string_append_formated(string, "Register #%d, type: ", reg_access.index);
        }
        datatype_append_to_string(reg.type, string, type_system);
        if (reg_access.definition_block != current_block) {
            string_append_formated(string, " (Non local)");
        }
        break;
    }
    case IR_Data_Access_Type::ADDRESS_OF_VALUE: {
        string_append(string, "Addr-Of: ");
        ir_data_access_append_to_string(access->option.address_of_value, string, current_block, compilation_data);
        break;
    }
    case IR_Data_Access_Type::ARRAY_ELEMENT_ACCESS: {
        string_append(string, "Array_Access: ");
        ir_data_access_append_to_string(access->option.array_access.array_access, string, current_block, compilation_data);
        string_append(string, ", Index_Access: ");
        ir_data_access_append_to_string(access->option.array_access.index_access, string, current_block, compilation_data);
        break;
    }
    case IR_Data_Access_Type::POINTER_DEREFERENCE: {
        string_append(string, "Dereference: ");
        ir_data_access_append_to_string(access->option.pointer_value, string, current_block, compilation_data);
        break;
    }
    case IR_Data_Access_Type::NON_DESTRUCTIVE_CAST: {
        string_append_formated(string, "Nondestructive-Cast(%s) ");
        ir_data_access_append_to_string(access->option.non_destructive_cast.value_access, string, current_block, compilation_data);
        break;
    }
    case IR_Data_Access_Type::MEMBER_ACCESS: {
        string_append_formated(string, "Member \"%s\" of: ", access->option.member_access.member.name->characters);
        ir_data_access_append_to_string(access->option.member_access.struct_access, string, current_block, compilation_data);
        break;
    }
    case IR_Data_Access_Type::NOTHING: {
        string_append(string, "Nothing-Access");
        break;
    }
    default: panic("");
    }
}

void indent_string(String* string, int indentation) {
    for (int i = 0; i < indentation; i++) {
        string_append_formated(string, "    ");
    }
}

void ir_code_block_append_to_string(IR_Code_Block* code_block, String* string, int indentation, Compilation_Data* compilation_data);
void ir_instruction_append_to_string(IR_Instruction* instruction, String* string, int indentation, IR_Code_Block* code_block, Compilation_Data* compilation_data)
{
    indent_string(string, indentation);
    switch (instruction->type)
    {
    case IR_Instruction_Type::FUNCTION_ADDRESS:
    {
        IR_Instruction_Function_Address* function_address = &instruction->options.function_address;
        auto& function = function_address->function;

        string_append_formated(string, "FUNCTION_ADDRESS of %s\n", function->name->characters);
        indent_string(string, indentation + 1);
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(function_address->destination, string, code_block, compilation_data);
        break;
    }
    case IR_Instruction_Type::OPERATION:
    {
        auto& op = instruction->options.operation;
        string->append(ir_operation_as_string(op.type));
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(op.destination, string, code_block, compilation_data);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "operand 1: ");
        ir_data_access_append_to_string(op.operand_1, string, code_block, compilation_data);
        string_append_formated(string, "\n");
        if (op.operand_2 != nullptr) {
            indent_string(string, indentation + 1);
            string_append_formated(string, "operand 2: ");
            ir_data_access_append_to_string(op.operand_2, string, code_block, compilation_data);
            string_append_formated(string, "\n");
        }
        break;
    }
    case IR_Instruction_Type::BLOCK: {
        string_append_formated(string, "BLOCK\n");
        ir_code_block_append_to_string(instruction->options.block, string, indentation + 1, compilation_data);
        break;
    }
    case IR_Instruction_Type::VARIABLE_DEFINITION: {
        string_append_formated(string, "VARIABLE_DEFINITION %s", instruction->options.variable_definition.symbol->id->characters);
        if (instruction->options.variable_definition.initial_value.available) {
            string_append(string, ", value: ");
            ir_data_access_append_to_string(instruction->options.variable_definition.initial_value.value, string, code_block, compilation_data);
        }
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
    case IR_Instruction_Type::FUNCTION_CALL:
    {
        IR_Instruction_Call* call = &instruction->options.call;
        string_append_formated(string, "FUNCTION_CALL\n");
        indent_string(string, indentation + 1);

        Call_Signature* function_sig;
        switch (call->call_type)
        {
        case IR_Instruction_Call_Type::FUNCTION_CALL:
            function_sig = call->options.function->signature;
            break;
        case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL: {
            auto type = call->options.pointer_access->datatype;
            assert(type->type == Datatype_Type::FUNCTION_POINTER, "Function pointer call must be of function type!");
            function_sig = downcast<Datatype_Function_Pointer>(type)->signature;
            break;
        }
        case IR_Instruction_Call_Type::BUILTIN_CALL:
            function_sig = 0;
            break;
        default:
            panic("Hey");
            return;
        }
        if (function_sig != 0) {
            if (function_sig->return_type().available) {
                string_append_formated(string, "dst: ");
                ir_data_access_append_to_string(call->destination, string, code_block, compilation_data);
                string_append_formated(string, "\n");
                indent_string(string, indentation + 1);
            }
        }
        string_append_formated(string, "args: (%d)\n", call->arguments.size);
        for (int i = 0; i < call->arguments.size; i++) {
            indent_string(string, indentation + 2);
            ir_data_access_append_to_string(call->arguments[i], string, code_block, compilation_data);
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
            ir_data_access_append_to_string(call->options.pointer_access, string, code_block, compilation_data);
            break;
        case IR_Instruction_Call_Type::BUILTIN_CALL:
            string_append_formated(string, "BUILTIN_CALL, type: ");
            string->append(ir_builtin_fn_as_string(call->options.builtin_fn));
            break;
        }
        break;
    }
    case IR_Instruction_Type::IF: {
        string_append_formated(string, "IF ");
        ir_data_access_append_to_string(instruction->options.if_instr.condition, string, code_block, compilation_data);
        string_append_formated(string, "\n");
        ir_code_block_append_to_string(instruction->options.if_instr.true_branch, string, indentation + 1, compilation_data);
        indent_string(string, indentation);
        string_append_formated(string, "ELSE\n");
        ir_code_block_append_to_string(instruction->options.if_instr.false_branch, string, indentation + 1, compilation_data);
        break;
    }
    case IR_Instruction_Type::MATCH: 
    {
        Datatype* value_type = instruction->options.switch_instr.condition_access->datatype;
        Datatype_Enum* enum_type = 0;
        if (value_type->type == Datatype_Type::STRUCT) {
            enum_type = downcast<Datatype_Enum>(downcast<Datatype_Struct>(value_type)->tag_member.datatype);
        }
        else
        {
            assert(value_type->type == Datatype_Type::ENUM, "If not union, this must be an enum");
            enum_type = downcast<Datatype_Enum>(value_type);
        }
        string_append_formated(string, "MATCH\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "Condition access: ");
        ir_data_access_append_to_string(instruction->options.switch_instr.condition_access, string, code_block, compilation_data);
        string_append_formated(string, "\n");
        for (int i = 0; i < instruction->options.switch_instr.cases.size; i++) {
            IR_Switch_Case* switch_case = &instruction->options.switch_instr.cases[i];
            indent_string(string, indentation + 1);
            Optional<Enum_Member> member = enum_type_find_member_by_value(enum_type, switch_case->value);
            assert(member.available, "");
            string_append_formated(string, "Case %s: \n", member.value.name->characters);
            ir_code_block_append_to_string(switch_case->block, string, indentation + 2, compilation_data);
        }
        indent_string(string, indentation + 1);
        string_append_formated(string, "Default case: \n");
        ir_code_block_append_to_string(instruction->options.switch_instr.default_block, string, indentation + 2, compilation_data);
        break;
    }
    case IR_Instruction_Type::WHILE: {
        string_append_formated(string, "WHILE\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "Condition code: \n");
        ir_code_block_append_to_string(instruction->options.while_instr.condition_code, string, indentation + 2, compilation_data);
        indent_string(string, indentation + 1);
        string_append_formated(string, "Condition access: ");
        ir_data_access_append_to_string(instruction->options.while_instr.condition_access, string, code_block, compilation_data);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "Body: \n");
        ir_code_block_append_to_string(instruction->options.while_instr.code, string, indentation + 2, compilation_data);
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
            ir_data_access_append_to_string(return_instr->options.return_value, string, code_block, compilation_data);
            break;
        case IR_Instruction_Return_Type::RETURN_EMPTY:
            string_append_formated(string, "RETURN");
            break;
        }
        break;
    }
    default: panic("What");
    }
}

void ir_code_block_append_to_string(IR_Code_Block* code_block, String* string, int indentation, Compilation_Data* compilation_data)
{
    auto type_system = compilation_data->type_system;

    indent_string(string, indentation);
    string_append_formated(string, "Registers:\n");
    for (int i = 0; i < code_block->registers.size; i++) {
        auto& reg = code_block->registers[i];
        indent_string(string, indentation + 1);
        if (reg.name.available) {
            string_append_formated(string, "#%d %s: ", i, reg.name.value->characters);
        }
        else {
            string_append_formated(string, "#%d: ", i);
        }
        datatype_append_to_string(reg.type, string, type_system);
        string_append_formated(string, "\n");
    }
    indent_string(string, indentation);
    string_append_formated(string, "Instructions:\n");
    for (int i = 0; i < code_block->instructions.size; i++) {
        ir_instruction_append_to_string(&code_block->instructions[i], string, indentation + 1, code_block, compilation_data);
        string_append_formated(string, "\n");
    }
}

void function_ir_append_to_string(Upp_Function* function, String* string, int indentation, Compilation_Data* compilation_data)
{
    auto type_system = compilation_data->type_system;

    indent_string(string, indentation);
    string_append_formated(string, "Function-Type:");
    call_signature_append_to_string(function->signature, string, type_system, datatype_format_make_default());
    string_append_formated(string, "\n");
    ir_code_block_append_to_string(function->ir_block, string, indentation, compilation_data);
}

void ir_program_append_to_string(String* string, bool print_generated_functions, Compilation_Data* compilation_data)
{
    string_append_formated(string, "Program Dump:\n-----------------\n");
    for (int i = 0; i < compilation_data->functions.size; i++)
    {
        auto function = compilation_data->functions[i];
        if (function->ir_block == nullptr) {
            continue;
        }
        if (function->symbol == nullptr && !print_generated_functions) { // This is not correct
            continue;
        }

        string_append_formated(string, "Function #%d ", i);
        function_ir_append_to_string(function, string, 0, compilation_data);
        string_append_formated(string, "\n");
    }
}



static IR_Instruction* add_instruction(IR_Instruction& instruction, IR_Code_Block* ir_block = nullptr)
{
    auto& gen = *ir_generator;
    if (ir_block == nullptr) {
        ir_block = gen.current_block;
        assert(gen.current_block != nullptr, "");
    }

    instruction.associated_expr = gen.current_expr;
    instruction.associated_statement = gen.current_statement;
    instruction.associated_pass = gen.current_pass;
    dynamic_array_push_back(&ir_block->instructions, instruction);
    return &ir_block->instructions[ir_block->instructions.size - 1];
}

IR_Data_Access* ir_data_access_create_nothing() {
    return &ir_generator->nothing_access;
}

IR_Instruction_Operation* add_operation_instruction(
    IR_Operation operation, IR_Data_Access* destination, IR_Data_Access* operand_1, IR_Data_Access* operand_2 = nullptr, IR_Code_Block* ir_block = nullptr)
{
    int param_count = ir_operation_parameter_count(operation);
    assert((param_count == 1 && operand_2 == nullptr) || (param_count == 2 && operand_2 != nullptr), "");
    
    IR_Instruction instruction;
    instruction.type = IR_Instruction_Type::OPERATION;
    instruction.options.operation.type = operation;
    instruction.options.operation.destination = destination;
    instruction.options.operation.operand_1 = operand_1;
    instruction.options.operation.operand_2 = operand_2 == nullptr ? ir_data_access_create_nothing() : operand_2;
    return &add_instruction(instruction)->options.operation;
}

IR_Data_Access* ir_data_access_create_global(Upp_Global* global)
{
    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = global->type;
    access->type = IR_Data_Access_Type::GLOBAL_DATA;
    access->option.global_index = global->index;
    dynamic_array_push_back(&ir_generator->data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_parameter(Upp_Function* function, int parameter_index)
{
    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = function->signature->parameters[parameter_index].datatype;
    access->type = IR_Data_Access_Type::PARAMETER;
    access->option.parameter.function = function;
    access->option.parameter.index = parameter_index;
    dynamic_array_push_back(&ir_generator->data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_register(int register_index)
{
    auto& gen = *ir_generator;
    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = gen.current_block->registers[register_index].type;
    access->type = IR_Data_Access_Type::REGISTER;
    access->option.register_access.definition_block = gen.current_block;
    access->option.register_access.index = register_index;
    dynamic_array_push_back(&ir_generator->data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_intermediate(Datatype* signature)
{
    auto& gen = *ir_generator;
    assert(gen.current_block != 0, "");
    assert(!datatype_is_unknown(signature), "Cannot have register with unknown type");
    assert(!type_size_is_unfinished(signature), "Cannot have register with 0 size!");

    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = signature;
    access->type = IR_Data_Access_Type::REGISTER;
    access->option.register_access.definition_block = gen.current_block;
    access->option.register_access.index = gen.current_block->registers.size;
    dynamic_array_push_back(&ir_generator->data_accesses, access);

    IR_Register reg;
    reg.type = signature;
    reg.name.available = false;
    reg.has_definition_instruction = false;
    dynamic_array_push_back(&gen.current_block->registers, reg);

    return access;
}

IR_Data_Access* ir_data_access_create_dereference(IR_Data_Access* pointer_access)
{
    // Shortcut for pointer accesses of address-of, e.g. *(&x) == xp
    if (pointer_access->type == IR_Data_Access_Type::ADDRESS_OF_VALUE) {
        return pointer_access->option.address_of_value;
    }

    Datatype* ptr_type = pointer_access->datatype;
    assert(ptr_type->type == Datatype_Type::POINTER, "");

    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = downcast<Datatype_Pointer>(ptr_type)->element_type;
    access->type = IR_Data_Access_Type::POINTER_DEREFERENCE;
    access->option.pointer_value = pointer_access;
    dynamic_array_push_back(&ir_generator->data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_non_destructive_cast(IR_Data_Access* value_access, Datatype* result_type)
{
    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = result_type;
    access->type = IR_Data_Access_Type::NON_DESTRUCTIVE_CAST;
    access->option.non_destructive_cast.value_access = value_access;
    dynamic_array_push_back(&ir_generator->data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_address_of(IR_Data_Access* value_access)
{
    // Shortcut for address_of pointer dereference, e.g. &(*xp) == xp
    if (value_access->type == IR_Data_Access_Type::POINTER_DEREFERENCE) {
        return value_access->option.pointer_value;
    }

    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = upcast(type_system_make_pointer(ir_generator->compilation_data->type_system, value_access->datatype));
    access->type = IR_Data_Access_Type::ADDRESS_OF_VALUE;
    access->option.address_of_value = value_access;
    dynamic_array_push_back(&ir_generator->data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_member(IR_Data_Access* struct_access, Struct_Member member)
{
    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = member.datatype;
    access->type = IR_Data_Access_Type::MEMBER_ACCESS;
    access->option.member_access.struct_access = struct_access;
    access->option.member_access.member = member;
    dynamic_array_push_back(&ir_generator->data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_array_or_slice_access(IR_Data_Access* array_access, IR_Data_Access* index_access, bool do_bounds_check)
{
    auto& types = ir_generator->compilation_data->type_system->predefined_types;

    Datatype* array_type = array_access->datatype;
    Datatype* element_type = 0;
    if (array_type->type == Datatype_Type::ARRAY) {
        element_type = downcast<Datatype_Array>(array_type)->element_type;
    }
    else if (array_type->type == Datatype_Type::SLICE) {
        element_type = downcast<Datatype_Slice>(array_type)->element_type;
    }
    else {
        panic("");
    }

    if (do_bounds_check)
    {
        IR_Data_Access* size_access = nullptr;
        if (array_type->type == Datatype_Type::SLICE) {
            auto slice = downcast<Datatype_Slice>(array_type);
            size_access = ir_data_access_create_member(array_access, slice->size_member);
        }
        else {
            auto arr = downcast<Datatype_Array>(array_type);
            size_access = ir_data_access_create_constant_usize(arr->element_count);
        }

        auto& gen = *ir_generator;
        assert(gen.current_block != 0, "");
        IR_Data_Access* condition_access = ir_data_access_create_intermediate(upcast(types.bool_type));
        add_operation_instruction(IR_Operation::GREATER_OR_EQUAL, condition_access, index_access, size_access);

        IR_Instruction if_instr;
        if_instr.type = IR_Instruction_Type::IF;
        if_instr.options.if_instr.condition = condition_access;
        if_instr.options.if_instr.true_branch = ir_code_block_create();
        if_instr.options.if_instr.false_branch = ir_code_block_create();
        add_instruction(if_instr);

        IR_Instruction exit_instr;
        exit_instr.type = IR_Instruction_Type::RETURN;
        exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
        exit_instr.options.return_instr.options.exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Array out of bounds access");
        add_instruction(exit_instr, if_instr.options.if_instr.true_branch);
    }

    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = element_type;
    access->type = IR_Data_Access_Type::ARRAY_ELEMENT_ACCESS;
    access->option.array_access.array_access = array_access;
    access->option.array_access.index_access = index_access;
    dynamic_array_push_back(&ir_generator->data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_constant(Datatype* signature, Array<byte> bytes)
{
    auto result = constant_pool_add_constant(ir_generator->compilation_data->constant_pool, signature, bytes);
    assert(result.success, "Must always work");

    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = result.options.constant.type;
    access->type = IR_Data_Access_Type::CONSTANT;
    access->option.constant_index = result.options.constant.constant_index;
    dynamic_array_push_back(&ir_generator->data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_constant(Upp_Constant constant)
{
    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = constant.type;
    access->type = IR_Data_Access_Type::CONSTANT;
    access->option.constant_index = constant.constant_index;
    dynamic_array_push_back(&ir_generator->data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_constant_i32(i32 value) {
    auto& types = ir_generator->compilation_data->type_system->predefined_types;
    return ir_data_access_create_constant(upcast(types.i32_type), array_create_static((byte*)&value, sizeof(i32)));
}

IR_Data_Access* ir_data_access_create_constant_usize(u64 value) {
    auto& types = ir_generator->compilation_data->type_system->predefined_types;
    return ir_data_access_create_constant(upcast(types.usize), array_create_static_as_bytes(&value, 1));
}

IR_Data_Access* ir_data_access_create_constant_bool(bool value) {
    auto& types = ir_generator->compilation_data->type_system->predefined_types;
    return ir_data_access_create_constant(upcast(types.bool_type), array_create_static_as_bytes(&value, 1));
}




// Code Gen
Expression_Info* get_info(AST::Expression* node) {
    return pass_get_node_info(ir_generator->current_pass, node, Info_Query::READ_NOT_NULL, ir_generator->compilation_data);
}

Statement_Info* get_info(AST::Statement* node) {
    return pass_get_node_info(ir_generator->current_pass, node, Info_Query::READ_NOT_NULL, ir_generator->compilation_data);
}

Case_Info* get_info(AST::Switch_Case* node) {
    return pass_get_node_info(ir_generator->current_pass, node, Info_Query::READ_NOT_NULL, ir_generator->compilation_data);
}

Symbol* get_info(AST::Symbol_Node* node) {
    return pass_get_node_info(ir_generator->current_pass, node, Info_Query::READ_NOT_NULL, ir_generator->compilation_data)->symbol;
}

Call_Info* get_info(AST::Call_Node* node) {
    return pass_get_node_info(ir_generator->current_pass, node, Info_Query::READ_NOT_NULL, ir_generator->compilation_data);
}

void generate_member_initalizers(IR_Data_Access* struct_access, AST::Call_Node* call_node)
{
    auto call_info = get_info(call_node);
    assert(call_info->origin.type == Call_Origin_Type::STRUCT_INITIALIZER || call_info->origin.type == Call_Origin_Type::UNION_INITIALIZER, "");
    Datatype_Struct* structure = call_info->origin.options.structure;

    for (int i = 0; i < call_info->parameter_values.size; i++)
    {
        auto& param_value = call_info->parameter_values[i]; 
        auto& member = structure->members[i];
        if (param_value.value_type == Parameter_Value_Type::NOT_SET) continue;
        assert(param_value.value_type != Parameter_Value_Type::DATATYPE_KNOWN, "");

        IR_Data_Access* value_access;
        if (param_value.value_type == Parameter_Value_Type::COMPTIME_VALUE) {
            value_access = ir_data_access_create_constant(param_value.options.constant);
        }
        else {
            assert(param_value.options.argument_index != -1, "");
            auto arg_expr = call_info->argument_infos[param_value.options.argument_index].expression;
            value_access = ir_generator_generate_expression(arg_expr);
        }

        add_operation_instruction(IR_Operation::MOVE, ir_data_access_create_member(struct_access, member), value_access);
    }
    for (int i = 0; i < call_node->subtype_initializers.size; i++) 
    {
        auto initializer = call_node->subtype_initializers[i];
        generate_member_initalizers(struct_access, initializer->call_node);
    }
}

int ir_generator_push_label_instruction(IR_Code_Block* code_block = nullptr)
{
    if (code_block == nullptr) {
        assert(ir_generator->current_block != nullptr, "");
        code_block = ir_generator->current_block;
    }

    IR_Instruction_Reference ref;
    ref.block = code_block;
    ref.index = code_block->instructions.size;
    dynamic_array_push_back(&ir_generator->label_positions, ref);

    IR_Instruction label;
    label.type = IR_Instruction_Type::LABEL;
    label.options.label_index = ir_generator->label_positions.size;
    add_instruction(label, code_block);

    return label.options.label_index;
}

IR_Data_Access* ir_generator_generate_cast(IR_Data_Access* source, IR_Data_Access* destination, Auto_Cast_Info auto_cast_info)
{
    auto cast_type = auto_cast_info.type;
    auto& gen = *ir_generator;

    auto type_system = ir_generator->compilation_data->type_system;
    auto& types = type_system->predefined_types;
    auto source_type = source->datatype;

    auto move_access_to_destination = [&](IR_Data_Access* access) -> IR_Data_Access* {
        if (destination == 0) {
            destination = access;
            return access;
        }
        add_operation_instruction(IR_Operation::MOVE, destination, access);
        return destination;
    };
    auto make_destination_access_on_demand = [&](Datatype* result_type) -> IR_Data_Access* {
        if (destination == 0) {
            destination = ir_data_access_create_intermediate(result_type);
        }
        return destination;
    };

    switch (cast_type)
    {
    case Auto_Cast_Type::NO_OPERATION: {
        return move_access_to_destination(source);
    }
    case Auto_Cast_Type::PATTERN_CAST:
    case Auto_Cast_Type::INVALID:
    case Auto_Cast_Type::CUSTOM_CAST_INVALID_FUNCTION:
    case Auto_Cast_Type::UNKNOWN: {
        panic("Should not get to ir-generator");
        return nullptr;
    }
    case Auto_Cast_Type::FUNCTION_POINTERS:
    case Auto_Cast_Type::TO_BASE_TYPE: {
        return move_access_to_destination(ir_data_access_create_non_destructive_cast(source, auto_cast_info.result_type));
    }
    case Auto_Cast_Type::ADDRESS_OF: 
    {
        return move_access_to_destination(ir_data_access_create_address_of(source));
    }
    case Auto_Cast_Type::PRIMITIVE_CAST: 
    {
        auto destination = make_destination_access_on_demand(auto_cast_info.result_type);
        add_operation_instruction(IR_Operation::PRIMITIVE_CAST, destination, source);
        return destination;
    }
    case Auto_Cast_Type::DEREFERENCE: 
    {
        int pointer_from = datatype_get_modifier_info(source->datatype).pointer_level;
        int pointer_to = datatype_get_modifier_info(auto_cast_info.result_type).pointer_level;
        assert(pointer_from >= pointer_to, "");
        IR_Data_Access* result_access = source;
        for (int i = 0; i < pointer_from - pointer_to; i++) {
            result_access = ir_data_access_create_dereference(result_access);
        }
        return move_access_to_destination(result_access);
    }
    case Auto_Cast_Type::CUSTOM_CAST: 
    {
        Upp_Function* function = auto_cast_info.custom_cast_function;
        assert(function != nullptr, "");

        bool to_by_ref   = !types_are_equal(function->signature->return_type().value, auto_cast_info.result_type);
        bool from_by_ref = !types_are_equal(function->signature->parameters[0].datatype, source_type);
        if (from_by_ref) {
            source = ir_data_access_create_address_of(source);
        }

        IR_Instruction instr;
        instr.type = IR_Instruction_Type::FUNCTION_CALL;
        instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
        instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
        dynamic_array_push_back(&instr.options.call.arguments, source);
        if (to_by_ref) {
            instr.options.call.destination = ir_data_access_create_intermediate(function->signature->return_type().value);
        }
        else {
            instr.options.call.destination = make_destination_access_on_demand(auto_cast_info.result_type);
        }
        instr.options.call.options.function = auto_cast_info.custom_cast_function;
        add_instruction(instr);

        if (to_by_ref) {
            return move_access_to_destination(ir_data_access_create_dereference(instr.options.call.destination));
        }

        return destination;
    }
    default: panic("");
    }

    panic("");
    return nullptr;
}

IR_Data_Access* ir_generator_generate_overload_access(
    Custom_Operator_Instance_Value instance, AST::Expression* arg0, AST::Expression* arg1, IR_Data_Access* destination = nullptr)
{
    Upp_Function* instance_function = instance.instance_functions[0];
    assert(instance_function != nullptr, "");

    auto move_access_to_destination = [&](IR_Data_Access* access) -> IR_Data_Access* {
        if (destination == 0) {
            destination = access;
            return access;
        }
        add_operation_instruction(IR_Operation::MOVE, destination, access);
        return destination;
    };
    auto make_destination_access_on_demand = [&](Datatype* result_type) -> IR_Data_Access* {
        if (destination == 0) {
            destination = ir_data_access_create_intermediate(result_type);
        }
        return destination;
    };

    // Create arguments
    IR_Data_Access* access0 = nullptr;
    IR_Data_Access* access1 = nullptr;
    if (arg0 != nullptr) {
        access0 = ir_generator_generate_expression(arg0);
        if (instance.custom_op->parameters[0].by_reference) {
            access0 = ir_data_access_create_address_of(access0);
        }
    }
    if (arg1 != nullptr) {
        access1 = ir_generator_generate_expression(arg1);
        if (instance.custom_op->parameters[1].by_reference) {
            access1 = ir_data_access_create_address_of(access1);
        }
    }

    IR_Instruction instr;
    instr.type = IR_Instruction_Type::FUNCTION_CALL;
    instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
    instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(access1 == nullptr ? 1 : 2);
    if (access0 != nullptr) {
        dynamic_array_push_back(&instr.options.call.arguments, access0);
    }
    if (access1 != nullptr) {
        dynamic_array_push_back(&instr.options.call.arguments, access1);
    }
    if (instance.custom_op->result_by_reference) {
        instr.options.call.destination = ir_data_access_create_intermediate(instance_function->signature->return_type().value);
    }
    else {
        instr.options.call.destination = make_destination_access_on_demand(instance_function->signature->return_type().value);
    }
    instr.options.call.options.function = instance_function;
    add_instruction(instr);

    if (instance.custom_op->result_by_reference) {
        return move_access_to_destination(ir_data_access_create_dereference(instr.options.call.destination));
    }
    return instr.options.call.destination;
}

IR_Data_Access* ir_generator_generate_expression_no_cast(AST::Expression* expression, IR_Data_Access* destination = 0)
{
    auto compilation_data = ir_generator->compilation_data;
    Constant_Pool* constant_pool = compilation_data->constant_pool;
    auto type_system = compilation_data->type_system;
    auto& types = type_system->predefined_types;
    auto& gen = *ir_generator;
    auto& ids = compilation_data->identifier_pool.predefined_ids;
    auto ir_block = gen.current_block;

    auto backup_expr = gen.current_expr;
    gen.current_expr = expression;
    SCOPE_EXIT(gen.current_expr = expression);

    auto info = get_info(expression);
    auto result_type = expression_info_get_datatype(info, true, type_system);
    if (info->result_type == Expression_Result_Type::VALUE) {
        result_type = info->options.value.datatype;
    }
    assert(info->is_valid, "Cannot contain errors!");

    auto move_access_to_destination = [&](IR_Data_Access* access) -> IR_Data_Access* {
        if (destination == 0) {
            destination = access;
            return access;
        }
        add_operation_instruction(IR_Operation::MOVE, destination, access);
        return destination;
    };
    auto make_destination_access_on_demand = [&](Datatype* result_type) -> IR_Data_Access* {
        if (destination == 0) {
            destination = ir_data_access_create_intermediate(result_type);
        }
        return destination;
    };

    // Handle different expression results
    switch (info->result_type)
    {
    case Expression_Result_Type::CONSTANT:
        return move_access_to_destination(ir_data_access_create_constant(info->options.constant));
    case Expression_Result_Type::DATATYPE:
        return move_access_to_destination(ir_data_access_create_constant(upcast(types.type_handle), array_create_static_as_bytes(&result_type->type_handle, 1)));
    case Expression_Result_Type::FUNCTION: {
        // Function pointer read
        IR_Instruction load_instr;
        load_instr.type = IR_Instruction_Type::FUNCTION_ADDRESS;
        load_instr.options.function_address.function = info->options.function;
        load_instr.options.function_address.destination = make_destination_access_on_demand(result_type);
        add_instruction(load_instr);
        return destination;
    }
    case Expression_Result_Type::HARDCODED_FUNCTION:
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    case Expression_Result_Type::POLYMORPHIC_STRUCT:
        panic("must not happen");
    case Expression_Result_Type::NOTHING: // Nothing also needs to generate the expression...
    case Expression_Result_Type::VALUE:
        break; // Rest of this function
    default: panic("");
    }

    // Handle expression value
    Datatype* value_type = result_type;
    switch (expression->type)
    {
    case AST::Expression_Type::BINARY_OPERATION:
    {
        auto& binop = expression->options.binop;

        // Handle overloads
        auto& overload = info->specifics.overload;
        if (overload.instance_functions[0] != nullptr) {
            return ir_generator_generate_overload_access(overload, binop.left, binop.right, destination);
        }

        auto op_instr = add_operation_instruction(
            ast_binop_to_ir_operation(binop.type),
            make_destination_access_on_demand(result_type),
            ir_generator_generate_expression(binop.left),
            ir_generator_generate_expression(binop.right)
        );
        return op_instr->destination;
    }
    case AST::Expression_Type::UNARY_OPERATION:
    {
        auto& unop = expression->options.unop;
        IR_Data_Access* access = ir_generator_generate_expression(expression->options.unop.expr);

        auto& overload = info->specifics.overload;
        if (overload.instance_functions[0] != nullptr) {
            return ir_generator_generate_overload_access(overload, unop.expr, nullptr, destination);
        }

        switch (expression->options.unop.type)
        {
        case AST::Unop::ADDRESS_OF: {
            return move_access_to_destination(ir_data_access_create_address_of(access));
        }
        case AST::Unop::DEREFERENCE: {
            return move_access_to_destination(ir_data_access_create_dereference(access));
        }
        case AST::Unop::NOT: 
        case AST::Unop::NEGATE:
        {
            auto op_instr = add_operation_instruction(
                (expression->options.unop.type == AST::Unop::NOT ? IR_Operation::NOT : IR_Operation::NEGATE),
                make_destination_access_on_demand(result_type),
                access
            );
            return op_instr->destination;
        }
        default: panic("HEY");
        }
        panic("HEY");
        break;
    }
    case AST::Expression_Type::PATTERN_VARIABLE: {
        panic("Shouldn't happen!");
    }
    case AST::Expression_Type::FUNCTION_CALL:
    {
        auto& call = expression->options.call;
        auto call_info = get_info(call.call_node);

        IR_Instruction call_instr;
        call_instr.type = IR_Instruction_Type::FUNCTION_CALL;
        switch (call_info->origin.type)
        {
        case Call_Origin_Type::FUNCTION: {
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            call_instr.options.call.options.function = call_info->origin.options.function;
            assert(call_instr.options.call.options.function != nullptr, "");
            break;
        }
        case Call_Origin_Type::POLY_FUNCTION: {
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            assert(call_info->instanciated, "");
            call_instr.options.call.options.function = call_info->instanciation_data.function;
            assert(call_instr.options.call.options.function != nullptr, "");
            assert(call_instr.options.call.options.function != nullptr, "");
            break;
        }
        case Call_Origin_Type::FUNCTION_POINTER: {
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_POINTER_CALL;
            assert(!call.is_dot_call, "");
            call_instr.options.call.options.pointer_access = ir_generator_generate_expression(call.expr);
            break;
        }
        case Call_Origin_Type::HARDCODED:
        {
            auto& hardcoded = call_info->origin.options.hardcoded;
            auto hardcoded_info = hardcoded_type_get_info(hardcoded);

            if ((int)hardcoded_info.ir_operation != -1)
            {
                auto value_expr = call_info->argument_infos[call_info->parameter_values[0].options.argument_index].expression;
                IR_Data_Access* first_arg = ir_generator_generate_expression(
                    call_info->argument_infos[call_info->parameter_values[0].options.argument_index].expression
                );
                IR_Data_Access* second_arg = nullptr;
                if (ir_operation_parameter_count(hardcoded_info.ir_operation) == 2) {
                    second_arg = ir_generator_generate_expression(
                        call_info->argument_infos[call_info->parameter_values[0].options.argument_index].expression
                    );
                }
                auto instr = add_operation_instruction(
                    hardcoded_info.ir_operation,
                    make_destination_access_on_demand(result_type),
                    first_arg, second_arg
                );
                return instr->destination;
            }

            switch (hardcoded)
            {
            case Hardcoded_Type::ALIGN_OF:
            case Hardcoded_Type::SIZE_OF:
            case Hardcoded_Type::RETURN_TYPE: 
            case Hardcoded_Type::TYPE_OF: {
                panic("Should be handled in semantic analyser");
                break;
            }
            case Hardcoded_Type::ENUM_VALUE_AS_STRING:
            {
                AST::Expression* arg_expr = call_info->argument_infos[call_info->parameter_values[0].options.argument_index].expression;
                Datatype* datatype = expression_info_get_datatype(get_info(arg_expr), false, type_system);
                assert(datatype->type == Datatype_Type::ENUM, "");
                Datatype_Enum* enumeration = downcast<Datatype_Enum>(datatype);

                // Create function if not cached
                if (enumeration->value_as_string_fn == nullptr)
                {
                    Call_Signature* signature = call_signature_create_empty();
                    call_signature_add_parameter(signature, ids.value, datatype, true, false, false);
                    call_signature_add_return_type(signature, upcast(types.string), compilation_data);
                    signature = call_signature_register(signature, compilation_data);
                    String* name = nullptr;
                    {
                        Arena* arena = &compilation_data->tmp_arena;
                        auto checkpoint = arena->make_checkpoint();
                        SCOPE_EXIT(checkpoint.rewind());
                        String buffer = string_create(arena);
                        static int counter = 0;
                        buffer.append("_enum_as_string_");
                        buffer.append(enumeration->name);
                        buffer.append_formated("_%d", counter);
                        counter += 1;
                        name = identifier_pool_add(&compilation_data->identifier_pool, buffer);
                    }

                    Upp_Function* function = upp_function_create_empty(signature, name, compilation_data);
                    enumeration->value_as_string_fn = function;

                    function->ir_block = ir_code_block_create(function);
                    RESTORE_ON_SCOPE_EXIT(ir_generator->current_block, function->ir_block);

                    IR_Instruction switch_instr;
                    switch_instr.type = IR_Instruction_Type::MATCH;
                    switch_instr.options.switch_instr.condition_access = ir_data_access_create_parameter(function, 0);
                    switch_instr.options.switch_instr.default_block = ir_code_block_create(function);
                    switch_instr.options.switch_instr.cases = dynamic_array_create<IR_Switch_Case>();
                    {
                        RESTORE_ON_SCOPE_EXIT(ir_generator->current_block, switch_instr.options.switch_instr.default_block);
                        IR_Instruction return_instr;
                        return_instr.type = IR_Instruction_Type::RETURN;
                        return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_DATA;
                        return_instr.options.return_instr.options.return_value = 
                            ir_data_access_create_constant(constant_pool->predefined.empty_string);
                        add_instruction(return_instr);
                    }
                    for (int i = 0; i < enumeration->members.size; i++)
                    {
                        Enum_Member& member = enumeration->members[i];

                        IR_Switch_Case switch_case;
                        switch_case.block = ir_code_block_create(function);
                        switch_case.value = member.value;

                        RESTORE_ON_SCOPE_EXIT(ir_generator->current_block, switch_case.block);
                        dynamic_array_push_back(&switch_instr.options.switch_instr.cases, switch_case);

                        IR_Instruction return_instr;
                        return_instr.type = IR_Instruction_Type::RETURN;
                        return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_DATA;
                        return_instr.options.return_instr.options.return_value = 
                            ir_data_access_create_constant(constant_pool->add_string_assume_valid(*member.name));
                        add_instruction(return_instr);
                    }
                    add_instruction(switch_instr);

                    // Usually we shouldn't reach this point, as switch also handles default block
                    IR_Instruction exit_instr;
                    exit_instr.type = IR_Instruction_Type::RETURN;
                    exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
                    exit_instr.options.return_instr.options.exit_code = exit_code_make(Exit_Code_Type::CODE_ERROR, "Enum-value to string invalid case");
                    add_instruction(exit_instr);
                }

                // Call function
                IR_Instruction call_instr;
                call_instr.type = IR_Instruction_Type::FUNCTION_CALL;
                call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
                call_instr.options.call.options.function = enumeration->value_as_string_fn;
                call_instr.options.call.destination = make_destination_access_on_demand(upcast(types.string));
                call_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>();
                dynamic_array_push_back(
                    &call_instr.options.call.arguments, ir_generator_generate_expression(arg_expr)
                );
                add_instruction(call_instr);

                return destination;
            }
            case Hardcoded_Type::ASSERT_FN:
            {
                auto call_info = get_info(call.call_node);
                auto arg_expr = call_info->argument_infos[call_info->parameter_values[0].options.argument_index].expression;

                IR_Instruction if_instr;
                if_instr.type = IR_Instruction_Type::IF;
                if_instr.options.if_instr.condition = ir_generator_generate_expression(arg_expr);
                if_instr.options.if_instr.true_branch = ir_code_block_create();
                if_instr.options.if_instr.false_branch = ir_code_block_create();
                add_instruction(if_instr);

                IR_Instruction exit_instr;
                exit_instr.type = IR_Instruction_Type::RETURN;
                exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
                exit_instr.options.return_instr.options.exit_code = exit_code_make(Exit_Code_Type::CODE_ERROR, "Assertion failed");
                add_instruction(exit_instr, if_instr.options.if_instr.false_branch);

                call_instr.options.call.destination = ir_data_access_create_nothing();
                return call_instr.options.call.destination;
            }
            case Hardcoded_Type::PANIC_FN:
            {
                IR_Instruction exit_instr;
                exit_instr.type = IR_Instruction_Type::RETURN;
                exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
                exit_instr.options.return_instr.options.exit_code = exit_code_make(Exit_Code_Type::CODE_ERROR, "Panic called");
                add_instruction(exit_instr);
                return ir_data_access_create_nothing();
            }
            case Hardcoded_Type::STRUCT_TAG: 
            {
                auto call_info = get_info(call.call_node);
                auto arg_expr = call_info->argument_infos[call_info->parameter_values[0].options.argument_index].expression;

                auto structure = downcast<Datatype_Struct>(get_info(arg_expr)->auto_cast_info.result_type);
                auto struct_access = ir_generator_generate_expression(arg_expr);
                return move_access_to_destination(ir_data_access_create_member(struct_access, structure->tag_member));
            }
            default: break; // All other hardcoded-functions are passed on to the next stages
            }

            assert((int)hardcoded_info.builtin_fn != -1, "Other hardcoded functions should have been handled by now");
            call_instr.options.call.call_type = IR_Instruction_Call_Type::BUILTIN_CALL;
            call_instr.options.call.options.builtin_fn = hardcoded_info.builtin_fn;
            break;
        }
        default: {
            panic("Other cases shouldn't have made it this far...");
        }
        }

        // Generate return value
        auto signature = call_info->origin.signature;
        call_instr.options.call.destination = ir_data_access_create_nothing();
        if (signature->return_type_index != -1) {
            // Note: There was more code here before, maybe this was necessary?
            call_instr.options.call.destination = make_destination_access_on_demand(result_type);
        }

        // Generate arguments 
        call_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(signature->parameters.size);
        for (int i = 0; i < call_info->parameter_values.size && i != call_info->origin.signature->return_type_index; i++)
        {
            auto& param_info = call_info->origin.signature->parameters[i];
            if (param_info.pattern_variable_index != -1) continue;
            auto& param_value = call_info->parameter_values[i];

            // All parameters must be set, or use default value
            IR_Data_Access* argument_access = nullptr;
            if (param_value.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION) 
            {
                auto& arg_expr = call_info->argument_infos[param_value.options.argument_index].expression;
                argument_access = ir_generator_generate_expression(arg_expr);
            }
            else if (param_value.value_type == Parameter_Value_Type::COMPTIME_VALUE)
            {
                argument_access = ir_data_access_create_constant(param_value.options.constant);
            }
            else 
            {
                panic("We don't have default arguments anymore, so this shouldn't happen");
            }
            dynamic_array_push_back(&call_instr.options.call.arguments, argument_access);
        }

        add_instruction(call_instr);
        return call_instr.options.call.destination;
    }
    case AST::Expression_Type::PATH_LOOKUP:
    {
        auto symbol = get_info(expression->options.path_lookup->last());
        IR_Data_Access* result_access;
        switch (symbol->type)
        {
        case Symbol_Type::GLOBAL: {
            result_access = ir_data_access_create_global(symbol->options.global);
            break;
        }
        case Symbol_Type::VARIABLE: {
            result_access = *hashtable_find_element(&ir_generator->variable_mapping, symbol);
            break;
        }
        case Symbol_Type::PARAMETER: {
            result_access = ir_data_access_create_parameter(ir_block->function, symbol->options.parameter.index);
            break;
        }
        default: panic("Other Symbol-cases must be handled by analyser or in this function above!");
        }
        return move_access_to_destination(result_access);
    }
    case AST::Expression_Type::IF_THEN_ELSE:
    {
        auto& if_then_else = expression->options.if_then_else;

        // Create result-register, because both code paths needs to write to it
        if (destination == nullptr) {
            destination = ir_data_access_create_intermediate(result_type);
        }

        // Generate if and code-blocks
        IR_Instruction if_instr;
        if_instr.type = IR_Instruction_Type::IF;
        if_instr.options.if_instr.condition = ir_generator_generate_expression(if_then_else.condition);
        if_instr.options.if_instr.true_branch = ir_code_block_create();
        if_instr.options.if_instr.false_branch = ir_code_block_create();
        {
            RESTORE_ON_SCOPE_EXIT(ir_generator->current_block, if_instr.options.if_instr.true_branch);
            destination = ir_generator_generate_expression(if_then_else.then_value, destination);
        }
        {
            RESTORE_ON_SCOPE_EXIT(ir_generator->current_block, if_instr.options.if_instr.false_branch);
            destination = ir_generator_generate_expression(if_then_else.else_value, destination);
        }
        add_instruction(if_instr);

        return destination;
    }
    case AST::Expression_Type::STRUCT_INITIALIZER:
    {
        // Handle slice-initializer first
        auto call_info = get_info(expression->options.struct_initializer.call_node);
        if (call_info->origin.type == Call_Origin_Type::SLICE_INITIALIZER)
        {
            Datatype_Slice* slice_type = call_info->origin.options.slice_type;
            assert(call_info->parameter_values.size == 2, "");

            IR_Data_Access* slice_access = make_destination_access_on_demand(result_type);
            IR_Data_Access* data_access = ir_data_access_create_member(slice_access, slice_type->data_member);
            IR_Data_Access* size_access = ir_data_access_create_member(slice_access, slice_type->size_member);

            AST::Expression* data_expr = call_info->argument_infos[call_info->parameter_values[0].options.argument_index].expression;
            AST::Expression* size_expr = call_info->argument_infos[call_info->parameter_values[0].options.argument_index].expression;
            ir_generator_generate_expression(data_expr, data_access);
            ir_generator_generate_expression(size_expr, size_access);

            return slice_access;
        }

        IR_Data_Access* struct_access = make_destination_access_on_demand(result_type);
        assert(call_info->origin.type == Call_Origin_Type::STRUCT_INITIALIZER ||
            call_info->origin.type == Call_Origin_Type::UNION_INITIALIZER, "");

        // First, set all tags to correct values
        Datatype_Struct* structure = info->specifics.struct_init_lowest_subtype;
        while (structure->parent != nullptr)
        {
            Datatype_Struct* parent = structure->parent;
            assert(parent->subtypes.size > 0 && structure->subtype_index < parent->subtypes.size, "");
            int tag_value = structure->subtype_index + 1;

            add_operation_instruction(
                IR_Operation::MOVE, 
                ir_data_access_create_member(struct_access, parent->tag_member),
                ir_data_access_create_constant(
                    constant_pool->add_enum_value_assume_valid(downcast<Datatype_Enum>(parent->tag_member.datatype), tag_value)
                )
            );
            structure = structure->parent;
        }

        // Generate initializers for members
        generate_member_initalizers(struct_access, expression->options.struct_initializer.call_node);
        return struct_access;
    }
    case AST::Expression_Type::ARRAY_INITIALIZER:
    {
        auto& array_init = expression->options.array_initializer;

        // Handle empty slice
        if (array_init.values.size == 0)
        {
            assert(array_init.values.size == 0, "");
            Upp_Slice_Base slice_base;
            slice_base.data = 0;
            slice_base.size = 0;
            auto slice_constant = constant_pool_add_constant(gen.compilation_data->constant_pool, result_type, array_create_static_as_bytes(&slice_base, 1));
            assert(slice_constant.success, "Empty slice must succeed!");

            auto destination = make_destination_access_on_demand(result_type);
            add_operation_instruction(IR_Operation::MOVE, destination, ir_data_access_create_constant(slice_constant.options.constant));
            return destination;
        }

        IR_Data_Access* result_access = nullptr;
        IR_Data_Access* array_access = nullptr;
        if (result_type->type == Datatype_Type::ARRAY) {
            array_access = make_destination_access_on_demand(result_type);
            result_access = array_access;
        }
        else 
        {
            assert(result_type->type == Datatype_Type::SLICE, "");
            Datatype_Slice* slice_type = downcast<Datatype_Slice>(result_type);
            // Create register/local-memory for array
            array_access = ir_data_access_create_intermediate(
                upcast(type_system_make_array(type_system, slice_type->element_type, true, array_init.values.size))
            );

            // Init slice (Set size and data members)
            result_access = make_destination_access_on_demand(result_type);
            add_operation_instruction(
                IR_Operation::MOVE,
                ir_data_access_create_member(result_access, slice_type->size_member),
                ir_data_access_create_constant_usize(array_init.values.size)
            );

            add_operation_instruction(
                IR_Operation::MOVE,
                ir_data_access_create_member(result_access, slice_type->data_member),
                ir_data_access_create_address_of(ir_data_access_create_array_or_slice_access(
                    array_access, ir_data_access_create_constant(constant_pool->predefined.usize_zero), false
                ))
            );
        }

        for (int i = 0; i < array_init.values.size; i++)
        {
            ir_generator_generate_expression(
                array_init.values[i], 
                ir_data_access_create_array_or_slice_access(
                    array_access, ir_data_access_create_constant_usize(i), false
                )
            );
        }

        return result_access;
    }
    case AST::Expression_Type::ARRAY_ACCESS:
    {
        auto& access_node = expression->options.array_access;
        auto& overload = info->specifics.overload;
        if (overload.instance_functions[0] != nullptr) {
            return ir_generator_generate_overload_access(overload, access_node.array_expr, access_node.index_expr, destination);
        }

        return move_access_to_destination(ir_data_access_create_array_or_slice_access(
            ir_generator_generate_expression(expression->options.array_access.array_expr),
            ir_generator_generate_expression(expression->options.array_access.index_expr),
            true
        ));
    }
    case AST::Expression_Type::SUBTYPE_ACCESS:
    case AST::Expression_Type::BASETYPE_ACCESS:
    {
		bool is_base_access = expression->type == AST::Expression_Type::BASETYPE_ACCESS;
		AST::Expression* child_expr = is_base_access ? expression->options.basetype_access_expr : expression->options.subtype_access.expr;
        auto source = ir_generator_generate_expression(child_expr, destination);
        auto result_access = ir_data_access_create_non_destructive_cast(source, result_type);

        // If only pointers were cast, then we don't do any tag-check (Pointer could be null, not sure what we wanna do then)
        if (result_type->type == Datatype_Type::POINTER) {
            assert(source->datatype->type == Datatype_Type::POINTER, "");
            return result_access;
        }

        // Tag-check on downcast
        auto dst_type = result_type;
        Datatype_Struct* src_struct = downcast<Datatype_Struct>(source->datatype);
        Datatype_Struct* dst_struct = downcast<Datatype_Struct>(result_access->datatype);
        if (!is_base_access)
        {
            int child_tag_value = dst_struct->subtype_index + 1;

            IR_Data_Access* condition_access = ir_data_access_create_intermediate(upcast(types.bool_type));
            add_operation_instruction(
                IR_Operation::NOT_EQUAL,
                condition_access,
                ir_data_access_create_member(source, src_struct->tag_member),
                ir_data_access_create_constant(src_struct->tag_member.datatype, array_create_static_as_bytes<int>(&child_tag_value, 1))
            );

            IR_Instruction if_instr;
            if_instr.type = IR_Instruction_Type::IF;
            if_instr.options.if_instr.condition = condition_access;
            if_instr.options.if_instr.true_branch = ir_code_block_create();
            if_instr.options.if_instr.false_branch = ir_code_block_create();
            add_instruction(if_instr);

            IR_Instruction exit_instr;
            exit_instr.type = IR_Instruction_Type::RETURN;
            exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
            exit_instr.options.return_instr.options.exit_code = exit_code_make(
                Exit_Code_Type::CODE_ERROR, "Struct subtype downcast failed, tag value did not match downcast type");
            add_instruction(exit_instr, if_instr.options.if_instr.true_branch);
        }

        return result_access;
    }
    case AST::Expression_Type::MEMBER_ACCESS:
    {
        auto mem_access = expression->options.member_access;
        auto src_type = expression_info_get_datatype(get_info(mem_access.expr), false, type_system);

        // Handle special case of array.data, which basically becomes an address of, but has the type of element pointer
        auto source = ir_generator_generate_expression(mem_access.expr);
        if (src_type->type == Datatype_Type::ARRAY)
        {
            assert(mem_access.name == ids.data, "Member access on array must be data or handled elsewhere!");
            IR_Data_Access* result_access = ir_data_access_create_address_of(source);
            return move_access_to_destination(
                ir_data_access_create_address_of(ir_data_access_create_array_or_slice_access(
                    source, ir_data_access_create_constant_usize(0), false
                ))
            );
        }

        // Handle normal member access
        assert(info->specifics.member_access.type == Member_Access_Type::STRUCT_MEMBER_ACCESS, "");
        return move_access_to_destination(ir_data_access_create_member(source, info->specifics.member_access.options.member));
    }
    case AST::Expression_Type::POINTER_TYPE:
    case AST::Expression_Type::BAKE:
    case AST::Expression_Type::ERROR_EXPR:
    case AST::Expression_Type::FUNCTION_POINTER_TYPE:
    case AST::Expression_Type::LITERAL_READ:
    case AST::Expression_Type::AUTO_ENUM:
    case AST::Expression_Type::ARRAY_TYPE: {
        panic("A different path should have handled this before!");
    }
    default: panic("HEY");
    }

    panic("HEY");
    return nullptr;
}

IR_Data_Access* ir_generator_generate_expression(AST::Expression* expression, IR_Data_Access* destination);
IR_Data_Access* ir_generator_generate_expression_in_block(IR_Code_Block* code_block, AST::Expression* expression, IR_Data_Access* destination = nullptr)
{
    auto backup = ir_generator->current_block;
    ir_generator->current_block = code_block;
    IR_Data_Access* result = ir_generator_generate_expression(expression, destination);
    ir_generator->current_block = backup;
    return result;
}


IR_Data_Access* ir_generator_generate_expression(AST::Expression* expression, IR_Data_Access* destination)
{
    auto info = get_info(expression);
    auto& auto_cast_info = info->auto_cast_info;
    auto cast_type = auto_cast_info.type;
    auto& gen = *ir_generator;

    auto backup_expr = gen.current_expr;
    gen.current_expr = expression;
    SCOPE_EXIT(gen.current_expr = expression);

    // Early-exit if there is nothing to do
    if (auto_cast_info.type == Auto_Cast_Type::NO_OPERATION) {
        return ir_generator_generate_expression_no_cast(expression, destination);
    }

    // Apply cast
    IR_Data_Access* result = ir_generator_generate_expression_no_cast(expression, nullptr);
    return ir_generator_generate_cast(result, destination, auto_cast_info);
}

void ir_generator_work_through_defers(int defer_to_index, bool rewind_stack)
{
    auto& gen = *ir_generator;
    auto& defers = ir_generator->defer_stack;
    for (int i = defers.size - 1; i >= defer_to_index; i--)
    {
        auto& defer = defers[i];
        if (defer.is_block)
        {
            auto block = defer.options.block;
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::BLOCK;
            instr.options.block = ir_code_block_create();
            add_instruction(instr);
            ir_generator_generate_block(instr.options.block, block);
        }
        else
        {
            add_operation_instruction(IR_Operation::MOVE, defer.options.defer_restore.left_access, defer.options.defer_restore.restore_value);
        }
    }
    if (rewind_stack) {
        dynamic_array_rollback_to_size(&defers, defer_to_index);
    }
}

void ir_generator_generate_block_loop_increment(IR_Code_Block* ir_block, AST::Code_Block* loop_block)
{
    auto& predefined_constants = ir_generator->compilation_data->constant_pool->predefined;

    auto backup = ir_generator->current_block;
    ir_generator->current_block = ir_block;
    SCOPE_EXIT(ir_generator->current_block = backup);

    Loop_Increment* loop_increment = hashtable_find_element(&ir_generator->loop_increment_instructions, loop_block);
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
        add_operation_instruction(
            IR_Operation::ADDITION, foreach.index_access, foreach.index_access, ir_data_access_create_constant(predefined_constants.usize_one)
        );

        // Update pointer
        if (!foreach.is_custom_iterator)
        {
            add_operation_instruction(IR_Operation::MOVE,
                foreach.loop_variable_access,
                ir_data_access_create_address_of(
                    ir_data_access_create_array_or_slice_access(foreach.iterable_access, foreach.index_access, false)
                )
            );
        }
    }
}

void ir_generator_generate_statement(AST::Statement* statement, IR_Code_Block* ir_block)
{
    auto stat_info = get_info(statement);
    auto& gen = *ir_generator;
    auto& types = ir_generator->compilation_data->type_system->predefined_types;

    auto backup_block = gen.current_block;
    gen.current_block = ir_block;
    SCOPE_EXIT(gen.current_block = backup_block);

    auto backup_stat = gen.current_statement;
    gen.current_statement = statement;
    SCOPE_EXIT(gen.current_statement = backup_stat);

    switch (statement->type)
    {
    case AST::Statement_Type::DEFER:
    {
        Defer_Item item;
        item.is_block = true;
        item.options.block = statement->options.defer_block;
        dynamic_array_push_back(&ir_generator->defer_stack, item);
        break;
    }
    case AST::Statement_Type::DEFER_RESTORE:
    {
        auto& restore = statement->options.defer_restore;

        IR_Data_Access* left_access = ir_generator_generate_expression(restore.left_side);
        IR_Data_Access* copy_access = ir_data_access_create_intermediate(left_access->datatype);

        // Copy current value to temporary
        add_operation_instruction(IR_Operation::MOVE, copy_access, left_access);

        // Write assignment to value
        ir_generator_generate_expression(restore.right_side, left_access);

        Defer_Item defer_item;
        defer_item.is_block = false;
        defer_item.options.defer_restore.left_access = left_access;
        defer_item.options.defer_restore.restore_value = copy_access;
        dynamic_array_push_back(&ir_generator->defer_stack, defer_item);
        break;
    }
    case AST::Statement_Type::DEFINITION:
    {
        auto definition = statement->options.definition;
        if (definition->type != AST::Definition_Type::VARIABLE) {
            // Comptime definitions should be already handled
            return;
        }

        AST::Definition_Value* value_node = &definition->options.value;
        Symbol* variable_symbol = get_info(value_node->symbol);

        IR_Register var_reg;
        var_reg.has_definition_instruction = true;
        var_reg.name = optional_make_success(value_node->symbol->name);
        var_reg.type = variable_symbol->options.variable_type;
        dynamic_array_push_back(&ir_block->registers, var_reg);
        IR_Data_Access* variable_access = ir_data_access_create_register(ir_block->registers.size - 1);
        bool success = hashtable_insert_element(&ir_generator->variable_mapping, variable_symbol, variable_access);
        assert(success, "Variable symbols should not be encountered twice");

        IR_Instruction definition_instr;
        definition_instr.type = IR_Instruction_Type::VARIABLE_DEFINITION;
        definition_instr.options.variable_definition.symbol = variable_symbol;
        definition_instr.options.variable_definition.variable_access = variable_access;
        definition_instr.options.variable_definition.initial_value.available = false;

        if (value_node->value_expr.available) {
            definition_instr.options.variable_definition.initial_value = optional_make_success(
                ir_generator_generate_expression(value_node->value_expr.value, nullptr)
            );
        }
        add_instruction(definition_instr);

        break;
    }
    case AST::Statement_Type::BLOCK:
    {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::BLOCK;
        instr.options.block = ir_code_block_create(ir_block->function);
        add_instruction(instr);
        ir_generator_generate_block(instr.options.block, statement->options.block);

        int label_index = ir_generator_push_label_instruction();
        hashtable_insert_element(&ir_generator->labels_break, statement->options.block, label_index);
        break;
    }
    case AST::Statement_Type::IF_STATEMENT:
    {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::IF;
        instr.options.if_instr.condition = ir_generator_generate_expression(statement->options.if_statement.condition);
        instr.options.if_instr.true_branch = ir_code_block_create(ir_block->function);
        instr.options.if_instr.false_branch = ir_code_block_create(ir_block->function);
        add_instruction(instr);
        ir_generator_generate_block(instr.options.if_instr.true_branch, statement->options.if_statement.block);
        if (statement->options.if_statement.else_block.available) {
            ir_generator_generate_block(instr.options.if_instr.false_branch, statement->options.if_statement.else_block.value);
        }

        int label_index = ir_generator_push_label_instruction();
        bool valid = hashtable_insert_element(&ir_generator->labels_break, statement->options.if_statement.block, label_index);
        assert(valid, "");
        if (statement->options.if_statement.else_block.available) {
            valid = hashtable_insert_element(&ir_generator->labels_break, statement->options.if_statement.else_block.value, label_index);
            assert(valid, "");
        }
        break;
    }
    case AST::Statement_Type::SWITCH_STATEMENT:
    {
        auto& switch_info = get_info(statement)->specifics.switch_statement;
        IR_Data_Access* condition_access = ir_generator_generate_expression(statement->options.switch_statement.condition);

        IR_Instruction instr;
        instr.type = IR_Instruction_Type::MATCH;
        instr.options.switch_instr.condition_access = condition_access;
        instr.options.switch_instr.cases = dynamic_array_create<IR_Switch_Case>(statement->options.switch_statement.cases.size);

        // Check for subtype access
        auto cond_type = instr.options.switch_instr.condition_access->datatype;
        if (switch_info.structure != nullptr) {
            cond_type = switch_info.structure->tag_member.datatype;
            instr.options.switch_instr.condition_access = ir_data_access_create_member(condition_access, switch_info.structure->tag_member);
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
            new_case.block = ir_code_block_create();

            // Create case variable if available
            if (case_info->variable_symbol != 0) {
                // Generate pointer access to subtype
                IR_Data_Access* pointer_access = ir_data_access_create_address_of(condition_access);
                // new_case.block->registers[pointer_access.index] = case_info->variable_symbol->options.variable_type; // Update type to subtype
                hashtable_insert_element(&ir_generator->variable_mapping, get_info(switch_case->variable_definition.value), pointer_access);
            }

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
            exit_instr.options.return_instr.options.exit_code = exit_code_make(Exit_Code_Type::CODE_ERROR, "Switch did not contain the correct case");
            add_instruction(exit_instr, instr.options.switch_instr.default_block);
        }
        add_instruction(instr);

        int break_label_index = ir_generator_push_label_instruction();
        for (int i = 0; i < statement->options.switch_statement.cases.size; i++) {
            auto switch_case = statement->options.switch_statement.cases[i];
            hashtable_insert_element(&ir_generator->labels_break, switch_case->block, break_label_index);
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
            IR_Data_Access* access = ir_data_access_create_intermediate(symbol->options.variable_type);
            hashtable_insert_element(&ir_generator->variable_mapping, get_info(for_loop.loop_variable_definition), access);
            ir_generator_generate_expression(for_loop.initial_value, access);
        }

        // Register loop increment method
        {
            Loop_Increment increment;
            increment.type = Loop_Type::FOR_LOOP;
            increment.options.increment_statement = for_loop.increment_statement;
            hashtable_insert_element(&ir_generator->loop_increment_instructions, for_loop.body_block, increment);
        }

        // Push Loop + continue/break labels
        int continue_label_index = ir_generator_push_label_instruction();
        hashtable_insert_element(&ir_generator->labels_continue, for_loop.body_block, continue_label_index);


        IR_Instruction instr;
        instr.type = IR_Instruction_Type::WHILE;
        instr.options.while_instr.condition_code = ir_code_block_create();
        instr.options.while_instr.code = ir_code_block_create();
        instr.options.while_instr.condition_access = ir_generator_generate_expression_in_block(instr.options.while_instr.condition_code, for_loop.condition);
        add_instruction(instr);
        ir_generator_generate_block(instr.options.while_instr.code, for_loop.body_block);

        // Push increment instruction at end of while block
        ir_generator_generate_statement(for_loop.increment_statement, instr.options.while_instr.code);

        // Push break label
        int break_label_index = ir_generator_push_label_instruction();
        hashtable_insert_element(&ir_generator->labels_break, for_loop.body_block, break_label_index);

        break;
    }
    case AST::Statement_Type::FOREACH_LOOP:
    {
        auto& foreach_loop = statement->options.foreach_loop;
        auto& loop_info = get_info(statement)->specifics.foreach_loop;
        auto& overload = loop_info.overload;
        bool is_overload = overload.custom_op != nullptr;

        // Create and initialize index data-access (Always available)
        IR_Data_Access* index_access = ir_data_access_create_intermediate(upcast(types.usize));
        {
            // Initialize
            add_operation_instruction(IR_Operation::MOVE, index_access, ir_data_access_create_constant_usize(0));

            if (foreach_loop.index_variable_definition.available) {
                assert(loop_info.index_variable_symbol != 0 && loop_info.index_variable_symbol->type == Symbol_Type::VARIABLE, "");
                hashtable_insert_element(&ir_generator->variable_mapping, get_info(foreach_loop.index_variable_definition.value), index_access);
            }
        }

        // Create iterable access
        IR_Data_Access* iterable_access = ir_generator_generate_expression(foreach_loop.expression);
        auto iterable_type = iterable_access->datatype;

        // Create and initialize loop variable
        Datatype* iterator_type = nullptr;
        IR_Data_Access* iterator_access = nullptr; // Only valid for custom iterators
        IR_Data_Access* loop_variable_access = ir_data_access_create_intermediate(upcast(loop_info.loop_variable_symbol->options.variable_type));
        {
            hashtable_insert_element(&ir_generator->variable_mapping, get_info(foreach_loop.loop_variable_definition), loop_variable_access);

            // Initialize
            if (is_overload) 
            {
                iterator_type = overload.instance_functions[0]->signature->return_type().value;
                iterator_access = ir_data_access_create_intermediate(iterator_type);
                IR_Instruction iter_create_instr;
                iter_create_instr.type = IR_Instruction_Type::FUNCTION_CALL;
                iter_create_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
                iter_create_instr.options.call.destination = iterator_access;
                iter_create_instr.options.call.options.function = overload.instance_functions[0];
                iter_create_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
                if (overload.custom_op->parameters[0].by_reference) {
                    dynamic_array_push_back(&iter_create_instr.options.call.arguments, ir_data_access_create_address_of(iterable_access));
                }
                else {
                    dynamic_array_push_back(&iter_create_instr.options.call.arguments, iterable_access);
                }
                add_instruction(iter_create_instr);
                assert(iter_create_instr.options.call.options.function != nullptr, "");
            }
            else
            {
                iterable_type = iterable_type;
                assert(iterable_type->type == Datatype_Type::ARRAY || iterable_type->type == Datatype_Type::SLICE, "Other types not supported currently");
                add_operation_instruction(
                    IR_Operation::MOVE,
                    loop_variable_access,
                    ir_data_access_create_address_of(
                        ir_data_access_create_array_or_slice_access(iterable_access, index_access, false)
                    )
                );
            }
        }

        // Register loop increment method
        {
            Loop_Increment increment;
            increment.type = Loop_Type::FOREACH_LOOP;
            increment.options.foreach_loop.index_access = index_access;
            increment.options.foreach_loop.iterable_access = iterable_access;
            increment.options.foreach_loop.loop_variable_access = loop_variable_access;
            increment.options.foreach_loop.is_custom_iterator   = is_overload;
            hashtable_insert_element(&ir_generator->loop_increment_instructions, foreach_loop.body_block, increment);
        }

        // Push loop
        {
            // Push loop_start label
            {
                int continue_label_index = ir_generator_push_label_instruction();
                hashtable_insert_element(&ir_generator->labels_continue, foreach_loop.body_block, continue_label_index);
            }

            // Push loop
            {
                // Create condition code
                IR_Data_Access* condition_access = ir_data_access_create_intermediate(upcast(types.bool_type));
                IR_Code_Block* condition_code = ir_code_block_create();
                if (is_overload)
                {
                    IR_Instruction next_call_instr;
                    next_call_instr.type = IR_Instruction_Type::FUNCTION_CALL;
                    next_call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
                    next_call_instr.options.call.destination = loop_variable_access;
                    next_call_instr.options.call.options.function = overload.instance_functions[1];
                    next_call_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
                    dynamic_array_push_back(&next_call_instr.options.call.arguments, ir_data_access_create_address_of(iterator_access));
                    add_instruction(next_call_instr, condition_code);

                    void* empty = nullptr;
                    add_operation_instruction(
                        IR_Operation::NOT_EQUAL,
                        condition_access,
                        loop_variable_access,
                        ir_data_access_create_constant(
                            loop_variable_access->datatype, array_create_static_as_bytes<void*>(&empty, 1)
                        )
                    );
                }
                else
                {
                    IR_Data_Access* array_size_access;
                    if (iterable_type->type == Datatype_Type::ARRAY) {
                        auto array_type = downcast<Datatype_Array>(iterable_type);
                        assert(array_type->count_known, "");
                        array_size_access = ir_data_access_create_constant_usize(array_type->element_count);
                    }
                    else if (iterable_type->type == Datatype_Type::SLICE) {
                        auto slice_type = downcast<Datatype_Slice>(iterable_type);
                        array_size_access = ir_data_access_create_member(iterable_access, slice_type->size_member);
                    }

                    add_operation_instruction(
                        IR_Operation::LESS, condition_access, index_access, array_size_access
                    );
                }

                // Push loop + create body code
                IR_Instruction instr;
                instr.type = IR_Instruction_Type::WHILE;
                instr.options.while_instr.condition_code = condition_code;
                instr.options.while_instr.condition_access = condition_access;
                instr.options.while_instr.code = ir_code_block_create(ir_block->function);

                // Create get_value call for custom operators
                auto backup = ir_generator->current_block;
                ir_generator->current_block = instr.options.while_instr.code;

                ir_generator_generate_block(instr.options.while_instr.code, foreach_loop.body_block);

                ir_generator->current_block = backup;
                add_instruction(instr);

                // Push increment instruction at end of while block
                ir_generator_generate_block_loop_increment(instr.options.while_instr.code, foreach_loop.body_block);
            }

            // Push break label
            {
                int break_label_index = ir_generator_push_label_instruction();
                hashtable_insert_element(&ir_generator->labels_break, foreach_loop.body_block, break_label_index);
            }
        }
        break;
    }
    case AST::Statement_Type::WHILE_STATEMENT:
    {
        int continue_label_index = ir_generator_push_label_instruction();
        hashtable_insert_element(&ir_generator->labels_continue, statement->options.while_statement.block, continue_label_index);

        IR_Instruction instr;
        instr.type = IR_Instruction_Type::WHILE;
        instr.options.while_instr.condition_code = ir_code_block_create(ir_block->function);
        instr.options.while_instr.code = ir_code_block_create(ir_block->function);
        if (statement->options.while_statement.condition.available) {
            instr.options.while_instr.condition_access = ir_generator_generate_expression_in_block(
                instr.options.while_instr.condition_code, statement->options.while_statement.condition.value
            );
        }
        else {
            instr.options.while_instr.condition_access = ir_data_access_create_constant_bool(true);
        }
        ir_generator_generate_block(instr.options.while_instr.code, statement->options.while_statement.block);
        add_instruction(instr);

        int break_label_index = ir_generator_push_label_instruction();
        hashtable_insert_element(&ir_generator->labels_break, statement->options.while_statement.block, break_label_index);

        break;
    }
    case AST::Statement_Type::BREAK_STATEMENT:
    case AST::Statement_Type::CONTINUE_STATEMENT:
    {
        bool is_continue = statement->type == AST::Statement_Type::CONTINUE_STATEMENT;
        auto goto_block = stat_info->specifics.block;
        ir_generator_work_through_defers(*hashtable_find_element(&ir_generator->block_defer_depths, goto_block), false);

        // Push loop increment instructions if they are available
        if (is_continue) {
            ir_generator_generate_block_loop_increment(ir_block, goto_block);
        }

        IR_Instruction instr;
        instr.type = IR_Instruction_Type::GOTO;
        add_instruction(instr);

        Unresolved_Goto fill_out;
        fill_out.block = ir_block;
        fill_out.instruction_index = ir_generator->current_block->instructions.size - 1;
        fill_out.break_block = goto_block;
        dynamic_array_push_back(is_continue ? &ir_generator->fill_out_continues : &ir_generator->fill_out_breaks, fill_out);
        break;
    }
    case AST::Statement_Type::RETURN_STATEMENT:
    {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::RETURN;
        if (statement->options.return_value.available) {
            instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_DATA;
            instr.options.return_instr.options.return_value = ir_generator_generate_expression(statement->options.return_value.value);
            if (ir_generator->defer_stack.size != 0) 
            {
                // Copy the generated expression to another location, so defers cannot interfere
                auto copy_access = ir_data_access_create_intermediate(instr.options.return_instr.options.return_value->datatype);
                add_operation_instruction(IR_Operation::MOVE,
                    copy_access,
                    instr.options.return_instr.options.return_value
                );
                instr.options.return_instr.options.return_value = copy_access;
            }
        }
        else {
            instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_EMPTY;
        }

        ir_generator_work_through_defers(0, false);
        add_instruction(instr);
        break;
    }
    case AST::Statement_Type::EXPRESSION_STATEMENT: {
        ir_generator_generate_expression(statement->options.expression);
        break;
    }
    case AST::Statement_Type::BINOP_ASSIGNMENT:
    {
        auto info = get_info(statement);
        auto& assign = statement->options.binop_assignment;

        IR_Data_Access* left_access = ir_generator_generate_expression(assign.left_side);
        Datatype* type = left_access->datatype;
        IR_Data_Access* right_access = ir_generator_generate_expression(assign.right_side);

        // Handle overloads
        auto& overload = info->specifics.overload;
        if (overload.instance_functions[0] != nullptr)
        {
            IR_Instruction call;
            call.type = IR_Instruction_Type::FUNCTION_CALL;
            call.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            call.options.call.destination = left_access;
            call.options.call.options.function = info->specifics.overload.instance_functions[0];
            call.options.call.arguments = dynamic_array_create<IR_Data_Access*>(2);
            dynamic_array_push_back(&call.options.call.arguments, left_access);
            dynamic_array_push_back(&call.options.call.arguments, right_access);
            add_instruction(call);
            break;
        }

        add_operation_instruction(ast_binop_to_ir_operation(assign.binop), left_access, left_access, right_access);
        break;
    }
    case AST::Statement_Type::ASSIGNMENT:
    {
        auto& assignment = statement->options.assignment;
        IR_Data_Access* left_access = ir_generator_generate_expression(assignment.left_side);
        ir_generator_generate_expression(assignment.right_side, left_access);
        break;
    }
    default: panic("Statment type invalid!");
    }
}

void ir_generator_generate_block(IR_Code_Block* ir_block, AST::Code_Block* ast_block)
{
    auto& gen = *ir_generator;

    int defer_start_index = gen.defer_stack.size;
    hashtable_insert_element(&gen.block_defer_depths, ast_block, defer_start_index);

    // Generate code
    auto backup = gen.current_block;
    gen.current_block = ir_block;
    for (int i = 0; i < ast_block->statements.size; i++) {
        ir_generator_generate_statement(ast_block->statements[i], ir_block);
    }

    ir_generator_work_through_defers(defer_start_index, true);
    gen.current_block = backup;
}

void ir_generator_generate_function(Upp_Function* function, Compilation_Data* compilation_data)
{
    Timing_Task before_task = compilation_data->task_current;
    SCOPE_EXIT(compilation_data_switch_timing_task(compilation_data, before_task));
    compilation_data_switch_timing_task(compilation_data, Timing_Task::CODE_GEN);

    ir_generator = compilation_data->ir_generator;
    ir_generator->current_block = nullptr;
    ir_generator->current_expr = nullptr;
    ir_generator->current_pass = nullptr;
    ir_generator->current_statement = nullptr;

    if (function->ir_block != nullptr) { // Function already generated
        return;
    }
    if (function->contains_errors || function->is_extern) {
        return;
    }
    if (!function->body_node.available) {
        return;
    }
    if (function->poly_type == Poly_Type::BASE || function->poly_type == Poly_Type::PARTIAL) {
        return;
    }

    // Generate function code
    function->ir_block = ir_code_block_create(function);
    ir_generator->current_pass = function->body_pass;
    Function_Body& body = function->body_node.value;
    if (body.is_expression)
    {
        IR_Instruction return_instr;
        return_instr.type = IR_Instruction_Type::RETURN;
        return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_DATA;
        return_instr.options.return_instr.options.return_value = 
            ir_generator_generate_expression_in_block(function->ir_block, body.expr);
        add_instruction(return_instr, function->ir_block);
    }
    else {
        ir_generator_generate_block(function->ir_block, body.block);
    }

    // Add empty return
    if (!function->signature->return_type().available) {
        IR_Instruction return_instr;
        return_instr.type = IR_Instruction_Type::RETURN;
        return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_EMPTY;
        add_instruction(return_instr, function->ir_block);
    }

    // Fill out breaks and continues
    for (int j = 0; j < ir_generator->fill_out_breaks.size; j++) {
        Unresolved_Goto fill_out = ir_generator->fill_out_breaks[j];
        int label_index = *hashtable_find_element(&ir_generator->labels_break, fill_out.break_block);
        assert(fill_out.block->instructions[fill_out.instruction_index].type == IR_Instruction_Type::GOTO, "");
        fill_out.block->instructions[fill_out.instruction_index].options.label_index = label_index;
    }
    for (int j = 0; j < ir_generator->fill_out_continues.size; j++) {
        Unresolved_Goto fill_out = ir_generator->fill_out_continues[j];
        int label_index = *hashtable_find_element(&ir_generator->labels_continue, fill_out.break_block);
        assert(fill_out.block->instructions[fill_out.instruction_index].type == IR_Instruction_Type::GOTO, "");
        fill_out.block->instructions[fill_out.instruction_index].options.label_index = label_index;
    }
    dynamic_array_reset(&ir_generator->fill_out_breaks);
    dynamic_array_reset(&ir_generator->fill_out_continues);
    hashtable_reset(&ir_generator->variable_mapping);
    hashtable_reset(&ir_generator->labels_break);
    hashtable_reset(&ir_generator->labels_continue);
    hashtable_reset(&ir_generator->block_defer_depths);
    hashtable_reset(&ir_generator->loop_increment_instructions);
}

void ir_generator_finish(Compilation_Data* compilation_data)
{
    Type_System& type_system = *compilation_data->type_system;
    auto& types = type_system.predefined_types;
    ir_generator = compilation_data->ir_generator;

    // Generate entry function
    {
        auto entry_function = upp_function_create_empty(
            compilation_data->empty_call_signature,
            identifier_pool_add(&compilation_data->identifier_pool, string_create_static("__upp_entry_function_")), compilation_data
        );
        entry_function->ir_block = ir_code_block_create(entry_function);
        compilation_data->entry_function = entry_function;
        ir_generator->current_block = entry_function->ir_block;

        // Initialize all globals
        auto& globals = compilation_data->globals;
        for (int i = 0; i < globals.size; i++)
        {
            auto global = globals[i];
            if (!global->has_initial_value) continue;

            ir_generator->current_pass = global->definition_workload->analysis_pass;
            add_operation_instruction(IR_Operation::MOVE, ir_data_access_create_global(global), ir_generator_generate_expression(global->init_expr));
            ir_generator->current_pass = nullptr;
        }

        // Call main
        {
            IR_Instruction call_instr;
            call_instr.type = IR_Instruction_Type::FUNCTION_CALL;
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            call_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
            call_instr.options.call.options.function = compilation_data->main_function;
            add_instruction(call_instr);
            assert(call_instr.options.call.options.function != nullptr, "");
        }

        // Exit
        {
            IR_Instruction exit_instr;
            exit_instr.type = IR_Instruction_Type::RETURN;
            exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
            exit_instr.options.return_instr.options.exit_code = exit_code_make(Exit_Code_Type::SUCCESS);
            add_instruction(exit_instr);
        }
    }
}

IR_Generator* ir_generator_create(Compilation_Data* compilation_data)
{
    auto& type_system = compilation_data->type_system;
    auto& types = type_system->predefined_types;

    IR_Generator* generator = new IR_Generator;
    generator->compilation_data = compilation_data;
    generator->nothing_access.datatype = upcast(types.unknown_type);
    generator->nothing_access.type = IR_Data_Access_Type::NOTHING;

    // Create datastructures
    {
        generator->data_accesses = dynamic_array_create<IR_Data_Access*>();
        generator->loop_increment_instructions = hashtable_create_pointer_empty<AST::Code_Block*, Loop_Increment>(8);
        generator->variable_mapping = hashtable_create_pointer_empty<Symbol*, IR_Data_Access*>(8);
        generator->labels_break = hashtable_create_pointer_empty<AST::Code_Block*, int>(8);
        generator->labels_continue = hashtable_create_pointer_empty<AST::Code_Block*, int>(8);
        generator->block_defer_depths = hashtable_create_pointer_empty<AST::Code_Block*, int>(8);

        generator->defer_stack = dynamic_array_create<Defer_Item>();
        generator->fill_out_breaks = dynamic_array_create<Unresolved_Goto>();
        generator->fill_out_continues = dynamic_array_create<Unresolved_Goto>();
        generator->label_positions = dynamic_array_create<IR_Instruction_Reference>();
    }

    return generator;
}

void ir_data_access_delete(IR_Data_Access** access) {
    delete (*access);
}

void ir_generator_destroy(IR_Generator* generator)
{
    hashtable_destroy(&generator->variable_mapping);
    hashtable_destroy(&generator->labels_break);
    hashtable_destroy(&generator->labels_continue);
    hashtable_destroy(&generator->block_defer_depths);
    hashtable_destroy(&generator->loop_increment_instructions);

    dynamic_array_for_each(generator->data_accesses, ir_data_access_delete);
    dynamic_array_destroy(&generator->data_accesses);
    dynamic_array_destroy(&generator->defer_stack);
    dynamic_array_destroy(&generator->fill_out_breaks);
    dynamic_array_destroy(&generator->fill_out_continues);
    dynamic_array_destroy(&generator->label_positions);

    delete generator;
}

IR_Operation ast_binop_to_ir_operation(AST::Binop binop)
{
    switch (binop)
    {
    case AST::Binop::ADDITION: return IR_Operation::ADDITION;
    case AST::Binop::SUBTRACTION: return IR_Operation::SUBTRACTION;
    case AST::Binop::DIVISION: return IR_Operation::DIVISION;
    case AST::Binop::MULTIPLICATION: return IR_Operation::MULTIPLICATION;
    case AST::Binop::MODULO: return IR_Operation::MODULO;
    case AST::Binop::AND: return IR_Operation::AND;
    case AST::Binop::OR: return IR_Operation::OR;
    case AST::Binop::EQUAL: return IR_Operation::EQUAL;
    case AST::Binop::NOT_EQUAL: return IR_Operation::NOT_EQUAL;
    case AST::Binop::LESS: return IR_Operation::LESS;
    case AST::Binop::LESS_OR_EQUAL: return IR_Operation::LESS_OR_EQUAL;
    case AST::Binop::GREATER: return IR_Operation::GREATER;
    case AST::Binop::GREATER_OR_EQUAL: return IR_Operation::GREATER_OR_EQUAL;
    case AST::Binop::POINTER_EQUAL: return IR_Operation::EQUAL;
    case AST::Binop::POINTER_NOT_EQUAL: return IR_Operation::NOT_EQUAL;
    case AST::Binop::INVALID: panic("Shouldn't happen"); return IR_Operation::ADDITION;
    default: panic("");
    }

    return IR_Operation::ADDITION;
}

const char* ir_operation_as_string(IR_Operation operation)
{
    switch (operation)
    {
    case IR_Operation::MOVE: return "MOVE";
    case IR_Operation::PRIMITIVE_CAST: return "PRIMITIVE_CAST";
    case IR_Operation::ADDITION: return "ADDITION";
    case IR_Operation::SUBTRACTION: return "SUBTRACTION";
    case IR_Operation::DIVISION: return "DIVISION";
    case IR_Operation::MULTIPLICATION: return "MULTIPLICATION";
    case IR_Operation::MODULO: return "MODULO";
    case IR_Operation::NEGATE: return "NEGATE";
    case IR_Operation::EQUAL: return "EQUAL";
    case IR_Operation::NOT_EQUAL: return "NOT_EQUAL";
    case IR_Operation::LESS: return "LESS";
    case IR_Operation::LESS_OR_EQUAL: return "LESS_OR_EQUAL";
    case IR_Operation::GREATER: return "GREATER";
    case IR_Operation::GREATER_OR_EQUAL: return "GREATER_OR_EQUAL";
    case IR_Operation::AND: return "AND";
    case IR_Operation::OR: return "OR";
    case IR_Operation::NOT: return "NOT";
    case IR_Operation::BITWISE_NOT: return "BITWISE_NOT";
    case IR_Operation::BITWISE_AND: return "BITWISE_AND";
    case IR_Operation::BITWISE_OR: return "BITWISE_OR";
    case IR_Operation::BITWISE_XOR: return "BITWISE_XOR";
    case IR_Operation::BITWISE_SHIFT_LEFT: return "BITWISE_SHIFT_LEFT";
    case IR_Operation::BITWISE_SHIFT_RIGHT: return "BITWISE_SHIFT_RIGHT";
	case IR_Operation::HIGHEST_SET_BIT: return "HIGHEST_SET_BIT";
	case IR_Operation::LOWEST_SET_BIT: return "LOWEST_SET_BIT";
	case IR_Operation::FLOAT_ABS: return "FLOAT_ABS";
	case IR_Operation::FLOAT_MODULO: return "FLOAT_MODULO";
	case IR_Operation::FLOAT_REMAINDER: return "FLOAT_REMAINDER";
	case IR_Operation::ROUND_UP: return "ROUND_UP";     
	case IR_Operation::ROUND_DOWN: return "ROUND_DOWN";   
	case IR_Operation::ROUND_TOWARDS_ZERO: return "ROUND_TOWARDS_ZERO";
	case IR_Operation::ROUND_NEAREST: return "ROUND_NEAREST";     
	case IR_Operation::EXP: return "EXP";
	case IR_Operation::LN: return "LN";
	case IR_Operation::LOG10: return "LOG10";
	case IR_Operation::LOG2: return "LOG2";
	case IR_Operation::POW: return "POW";
	case IR_Operation::SQUARE_ROOT: return "SQUARE_ROOT";
	case IR_Operation::CUBE_ROOT: return "CUBE_ROOT";
	case IR_Operation::SIN: return "SIN";
	case IR_Operation::COS: return "COS";
	case IR_Operation::TAN: return "TAN";
	case IR_Operation::ASIN: return "ASIN";
	case IR_Operation::ACOS: return "ACOS";
	case IR_Operation::ATAN: return "ATAN";
	case IR_Operation::ATAN2: return "ATAN2";
	case IR_Operation::SINH: return "SINH";
	case IR_Operation::COSH: return "COSH";
	case IR_Operation::TANH: return "TANH";
	case IR_Operation::ASINH: return "ASINH";
	case IR_Operation::ACOSH: return "ACOSH";
	case IR_Operation::ATANH: return "ATANH";
	case IR_Operation::IS_NAN: return "IS_NAN";
	case IR_Operation::IS_FINITE: return "IS_FINITE";
	case IR_Operation::IS_INFINITE: return "IS_INFINITE";
    default: panic("");
    }

    return "";
}

int ir_operation_parameter_count(IR_Operation operation)
{
    switch (operation)
    {
    case IR_Operation::ADDITION:
    case IR_Operation::SUBTRACTION:
    case IR_Operation::DIVISION:
    case IR_Operation::MULTIPLICATION:
    case IR_Operation::MODULO:
    case IR_Operation::EQUAL:
    case IR_Operation::NOT_EQUAL:
    case IR_Operation::LESS:
    case IR_Operation::LESS_OR_EQUAL:
    case IR_Operation::GREATER:
    case IR_Operation::GREATER_OR_EQUAL: 
    case IR_Operation::AND: 
    case IR_Operation::OR: 
    case IR_Operation::BITWISE_AND: 
    case IR_Operation::BITWISE_OR: 
    case IR_Operation::BITWISE_XOR:
    case IR_Operation::BITWISE_SHIFT_LEFT: 
    case IR_Operation::BITWISE_SHIFT_RIGHT: 
	case IR_Operation::FLOAT_MODULO: 
	case IR_Operation::FLOAT_REMAINDER: 
	case IR_Operation::POW: 
	case IR_Operation::ATAN2: 
        return 2;
    }
    return 1;
}

const char* ir_builtin_fn_as_string(IR_Builtin_Function fn)
{
    switch (fn)
    {
    case IR_Builtin_Function::TYPE_INFO: return "TYPE_INFO";
    case IR_Builtin_Function::MEMORY_COPY: return "MEMORY_COPY";
    case IR_Builtin_Function::MEMORY_COPY_NO_OVERLAP: return "MEMORY_COPY_NO_OVERLAP";
    case IR_Builtin_Function::MEMORY_ZERO: return "MEMORY_ZERO";
    case IR_Builtin_Function::MEMORY_COMPARE: return "MEMORY_COMPARE";
    case IR_Builtin_Function::SYSTEM_ALLOC: return "SYSTEM_ALLOC";
    case IR_Builtin_Function::SYSTEM_FREE: return "SYSTEM_FREE";
    case IR_Builtin_Function::PRINT_I32: return "PRINT_I32";
    case IR_Builtin_Function::PRINT_F32: return "PRINT_F32";
    case IR_Builtin_Function::PRINT_BOOL: return "PRINT_BOOL";
    case IR_Builtin_Function::PRINT_LINE: return "PRINT_LINE";
    case IR_Builtin_Function::PRINT_STRING: return "PRINT_STRING";
    case IR_Builtin_Function::READ_I32: return "READ_I32";
    case IR_Builtin_Function::READ_F32: return "READ_F32";
    case IR_Builtin_Function::READ_BOOL: return "READ_BOOL";
    default: panic("");
    }
    return "";
}

Hardcoded_Type ir_builtin_fn_to_hardcoded_type(IR_Builtin_Function fn)
{
    switch (fn)
    {
    case IR_Builtin_Function::TYPE_INFO: return Hardcoded_Type::TYPE_INFO;
    case IR_Builtin_Function::MEMORY_COPY: return Hardcoded_Type::MEMORY_COPY;
    case IR_Builtin_Function::MEMORY_COPY_NO_OVERLAP: return Hardcoded_Type::MEMORY_COPY_NO_OVERLAP;
    case IR_Builtin_Function::MEMORY_ZERO: return Hardcoded_Type::MEMORY_ZERO;
    case IR_Builtin_Function::MEMORY_COMPARE: return Hardcoded_Type::MEMORY_COMPARE;
    case IR_Builtin_Function::SYSTEM_ALLOC: return Hardcoded_Type::SYSTEM_ALLOC;
    case IR_Builtin_Function::SYSTEM_FREE: return Hardcoded_Type::SYSTEM_FREE;
    case IR_Builtin_Function::PRINT_I32: return Hardcoded_Type::PRINT_I32;
    case IR_Builtin_Function::PRINT_F32: return Hardcoded_Type::PRINT_F32;
    case IR_Builtin_Function::PRINT_BOOL: return Hardcoded_Type::PRINT_BOOL;
    case IR_Builtin_Function::PRINT_LINE: return Hardcoded_Type::PRINT_LINE;
    case IR_Builtin_Function::PRINT_STRING: return Hardcoded_Type::PRINT_STRING;
    case IR_Builtin_Function::READ_I32: return Hardcoded_Type::READ_I32;
    case IR_Builtin_Function::READ_F32: return Hardcoded_Type::READ_F32;
    case IR_Builtin_Function::READ_BOOL: return Hardcoded_Type::READ_BOOL;
    default: panic("");
    }
    return (Hardcoded_Type)-1;
}
