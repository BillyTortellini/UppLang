#include "rc_analyser.hpp"

#include "compiler_misc.hpp"
#include "compiler.hpp"
#include "semantic_analyser.hpp"
#include "ast.hpp"

// Globals
static Dependency_Analyser dependency_analyser;

// SYMBOL TABLE FUNCTIONS
Symbol_Table* symbol_table_create(Symbol_Table* parent, AST::Base* definition_node)
{
    auto& analyser = dependency_analyser;
    Symbol_Table* result = new Symbol_Table;
    dynamic_array_push_back(&analyser.allocated_symbol_tables, result);
    result->parent = parent;
    result->symbols = hashtable_create_pointer_empty<String*, Symbol*>(4);
    return result;
}

void symbol_destroy(Symbol* symbol) {
    dynamic_array_destroy(&symbol->references);
}

void symbol_table_destroy(Symbol_Table* symbol_table)
{
    auto it = hashtable_iterator_create(&symbol_table->symbols);
    while (hashtable_iterator_has_next(&it)) {
        Symbol* symbol = *it.value;
        symbol_destroy(symbol);
        delete symbol;
        hashtable_iterator_next(&it);
    }
    hashtable_destroy(&symbol_table->symbols);
    delete symbol_table;
}

Symbol* symbol_table_define_symbol(Symbol_Table* symbol_table, String* id, Symbol_Type type, AST::Base* definition_node)
{
    assert(id != 0, "HEY");

    // Check if already defined in same scope
    Symbol* found_symbol = symbol_table_find_symbol(symbol_table, id, false, 0);
    if (found_symbol != 0) {
        auto& analyser = dependency_analyser;
        dependency_analyser_log_error(found_symbol, definition_node);
        String temp_identifer = string_create_empty(128);
        SCOPE_EXIT(string_destroy(&temp_identifer));
        string_append_formated(&temp_identifer, "__temporary_%d", analyser.errors.size);
        id = identifier_pool_add(&analyser.compiler->identifier_pool, temp_identifer);
    }

    Symbol* new_sym = new Symbol;
    new_sym->definition_node = definition_node;
    new_sym->id = id;
    new_sym->type = type;
    new_sym->origin_table = symbol_table;
    new_sym->references = dynamic_array_create_empty<AST::Symbol_Read*>(1);
    new_sym->origin_item = dependency_analyser.analysis_item;

    hashtable_insert_element(&symbol_table->symbols, id, new_sym);
    return new_sym;
}

Symbol* symbol_table_find_symbol(Symbol_Table* table, String* id, bool only_current_scope, Symbol_Dependency* dependency)
{
    if (dependency != 0 && dependency->read->resolved_symbol != 0) {
        panic("Symbol already found, I dont know if this path has a use case");
        return dependency->read->resolved_symbol;
    }
    Symbol** found = hashtable_find_element(&table->symbols, id);
    if (found == 0) {
        if (!only_current_scope && table->parent != 0) {
            return symbol_table_find_symbol(table->parent, id, only_current_scope, dependency);
        }
        return nullptr;
    }

    // Variables/Parameters need special treatment since we have inner definitions that cannot 'see' outer function variables
    Symbol_Type sym_type = (*found)->type;
    if (dependency != 0 && dependency->item == (*found)->origin_item &&
        (sym_type == Symbol_Type::VARIABLE_UNDEFINED || sym_type == Symbol_Type::VARIABLE || sym_type == Symbol_Type::POLYMORPHIC_PARAMETER))
    {
        Analysis_Item* read_item = dependency->item;
        Analysis_Item* definition_item = (*found)->origin_item;
        if (!(definition_item->type == Analysis_Item_Type::FUNCTION && definition_item->options.function_body_item == read_item)) {
            return nullptr;
        }
    }
    if (dependency != 0) {
        dynamic_array_push_back(&((*found)->references), dependency->read);
    }
    return *found;
}

