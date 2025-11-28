
#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "source_code.hpp"
#include "compiler_misc.hpp"
#include "semantic_analyser.hpp"
#include "symbol_table.hpp"
#include "../../datastructures/allocators.hpp"

namespace AST
{
    struct Node;
    struct Expression;
    struct Call_Node;
};
namespace Rich_Text
{
    struct Rich_Text;
};

struct Pattern_Variable;
struct Datatype_Format;
struct Type_System;
struct Call_Signature;
struct Modtree_Function;
struct Datatype_Struct;
struct Workload_Structure_Polymorphic;
struct Analysis_Pass;
struct Call_Info;
struct Symbol;



// Semantic_Info
enum class Semantic_Info_Type
{
    EXPRESSION_INFO,
    SYMBOL_LOOKUP,
    CALL_INFORMATION,
    ARGUMENT,
    MARKUP,
    ERROR_ITEM
};

struct Semantic_Info_Symbol {
    Symbol* symbol;
    bool is_definition;
    Analysis_Pass* pass;
    bool add_color;
};

struct Semantic_Info_Expression 
{
    AST::Expression* expr;
    Expression_Info* info;
    Analysis_Pass* analysis_pass;
    bool is_member_access;
    struct {
        Datatype* value_type; // Since the Editor cannot query Analysis-Pass, we store member-access infos here...
        bool has_definition;
        Text_Index definition_index;
        Compilation_Unit* member_definition_unit;
    } member_access_info;
};

struct Semantic_Info_Call 
{
    Call_Info* callable_call;
    AST::Call_Node* call_node;
    int test;
};

struct Semantic_Info_Argument {
    AST::Call_Node* call_node;
    int argument_index;
    int test;
};

union Semantic_Info_Option 
{
    Semantic_Info_Option() {};
    Semantic_Info_Expression expression;
    Semantic_Info_Symbol symbol_info;
    Semantic_Info_Argument argument_info;
    Semantic_Info_Call call_info;
    vec3 markup_color;
    int error_index;
};

struct Semantic_Info
{
    Semantic_Info_Type type;
    Semantic_Info_Option options;
    Analysis_Pass* pass;
    int analysis_item_index;
};

struct Call_Signature;

struct Compiler_Analysis_Data
{
    // Compiler data
    Dynamic_Array<Compiler_Error_Info> compiler_errors; // List of parser and semantic errors
    Constant_Pool constant_pool;
    Type_System type_system;
    Extern_Sources extern_sources;

    // Semantic analyser
    ModTree_Program* program;
    Symbol_Table* root_symbol_table;
    Upp_Module* builtin_module;
    Dynamic_Array<Function_Slot> function_slots;
    Dynamic_Array<Semantic_Error> semantic_errors;

    Hashtable<AST::Node*, Node_Passes> ast_to_pass_mapping;
    Hashtable<AST_Info_Key, Analysis_Info*> ast_to_info_mapping;
    Hashtable<AST::Expression*, Pattern_Variable*> pattern_variable_expression_mapping;

    // Workload executer
    Dynamic_Array<Workload_Base*> all_workloads;

    // Semantic_Info
    Dynamic_Array<Semantic_Info> semantic_infos;
    int next_analysis_item_index;

    // Call_Signatures and callables
    Hashset<Call_Signature*> call_signatures; // Callables get duplicated
    Call_Signature* hardcoded_function_signatures[(int)Hardcoded_Type::MAX_ENUM_VALUE];
    Call_Signature* context_change_type_signatures[(int)Custom_Operator_Type::MAX_ENUM_VALUE];
    Call_Signature* cast_signature;
    Call_Signature* empty_call_signature;

    // Allocations
    Arena arena;
    Dynamic_Array<Symbol_Table*> allocated_symbol_tables;
    Dynamic_Array<Symbol*> allocated_symbols;
    Dynamic_Array<Analysis_Pass*> allocated_passes;
    Dynamic_Array<Function_Progress*> allocated_function_progresses;
    Dynamic_Array<Custom_Operator_Table*> allocated_custom_operator_tables;
    Dynamic_Array<AST::Node*> allocated_nodes;
};

Compiler_Analysis_Data* compiler_analysis_data_create();
void compiler_analysis_data_destroy(Compiler_Analysis_Data* data);

void compiler_analysis_update_source_code_information();



// Note: Call_Signatures get deduplicated (Because function-pointer-types get deduplicated, so we need to do this anyway)
Call_Signature* call_signature_create_empty();
void call_signature_destroy(Call_Signature* signature);
// Note: Returned pointer is invalidated if another parameter is added
Call_Parameter* call_signature_add_parameter(
    Call_Signature* signature, String* name, Datatype* datatype, 
    bool required, bool requires_named_addressing, bool must_not_be_set);

Call_Parameter* call_signature_add_return_type(Call_Signature* signature, Datatype* datatype);
Call_Signature* call_signature_register(Call_Signature* signature); // Takes ownership of signature, returns deduplicated signature
void call_signature_append_to_rich_text(Call_Signature* signature, Rich_Text::Rich_Text* text, Datatype_Format* format, Type_System* type_system);
void call_signature_append_to_string(String* string, Type_System* type_system, Call_Signature* signature, Datatype_Format format);




