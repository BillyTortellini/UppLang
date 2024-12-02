#include "ir_code.hpp"
#include "compiler.hpp"
#include "bytecode_generator.hpp"
#include "ast.hpp"
#include "symbol_table.hpp"
#include "editor_analysis_info.hpp"

void ir_generator_generate_block(IR_Code_Block* ir_block, AST::Code_Block* ast_block);
IR_Data_Access* ir_generator_generate_expression(IR_Code_Block* ir_block, AST::Expression* expression, IR_Data_Access* destination = 0);
void ir_generator_generate_statement(AST::Statement* statement, IR_Code_Block* ir_block);
IR_Data_Access* ir_data_access_create_constant_u64(u64 value);



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
    case IR_Instruction_Type::FUNCTION_ADDRESS:
    case IR_Instruction_Type::UNARY_OP:
    case IR_Instruction_Type::BINARY_OP:
    case IR_Instruction_Type::LABEL:
    case IR_Instruction_Type::GOTO:
    case IR_Instruction_Type::VARIABLE_DEFINITION:
        break;
    default: panic("Lul");
    }
}

IR_Code_Block* ir_code_block_create(IR_Function* function)
{
    IR_Code_Block* block = new IR_Code_Block();
    block->function = function;
    block->instructions = dynamic_array_create<IR_Instruction>();
    block->registers = dynamic_array_create<IR_Register>();
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

IR_Function* ir_function_create(Datatype_Function* signature, int slot_index = -1)
{
    IR_Function* function = new IR_Function();
    function->code = ir_code_block_create(function);
    function->function_type = signature;
    function->program = ir_generator.program;
    dynamic_array_push_back(&ir_generator.program->functions, function);

    auto& slots = compiler.analysis_data->function_slots;
    if (slot_index == -1) {
        Function_Slot slot;
        slot.modtree_function = nullptr;
        slot.ir_function = nullptr;
        slot.index = slots.size;
        dynamic_array_push_back(&slots, slot);
        slot_index = slot.index;
    }
    function->function_slot_index = slot_index;

    auto& slot = slots[slot_index];
    slot.ir_function = function;
    dynamic_array_push_back(&ir_generator.queued_function_slot_indices, slot_index);
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
    auto type_system = &compiler.analysis_data->type_system;
    switch (access->type)
    {
    case IR_Data_Access_Type::CONSTANT: {
        auto const_index = access->option.constant_index;
        Upp_Constant* constant = &compiler.analysis_data->constant_pool.constants[const_index];
        string_append_formated(string, "Constant #%d ", const_index);
        datatype_append_to_string(string, type_system, constant->type);
        string_append_formated(string, " ");
        datatype_append_value_to_string(constant->type, type_system, constant->memory, string);
        break;
    }
    case IR_Data_Access_Type::GLOBAL_DATA: {
        string_append_formated(string, "Global #%d, type: ", access->option.global_index);
        datatype_append_to_string(string, type_system, access->datatype);
        break;
    }
    case IR_Data_Access_Type::PARAMETER: {
        auto& param_info = access->option.parameter;
        auto& param = param_info.function->function_type->parameters[param_info.index];
        string_append_formated(string, "Param #%s, type: ", param.name->characters);
        datatype_append_to_string(string, type_system, param.type);
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
        datatype_append_to_string(string, type_system, reg.type);
        if (reg_access.definition_block != current_block) {
            string_append_formated(string, " (Non local)");
        }
        break;
    }
    case IR_Data_Access_Type::ADDRESS_OF_VALUE: {
        string_append(string, "Addr-Of: ");
        ir_data_access_append_to_string(access->option.address_of_value, string, current_block);
        break;
    }
    case IR_Data_Access_Type::ARRAY_ELEMENT_ACCESS: {
        string_append(string, "Array_Access: ");
        ir_data_access_append_to_string(access->option.array_access.array_access, string, current_block);
        string_append(string, ", Index_Access: ");
        ir_data_access_append_to_string(access->option.array_access.index_access, string, current_block);
        break;
    }
    case IR_Data_Access_Type::POINTER_DEREFERENCE: {
        string_append(string, "Dereference: ");
        ir_data_access_append_to_string(access->option.pointer_value, string, current_block);
        break;
    }
    case IR_Data_Access_Type::MEMBER_ACCESS: {
        string_append_formated(string, "Member \"%s\" of: ", access->option.member_access.member.id->characters);
        ir_data_access_append_to_string(access->option.member_access.struct_access, string, current_block);
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

void ir_code_block_append_to_string(IR_Code_Block* code_block, String* string, int indentation);
void ir_instruction_append_to_string(IR_Instruction* instruction, String* string, int indentation, IR_Code_Block* code_block)
{
    indent_string(string, indentation);
    switch (instruction->type)
    {
    case IR_Instruction_Type::FUNCTION_ADDRESS:
    {
        IR_Instruction_Function_Address* function_address = &instruction->options.function_address;
        const char* name = "predefined_function";
        auto& modtree = compiler.analysis_data->function_slots[function_address->function_slot_index].modtree_function;
        if (modtree != nullptr) {
            name = modtree->name->characters;
        }

        string_append_formated(string, "FUNCTION_ADDRESS of %s\n", name);
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(function_address->destination, string, code_block);
        break;
    }
    case IR_Instruction_Type::BINARY_OP:
    {
        string_append_formated(string, "BINARY_OP ");
        switch (instruction->options.binary_op.type)
        {
        case IR_Binop::ADDITION: string_append_formated(string, "ADDITION"); break;
        case IR_Binop::SUBTRACTION: string_append_formated(string, "SUBTRACTION"); break;
        case IR_Binop::DIVISION: string_append_formated(string, "DIVISION"); break;
        case IR_Binop::MULTIPLICATION: string_append_formated(string, "MULTIPLICATION"); break;
        case IR_Binop::MODULO: string_append_formated(string, "MODULO"); break;
        case IR_Binop::AND: string_append_formated(string, "AND"); break;
        case IR_Binop::OR: string_append_formated(string, "OR"); break;
        case IR_Binop::BITWISE_AND: string_append_formated(string, "BITWISE_AND"); break;
        case IR_Binop::BITWISE_OR: string_append_formated(string, "BITWISE_OR"); break;
        case IR_Binop::BITWISE_XOR: string_append_formated(string, "BITWISE_XOR"); break;
        case IR_Binop::BITWISE_SHIFT_LEFT: string_append_formated(string, "BITWISE_SHIFT_LEFT"); break;
        case IR_Binop::BITWISE_SHIFT_RIGHT: string_append_formated(string, "BITWISE_SHIFT_RIGHT"); break;
        case IR_Binop::EQUAL: string_append_formated(string, "EQUAL"); break;
        case IR_Binop::NOT_EQUAL: string_append_formated(string, "NOT_EQUAL"); break;
        case IR_Binop::LESS: string_append_formated(string, "LESS"); break;
        case IR_Binop::LESS_OR_EQUAL: string_append_formated(string, "LESS_OR_EQUAL"); break;
        case IR_Binop::GREATER: string_append_formated(string, "GREATER"); break;
        case IR_Binop::GREATER_OR_EQUAL: string_append_formated(string, "GREATER_OR_EQUAL"); break;
        default: panic("");
        }

        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "left: ");
        ir_data_access_append_to_string(instruction->options.binary_op.operand_left, string, code_block);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "right: ");
        ir_data_access_append_to_string(instruction->options.binary_op.operand_right, string, code_block);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(instruction->options.binary_op.destination, string, code_block);
        break;
    }
    case IR_Instruction_Type::BLOCK: {
        string_append_formated(string, "BLOCK\n");
        ir_code_block_append_to_string(instruction->options.block, string, indentation + 1);
        break;
    }
    case IR_Instruction_Type::VARIABLE_DEFINITION: {
        string_append_formated(string, "VARIABLE_DEFINITION %s", instruction->options.variable_definition.symbol->id->characters);
        if (instruction->options.variable_definition.initial_value.available) {
            string_append(string, ", value: ");
            ir_data_access_append_to_string(instruction->options.variable_definition.initial_value.value, string, code_block);
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
        ir_data_access_append_to_string(cast->source, string, code_block);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(cast->destination, string, code_block);
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
            function_sig = call->options.function->signature;
            break;
        case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL: {
            auto type = datatype_get_non_const_type(call->options.pointer_access->datatype);
            assert(type->type == Datatype_Type::FUNCTION, "Function pointer call must be of function type!");
            function_sig = downcast<Datatype_Function>(type);
            break;
        }
        case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
            function_sig = 0;
            break;
        default:
            panic("Hey");
            return;
        }
        if (function_sig != 0) {
            if (function_sig->return_type.available) {
                string_append_formated(string, "dst: ");
                ir_data_access_append_to_string(call->destination, string, code_block);
                string_append_formated(string, "\n");
                indent_string(string, indentation + 1);
            }
        }
        string_append_formated(string, "args: (%d)\n", call->arguments.size);
        for (int i = 0; i < call->arguments.size; i++) {
            indent_string(string, indentation + 2);
            ir_data_access_append_to_string(call->arguments[i], string, code_block);
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
            ir_data_access_append_to_string(call->options.pointer_access, string, code_block);
            break;
        case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
            string_append_formated(string, "HARDCODED_FUNCTION_CALL, type: ");
            hardcoded_type_append_to_string(string, call->options.hardcoded);
            break;
        }
        break;
    }
    case IR_Instruction_Type::IF: {
        string_append_formated(string, "IF ");
        ir_data_access_append_to_string(instruction->options.if_instr.condition, string, code_block);
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
        ir_data_access_append_to_string(instruction->options.move.source, string, code_block);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(instruction->options.move.destination, string, code_block);
        break;
    }
    case IR_Instruction_Type::SWITCH: 
    {
        Datatype* value_type = instruction->options.switch_instr.condition_access->datatype;
        Datatype_Enum* enum_type = 0;
        if (value_type->base_type->type == Datatype_Type::STRUCT) {
            auto structure = downcast<Datatype_Struct>(value_type->base_type);
            enum_type = downcast<Datatype_Enum>(type_mods_get_subtype(structure, value_type->mods)->tag_member.type);
        }
        else
        {
            assert(value_type->type == Datatype_Type::ENUM, "If not union, this must be an enum");
            enum_type = downcast<Datatype_Enum>(value_type);
        }
        string_append_formated(string, "SWITCH\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "Condition access: ");
        ir_data_access_append_to_string(instruction->options.switch_instr.condition_access, string, code_block);
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
        ir_data_access_append_to_string(instruction->options.while_instr.condition_access, string, code_block);
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
            ir_data_access_append_to_string(return_instr->options.return_value, string, code_block);
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
        case IR_Unop::NEGATE:
            string_append_formated(string, "NEGATE");
            break;
        case IR_Unop::NOT:
            string_append_formated(string, "NOT");
            break;
        }

        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "dst: ");
        ir_data_access_append_to_string(instruction->options.unary_op.destination, string, code_block);
        string_append_formated(string, "\n");
        indent_string(string, indentation + 1);
        string_append_formated(string, "operand: ");
        ir_data_access_append_to_string(instruction->options.unary_op.source, string, code_block);
        break;
    }
    default: panic("What");
    }
}

void ir_code_block_append_to_string(IR_Code_Block* code_block, String* string, int indentation)
{
    auto type_system = &compiler.analysis_data->type_system;

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
        datatype_append_to_string(string, type_system, reg.type);
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
    auto type_system = &compiler.analysis_data->type_system;

    indent_string(string, indentation);
    string_append_formated(string, "Function-Type:");
    datatype_append_to_string(string, type_system, upcast(function->function_type));
    string_append_formated(string, "\n");
    ir_code_block_append_to_string(function->code, string, indentation);
}

void ir_program_append_to_string(IR_Program* program, String* string, bool print_generated_functions)
{
    string_append_formated(string, "Program Dump:\n-----------------\n");
    for (int i = 0; i < program->functions.size; i++)
    {
        auto function = program->functions[i];
        const auto& slot = compiler.analysis_data->function_slots[function->function_slot_index];
        if (slot.modtree_function == nullptr && !print_generated_functions) {
            continue;
        }

        string_append_formated(string, "Function #%d ", i);
        ir_function_append_to_string(program->functions[i], string, 0);
        string_append_formated(string, "\n");
    }
}



IR_Data_Access* ir_data_access_create_nothing() {
    return &ir_generator.nothing_access;
}

IR_Data_Access* ir_data_access_create_global(ModTree_Global* global)
{
    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = global->type;
    access->type = IR_Data_Access_Type::GLOBAL_DATA;
    access->option.global_index = global->index;
    dynamic_array_push_back(&ir_generator.data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_parameter(IR_Function* function, int parameter_index)
{
    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = function->function_type->parameters[parameter_index].type;
    access->type = IR_Data_Access_Type::PARAMETER;
    access->option.parameter.function = function;
    access->option.parameter.index = parameter_index;
    dynamic_array_push_back(&ir_generator.data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_register(IR_Code_Block* block, int register_index)
{
    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = block->registers[register_index].type;
    access->type = IR_Data_Access_Type::REGISTER;
    access->option.register_access.definition_block = block;
    access->option.register_access.index = register_index;
    dynamic_array_push_back(&ir_generator.data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_intermediate(IR_Code_Block* block, Datatype* signature)
{
    assert(block != 0, "");
    assert(!datatype_is_unknown(signature), "Cannot have register with unknown type");
    assert(!type_size_is_unfinished(signature), "Cannot have register with 0 size!");

    // Note: I don't think there is ever the need to have constant intermediates...
    // This is here to make the C-Generator work 
    signature = datatype_get_non_const_type(signature);

    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = signature;
    access->type = IR_Data_Access_Type::REGISTER;
    access->option.register_access.definition_block = block;
    access->option.register_access.index = block->registers.size;
    dynamic_array_push_back(&ir_generator.data_accesses, access);

    IR_Register reg;
    reg.type = signature;
    reg.name.available = false;
    reg.has_definition_instruction = false;
    dynamic_array_push_back(&block->registers, reg);

    return access;
}

IR_Data_Access* ir_data_access_create_dereference(IR_Data_Access* pointer_access)
{
    // Shortcut for pointer accesses of address-of, e.g. *(&x) == xp
    if (pointer_access->type == IR_Data_Access_Type::ADDRESS_OF_VALUE) {
        return pointer_access->option.address_of_value;
    }

    Datatype* ptr_type = datatype_get_non_const_type(pointer_access->datatype);
    assert(ptr_type->type == Datatype_Type::POINTER, "");

    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = downcast<Datatype_Pointer>(ptr_type)->element_type;
    access->type = IR_Data_Access_Type::POINTER_DEREFERENCE;
    access->option.pointer_value = pointer_access;
    dynamic_array_push_back(&ir_generator.data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_address_of(IR_Data_Access* value_access)
{
    // Shortcut for address_of pointer dereference, e.g. &(*xp) == xp
    if (value_access->type == IR_Data_Access_Type::POINTER_DEREFERENCE) {
        return value_access->option.pointer_value;
    }

    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = upcast(type_system_make_pointer(value_access->datatype));
    access->type = IR_Data_Access_Type::ADDRESS_OF_VALUE;
    access->option.address_of_value = value_access;
    dynamic_array_push_back(&ir_generator.data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_member(IR_Data_Access* struct_access, Struct_Member member)
{
    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = member.type;
    access->type = IR_Data_Access_Type::MEMBER_ACCESS;
    access->option.member_access.struct_access = struct_access;
    access->option.member_access.member = member;
    dynamic_array_push_back(&ir_generator.data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_array_or_slice_access(IR_Data_Access* array_access, IR_Data_Access* index_access, bool do_bounds_check, IR_Code_Block* ir_block)
{
    auto& types = compiler.analysis_data->type_system.predefined_types;

    Datatype* array_type = datatype_get_non_const_type(array_access->datatype);
    Datatype* element_type = 0;
    if (array_type->type == Datatype_Type::ARRAY) {
        element_type = downcast<Datatype_Array>(array_type)->element_type;
        if (array_access->datatype->mods.is_constant) {
            element_type = type_system_make_constant(element_type);
        }
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
            size_access = ir_data_access_create_constant_u64(arr->element_count);
        }

        assert(ir_block != 0, "");
        IR_Data_Access* condition_access = ir_data_access_create_intermediate(ir_block, upcast(types.bool_type));
        IR_Instruction cmp_instr;
        cmp_instr.type = IR_Instruction_Type::BINARY_OP;
        cmp_instr.options.binary_op.destination = condition_access;
        cmp_instr.options.binary_op.operand_left = index_access;
        cmp_instr.options.binary_op.operand_right = size_access;
        cmp_instr.options.binary_op.type = IR_Binop::GREATER_OR_EQUAL;
        dynamic_array_push_back(&ir_block->instructions, cmp_instr);

        IR_Instruction if_instr;
        if_instr.type = IR_Instruction_Type::IF;
        if_instr.options.if_instr.condition = condition_access;
        if_instr.options.if_instr.true_branch = ir_code_block_create(ir_block->function);
        if_instr.options.if_instr.false_branch = ir_code_block_create(ir_block->function);

        IR_Instruction exit_instr;
        exit_instr.type = IR_Instruction_Type::RETURN;
        exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
        exit_instr.options.return_instr.options.exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Array out of bounds access");

        dynamic_array_push_back(&if_instr.options.if_instr.true_branch->instructions, exit_instr);
        dynamic_array_push_back(&ir_block->instructions, if_instr);
    }

    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = element_type;
    access->type = IR_Data_Access_Type::ARRAY_ELEMENT_ACCESS;
    access->option.array_access.array_access = array_access;
    access->option.array_access.index_access = index_access;
    dynamic_array_push_back(&ir_generator.data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_constant(Datatype* signature, Array<byte> bytes)
{
    auto result = constant_pool_add_constant(signature, bytes);
    assert(result.success, "Must always work");

    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = result.options.constant.type;
    access->type = IR_Data_Access_Type::CONSTANT;
    access->option.constant_index = result.options.constant.constant_index;
    dynamic_array_push_back(&ir_generator.data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_constant(Upp_Constant constant)
{
    IR_Data_Access* access = new IR_Data_Access;
    access->datatype = constant.type;
    access->type = IR_Data_Access_Type::CONSTANT;
    access->option.constant_index = constant.constant_index;
    dynamic_array_push_back(&ir_generator.data_accesses, access);
    return access;
}

IR_Data_Access* ir_data_access_create_constant_i32(i32 value) {
    return ir_data_access_create_constant(
        upcast(compiler.analysis_data->type_system.predefined_types.i32_type),
        array_create_static((byte*)&value, sizeof(i32))
    );
}

IR_Data_Access* ir_data_access_create_constant_u64(u64 value) {
    return ir_data_access_create_constant(
        upcast(compiler.analysis_data->type_system.predefined_types.u64_type),
        array_create_static_as_bytes(&value, 1)
    );
}

IR_Data_Access* ir_data_access_create_constant_bool(bool value) {
    return ir_data_access_create_constant(upcast(compiler.analysis_data->type_system.predefined_types.bool_type), array_create_static_as_bytes(&value, 1));
}




// Code Gen
Expression_Info* get_info(AST::Expression* node) {
    return pass_get_node_info(ir_generator.current_pass, node, Info_Query::READ_NOT_NULL);
}

Statement_Info* get_info(AST::Statement* node) {
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

Parameter_Matching_Info* get_info(AST::Arguments* node) {
    return pass_get_node_info(ir_generator.current_pass, node, Info_Query::READ_NOT_NULL);
}

void generate_member_initalizers(IR_Code_Block* ir_block, IR_Data_Access* struct_access, AST::Arguments* arguments)
{
    auto param_infos = get_info(arguments);
    assert(param_infos->call_type == Call_Type::STRUCT_INITIALIZER || param_infos->call_type == Call_Type::UNION_INITIALIZER, "");
    auto& init_info = param_infos->options.struct_init;

    for (int i = 0; i < param_infos->matched_parameters.size; i++)
    {
        auto& param_info = param_infos->matched_parameters[i];
        if (!param_info.is_set) continue;
        assert(param_info.expression != 0 && param_info.argument_index >= 0, "");
        auto& member = init_info.content->members[i];

        IR_Instruction move_instr;
        move_instr.type = IR_Instruction_Type::MOVE;
        move_instr.options.move.destination = ir_data_access_create_member(struct_access, member);
        move_instr.options.move.source = ir_generator_generate_expression(ir_block, param_info.expression);
        dynamic_array_push_back(&ir_block->instructions, move_instr);
    }
    for (int i = 0; i < arguments->subtype_initializers.size; i++) {
        auto initializer = arguments->subtype_initializers[i];
        generate_member_initalizers(ir_block, struct_access, initializer->arguments);
    }
}

IR_Data_Access* ir_generator_generate_expression_no_cast(IR_Code_Block* ir_block, AST::Expression* expression, IR_Data_Access* destination = 0)
{
    auto info = get_info(expression);
    auto result_type = expression_info_get_type(info, true);
    auto type_system = &compiler.analysis_data->type_system;
    auto& types = type_system->predefined_types;
    assert(info->is_valid, "Cannot contain errors!"); 

    auto move_access_to_destination = [&](IR_Data_Access* access) -> IR_Data_Access* {
        if (destination == 0) {
            destination = access;
            return access;
        }
        IR_Instruction move;
        move.type = IR_Instruction_Type::MOVE;
        move.options.move.destination = destination;
        move.options.move.source = access;
        dynamic_array_push_back(&ir_block->instructions, move);
        return destination;
    };
    auto make_destination_access_on_demand = [&](Datatype* result_type) -> IR_Data_Access* {
        if (destination == 0) {
            destination = ir_data_access_create_intermediate(ir_block, result_type);
        }
        return destination;
    };

    // Handle different expression results
    switch (info->result_type)
    {
    case Expression_Result_Type::CONSTANT: 
        return move_access_to_destination(ir_data_access_create_constant(info->options.constant));
    case Expression_Result_Type::TYPE:
        return move_access_to_destination(ir_data_access_create_constant(upcast(types.type_handle), array_create_static_as_bytes(&result_type->type_handle, 1)));
    case Expression_Result_Type::FUNCTION: {
        // Function pointer read
        IR_Instruction load_instr;
        load_instr.type = IR_Instruction_Type::FUNCTION_ADDRESS;
        load_instr.options.function_address.function_slot_index = info->options.function->function_slot_index;
        load_instr.options.function_address.destination = make_destination_access_on_demand(result_type);
        dynamic_array_push_back(&ir_block->instructions, load_instr);
        return destination;
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

    auto value_type = info->cast_info.initial_type;
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
            instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(2);
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
            instr.options.call.destination = make_destination_access_on_demand(value_type);
            instr.options.call.options.function = info->specifics.overload.function;
            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.call.destination;
        }

        IR_Instruction instr;
        instr.type = IR_Instruction_Type::BINARY_OP;
        instr.options.binary_op.type = ast_binop_to_ir_binop(binop.type);
        instr.options.binary_op.operand_left = ir_generator_generate_expression(ir_block, binop.left);
        instr.options.binary_op.operand_right = ir_generator_generate_expression(ir_block, binop.right);
        instr.options.binary_op.destination = make_destination_access_on_demand(value_type);
        dynamic_array_push_back(&ir_block->instructions, instr);
        return instr.options.binary_op.destination;
    }
    case AST::Expression_Type::UNARY_OPERATION:
    {
        auto& unop = expression->options.unop;
        IR_Data_Access* access = ir_generator_generate_expression(ir_block, expression->options.unop.expr);

        if (info->specifics.overload.function != 0) {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::FUNCTION_CALL;
            instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
            dynamic_array_push_back(&instr.options.call.arguments, access);
            instr.options.call.destination = make_destination_access_on_demand(value_type);
            instr.options.call.options.function = info->specifics.overload.function;
            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.call.destination;
        }

        switch (expression->options.unop.type)
        {
        case AST::Unop::POINTER: {
            return move_access_to_destination(ir_data_access_create_address_of(access));
        }
        case AST::Unop::DEREFERENCE: {
            return move_access_to_destination(ir_data_access_create_dereference(access));
        }
        case AST::Unop::NOT: {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::UNARY_OP;
            instr.options.unary_op.destination = make_destination_access_on_demand(result_type);
            instr.options.unary_op.type = IR_Unop::NOT;
            instr.options.unary_op.source = access;
            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.unary_op.destination;
        }
        case AST::Unop::NEGATE: {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::UNARY_OP;
            instr.options.unary_op.destination = make_destination_access_on_demand(result_type);
            instr.options.unary_op.type = IR_Unop::NEGATE;
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
            call_instr.options.call.options.function = call_info->options.function;
            signature = call_info->options.function->signature;
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
                if_instr.options.if_instr.condition = ir_generator_generate_expression(ir_block, call.arguments->arguments[0]->value);
                if_instr.options.if_instr.true_branch = ir_code_block_create(ir_block->function);
                if_instr.options.if_instr.false_branch = ir_code_block_create(ir_block->function);

                IR_Instruction exit_instr;
                exit_instr.type = IR_Instruction_Type::RETURN;
                exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
                exit_instr.options.return_instr.options.exit_code = exit_code_make(Exit_Code_Type::CODE_ERROR, "Assertion failed");

                dynamic_array_push_back(&if_instr.options.if_instr.false_branch->instructions, exit_instr);
                dynamic_array_push_back(&ir_block->instructions, if_instr);

                call_instr.options.call.destination = ir_data_access_create_nothing();
                return call_instr.options.call.destination;
            }
            case Hardcoded_Type::REALLOCATE:
            {
                auto param_mapping = get_info(call.arguments);

                IR_Data_Access* slice_ptr_access = ir_generator_generate_expression(ir_block, param_mapping->matched_parameters[0].expression);
                IR_Data_Access* new_size_access = ir_generator_generate_expression(ir_block, param_mapping->matched_parameters[1].expression);

                assert(slice_ptr_access->datatype->mods.pointer_level == 1 && slice_ptr_access->datatype->base_type->type == Datatype_Type::SLICE, "");
                Datatype_Slice* slice = downcast<Datatype_Slice>(slice_ptr_access->datatype->base_type);

                IR_Data_Access* slice_size_access = ir_data_access_create_member(ir_data_access_create_dereference(slice_ptr_access), slice->size_member);
                IR_Data_Access* slice_data_access = ir_data_access_create_member(ir_data_access_create_dereference(slice_ptr_access), slice->data_member);

                IR_Data_Access* new_byte_size_access;
                IR_Data_Access* old_byte_size_access;
                if (slice->element_type->memory_info.value.size == 1) {
                    new_byte_size_access = new_size_access;
                    old_byte_size_access = slice_size_access;
                }
                else {
                    old_byte_size_access = ir_data_access_create_intermediate(ir_block, upcast(types.u64_type));
                    new_byte_size_access = ir_data_access_create_intermediate(ir_block, upcast(types.u64_type));

                    IR_Instruction binop;
                    binop.type = IR_Instruction_Type::BINARY_OP;
                    binop.options.binary_op.type = IR_Binop::MULTIPLICATION;
                    binop.options.binary_op.operand_left = new_size_access;
                    binop.options.binary_op.operand_right = ir_data_access_create_constant_u64(slice->element_type->memory_info.value.size);
                    binop.options.binary_op.destination = new_byte_size_access;
                    dynamic_array_push_back(&ir_block->instructions, binop);

                    binop.options.binary_op.operand_left = slice_size_access;
                    binop.options.binary_op.operand_right = ir_data_access_create_constant_u64(slice->element_type->memory_info.value.size);
                    binop.options.binary_op.destination = old_byte_size_access;
                    dynamic_array_push_back(&ir_block->instructions, binop);
                }

                // Call reallocate function from memory allocator
                IR_Data_Access* realloc_fn_access = ir_data_access_create_global(compiler.semantic_analyser->global_allocator);
                realloc_fn_access = ir_data_access_create_dereference(realloc_fn_access);
                realloc_fn_access = ir_data_access_create_member(realloc_fn_access, types.allocator->content.members[2]);

                IR_Data_Access* reallocate_result_ptr = ir_data_access_create_intermediate(ir_block, types.byte_pointer_optional);

                IR_Instruction alloc_instr;
                alloc_instr.type = IR_Instruction_Type::FUNCTION_CALL;
                alloc_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_POINTER_CALL;
                alloc_instr.options.call.options.pointer_access = realloc_fn_access;
                alloc_instr.options.call.destination = reallocate_result_ptr;
                alloc_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(4);
                dynamic_array_push_back(&alloc_instr.options.call.arguments, ir_data_access_create_global(compiler.semantic_analyser->global_allocator));
                dynamic_array_push_back(&alloc_instr.options.call.arguments, slice_data_access);
                dynamic_array_push_back(&alloc_instr.options.call.arguments, old_byte_size_access);
                dynamic_array_push_back(&alloc_instr.options.call.arguments, new_byte_size_access);
                dynamic_array_push_back(&ir_block->instructions, alloc_instr);

                // Set new pointer
                IR_Instruction cast_ptr_instr;
                cast_ptr_instr.type = IR_Instruction_Type::CAST;
                cast_ptr_instr.options.cast.destination = slice_data_access;
                cast_ptr_instr.options.cast.source = reallocate_result_ptr;
                cast_ptr_instr.options.cast.type = IR_Cast_Type::POINTERS;
                dynamic_array_push_back(&ir_block->instructions, cast_ptr_instr);

                // Set new size
                IR_Instruction move_size_instr;
                move_size_instr.type = IR_Instruction_Type::MOVE;
                move_size_instr.options.move.destination = slice_size_access;
                move_size_instr.options.move.source = new_size_access;
                dynamic_array_push_back(&ir_block->instructions, move_size_instr);

                return ir_data_access_create_nothing();
            }
            case Hardcoded_Type::PANIC_FN:
            {
                IR_Instruction exit_instr;
                exit_instr.type = IR_Instruction_Type::RETURN;
                exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
                exit_instr.options.return_instr.options.exit_code = exit_code_make(Exit_Code_Type::CODE_ERROR, "Panic called");
                dynamic_array_push_back(&ir_block->instructions, exit_instr);
                return ir_data_access_create_nothing();
            }
            case Hardcoded_Type::TYPE_OF: {
                panic("Should be handled in semantic analyser");
                break;
            }
            case Hardcoded_Type::BITWISE_NOT: 
            {
                auto matching = get_info(call.arguments);
                IR_Instruction unop;
                unop.type = IR_Instruction_Type::UNARY_OP;
                unop.options.unary_op.type = IR_Unop::BITWISE_NOT;
                unop.options.unary_op.source = ir_generator_generate_expression(ir_block, matching->matched_parameters[0].expression);
                unop.options.unary_op.destination = make_destination_access_on_demand(result_type);
                dynamic_array_push_back(&ir_block->instructions, unop);
                return unop.options.unary_op.destination;
            }
            case Hardcoded_Type::BITWISE_AND: 
            case Hardcoded_Type::BITWISE_OR: 
            case Hardcoded_Type::BITWISE_XOR: 
            case Hardcoded_Type::BITWISE_SHIFT_LEFT: 
            case Hardcoded_Type::BITWISE_SHIFT_RIGHT: 
            {
                auto matching = get_info(call.arguments);

                IR_Binop binop_type;
                switch (hardcoded)
                {
                case Hardcoded_Type::BITWISE_AND: binop_type = IR_Binop::BITWISE_AND; break;
                case Hardcoded_Type::BITWISE_OR: binop_type = IR_Binop::BITWISE_OR; break;
                case Hardcoded_Type::BITWISE_XOR: binop_type = IR_Binop::BITWISE_XOR; break;
                case Hardcoded_Type::BITWISE_SHIFT_LEFT: binop_type = IR_Binop::BITWISE_SHIFT_LEFT; break;
                case Hardcoded_Type::BITWISE_SHIFT_RIGHT: binop_type = IR_Binop::BITWISE_SHIFT_RIGHT; break;
                default: panic("");
                }

                IR_Instruction binop;
                binop.type = IR_Instruction_Type::BINARY_OP;
                binop.options.binary_op.type = binop_type;
                binop.options.binary_op.operand_left = ir_generator_generate_expression(ir_block, matching->matched_parameters[0].expression);
                binop.options.binary_op.operand_right = ir_generator_generate_expression(ir_block, matching->matched_parameters[1].expression);
                binop.options.binary_op.destination = make_destination_access_on_demand(result_type);
                dynamic_array_push_back(&ir_block->instructions, binop);
                return binop.options.binary_op.destination;
            }
            }
            call_instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
            call_instr.options.call.options.hardcoded = hardcoded;
            signature = hardcoded_type_to_signature(hardcoded);
            break;
        }
        case Expression_Result_Type::POLYMORPHIC_FUNCTION: 
        {
            auto function = call_info->options.polymorphic_function.instance_fn;
            assert(function != nullptr, "Must be instanciated at ir_code");
            assert(function->is_runnable, "Instances that reach ir-generator must be runnable!");
            call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            call_instr.options.call.options.function = function;
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
                call_instr.options.call.options.function = function;
                signature = function->signature;
            }
            else
            {
                call_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
                call_instr.options.call.options.function = dot_call.options.function;
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
            call_instr.options.call.destination = make_destination_access_on_demand(signature->return_type.value);
        }
        else {
            call_instr.options.call.destination = ir_data_access_create_nothing();
        }

        // Generate arguments 
        auto function_signature = info->specifics.function_call_signature;
        auto param_mapping = get_info(call.arguments);
        int next_mapping_index = 0;
        call_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(signature->parameters.size);

        bool is_dotcall = param_mapping->call_type == Call_Type::DOT_CALL || param_mapping->call_type == Call_Type::POLYMORPHIC_DOT_CALL;
        if (is_dotcall) {
            auto first_arg = call.expr;
            assert(first_arg->type == AST::Expression_Type::MEMBER_ACCESS, "Dot call must have a member access!");
            next_mapping_index += 1;
            IR_Data_Access* argument_access = ir_generator_generate_expression(ir_block, first_arg->options.member_access.expr);
            dynamic_array_push_back(&call_instr.options.call.arguments, argument_access);
        }
        
        for (int i = is_dotcall ? 1 : 0; i < function_signature->parameters.size; i++) 
        {
            auto& param = function_signature->parameters[i];
            Parameter_Match* match = &param_mapping->matched_parameters[next_mapping_index];
            if (match->ignore_during_code_generation) {
                while (match->ignore_during_code_generation) {
                    next_mapping_index += 1;
                    match = &param_mapping->matched_parameters[next_mapping_index];
                }
            }
            else {
                next_mapping_index += 1;
            }

            IR_Data_Access* argument_access;
            if (match->is_set) {
                argument_access = ir_generator_generate_expression(ir_block, match->expression);
            }
            else {
                assert(param.default_value_exists, "");
                assert(param.value_expr != 0 && param.value_pass != 0, "Must be, otherwise we shouldn't get to this point");
                RESTORE_ON_SCOPE_EXIT(ir_generator.current_pass, param.value_pass);
                argument_access = ir_generator_generate_expression(ir_block, param.value_expr);
            }
            dynamic_array_push_back(&call_instr.options.call.arguments, argument_access);
        }

        dynamic_array_push_back(&ir_block->instructions, call_instr);
        return call_instr.options.call.destination;
    }
    case AST::Expression_Type::PATH_LOOKUP:
    {
        auto symbol = get_info(expression->options.path_lookup);
        IR_Data_Access* result_access;
        switch (symbol->type)
        {
        case Symbol_Type::GLOBAL: {
            result_access = ir_data_access_create_global(symbol->options.global);
            break;
        }
        case Symbol_Type::VARIABLE: {
            result_access = *hashtable_find_element(&ir_generator.variable_mapping, AST::downcast<AST::Definition_Symbol>(symbol->definition_node));
            break;
        }
        case Symbol_Type::PARAMETER: {
            result_access = ir_data_access_create_parameter(ir_block->function, symbol->options.parameter.index_in_non_polymorphic_signature);
            break;
        }
        default: panic("Other Symbol-cases must be handled by analyser or in this function above!");
        }
        return move_access_to_destination(result_access);
    }
    case AST::Expression_Type::STRUCT_INITIALIZER:
    {
        IR_Data_Access* struct_access = make_destination_access_on_demand(result_type);
        auto& init_info = get_info(expression->options.struct_initializer.arguments)->options.struct_init;

        // First, set all tags to correct values
        Datatype_Struct* structure = downcast<Datatype_Struct>(result_type->base_type);
        {
            Struct_Content* content = &structure->content;
            Subtype_Index* subtype = result_type->mods.subtype_index;
            for (int i = 0; i < subtype->indices.size; i++) 
            {
                int tag_value = subtype->indices[i].index + 1;
                assert(content->subtypes.size > 0, "");
                
                IR_Instruction move_instr;
                move_instr.type = IR_Instruction_Type::MOVE;
                move_instr.options.move.destination = ir_data_access_create_member(struct_access, content->tag_member);
                move_instr.options.move.source = 
                    ir_data_access_create_constant(content->tag_member.type, array_create_static_as_bytes<int>(&tag_value, 1));
                dynamic_array_push_back(&ir_block->instructions, move_instr);

                content = content->subtypes[tag_value - 1];
            }
        }

        // Generate initializers for members
        generate_member_initalizers(ir_block, struct_access, expression->options.struct_initializer.arguments);
        return struct_access;
    }
    case AST::Expression_Type::ARRAY_INITIALIZER:
    {
        auto& array_init = expression->options.array_initializer;
        IR_Data_Access* array_access = make_destination_access_on_demand(result_type);
        auto array_type = datatype_get_non_const_type(result_type);
        if (array_type->type == Datatype_Type::SLICE)
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
            move.options.move.source = ir_data_access_create_constant(slice_constant.options.constant);
            dynamic_array_push_back(&ir_block->instructions, move);
        }
        else
        {
            assert(array_type->type == Datatype_Type::ARRAY, "");
            auto array = downcast<Datatype_Array>(array_type);
            assert(array->element_count == array_init.values.size, "");
            for (int i = 0; i < array_init.values.size; i++)
            {
                auto init_expr = array_init.values[i];

                IR_Data_Access* element_access = ir_data_access_create_array_or_slice_access(array_access, ir_data_access_create_constant_u64(i), false, ir_block);

                IR_Instruction move_instr;
                move_instr.type = IR_Instruction_Type::MOVE;
                move_instr.options.move.destination = element_access;
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
            instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(2);
            dynamic_array_push_back(&instr.options.call.arguments, ir_generator_generate_expression(ir_block, access.array_expr));
            dynamic_array_push_back(&instr.options.call.arguments, ir_generator_generate_expression(ir_block, access.index_expr));
            instr.options.call.destination = make_destination_access_on_demand(value_type);
            instr.options.call.options.function = info->specifics.overload.function;
            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.call.destination;
        }

        return move_access_to_destination(ir_data_access_create_array_or_slice_access(
            ir_generator_generate_expression(ir_block, expression->options.array_access.array_expr),
            ir_generator_generate_expression(ir_block, expression->options.array_access.index_expr),
            true, ir_block
        ));
    }
    case AST::Expression_Type::MEMBER_ACCESS:
    {
        auto mem_access = expression->options.member_access;
        auto src_type = get_info(mem_access.expr)->cast_info.result_type;

        // Handle custom member accesses
        if (info->specifics.member_access.type == Member_Access_Type::DOT_CALL_AS_MEMBER) 
        {
            auto source = ir_generator_generate_expression(ir_block, mem_access.expr);

            IR_Instruction instr;
            instr.type = IR_Instruction_Type::FUNCTION_CALL;
            instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            instr.options.call.options.function = info->specifics.member_access.options.dot_call_function;
            instr.options.call.destination = make_destination_access_on_demand(result_type);
            instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
            dynamic_array_push_back(&instr.options.call.arguments, source);

            dynamic_array_push_back(&ir_block->instructions, instr);
            return instr.options.call.destination;
        }
        else if (info->specifics.member_access.type == Member_Access_Type::STRUCT_UP_OR_DOWNCAST) 
        {
            auto source = ir_generator_generate_expression(ir_block, mem_access.expr, destination);

            // Tag-check on downcast
            auto dst_type = info->cast_info.result_type;
            assert(types_are_equal(src_type->base_type, dst_type->base_type), "");
            assert(src_type->base_type->type == Datatype_Type::STRUCT && dst_type->base_type->type == Datatype_Type::STRUCT, "");
            if (dst_type->mods.subtype_index->indices.size > src_type->mods.subtype_index->indices.size) 
            {
                assert(dst_type->mods.subtype_index->indices.size == src_type->mods.subtype_index->indices.size + 1, "Downcast must only be a single level");
                Struct_Content* base_content = type_mods_get_subtype(downcast<Datatype_Struct>(src_type->base_type), src_type->mods);
                int child_tag_value = dst_type->mods.subtype_index->indices[dst_type->mods.subtype_index->indices.size-1].index + 1; // Tag value == index + 1

                IR_Data_Access* condition_access = ir_data_access_create_intermediate(ir_block, upcast(types.bool_type));
                IR_Instruction condition_instr;
                condition_instr.type = IR_Instruction_Type::BINARY_OP;
                condition_instr.options.binary_op.destination = condition_access;
                condition_instr.options.binary_op.operand_left = ir_data_access_create_member(source, base_content->tag_member);
                condition_instr.options.binary_op.operand_right = 
                    ir_data_access_create_constant(base_content->tag_member.type, array_create_static_as_bytes<int>(&child_tag_value, 1));
                condition_instr.options.binary_op.type = IR_Binop::NOT_EQUAL;
                dynamic_array_push_back(&ir_block->instructions, condition_instr);

                IR_Instruction if_instr;
                if_instr.type = IR_Instruction_Type::IF;
                if_instr.options.if_instr.condition = condition_access;
                if_instr.options.if_instr.true_branch = ir_code_block_create(ir_block->function);
                if_instr.options.if_instr.false_branch = ir_code_block_create(ir_block->function);

                IR_Instruction exit_instr;
                exit_instr.type = IR_Instruction_Type::RETURN;
                exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
                exit_instr.options.return_instr.options.exit_code = exit_code_make(Exit_Code_Type::CODE_ERROR, "Struct subtype downcast failed, tag value did not match downcast type");

                dynamic_array_push_back(&if_instr.options.if_instr.true_branch->instructions, exit_instr);
                dynamic_array_push_back(&ir_block->instructions, if_instr);
            }

            return source;
        }
        else if (info->specifics.member_access.type == Member_Access_Type::OPTIONAL_PTR_ACCESS) {
            auto deref_count = info->specifics.member_access.options.optional_deref_count;
            if (deref_count == 0) {
                return ir_generator_generate_expression(ir_block, mem_access.expr, destination);
            }
            IR_Data_Access* deref = ir_generator_generate_expression(ir_block, mem_access.expr, destination);
            for (int i = 0; i < deref_count; i++) {
                deref = ir_data_access_create_dereference(deref);
            }
            return move_access_to_destination(deref);
        }

        // Handle special case of array.data, which basically becomes an address of, but has the type of element pointer
        auto source = ir_generator_generate_expression(ir_block, mem_access.expr);
        if (src_type->base_type->type == Datatype_Type::ARRAY)
        {
            assert(mem_access.name == compiler.identifier_pool.predefined_ids.data, "Member access on array must be data or handled elsewhere!");
            IR_Data_Access* result_access = ir_data_access_create_address_of(source);
            return move_access_to_destination(
                ir_data_access_create_address_of(ir_data_access_create_array_or_slice_access(
                    source, ir_data_access_create_constant_u64(0), false, ir_block
                ))
            );
        }

        // Handle normal member access
        assert(info->specifics.member_access.type == Member_Access_Type::STRUCT_MEMBER_ACCESS, "");
        return move_access_to_destination(ir_data_access_create_member(source, info->specifics.member_access.options.member));
    }
    case AST::Expression_Type::NEW_EXPR:
    {
        auto& new_expr = expression->options.new_expr;
        auto type_info = get_info(new_expr.type_expr);
        assert(type_info->result_type == Expression_Result_Type::TYPE, "Hey");
        assert(type_info->options.type->memory_info.available, "");
        auto& mem = type_info->options.type->memory_info.value;

        auto allocate_from_global_allocator = [](IR_Code_Block* ir_block, IR_Data_Access* size, IR_Data_Access* alignment, IR_Data_Access* destination) 
        {
            auto& types = compiler.analysis_data->type_system.predefined_types;
            auto& analyser = compiler.semantic_analyser;
            
            IR_Data_Access* alloc_fn_access = ir_data_access_create_global(analyser->global_allocator);
            alloc_fn_access = ir_data_access_create_dereference(alloc_fn_access);
            alloc_fn_access = ir_data_access_create_member(alloc_fn_access, types.allocator->content.members[0]);

            IR_Instruction alloc_instr;
            alloc_instr.type = IR_Instruction_Type::FUNCTION_CALL;
            alloc_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_POINTER_CALL;
            alloc_instr.options.call.options.pointer_access = alloc_fn_access;
            alloc_instr.options.call.destination = destination;
            alloc_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(3);
            dynamic_array_push_back(&alloc_instr.options.call.arguments, ir_data_access_create_global(analyser->global_allocator));
            dynamic_array_push_back(&alloc_instr.options.call.arguments, size);
            dynamic_array_push_back(&alloc_instr.options.call.arguments, alignment);
            dynamic_array_push_back(&ir_block->instructions, alloc_instr);
        };
        if (new_expr.count_expr.available)
        {
            assert(result_type->type == Datatype_Type::SLICE, "HEY");
            auto slice_type = downcast<Datatype_Slice>(result_type);
            IR_Data_Access* slice_access = make_destination_access_on_demand(result_type);
            IR_Data_Access* slice_data_access = ir_data_access_create_member(slice_access, slice_type->data_member);
            IR_Data_Access* slice_size_access = ir_data_access_create_member(slice_access, slice_type->size_member);

            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.source = ir_generator_generate_expression(ir_block, new_expr.count_expr.value);
            move_instr.options.move.destination = slice_size_access;
            dynamic_array_push_back(&ir_block->instructions, move_instr);

            IR_Instruction mult_instr;
            mult_instr.type = IR_Instruction_Type::BINARY_OP;
            mult_instr.options.binary_op.type = IR_Binop::MULTIPLICATION;
            mult_instr.options.binary_op.destination = ir_data_access_create_intermediate(ir_block, upcast(types.u64_type));
            mult_instr.options.binary_op.operand_left = slice_size_access;
            mult_instr.options.binary_op.operand_right = ir_data_access_create_constant_u64(mem.size);
            dynamic_array_push_back(&ir_block->instructions, mult_instr);

            u32 alignment_u32 = (u32)mem.alignment;
            allocate_from_global_allocator(
                ir_block,
                mult_instr.options.binary_op.destination,
                ir_data_access_create_constant(upcast(types.u32_type), array_create_static_as_bytes(&alignment_u32, 1)),
                slice_data_access
            );
            return slice_access;
        }
        else
        {
            IR_Data_Access* destination = make_destination_access_on_demand(result_type);
            u32 alignment_u32 = (u32)mem.alignment;
            allocate_from_global_allocator(
                ir_block,
                ir_data_access_create_constant_u64(mem.size),
                ir_data_access_create_constant(upcast(types.u32_type), array_create_static_as_bytes(&alignment_u32, 1)),
                destination
            );
            return destination;
        }

        panic("Unreachable");
        break;
    }
    case AST::Expression_Type::CAST: {
        return ir_generator_generate_expression(ir_block, expression->options.cast.operand, destination);
    }
    case AST::Expression_Type::OPTIONAL_CHECK: 
    {
        auto check_access = ir_generator_generate_expression(ir_block, expression->options.optional_check_value);
        auto type = datatype_get_non_const_type(check_access->datatype);

        if (info->specifics.is_optional_pointer_check) {
            destination = make_destination_access_on_demand(upcast(types.bool_type));
            assert(datatype_is_pointer(type), "");
            IR_Instruction check_instr;
            check_instr.type = IR_Instruction_Type::BINARY_OP;
            check_instr.options.binary_op.destination = destination;
            check_instr.options.binary_op.operand_left = check_access;
            void* null_val = nullptr;
            check_instr.options.binary_op.operand_right = ir_data_access_create_constant(check_access->datatype, array_create_static_as_bytes(&null_val, 1));
            check_instr.options.binary_op.type = IR_Binop::NOT_EQUAL;
            dynamic_array_push_back(&ir_block->instructions, check_instr);
            return destination;
        }

        assert(type->type == Datatype_Type::OPTIONAL_TYPE, "");
        auto opt = downcast<Datatype_Optional>(type);
        auto access = ir_data_access_create_member(check_access, opt->is_available_member);
        return move_access_to_destination(access);
    }
    case AST::Expression_Type::OPTIONAL_POINTER:
    case AST::Expression_Type::OPTIONAL_TYPE:
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
    return nullptr;
}

IR_Data_Access* ir_generator_generate_expression(IR_Code_Block* ir_block, AST::Expression* expression, IR_Data_Access* destination)
{
    auto info = get_info(expression);
    auto& cast_info = info->cast_info;
    auto cast_type = cast_info.cast_type;

    IR_Data_Access* source;
    if (cast_type == Cast_Type::NO_CAST && cast_info.deref_count == 0) {
        return ir_generator_generate_expression_no_cast(ir_block, expression, destination);
    }

    source = ir_generator_generate_expression_no_cast(ir_block, expression, 0);

    // Auto operations
    if (cast_info.deref_count < 0) {
        for (int i = 0; i < -cast_info.deref_count; i++) {
            source = ir_data_access_create_address_of(source);
        }
    }
    else
    {
        for (int i = 0; i < cast_info.deref_count; i++) {
            source = ir_data_access_create_dereference(source);
        }
    }

    // Early exit if no cast is required
    if (cast_type == Cast_Type::NO_CAST) {
        if (destination == 0) {
            return source;
        }

        IR_Instruction move;
        move.type = IR_Instruction_Type::MOVE;
        move.options.move.destination = destination;
        move.options.move.source = source;
        dynamic_array_push_back(&ir_block->instructions, move);
        return destination;
    }

    auto type_system = &compiler.analysis_data->type_system;
    auto& types = type_system->predefined_types;
    auto source_type = source->datatype;
    auto make_destination_access_on_demand = [&](Datatype* result_type) -> IR_Data_Access* {
        if (destination == 0) {
            destination = ir_data_access_create_intermediate(ir_block, result_type);
        }
        return destination;
    };
    auto make_simple_cast = [&](IR_Code_Block* ir_block, IR_Data_Access* source, Datatype* result_type, IR_Cast_Type cast_type) -> IR_Data_Access*
    {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::CAST;
        instr.options.cast.type = cast_type;
        instr.options.cast.source = source;
        instr.options.cast.destination = make_destination_access_on_demand(result_type);
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
        IR_Data_Access* slice_access = make_destination_access_on_demand(cast_info.result_type);
        // Set size
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::MOVE;
            instr.options.move.destination = ir_data_access_create_member(slice_access, slice_type->size_member);
            assert(array_type->count_known, "");
            instr.options.move.source = ir_data_access_create_constant_u64(array_type->element_count);
            dynamic_array_push_back(&ir_block->instructions, instr);
        }
        // Set data
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::MOVE;
            instr.options.move.source = ir_data_access_create_address_of(ir_data_access_create_array_or_slice_access(source, ir_data_access_create_constant_u64(0), false, ir_block));
            instr.options.move.destination = ir_data_access_create_member(slice_access, slice_type->data_member);
            dynamic_array_push_back(&ir_block->instructions, instr);
        }
        return slice_access;
    }
    case Cast_Type::TO_OPTIONAL: {
        auto opt_type = downcast<Datatype_Optional>(datatype_get_non_const_type(cast_info.result_type));
        auto destination = make_destination_access_on_demand(cast_info.result_type);
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::MOVE;
        instr.options.move.destination = ir_data_access_create_member(destination, opt_type->value_member);
        instr.options.move.source = source;
        dynamic_array_push_back(&ir_block->instructions, instr);

        bool value = true;
        instr.type = IR_Instruction_Type::MOVE;
        instr.options.move.destination = ir_data_access_create_member(destination, opt_type->is_available_member);
        instr.options.move.source = ir_data_access_create_constant_bool(true);
        dynamic_array_push_back(&ir_block->instructions, instr);

        return destination;
    }
    case Cast_Type::CUSTOM_CAST: {
        IR_Instruction instr;
        instr.type = IR_Instruction_Type::FUNCTION_CALL;
        instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
        instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
        dynamic_array_push_back(&instr.options.call.arguments, source);
        instr.options.call.destination = make_destination_access_on_demand(cast_info.result_type);
        instr.options.call.options.function = cast_info.options.custom_cast_function;
        dynamic_array_push_back(&ir_block->instructions, instr);
        return instr.options.call.destination;
    }
    case Cast_Type::FROM_ANY:
    {
        // Check if type matches given type, if not error out
        IR_Data_Access* access_operand = source;
        IR_Data_Access* access_valid_bool = ir_data_access_create_intermediate(ir_block, upcast(types.bool_type));
        IR_Data_Access* access_result = make_destination_access_on_demand(cast_info.result_type);
        {
            // Note: We always compare with non-const type, so any casts work from/to const
            auto type_handle = datatype_get_non_const_type(cast_info.result_type)->type_handle;

            IR_Instruction cmp_instr;
            cmp_instr.type = IR_Instruction_Type::BINARY_OP;
            cmp_instr.options.binary_op.type = IR_Binop::EQUAL;
            cmp_instr.options.binary_op.operand_left = ir_data_access_create_constant(
                types.type_handle, array_create_static_as_bytes(&type_handle, 1)
            );
            cmp_instr.options.binary_op.operand_right = ir_data_access_create_member(access_operand, types.any_type->content.members[1]);
            cmp_instr.options.binary_op.destination = access_valid_bool;
            dynamic_array_push_back(&ir_block->instructions, cmp_instr);
        }
        IR_Code_Block* branch_valid = ir_code_block_create(ir_block->function);
        IR_Code_Block* branch_invalid = ir_code_block_create(ir_block->function);
        {
            IR_Instruction if_instr;
            if_instr.type = IR_Instruction_Type::IF;
            if_instr.options.if_instr.condition = access_valid_bool;
            if_instr.options.if_instr.false_branch = branch_invalid;
            if_instr.options.if_instr.true_branch = branch_valid;
            dynamic_array_push_back(&ir_block->instructions, if_instr);
        }
        {
            // True_Branch
            IR_Data_Access* any_data_access = ir_data_access_create_member(access_operand, types.any_type->content.members[0]);
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
            move_instr.options.move.source = ir_data_access_create_dereference(any_data_access);
            dynamic_array_push_back(&branch_valid->instructions, move_instr);
        }
        {
            // False branch
            IR_Instruction exit_instr;
            exit_instr.type = IR_Instruction_Type::RETURN;
            exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
            exit_instr.options.return_instr.options.exit_code = exit_code_make(Exit_Code_Type::CODE_ERROR, "Any cast downcast failed, type index was invalid");
            dynamic_array_push_back(&branch_invalid->instructions, exit_instr);
        }
        return access_result;
    }
    case Cast_Type::TO_ANY:
    {
        IR_Data_Access* operand_access = source;
        if (operand_access->type == IR_Data_Access_Type::CONSTANT)
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::MOVE;
            instr.options.move.destination = ir_data_access_create_intermediate(ir_block, source_type);
            instr.options.move.source = operand_access;
            dynamic_array_push_back(&ir_block->instructions, instr);
            operand_access = instr.options.move.destination;
        }

        IR_Data_Access* any_access = make_destination_access_on_demand(upcast(types.any_type));
        // Set data
        {
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::MOVE;
            instr.options.move.source = ir_data_access_create_address_of(operand_access);
            // In theory a cast from pointer to voidptr would be better, but I think I can ignore it
            instr.options.move.destination = ir_data_access_create_member(any_access, types.any_type->content.members[0]);
            dynamic_array_push_back(&ir_block->instructions, instr);
        }
        // Set type
        {
            // Note: We always compare with non-const type, so any casts work from/to const
            auto type_handle = datatype_get_non_const_type(source_type)->type_handle;

            IR_Instruction instr;
            instr.type = IR_Instruction_Type::MOVE;
            instr.options.move.destination = ir_data_access_create_member(any_access, types.any_type->content.members[1]);
            instr.options.move.source = ir_data_access_create_constant(
                upcast(types.type_handle), array_create_static_as_bytes(&type_handle, 1)
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
        auto& defer = defers[i];
        if (defer.is_block) 
        {
            auto block = defer.options.block;
            IR_Instruction instr;
            instr.type = IR_Instruction_Type::BLOCK;
            instr.options.block = ir_code_block_create(ir_block->function);
            ir_generator_generate_block(instr.options.block, block);
            dynamic_array_push_back(&ir_block->instructions, instr);
        }
        else 
        {
            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.destination = defer.options.defer_restore.left_access;
            move_instr.options.move.source = defer.options.defer_restore.restore_value;
            dynamic_array_push_back(&ir_block->instructions, move_instr);
        }
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
        increment.options.binary_op.type = IR_Binop::ADDITION;
        increment.options.binary_op.destination = foreach.index_access;
        increment.options.binary_op.operand_left = foreach.index_access;
        increment.options.binary_op.operand_right = ir_data_access_create_constant_u64(1);
        dynamic_array_push_back(&ir_block->instructions, increment);

        // Update pointer
        if (foreach.is_custom_iterator)
        {
            IR_Instruction next_call;
            next_call.type = IR_Instruction_Type::FUNCTION_CALL;
            next_call.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            next_call.options.call.destination = foreach.loop_variable_access;
            next_call.options.call.options.function = foreach.next_function;
            next_call.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
            IR_Data_Access* argument_access = foreach.iterator_access;
            for (int i = 0; i < foreach.iterator_deref_value; i++) {
                argument_access = ir_data_access_create_dereference(argument_access);
            }
            if (foreach.iterator_deref_value == -1) {
                argument_access = ir_data_access_create_address_of(foreach.iterator_access);
            }
            dynamic_array_push_back(&next_call.options.call.arguments, argument_access);
            dynamic_array_push_back(&ir_block->instructions, next_call);
        }
        else {
            IR_Instruction element_instr;
            element_instr.type = IR_Instruction_Type::MOVE;
            element_instr.options.move.source = ir_data_access_create_address_of(ir_data_access_create_array_or_slice_access(foreach.iterable_access, foreach.index_access, false, ir_block));
            element_instr.options.move.destination = foreach.loop_variable_access;
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
        Defer_Item item;
        item.is_block = true;
        item.options.block = statement->options.defer_block;
        dynamic_array_push_back(&ir_generator.defer_stack, item);
        break;
    }
    case AST::Statement_Type::DEFER_RESTORE:
    {
        auto& restore = statement->options.defer_restore;

        IR_Data_Access* left_access = ir_generator_generate_expression(ir_block, restore.left_side);
        IR_Data_Access* copy_access = ir_data_access_create_intermediate(ir_block, left_access->datatype);

        // Copy current value to temporary
        IR_Instruction move_instr;
        move_instr.type = IR_Instruction_Type::MOVE;
        move_instr.options.move.destination = copy_access;
        move_instr.options.move.source = left_access;
        dynamic_array_push_back(&ir_block->instructions, move_instr);

        // Write assignment to value
        ir_generator_generate_expression(ir_block, restore.right_side, left_access);

        Defer_Item defer_item;
        defer_item.is_block = false;
        defer_item.options.defer_restore.left_access = left_access;
        defer_item.options.defer_restore.restore_value = copy_access;
        dynamic_array_push_back(&ir_generator.defer_stack, defer_item);
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

        IR_Data_Access* broadcast_value;
        Datatype* broadcast_type;
        if (definition->values.size == 1 && definition->symbols.size > 1) {
            broadcast_value = ir_generator_generate_expression(ir_block, definition->values[0]);
            broadcast_type = broadcast_value->datatype;
        }

        // Define all variables
        for (int i = 0; i < definition->symbols.size; i++)
        {
            auto symbol = get_info(definition->symbols[i]);
            assert(symbol->type == Symbol_Type::VARIABLE, "");
            auto var_type = symbol->options.variable_type;
            IR_Register reg;
            reg.name = optional_make_success<String*>(symbol->id);
            reg.type = var_type;
            reg.has_definition_instruction = true;
            dynamic_array_push_back(&ir_block->registers, reg);

            IR_Data_Access* access = ir_data_access_create_register(ir_block, ir_block->registers.size - 1);
            hashtable_insert_element(&ir_generator.variable_mapping, definition->symbols[i], access);

            IR_Instruction definition_instr;
            definition_instr.type = IR_Instruction_Type::VARIABLE_DEFINITION;
            definition_instr.options.variable_definition.symbol = symbol;
            definition_instr.options.variable_definition.variable_access = access;
            definition_instr.options.variable_definition.initial_value.available = false;

            if (definition->values.size == 1 && definition->symbols.size > 1) {
                // I guess this is still struct split, which currently isn't supported anymore
                if (is_split) {
                    assert(broadcast_type->type == Datatype_Type::STRUCT, "");
                    definition_instr.options.variable_definition.initial_value = optional_make_success(
                        ir_data_access_create_member(
                            broadcast_value, downcast<Datatype_Struct>(broadcast_type)->content.members[i]
                        )
                    );
                }
                else {
                    definition_instr.options.variable_definition.initial_value = optional_make_success(broadcast_value);
                }
            }
            else if (definition->values.size != 0) {
                assert(i < definition->values.size, "Must be guaranteed by semantic analyser!");
                definition_instr.options.variable_definition.initial_value = optional_make_success(
                    ir_generator_generate_expression(ir_block, definition->values[i])
                );
            }

            dynamic_array_push_back(&ir_block->instructions, definition_instr);
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
        auto& switch_info = get_info(statement)->specifics.switch_statement;
        IR_Data_Access* condition_access = ir_generator_generate_expression(ir_block, statement->options.switch_statement.condition);

        IR_Instruction instr;
        instr.type = IR_Instruction_Type::SWITCH;
        instr.options.switch_instr.condition_access = condition_access;
        instr.options.switch_instr.cases = dynamic_array_create<IR_Switch_Case>(statement->options.switch_statement.cases.size);

        // Check for subtype access
        auto cond_type = instr.options.switch_instr.condition_access->datatype;
        if (switch_info.base_content != nullptr) {
            cond_type = switch_info.base_content->tag_member.type;
            instr.options.switch_instr.condition_access = ir_data_access_create_member(condition_access, switch_info.base_content->tag_member);
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

            // Create case variable if available
            if (case_info->variable_symbol != 0) {
                // Generate pointer access to subtype
                IR_Data_Access* pointer_access = ir_data_access_create_address_of(condition_access);
                // new_case.block->registers[pointer_access.index] = case_info->variable_symbol->options.variable_type; // Update type to subtype
                hashtable_insert_element(&ir_generator.variable_mapping, switch_case->variable_definition.value, pointer_access);
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
            IR_Data_Access* access = ir_data_access_create_intermediate(ir_block, symbol->options.variable_type);
            hashtable_insert_element(&ir_generator.variable_mapping, for_loop.loop_variable_definition, access);
            ir_generator_generate_expression(ir_block, for_loop.initial_value, access);
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
        auto& types = compiler.analysis_data->type_system.predefined_types;

        // Create and initialize index data-access (Always available)
        IR_Data_Access* index_access = ir_data_access_create_intermediate(ir_block, upcast(types.u64_type));
        {
            // Initialize
            IR_Instruction initialize;
            initialize.type = IR_Instruction_Type::MOVE;
            initialize.options.move.destination = index_access;
            initialize.options.move.source = ir_data_access_create_constant_u64(0);
            dynamic_array_push_back(&ir_block->instructions, initialize);

            if (foreach_loop.index_variable_definition.available) {
                assert(loop_info.index_variable_symbol != 0 && loop_info.index_variable_symbol->type == Symbol_Type::VARIABLE, "");
                hashtable_insert_element(&ir_generator.variable_mapping, foreach_loop.index_variable_definition.value, index_access);
            }
        }

        // Create iterable access
        IR_Data_Access* iterable_access = ir_generator_generate_expression(ir_block, foreach_loop.expression);
        auto iterable_type = iterable_access->datatype;

        // Create and initialize loop variable
        Datatype* iterator_type = nullptr;
        IR_Data_Access* iterator_access; // Only valid for custom iterators
        IR_Data_Access* loop_variable_access = ir_data_access_create_intermediate(ir_block, upcast(loop_info.loop_variable_symbol->options.variable_type));
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
                iter_create_instr.options.call.options.function = loop_info.custom_op.fn_create;
                iter_create_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
                dynamic_array_push_back(&iter_create_instr.options.call.arguments, iterable_access);
                dynamic_array_push_back(&ir_block->instructions, iter_create_instr);
            }
            else
            {
                iterable_type = datatype_get_non_const_type(iterable_type);
                assert(iterable_type->type == Datatype_Type::ARRAY || iterable_type->type == Datatype_Type::SLICE, "Other types not supported currently");
                IR_Instruction element_instr;
                element_instr.type = IR_Instruction_Type::MOVE;
                element_instr.options.move.destination = loop_variable_access;
                element_instr.options.move.source = ir_data_access_create_address_of(ir_data_access_create_array_or_slice_access(iterable_access, index_access, false, ir_block));
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
                increment.options.foreach_loop.next_function = loop_info.custom_op.fn_next;
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
                IR_Data_Access* condition_access = ir_data_access_create_intermediate(ir_block, upcast(types.bool_type));
                if (loop_info.is_custom_op) 
                {
                    IR_Instruction has_next_call;
                    has_next_call.type = IR_Instruction_Type::FUNCTION_CALL;
                    has_next_call.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
                    has_next_call.options.call.destination = condition_access;
                    has_next_call.options.call.options.function = loop_info.custom_op.fn_has_next;
                    has_next_call.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
                    IR_Data_Access* argument_access = iterator_access;
                    for (int i = 0; i < loop_info.custom_op.has_next_pointer_diff; i++) {
                        argument_access = ir_data_access_create_dereference(argument_access);
                    }
                    if (loop_info.custom_op.has_next_pointer_diff == -1) {
                        argument_access = ir_data_access_create_address_of(iterator_access);
                    }
                    dynamic_array_push_back(&has_next_call.options.call.arguments, argument_access);
                    dynamic_array_push_back(&condition_code->instructions, has_next_call);
                }
                else 
                {
                    IR_Data_Access* array_size_access;
                    if (iterable_type->type == Datatype_Type::ARRAY) {
                        auto array_type = downcast<Datatype_Array>(iterable_type);
                        assert(array_type->count_known, "");
                        array_size_access = ir_data_access_create_constant_u64(array_type->element_count);
                    }
                    else if (iterable_type->type == Datatype_Type::SLICE) {
                        auto slice_type = downcast<Datatype_Slice>(iterable_type);
                        array_size_access = ir_data_access_create_member(iterable_access, slice_type->size_member);
                    }

                    IR_Instruction comparison;
                    comparison.type = IR_Instruction_Type::BINARY_OP;
                    comparison.options.binary_op.type = IR_Binop::LESS;
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
                    get_value_call.options.call.options.function = loop_info.custom_op.fn_get_value;
                    get_value_call.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
                    IR_Data_Access* argument_access = iterator_access;
                    for (int i = 0; i < loop_info.custom_op.has_next_pointer_diff; i++) {
                        argument_access = ir_data_access_create_dereference(argument_access);
                    }
                    if (loop_info.custom_op.has_next_pointer_diff == -1) {
                        argument_access = ir_data_access_create_address_of(iterator_access);
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
                move.options.move.destination = ir_data_access_create_intermediate(ir_block, move.options.move.source->datatype);
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

        IR_Data_Access* left_access = ir_generator_generate_expression(ir_block, assign.left_side);
        Datatype* type = left_access->datatype;
        IR_Data_Access* right_access = ir_generator_generate_expression(ir_block, assign.right_side);

        // Handle binop
        if (info->specifics.overload.function == 0)
        {
            IR_Instruction binop;
            binop.type = IR_Instruction_Type::BINARY_OP;
            binop.options.binary_op.type = ast_binop_to_ir_binop(assign.binop);
            binop.options.binary_op.destination = left_access;
            binop.options.binary_op.operand_left = left_access;
            binop.options.binary_op.operand_right = right_access;
            dynamic_array_push_back(&ir_block->instructions, binop);
        }
        else
        {
            IR_Instruction call;
            call.type = IR_Instruction_Type::FUNCTION_CALL;
            call.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_CALL;
            call.options.call.destination = left_access;
            call.options.call.options.function = info->specifics.overload.function;
            call.options.call.arguments = dynamic_array_create<IR_Data_Access*>(2);
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

        break;
    }
    case AST::Statement_Type::ASSIGNMENT:
    {
        auto& as = statement->options.assignment;
        bool is_struct_split = get_info(statement)->specifics.is_struct_split;

        IR_Data_Access* broadcast_value;
        Datatype* broadcast_type = 0;
        if (as.right_side.size == 1 && as.left_side.size > 1) {
            broadcast_value = ir_generator_generate_expression(ir_block, as.right_side[0]);
            broadcast_type = broadcast_value->datatype;
        }

        for (int i = 0; i < as.left_side.size; i++)
        {
            if (as.right_side.size == 1 && as.left_side.size > 1) 
            {
                IR_Instruction instr;
                instr.type = IR_Instruction_Type::MOVE;
                instr.options.move.destination = ir_generator_generate_expression(ir_block, as.left_side[i]);
                instr.options.move.source = broadcast_value;
                if (is_struct_split) { // Note: Struct split currently not supported anymore
                    assert(broadcast_type->type == Datatype_Type::STRUCT, "");
                    instr.options.move.source = ir_data_access_create_member(
                        broadcast_value, downcast<Datatype_Struct>(broadcast_type)->content.members[i]);
                }
                else {
                    instr.options.move.source = broadcast_value;
                }
                dynamic_array_push_back(&ir_block->instructions, instr);
            }
            else {
                IR_Data_Access* left_access = ir_generator_generate_expression(ir_block, as.left_side[i]);
                ir_generator_generate_expression(ir_block, as.right_side[i], left_access);
            }
        }
        break;
    }
    case AST::Statement_Type::DELETE_STATEMENT:
    {
        auto& types = compiler.analysis_data->type_system.predefined_types;
        auto& analyser = compiler.semantic_analyser;

        IR_Data_Access* delete_access = ir_generator_generate_expression(ir_block, statement->options.delete_expr);
        auto delete_type = datatype_get_non_const_type(delete_access->datatype);
        IR_Data_Access* size_access = nullptr;
        IR_Data_Access* pointer_to_delete_access = nullptr;
        if (delete_type->type == Datatype_Type::SLICE) 
        {
            auto slice = downcast<Datatype_Slice>(delete_type);
            pointer_to_delete_access = ir_data_access_create_member(delete_access, slice->data_member);
            size_access = ir_data_access_create_member(delete_access, slice->size_member);

            IR_Instruction multiply_instr;
            multiply_instr.type = IR_Instruction_Type::BINARY_OP;
            multiply_instr.options.binary_op.type = IR_Binop::MULTIPLICATION;
            multiply_instr.options.binary_op.destination = size_access;
            multiply_instr.options.binary_op.operand_left = size_access;
            u64 element_size_u64 = slice->element_type->memory_info.value.size;
            multiply_instr.options.binary_op.operand_right = ir_data_access_create_constant_u64(element_size_u64);
            dynamic_array_push_back(&ir_block->instructions, multiply_instr);
        }
        else
        {
            assert(delete_type->type == Datatype_Type::POINTER, "Can only delete slices or pointers");
            u64 element_size_u64 = downcast<Datatype_Pointer>(delete_type)->element_type->memory_info.value.size;
            size_access = ir_data_access_create_constant_u64(element_size_u64);
            pointer_to_delete_access = delete_access;
        }

        // Call delete function on current allocator
        IR_Data_Access* delete_fn_access = ir_data_access_create_global(analyser->global_allocator);
        delete_fn_access = ir_data_access_create_dereference(delete_fn_access);
        delete_fn_access = ir_data_access_create_member(delete_fn_access, types.allocator->content.members[1]);

        IR_Data_Access* byte_ptr_access = ir_data_access_create_intermediate(ir_block, types.byte_pointer);
        IR_Instruction byte_ptr_cast;
        byte_ptr_cast.type = IR_Instruction_Type::CAST;
        byte_ptr_cast.options.cast.destination = byte_ptr_access;
        byte_ptr_cast.options.cast.source = pointer_to_delete_access;
        byte_ptr_cast.options.cast.type = IR_Cast_Type::POINTERS;
        dynamic_array_push_back(&ir_block->instructions, byte_ptr_cast);

        IR_Instruction alloc_instr;
        alloc_instr.type = IR_Instruction_Type::FUNCTION_CALL;
        alloc_instr.options.call.call_type = IR_Instruction_Call_Type::FUNCTION_POINTER_CALL;
        alloc_instr.options.call.options.pointer_access = delete_fn_access;
        alloc_instr.options.call.destination = ir_data_access_create_intermediate(ir_block, upcast(types.bool_type));
        alloc_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(3);
        dynamic_array_push_back(&alloc_instr.options.call.arguments, ir_data_access_create_global(analyser->global_allocator));
        dynamic_array_push_back(&alloc_instr.options.call.arguments, byte_ptr_access);
        dynamic_array_push_back(&alloc_instr.options.call.arguments, size_access);
        dynamic_array_push_back(&ir_block->instructions, alloc_instr);

        // Set pointer access to null
        IR_Instruction set_null_instr;
        set_null_instr.type = IR_Instruction_Type::CAST;
        set_null_instr.options.cast.destination = pointer_to_delete_access;
        void* null_val = nullptr;
        set_null_instr.options.cast.source = ir_data_access_create_constant(types.byte_pointer, array_create_static_as_bytes(&null_val, 1));
        set_null_instr.options.cast.type = IR_Cast_Type::POINTERS;
        dynamic_array_push_back(&ir_block->instructions, set_null_instr);

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
    for (int i = 0; i < ir_generator.queued_function_slot_indices.size; i++)
    {
        auto& slot = compiler.analysis_data->function_slots[ir_generator.queued_function_slot_indices[i]];
        ModTree_Function* mod_func = slot.modtree_function;
        IR_Function* ir_func = slot.ir_function;
        if (mod_func == 0) {
            if (gen_bytecode) {
                bytecode_generator_compile_function(compiler.bytecode_generator, ir_func);
            }
            continue;
        }

        // Generate function code
        if (mod_func->function_type == ModTree_Function_Type::NORMAL)
        {
            ir_generator.current_pass = mod_func->options.normal.progress->body_workload->base.current_pass;
            ir_generator_generate_block(ir_func->code, mod_func->options.normal.progress->body_workload->body_node);
        }
        else if (mod_func->function_type == ModTree_Function_Type::BAKE)
        {
            ir_generator.current_pass = mod_func->options.bake->analysis_workload->base.current_pass;
            auto bake_node = mod_func->options.bake->analysis_workload->bake_node;
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
        else {
            panic("Extern functions should have been filtered out by here");
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
    dynamic_array_reset(&ir_generator.queued_function_slot_indices);

    // Do bytecode stuff
    if (gen_bytecode) {
        bytecode_generator_update_references(compiler.bytecode_generator);
    }
}

void ir_generator_queue_function(ModTree_Function* function)
{
    if (!function->is_runnable) {
        return;
    }

    if (function->function_type == ModTree_Function_Type::EXTERN) {
        return;
    }
    else if (function->function_type == ModTree_Function_Type::NORMAL) {
        assert((function->options.normal.progress->type != Polymorphic_Analysis_Type::POLYMORPHIC_BASE), "Function cannot be polymorhic here!");
    }

    auto& slots = compiler.analysis_data->function_slots;
    auto& slot = slots[function->function_slot_index];
    if (slot.ir_function != nullptr) return; // Already queued
    ir_function_create(function->signature, function->function_slot_index);
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
        auto& type_system = compiler.analysis_data->type_system;
        auto& types = type_system.predefined_types;

        auto& slots = compiler.analysis_data->function_slots;
        auto entry_function = ir_function_create(type_system_make_function({}), -1);
        ir_generator.program->entry_function = entry_function;

        // Initialize default allocator
        {
            // Inititalize default allocator (Load function pointers)
            IR_Data_Access* alloc_access = ir_data_access_create_global(compiler.semantic_analyser->default_allocator);
            IR_Instruction address_instr;
            address_instr.type = IR_Instruction_Type::FUNCTION_ADDRESS;

            address_instr.options.function_address.function_slot_index = ir_generator.default_allocate_function->function_slot_index;
            address_instr.options.function_address.destination = ir_data_access_create_member(alloc_access, types.allocator->content.members[0]);
            dynamic_array_push_back(&entry_function->code->instructions, address_instr);

            address_instr.options.function_address.function_slot_index = ir_generator.default_free_function->function_slot_index;
            address_instr.options.function_address.destination = ir_data_access_create_member(alloc_access, types.allocator->content.members[1]);
            dynamic_array_push_back(&entry_function->code->instructions, address_instr);

            address_instr.options.function_address.function_slot_index = ir_generator.default_reallocate_function->function_slot_index;
            address_instr.options.function_address.destination = ir_data_access_create_member(alloc_access, types.allocator->content.members[2]);
            dynamic_array_push_back(&entry_function->code->instructions, address_instr);

            // Set global allocator to default allocator
            IR_Instruction move_instr;
            move_instr.type = IR_Instruction_Type::MOVE;
            move_instr.options.move.source = ir_data_access_create_address_of(ir_data_access_create_global(compiler.semantic_analyser->default_allocator));
            move_instr.options.move.destination = ir_data_access_create_global(compiler.semantic_analyser->global_allocator);
            dynamic_array_push_back(&entry_function->code->instructions, move_instr);
        }

        // Initialize all globals
        auto& globals = compiler.analysis_data->program->globals;
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
            call_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
            call_instr.options.call.options.function = ir_generator.modtree->main_function;
            dynamic_array_push_back(&entry_function->code->instructions, call_instr);
        }

        // Exit
        {
            IR_Instruction exit_instr;
            exit_instr.type = IR_Instruction_Type::RETURN;
            exit_instr.options.return_instr.type = IR_Instruction_Return_Type::EXIT;
            exit_instr.options.return_instr.options.exit_code = exit_code_make(Exit_Code_Type::SUCCESS);
            dynamic_array_push_back(&entry_function->code->instructions, exit_instr);
        }

        // Generate entry + default allocator functions
        ir_generator_generate_queued_items(gen_bytecode);
    }
}

// Generator
IR_Generator ir_generator;

IR_Generator* ir_generator_initialize()
{
    ir_generator.program = 0;
    ir_generator.next_label_index = 0;

    ir_generator.nothing_access.type = IR_Data_Access_Type::NOTHING;
    ir_generator.nothing_access.datatype = nullptr;

    ir_generator.data_accesses = dynamic_array_create<IR_Data_Access*>();
    ir_generator.loop_increment_instructions = hashtable_create_pointer_empty<AST::Code_Block*, Loop_Increment>(8);
    ir_generator.variable_mapping = hashtable_create_pointer_empty<AST::Definition_Symbol*, IR_Data_Access*>(8);
    ir_generator.labels_break = hashtable_create_pointer_empty<AST::Code_Block*, int>(8);
    ir_generator.labels_continue = hashtable_create_pointer_empty<AST::Code_Block*, int>(8);
    ir_generator.block_defer_depths = hashtable_create_pointer_empty<AST::Code_Block*, int>(8);

    ir_generator.queued_function_slot_indices = dynamic_array_create<int>();
    ir_generator.defer_stack = dynamic_array_create<Defer_Item>();
    ir_generator.fill_out_breaks = dynamic_array_create<Unresolved_Goto>();
    ir_generator.fill_out_continues = dynamic_array_create<Unresolved_Goto>();
    return &ir_generator;
}

void ir_data_access_delete(IR_Data_Access** access) {
    delete (*access);
}

void ir_generator_reset()
{
    if (ir_generator.program != 0) {
        ir_program_destroy(ir_generator.program);
    }

    ir_generator.nothing_access.datatype = compiler.analysis_data->type_system.predefined_types.unknown_type;
    ir_generator.program = ir_program_create(&compiler.analysis_data->type_system);
    ir_generator.next_label_index = 0;
    ir_generator.modtree = compiler.analysis_data->program;

    hashtable_reset(&ir_generator.variable_mapping);
    hashtable_reset(&ir_generator.labels_break);
    hashtable_reset(&ir_generator.labels_continue);
    hashtable_reset(&ir_generator.block_defer_depths);
    hashtable_reset(&ir_generator.loop_increment_instructions);

    dynamic_array_for_each(ir_generator.data_accesses, ir_data_access_delete);
    dynamic_array_reset(&ir_generator.data_accesses);

    dynamic_array_reset(&ir_generator.defer_stack);
    dynamic_array_reset(&ir_generator.queued_function_slot_indices);
    dynamic_array_reset(&ir_generator.fill_out_breaks);
    dynamic_array_reset(&ir_generator.fill_out_continues);

    {
        auto& type_system = compiler.analysis_data->type_system;
        auto& types = type_system.predefined_types;

        auto& slots = compiler.analysis_data->function_slots;

        // Create default alloc function
        IR_Function* default_alloc_function = ir_function_create(types.allocate_function, -1);
        {
            auto fn = default_alloc_function;
            IR_Data_Access* pointer_value = ir_data_access_create_intermediate(fn->code, types.byte_pointer_optional);
            IR_Data_Access* size_param_u64 = ir_data_access_create_parameter(fn, 1);

            IR_Instruction call_instr;
            call_instr.type = IR_Instruction_Type::FUNCTION_CALL;
            call_instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
            call_instr.options.call.options.hardcoded = Hardcoded_Type::MALLOC_SIZE_U64;
            call_instr.options.call.destination = pointer_value;
            call_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
            dynamic_array_push_back(&call_instr.options.call.arguments, size_param_u64);
            dynamic_array_push_back(&fn->code->instructions, call_instr);

            IR_Instruction return_instr;
            return_instr.type = IR_Instruction_Type::RETURN;
            return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_DATA;
            return_instr.options.return_instr.options.return_value = pointer_value;
            dynamic_array_push_back(&fn->code->instructions, return_instr);
        }

        // Default free function
        IR_Function* default_free_function = ir_function_create(types.free_function, -1);
        {
            auto fn = default_free_function;
            IR_Data_Access* pointer_value = ir_data_access_create_parameter(fn, 1);

            IR_Instruction call_instr;
            call_instr.type = IR_Instruction_Type::FUNCTION_CALL;
            call_instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
            call_instr.options.call.options.hardcoded = Hardcoded_Type::FREE_POINTER;
            call_instr.options.call.destination = ir_data_access_create_nothing();
            call_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
            dynamic_array_push_back(&call_instr.options.call.arguments, pointer_value);
            dynamic_array_push_back(&fn->code->instructions, call_instr);

            IR_Instruction return_instr;
            return_instr.type = IR_Instruction_Type::RETURN;
            return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_DATA;
            return_instr.options.return_instr.options.return_value = ir_data_access_create_constant_bool(true);
            dynamic_array_push_back(&fn->code->instructions, return_instr);
        }

        // Default realloc function
        IR_Function* default_reallocate_function = ir_function_create(types.reallocate_function, -1);
        {
            auto fn = default_reallocate_function;
            IR_Data_Access* pointer_value = ir_data_access_create_parameter(fn, 1);
            IR_Data_Access* new_size_u64 = ir_data_access_create_parameter(fn, 3);
            IR_Data_Access* new_pointer_value = ir_data_access_create_intermediate(fn->code, types.byte_pointer_optional);

            // Allocate new memory
            IR_Instruction alloc_call_instr;
            alloc_call_instr.type = IR_Instruction_Type::FUNCTION_CALL;
            alloc_call_instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
            alloc_call_instr.options.call.options.hardcoded = Hardcoded_Type::MALLOC_SIZE_U64;
            alloc_call_instr.options.call.destination = new_pointer_value;
            alloc_call_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
            dynamic_array_push_back(&alloc_call_instr.options.call.arguments, new_size_u64);
            dynamic_array_push_back(&fn->code->instructions, alloc_call_instr);

            // Copy old data into new data
            IR_Instruction memcpy_call;
            memcpy_call.type = IR_Instruction_Type::FUNCTION_CALL;
            memcpy_call.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
            memcpy_call.options.call.options.hardcoded = Hardcoded_Type::MEMORY_COPY;
            memcpy_call.options.call.destination = ir_data_access_create_nothing();
            memcpy_call.options.call.arguments = dynamic_array_create<IR_Data_Access*>(3);
            dynamic_array_push_back(&memcpy_call.options.call.arguments, new_pointer_value);
            dynamic_array_push_back(&memcpy_call.options.call.arguments, pointer_value);
            dynamic_array_push_back(&memcpy_call.options.call.arguments, new_size_u64);
            dynamic_array_push_back(&fn->code->instructions, memcpy_call);

            // Delete old data
            IR_Instruction call_instr;
            call_instr.type = IR_Instruction_Type::FUNCTION_CALL;
            call_instr.options.call.call_type = IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL;
            call_instr.options.call.options.hardcoded = Hardcoded_Type::FREE_POINTER;
            call_instr.options.call.destination = ir_data_access_create_nothing();
            call_instr.options.call.arguments = dynamic_array_create<IR_Data_Access*>(1);
            dynamic_array_push_back(&call_instr.options.call.arguments, pointer_value);
            dynamic_array_push_back(&fn->code->instructions, call_instr);

            // And return new memory pointer
            IR_Instruction return_instr;
            return_instr.type = IR_Instruction_Type::RETURN;
            return_instr.options.return_instr.type = IR_Instruction_Return_Type::RETURN_DATA;
            return_instr.options.return_instr.options.return_value = new_pointer_value;
            dynamic_array_push_back(&fn->code->instructions, return_instr);
        }

        ir_generator.default_allocate_function = default_alloc_function;
        ir_generator.default_free_function = default_free_function;
        ir_generator.default_reallocate_function = default_reallocate_function;

        Upp_Allocator allocator;
        allocator.allocate_fn_index_plus_one =   default_alloc_function->function_slot_index + 1;
        allocator.free_fn_index_plus_one =       default_free_function->function_slot_index + 1;
        allocator.reallocate_fn_index_plus_one = default_reallocate_function->function_slot_index + 1;

        *(Upp_Allocator*)compiler.semantic_analyser->default_allocator->memory = allocator;
        *(Upp_Allocator**)compiler.semantic_analyser->global_allocator->memory = (Upp_Allocator*)compiler.semantic_analyser->default_allocator->memory;
    }
}

void ir_generator_destroy()
{
    if (ir_generator.program != 0) {
        ir_program_destroy(ir_generator.program);
    }
    hashtable_destroy(&ir_generator.variable_mapping);
    hashtable_destroy(&ir_generator.labels_break);
    hashtable_destroy(&ir_generator.labels_continue);
    hashtable_destroy(&ir_generator.block_defer_depths);
    hashtable_destroy(&ir_generator.loop_increment_instructions);

    dynamic_array_for_each(ir_generator.data_accesses, ir_data_access_delete);
    dynamic_array_destroy(&ir_generator.data_accesses);
    dynamic_array_destroy(&ir_generator.defer_stack);
    dynamic_array_destroy(&ir_generator.queued_function_slot_indices);
    dynamic_array_destroy(&ir_generator.fill_out_breaks);
    dynamic_array_destroy(&ir_generator.fill_out_continues);
}

IR_Binop ast_binop_to_ir_binop(AST::Binop binop)
{
    switch (binop)
    {
    case AST::Binop::ADDITION: return IR_Binop::ADDITION;
    case AST::Binop::SUBTRACTION: return IR_Binop::SUBTRACTION;
    case AST::Binop::DIVISION: return IR_Binop::DIVISION;
    case AST::Binop::MULTIPLICATION: return IR_Binop::MULTIPLICATION;
    case AST::Binop::MODULO: return IR_Binop::MODULO;
    case AST::Binop::AND: return IR_Binop::AND;
    case AST::Binop::OR: return IR_Binop::OR;
    case AST::Binop::EQUAL: return IR_Binop::EQUAL;
    case AST::Binop::NOT_EQUAL: return IR_Binop::NOT_EQUAL;
    case AST::Binop::LESS: return IR_Binop::LESS;
    case AST::Binop::LESS_OR_EQUAL: return IR_Binop::LESS_OR_EQUAL;
    case AST::Binop::GREATER: return IR_Binop::GREATER;
    case AST::Binop::GREATER_OR_EQUAL: return IR_Binop::GREATER_OR_EQUAL;
    case AST::Binop::POINTER_EQUAL: return IR_Binop::EQUAL;
    case AST::Binop::POINTER_NOT_EQUAL: return IR_Binop::NOT_EQUAL;
    case AST::Binop::INVALID: panic("Shouldn't happen"); return IR_Binop::ADDITION;
    default: panic("");
    }

    return IR_Binop::ADDITION;
}