void symbol_append_to_string(Symbol* symbol, String* string)
{
    string_append_formated(string, "%s ", symbol->id->characters);
    if (symbol->type == Symbol_Type::UNRESOLVED) {
        string_append_formated(string, "Analysis not finished!");
        return;
    }
    switch (symbol->type)
    {
    case Symbol_Type::VARIABLE_UNDEFINED:
        if (symbol->options.variable_undefined.is_parameter) {
            string_append_formated(string, "Parameter Undefined (#%d)", symbol->options.variable_undefined.parameter_index);
        }
        else {
            string_append_formated(string, "Variable Undefined");
        }
        break;
    case Symbol_Type::UNRESOLVED:
        string_append_formated(string, "Unresolved");
        break;
    case Symbol_Type::POLYMORPHIC_PARAMETER:
        string_append_formated(string, "Polymorphic Parameter");
        break;
    case Symbol_Type::VARIABLE:
        string_append_formated(string, "Variable");
        break;
    case Symbol_Type::TYPE:
        string_append_formated(string, "Type");
        break;
    case Symbol_Type::ERROR_SYMBOL:
        string_append_formated(string, "Error");
        break;
    case Symbol_Type::SYMBOL_ALIAS:
        string_append_formated(string, "Alias for %s", symbol->options.alias->id);
        break;
    case Symbol_Type::CONSTANT_VALUE:
        string_append_formated(string, "Constant %d", symbol->options.constant.constant_index);
        break;
    case Symbol_Type::HARDCODED_FUNCTION:
        string_append_formated(string, "Hardcoded Function");
        break;
    case Symbol_Type::EXTERN_FUNCTION:
        string_append_formated(string, "Extern Function");
        break;
    case Symbol_Type::FUNCTION:
        string_append_formated(string, "Function");
        break;
    case Symbol_Type::MODULE:
        string_append_formated(string, "Module");
        break;
    default: panic("What");
    }
}

void symbol_table_append_to_string_with_parent_info(String* string, Symbol_Table* table, bool is_parent, bool print_root)
{
    if (!print_root && table->parent == 0) return;
    if (!is_parent) {
        string_append_formated(string, "Symbols: \n");
    }
    auto iter = hashtable_iterator_create(&table->symbols);
    while (hashtable_iterator_has_next(&iter))
    {
        Symbol* s = *iter.value;
        if (is_parent) {
            string_append_formated(string, "\t");
        }
        symbol_append_to_string(s, string);
        string_append_formated(string, "\n");
        hashtable_iterator_next(&iter);
    }
    if (table->parent != 0) {
        symbol_table_append_to_string_with_parent_info(string, table->parent, true, print_root);
    }
}

void symbol_table_append_to_string(String* string, Symbol_Table* table, bool print_root) {
    symbol_table_append_to_string_with_parent_info(string, table, false, print_root);
}


// DEPENDENCY ANALYSIS
Analysis_Item* analysis_item_create_empty(Analysis_Item_Type type, Analysis_Item* parent_item, AST::Base* node)
{
    auto& analyser = dependency_analyser;
    Analysis_Item* item = new Analysis_Item;
    item->symbol_dependencies = dynamic_array_create_empty<Symbol_Dependency>(1);
    item->type = type;
    item->node = node;
    item->symbol = 0;
    if (parent_item != 0) {
        Item_Dependency item_dependency;
        item_dependency.dependent = parent_item;
        item_dependency.depends_on = item;
        item_dependency.type = type == Analysis_Item_Type::STRUCTURE ? dependency_analyser.dependency_type : Dependency_Type::NORMAL;
        dynamic_array_push_back(&analyser.item_dependencies, item_dependency);
    }
    dynamic_array_push_back(&analyser.analysis_items, item);
    bool worked = hashtable_insert_element(&dependency_analyser.mapping_ast_to_items, node, item);
    assert(worked, "");
    return item;
}

void analysis_item_destroy(Analysis_Item* item)
{
    dynamic_array_destroy(&item->symbol_dependencies);
    delete item;
}

void string_set_indentation(String* string, int indentation)
{
    for (int i = 0; i < indentation; i++) {
        string_append_formated(string, "  ");
    }
}

void analysis_item_append_to_string(Analysis_Item* item, String* string, int indentation)
{
    string_set_indentation(string, indentation);
    switch (item->type)
    {
    case Analysis_Item_Type::ROOT:
        string_append_formated(string, "Root");
        break;
    case Analysis_Item_Type::DEFINITION:
        string_append_formated(string, "Symbol \"%s\" Definition", item->symbol->id->characters);
        break;
    case Analysis_Item_Type::FUNCTION:
        if (item->symbol != 0) {
            string_append_formated(string, "Symbol \"%s\", ", item->symbol->id->characters);
        }
        string_append_formated(string, "Function");
        break;
    case Analysis_Item_Type::FUNCTION_BODY:
        if (item->symbol != 0) {
            string_append_formated(string, "Symbol \"%s\", ", item->symbol->id->characters);
        }
        string_append_formated(string, "Body");
        break;
    case Analysis_Item_Type::STRUCTURE:
        if (item->symbol != 0) {
            string_append_formated(string, "Symbol \"%s\", ", item->symbol->id->characters);
        }
        string_append_formated(string, "Structure");
        break;
    default: panic("");
    }

    if (item->symbol_dependencies.size != 0) {
        string_append_formated(string, ": ", item->symbol_dependencies.size);
    }
    for (int i = 0; i < item->symbol_dependencies.size; i++)
    {
        Symbol_Dependency read = item->symbol_dependencies[i];
        //ast_identifier_node_append_to_string(string, read->identifier_node);
        switch (read.type)
        {
        case Dependency_Type::NORMAL: break;
        case Dependency_Type::MEMBER_IN_MEMORY: string_append_formated(string, "(Member_In_Memory)"); break;
        case Dependency_Type::MEMBER_REFERENCE: string_append_formated(string, "(Member_Reference)"); break;
        default: panic("");
        }
        if (i != item->symbol_dependencies.size - 1) {
            string_append_formated(string, ", ");
        }
    }
    string_append_formated(string, "\n");
}

void dependency_analyser_append_to_string(String* string) {
    auto& items = dependency_analyser.analysis_items;
    for (int i = 0; i < items.size; i++) {
        analysis_item_append_to_string(items[i], string, 0);
    }
}

void dependency_analyser_log_error(Symbol* existing_symbol, AST::Base* error_node)
{
    Symbol_Error error;
    error.error_node = error_node;
    error.existing_symbol = existing_symbol;
    dynamic_array_push_back(&dependency_analyser.errors, error);
}

void analyse_ast_base(AST::Base* base)
{
    using namespace AST;
    auto& analyser = dependency_analyser;
    Symbol_Table* _backup = dependency_analyser.symbol_table;
    SCOPE_EXIT(dependency_analyser.symbol_table = _backup;);
    Dependency_Type _backup_type = analyser.dependency_type;
    SCOPE_EXIT(analyser.dependency_type = _backup_type);
    Analysis_Item* _backup_item = analyser.analysis_item;
    SCOPE_EXIT(analyser.analysis_item = _backup_item);

    switch (base->type)
    {
    case Base_Type::MODULE: {
        auto module = (Module*)base;
        module->symbol_table = symbol_table_create(analyser.symbol_table, base);
        analyser.symbol_table = module->symbol_table;
        break;
    }
    case Base_Type::EXPRESSION:
    {
        auto expr = (Expression*)base;
        auto& dep_type = analyser.dependency_type;
        if (dep_type != Dependency_Type::NORMAL)
        {
            if (expr->type == Expression_Type::FUNCTION_SIGNATURE || expr->type == Expression_Type::SLICE_TYPE ||
                (expr->type == Expression_Type::UNARY_OPERATION && expr->options.unop.type == Unop::POINTER)) {
                dep_type = Dependency_Type::MEMBER_REFERENCE;
            }
            else if (!(expr->type == Expression_Type::SYMBOL_READ ||
                expr->type == Expression_Type::ARRAY_TYPE ||
                expr->type == Expression_Type::STRUCTURE_TYPE))
            {
                // Reset to normal if we don't have a type expression
                dep_type = Dependency_Type::NORMAL;
            }
        }

        // Special Expression handling
        switch (expr->type)
        {
        case Expression_Type::ARRAY_TYPE:
        {
            // Special case for array since here we must analyse size and type with different settings
            auto& array = expr->options.array_type;
            analyse_ast_base(&array.type_expr->base);
            dep_type = Dependency_Type::NORMAL;
            analyse_ast_base(&array.size_expr->base);
            return;
        }
        case Expression_Type::FUNCTION:
        {
            auto& function = expr->options.function;
            Analysis_Item* function_item = analysis_item_create_empty(Analysis_Item_Type::FUNCTION, analyser.analysis_item, base);
            function_item->options.function_body_item = analysis_item_create_empty(Analysis_Item_Type::FUNCTION_BODY, 0, &function.body->base);
            if (base->parent->type == Base_Type::DEFINITION) {
                auto def = (Definition*)base->parent;
                if (def->value.available && def->value.value == expr) {
                    function_item->symbol = def->symbol;
                    function_item->options.function_body_item->symbol = def->symbol;
                }
            }

            function.symbol_table = symbol_table_create(analyser.symbol_table, base);
            analyser.symbol_table = function.symbol_table;
            analyser.analysis_item = function_item;
            analyse_ast_base(&function.signature->base);

            analyser.analysis_item = function_item->options.function_body_item;
            analyse_ast_base(&function.body->base);
            return;
        }
        case Expression_Type::STRUCTURE_TYPE:
        { auto& structure = expr->options.structure;
        Analysis_Item* struct_item = analysis_item_create_empty(Analysis_Item_Type::STRUCTURE, analyser.analysis_item, base);
        analyser.analysis_item = struct_item;
        analyser.dependency_type = Dependency_Type::MEMBER_IN_MEMORY;
        if (base->parent->type == Base_Type::DEFINITION) {
            auto def = (Definition*)base->parent;
            if (def->value.available && def->value.value == expr) {
                struct_item->symbol = def->symbol;
            }
        }
        for (int i = 0; i < structure.members.size; i++)
        {
            auto& def = structure.members[i];
            def->symbol = 0;
            if (def->type.available) {
                analyser.dependency_type = Dependency_Type::MEMBER_IN_MEMORY;
                analyse_ast_base(&def->type.value->base);
            }
            if (def->value.available) {
                analyser.dependency_type = Dependency_Type::NORMAL;
                analyse_ast_base(&def->value.value->base);
            }
        }
        return;
        }
        case Expression_Type::BAKE_BLOCK:
        case Expression_Type::BAKE_EXPR:
        {
            Analysis_Item* bake_item = analysis_item_create_empty(Analysis_Item_Type::BAKE, analyser.analysis_item, base);
            analyser.analysis_item = bake_item;
            break;
        }
        }
        break;
    }
    case Base_Type::ARGUMENT: break;
    case Base_Type::STATEMENT: break;
    case Base_Type::CODE_BLOCK: {
        auto block = (Code_Block*)base;
        block->symbol_table = symbol_table_create(analyser.symbol_table, base);
        analyser.symbol_table = block->symbol_table;
        break;
    }
    case Base_Type::DEFINITION:
    {
        auto definition = (Definition*)base;
        definition->symbol = symbol_table_define_symbol(analyser.symbol_table, definition->name, Symbol_Type::UNRESOLVED, base);
        if (!definition->is_comptime && definition->base.parent->type == Base_Type::STATEMENT) {
            definition->symbol->type = Symbol_Type::VARIABLE_UNDEFINED;
            break;
        }
        if (definition->value.available && definition->is_comptime)
        {
            auto child = definition->value.value;
            if (child->type == Expression_Type::FUNCTION || child->type == Expression_Type::STRUCTURE_TYPE) {
                analyse_ast_base(&child->base);
                if (definition->type.available) {
                    analyse_ast_base(&definition->type.value->base);
                }
                return;
            }
        }

        Analysis_Item* item = analysis_item_create_empty(Analysis_Item_Type::DEFINITION, analyser.analysis_item, base);
        analyser.analysis_item = item;
        break;
    }
    case Base_Type::PARAMETER:
    {
        auto param = (Parameter*)base;
        param->symbol = symbol_table_define_symbol(analyser.symbol_table, param->name, Symbol_Type::VARIABLE_UNDEFINED, base);
        break;
    }
    case Base_Type::SYMBOL_READ:
    {
        auto symbol_read = (Symbol_Read*)base;
        Symbol_Dependency dep;
        dep.item = analyser.analysis_item;
        dep.read = symbol_read;
        dep.symbol_table = analyser.symbol_table;
        dep.type = analyser.dependency_type;
        dynamic_array_push_back(&analyser.analysis_item->symbol_dependencies, dep);
        break;
    }
    default: panic("");
    }

    // Iterate over children
    int index = 0;
    Base* child = base_get_child(base, index);
    while (child != 0)
    {
        analyse_ast_base(child);
        index += 1;
        child = base_get_child(base, index);
    }
}

Dependency_Analyser* dependency_analyser_initialize()
{
    dependency_analyser.errors = dynamic_array_create_empty<Symbol_Error>(16);
    dependency_analyser.allocated_symbol_tables = dynamic_array_create_empty<Symbol_Table*>(16);
    dependency_analyser.root_symbol_table = 0;
    dependency_analyser.compiler = 0;
    dependency_analyser.analysis_items = dynamic_array_create_empty<Analysis_Item*>(1);
    dependency_analyser.item_dependencies = dynamic_array_create_empty<Item_Dependency>(1);
    dependency_analyser.mapping_ast_to_items = hashtable_create_pointer_empty<AST::Base*, Analysis_Item*>(1);
    return &dependency_analyser;
}

void dependency_analyser_destroy()
{
    // Destroy results
    auto& analyser = dependency_analyser;
    dynamic_array_destroy(&analyser.errors);
    for (int i = 0; i < analyser.analysis_items.size; i++) {
        analysis_item_destroy(analyser.analysis_items[i]);
    }
    dynamic_array_destroy(&analyser.analysis_items);
    dynamic_array_destroy(&analyser.item_dependencies);
    hashtable_destroy(&dependency_analyser.mapping_ast_to_items);

    // Destroy allocations
    for (int i = 0; i < dependency_analyser.allocated_symbol_tables.size; i++) {
        symbol_table_destroy(dependency_analyser.allocated_symbol_tables[i]);
    }
    dynamic_array_destroy(&dependency_analyser.allocated_symbol_tables);
}

void dependency_analyser_reset(Compiler* compiler)
{
    // Reset results
    auto& analyser = dependency_analyser;
    dynamic_array_reset(&dependency_analyser.errors);
    for (int i = 0; i < analyser.analysis_items.size; i++) {
        analysis_item_destroy(analyser.analysis_items[i]);
    }
    dynamic_array_reset(&analyser.analysis_items);
    dynamic_array_reset(&analyser.item_dependencies);

    // Reset allocations
    for (int i = 0; i < dependency_analyser.allocated_symbol_tables.size; i++) {
        symbol_table_destroy(dependency_analyser.allocated_symbol_tables[i]);
    }
    dynamic_array_reset(&dependency_analyser.allocated_symbol_tables);

    dependency_analyser.compiler = compiler;
    dependency_analyser.dependency_type = Dependency_Type::NORMAL;
    dependency_analyser.root_symbol_table = symbol_table_create(0, 0);
    dependency_analyser.analysis_item = 0;
    dependency_analyser.symbol_table = dependency_analyser.root_symbol_table;
    // Set predefined symbols
    {
        String* id_int = identifier_pool_add(&compiler->identifier_pool, string_create_static("int"));
        String* id_bool = identifier_pool_add(&compiler->identifier_pool, string_create_static("bool"));
        String* id_float = identifier_pool_add(&compiler->identifier_pool, string_create_static("float"));
        String* id_u8 = identifier_pool_add(&compiler->identifier_pool, string_create_static("u8"));
        String* id_u16 = identifier_pool_add(&compiler->identifier_pool, string_create_static("u16"));
        String* id_u32 = identifier_pool_add(&compiler->identifier_pool, string_create_static("u32"));
        String* id_u64 = identifier_pool_add(&compiler->identifier_pool, string_create_static("u64"));
        String* id_i8 = identifier_pool_add(&compiler->identifier_pool, string_create_static("i8"));
        String* id_i16 = identifier_pool_add(&compiler->identifier_pool, string_create_static("i16"));
        String* id_i32 = identifier_pool_add(&compiler->identifier_pool, string_create_static("i32"));
        String* id_i64 = identifier_pool_add(&compiler->identifier_pool, string_create_static("i64"));
        String* id_f64 = identifier_pool_add(&compiler->identifier_pool, string_create_static("f64"));
        String* id_f32 = identifier_pool_add(&compiler->identifier_pool, string_create_static("f32"));
        String* id_byte = identifier_pool_add(&compiler->identifier_pool, string_create_static("byte"));
        String* id_void = identifier_pool_add(&compiler->identifier_pool, string_create_static("void"));
        String* id_string = identifier_pool_add(&compiler->identifier_pool, string_create_static("String"));
        String* id_type = identifier_pool_add(&compiler->identifier_pool, string_create_static("Type"));
        String* id_type_information = identifier_pool_add(&compiler->identifier_pool, string_create_static("Type_Information"));
        String* id_any = identifier_pool_add(&compiler->identifier_pool, string_create_static("Any"));
        String* id_empty = identifier_pool_add(&compiler->identifier_pool, string_create_static("_"));
        // This placeholder can never be an identifier, becuase it starts with a number
        String* id_error = identifier_pool_add(&compiler->identifier_pool, string_create_static("0_ERROR_SYMBOL"));

        auto& analyser = dependency_analyser;
        auto& root = analyser.root_symbol_table;
        auto& pool = analyser.compiler->identifier_pool;
        auto& predef = analyser.predefined_symbols;
        predef.error_symbol = symbol_table_define_symbol(root, id_error, Symbol_Type::ERROR_SYMBOL, 0);
        predef.type_bool = symbol_table_define_symbol(root, id_bool, Symbol_Type::UNRESOLVED, 0);
        predef.type_int = symbol_table_define_symbol(root, id_int, Symbol_Type::UNRESOLVED, 0);
        predef.type_float = symbol_table_define_symbol(root, id_float, Symbol_Type::UNRESOLVED, 0);
        predef.type_u8 = symbol_table_define_symbol(root, id_u8, Symbol_Type::UNRESOLVED, 0);
        predef.type_u16 = symbol_table_define_symbol(root, id_u16, Symbol_Type::UNRESOLVED, 0);
        predef.type_u32 = symbol_table_define_symbol(root, id_u32, Symbol_Type::UNRESOLVED, 0);
        predef.type_u64 = symbol_table_define_symbol(root, id_u64, Symbol_Type::UNRESOLVED, 0);
        predef.type_i8 = symbol_table_define_symbol(root, id_i8, Symbol_Type::UNRESOLVED, 0);
        predef.type_i16 = symbol_table_define_symbol(root, id_i16, Symbol_Type::UNRESOLVED, 0);
        predef.type_i32 = symbol_table_define_symbol(root, id_i32, Symbol_Type::UNRESOLVED, 0);
        predef.type_i64 = symbol_table_define_symbol(root, id_i64, Symbol_Type::UNRESOLVED, 0);
        predef.type_f32 = symbol_table_define_symbol(root, id_f32, Symbol_Type::UNRESOLVED, 0);
        predef.type_f64 = symbol_table_define_symbol(root, id_f64, Symbol_Type::UNRESOLVED, 0);
        predef.type_byte = symbol_table_define_symbol(root, id_byte, Symbol_Type::UNRESOLVED, 0);
        predef.type_void = symbol_table_define_symbol(root, id_void, Symbol_Type::UNRESOLVED, 0);
        predef.type_string = symbol_table_define_symbol(root, id_string, Symbol_Type::UNRESOLVED, 0);
        predef.type_type = symbol_table_define_symbol(root, id_type, Symbol_Type::UNRESOLVED, 0);
        predef.type_type_information = symbol_table_define_symbol(root, id_type_information, Symbol_Type::UNRESOLVED, 0);
        predef.type_any = symbol_table_define_symbol(root, id_any, Symbol_Type::UNRESOLVED, 0);
        predef.type_empty = symbol_table_define_symbol(root, id_empty, Symbol_Type::UNRESOLVED, 0);
#define POOL_ADD(x) (identifier_pool_add(&pool, string_create_static(x)))
        predef.hardcoded_print_bool = symbol_table_define_symbol(root, POOL_ADD("print_bool"), Symbol_Type::UNRESOLVED, 0);
        predef.hardcoded_print_i32 = symbol_table_define_symbol(root, POOL_ADD("print_i32"), Symbol_Type::UNRESOLVED, 0);
        predef.hardcoded_print_f32 = symbol_table_define_symbol(root, POOL_ADD("print_f32"), Symbol_Type::UNRESOLVED, 0);
        predef.hardcoded_print_string = symbol_table_define_symbol(root, POOL_ADD("print_string"), Symbol_Type::UNRESOLVED, 0);
        predef.hardcoded_print_line = symbol_table_define_symbol(root, POOL_ADD("print_line"), Symbol_Type::UNRESOLVED, 0);
        predef.hardcoded_read_i32 = symbol_table_define_symbol(root, POOL_ADD("read_i32"), Symbol_Type::UNRESOLVED, 0);
        predef.hardcoded_read_f32 = symbol_table_define_symbol(root, POOL_ADD("read_f32"), Symbol_Type::UNRESOLVED, 0);
        predef.hardcoded_read_bool = symbol_table_define_symbol(root, POOL_ADD("read_bool"), Symbol_Type::UNRESOLVED, 0);
        predef.hardcoded_random_i32 = symbol_table_define_symbol(root, POOL_ADD("random_i32"), Symbol_Type::UNRESOLVED, 0);
        predef.function_assert = symbol_table_define_symbol(root, POOL_ADD("assert"), Symbol_Type::UNRESOLVED, 0);
        predef.global_type_informations = symbol_table_define_symbol(root, POOL_ADD("type_informations"), Symbol_Type::UNRESOLVED, 0);
#undef POOL_ADD
    }
}

void dependency_analyser_analyse(AST::Module* root_module)
{
    auto analyser = dependency_analyser;
    analyser.dependency_type = Dependency_Type::NORMAL;
    analyser.symbol_table = analyser.root_symbol_table;
    analyser.analysis_item = analysis_item_create_empty(Analysis_Item_Type::ROOT, 0, &root_module->base);
    analyse_ast_base(&root_module->base);
}


/* OLD EXTERN IMPORTS
void analyse_definitions(AST_Node* definitions_node)
{
case AST_Node_Type::EXTERN_FUNCTION_DECLARATION:
    {
        // Create Workload
        Analysis_Workload workload;
        workload.type = Analysis_Workload_Type::EXTERN_FUNCTION_DECLARATION;
        workload.options.extern_function.node = top_level_node;
        workload.options.extern_function.parent_module = module;
        dynamic_array_push_back(&analyser->active_workloads, workload);
        break;
    }
    break;
case AST_Node_Type::EXTERN_HEADER_IMPORT:
{
    // Create Workload
    Analysis_Workload workload;
    workload.type = Analysis_Workload_Type::EXTERN_HEADER_IMPORT;
    workload.options.extern_header.parent_module = module;
    workload.options.extern_header.node = top_level_node;
    dynamic_array_push_back(&analyser->active_workloads, workload);
    break;
}
    break;
case AST_Node_Type::LOAD_FILE:
{
    if (hashset_contains(&analyser->loaded_filenames, top_level_node->id)) {
        break;
    }
    hashset_insert_element(&analyser->loaded_filenames, top_level_node->id);

    Optional<String> file_content = file_io_load_text_file(top_level_node->id->characters);
    if (file_content.available)
    {
        String content = file_content.value;
        Code_Origin origin;
        origin.type = Code_Origin_Type::LOADED_FILE;
        origin.load_node = top_level_node;
        compiler_add_source_code(analyser->compiler, content, origin);
    }
    else {
        Semantic_Error error;
        error.type = Semantic_Error_Type::OTHERS_COULD_NOT_LOAD_FILE;
        error.error_node = top_level_node;
        semantic_analyser_log_error(analyser, error);
    }
    break;
}
    break;
case AST_Node_Type::EXTERN_LIB_IMPORT:
    {
        dynamic_array_push_back(&analyser->compiler->extern_sources.lib_files, top_level_node->id);
        break;
    }
    break;
default: panic("HEy");
}
*/


