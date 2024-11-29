#include "c_backend.hpp"

#include "compiler.hpp"
#include "../../utility/file_io.hpp"
#include <cstdlib>
#include <Windows.h>
#include "../../win32/windows_helper_functions.hpp"
#include "../../utility/hash_functions.hpp"
#include "semantic_analyser.hpp"
#include "../../win32/process.hpp"
#include "ir_code.hpp"
#include "symbol_table.hpp"
#include "editor_analysis_info.hpp"

// --------------
// - C_COMPILER -
// --------------

struct C_Compiler
{
    bool initialized;
    bool last_compile_successfull;
};

static C_Compiler c_compiler;

C_Compiler* c_compiler_initialize()
{
    C_Compiler& result = c_compiler;
    result.initialized = true;
    result.last_compile_successfull = false;

    // Load system vars (Required to use cl.exe and link.exe)
    {
        Optional<String> file_content = file_io_load_text_file("backend/misc/env_vars.txt");
        SCOPE_EXIT(file_io_unload_text_file(&file_content));
        String env_var = string_create_empty(64);
        String var_value = string_create_empty(64);
        SCOPE_EXIT(string_destroy(&env_var));
        SCOPE_EXIT(string_destroy(&var_value));

        if (file_content.available)
        {
            String string = file_content.value;
            // Each line_index contains a base_name, and is separated by \n
            int i = 0;
            bool parsing_name = true;
            while (i < string.size)
            {
                SCOPE_EXIT(i++);
                char c = string.characters[i];
                if (parsing_name) 
                {
                    if (c == '=') {
                        parsing_name = false;
                    }
                    else {
                        string_append_character(&env_var, c);
                    }
                }
                else {
                    if (c == '\r') continue;
                    if (c == '\n') {
                        //printf("Var base_name: %s = %s\n", env_var.characters, var_value.characters);
                        SetEnvironmentVariableA(env_var.characters, var_value.characters);
                        string_reset(&env_var);
                        string_reset(&var_value);
                        parsing_name = true;
                    }
                    else {
                        string_append_character(&var_value, c);
                    }
                }
            }
        }
        else {
            result.initialized = false;
        }
    }

    return &result;
}

void c_compiler_shutdown()
{
}

void c_compiler_compile()
{
    auto& comp = c_compiler;
    comp.last_compile_successfull = false;

    if (!comp.initialized) {
        logg("Compiler initialization failed!\n");
        return;
    }

    // Create compilation command
    String command = string_create_empty(128);
    SCOPE_EXIT(string_destroy(&command));
    {
        // Note: MDd should be replaced between debug and optimized build
        const char* compiler_options = "/MDd /EHsc /Zi /Fdbackend/build /Fobackend/build/ /std:c++latest"; // /std:c++latest is used for designated struct inititalizers
        const char* linker_options = "/OUT:backend/build/main.exe /PDB:backend/build/main.pdb";
        string_append_formated(&command, "\"cl\" ");
        string_append_formated(&command, compiler_options);

        auto& extern_sources = compiler.analysis_data->extern_sources;
        // Defines
        Dynamic_Array<String*> defines = extern_sources.compiler_settings[(int)Extern_Compiler_Setting::DEFINITION];
        for (int i = 0; i < defines.size; i++) {
            string_append_formated(&command, " /D \"%s\"", defines[i]->characters);
        }

        // Include directories
        Dynamic_Array<String*> includes = extern_sources.compiler_settings[(int)Extern_Compiler_Setting::INCLUDE_DIRECTORY];
        for (int i = 0; i < includes.size; i++) {
            string_append_formated(&command, " /I \"%s\"", includes[i]->characters);
        }

        // Forced includes (Header files)
        Dynamic_Array<String*> header_files = extern_sources.compiler_settings[(int)Extern_Compiler_Setting::HEADER_FILE];
        for (int i = 0; i < header_files.size; i++) {
            string_append_formated(&command, " /FI \"%s\"", header_files[i]->characters);
        }

        // Source files
        string_append(&command, " backend/src/main.cpp backend/hardcoded/hardcoded_functions.cpp");
        Dynamic_Array<String*> source_files = extern_sources.compiler_settings[(int)Extern_Compiler_Setting::SOURCE_FILE];
        for (int i = 0; i < source_files.size; i++) {
            string_append_formated(&command, " \"%s\"", source_files[i]->characters);
        }

        // LINKER
        string_append_formated(&command, " /link %s", linker_options);

        // Library directories
        auto lib_dirs = extern_sources.compiler_settings[(int)Extern_Compiler_Setting::LIBRARY_DIRECTORY];
        for (int i = 0; i < lib_dirs.size; i++) {
            string_append_formated(&command, " /LIBPATH:\"%s\"", lib_dirs[i]->characters);
        }

        // Libraries 
        Dynamic_Array<String*> lib_files = extern_sources.compiler_settings[(int)Extern_Compiler_Setting::LIBRARY];
        for (int i = 0; i < lib_files.size; i++) {
            string_append_formated(&command, " \"%s\"", lib_files[i]->characters);
        }

    }

    logg("Compile command:\n%s\n", command.characters);
    Optional<Process_Result> result = process_start(command);
    SCOPE_EXIT(process_result_destroy(&result));
    if (result.available) {
        comp.last_compile_successfull = result.value.exit_code == 0;
        logg("Compiler output: \n--------------\n%s\n", result.value.output.characters);
    }
    else {
        comp.last_compile_successfull = false;
    }
    return;
}

Exit_Code c_compiler_execute()
{
    auto& comp = c_compiler;
    if (!comp.initialized) {
        return exit_code_make(Exit_Code_Type::COMPILATION_FAILED, "Compiler not initialized");
    }
    if (!comp.last_compile_successfull) {
        logg("C_Compiler did not execute, since last compile was not successfull");
        return exit_code_make(Exit_Code_Type::COMPILATION_FAILED, "Last compilation was not successfull");
    }

    int exit_code_value = process_start_no_pipes(string_create_static("backend/build/main.exe"), true);
    if (exit_code_value < 0 || exit_code_value >= (int)Exit_Code_Type::MAX_ENUM_VALUE) {
        return exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Exit code value from program execution was invalid");
    }

    return exit_code_make((Exit_Code_Type) exit_code_value);
}



// ---------------
// - C_GENERATOR -
// ---------------
struct Compiler;
struct IR_Program;
struct Datatype;
struct IR_Function;
struct IR_Code_Block;
struct Upp_Constant;
struct ModTree_Function;

enum class C_Translation_Type
{
    FUNCTION,
    DATATYPE,
    REGISTER,
    CONSTANT
};

struct C_Translation
{
    C_Translation_Type type;
    union {
        int function_slot_index;
        struct {
            IR_Code_Block* code_block;
            int index;
        } register_translation;
        Datatype* datatype;
        struct {
            int index; // Index in constant pool
            bool requires_memory_address; 
        } constant;
    } options;
};

bool c_translation_is_equal(C_Translation* ap, C_Translation* bp) 
{
    auto& a = *ap;
    auto& b = *bp;
    if (a.type != b.type) return false;
    switch (a.type)
    {
    case C_Translation_Type::REGISTER: 
        return a.options.register_translation.code_block == b.options.register_translation.code_block &&
            a.options.register_translation.index == b.options.register_translation.index;
    case C_Translation_Type::DATATYPE:
        return types_are_equal(a.options.datatype, b.options.datatype);
    case C_Translation_Type::FUNCTION:
        return a.options.function_slot_index == b.options.function_slot_index;
    case C_Translation_Type::CONSTANT:
        return a.options.constant.index == b.options.constant.index && 
            a.options.constant.requires_memory_address == b.options.constant.requires_memory_address;
    default: panic("");
    }
    return false;
}

u64 c_translation_hash(C_Translation* tp)
{
    C_Translation& t = *tp;

    i32 type_value = (i32)t.type;
    u64 hash = hash_i32(&type_value);
    switch (t.type)
    {
    case C_Translation_Type::REGISTER: 
        hash = hash_combine(hash, hash_pointer(t.options.register_translation.code_block));
        hash = hash_combine(hash, hash_i32(&t.options.register_translation.index));
        break;
    case C_Translation_Type::DATATYPE:
        hash = hash_combine(hash, hash_pointer(t.options.datatype));
        break;
    case C_Translation_Type::FUNCTION:
        hash = hash_combine(hash, hash_i32(&t.options.function_slot_index));
        break;
    case C_Translation_Type::CONSTANT:
        hash = hash_combine(hash, hash_i32(&t.options.constant.index));
        hash = hash_bool(hash, t.options.constant.requires_memory_address);
        break;
    default: panic("");
    }
    return hash;
}



struct C_Type_Dependency
{
    Dynamic_Array<C_Type_Dependency*> depends_on;
    Dynamic_Array<C_Type_Dependency*> dependees;
    String type_definition; // e.g. struct A {int value; B b;} 
    Datatype* signature;
    int dependency_count;
};

void type_dependency_destroy(C_Type_Dependency** dep_ptr)
{
    C_Type_Dependency* dependency = *dep_ptr;
    dynamic_array_destroy(&dependency->dependees);
    dynamic_array_destroy(&dependency->depends_on);
    string_destroy(&dependency->type_definition);
    delete dependency;
}



enum class Generator_Section
{
    ENUM_DECLARATIONS, // Enums
    STRUCT_PROTOTYPES,
    TYPE_DECLARATIONS, // Slices, typedefs
    STRUCT_AND_ARRAY_DECLARATIONS, // Structs and arrays, as these types may have dependencies on each other (e.g. order of declaration is important)
    CONSTANT_ARRAY_HOLDERS, // Need to be after all structs and arrays...
    CONSTANTS,
    GLOBALS,
    FUNCTION_PROTOTYPES, // Required so that functions can call all other functions even when declared later
    FUNCTION_IMPLEMENTATION,

    MAX_ENUM_VALUE,
};

struct C_Generator
{
    String sections[(int)Generator_Section::MAX_ENUM_VALUE];
    String* text; // Current text that's being worked on
    int name_counter;

    Hashtable<Datatype*, C_Type_Dependency*> type_to_dependency_mapping;
    Dynamic_Array<C_Type_Dependency*> type_dependencies;

    Hashtable<C_Translation, String> translations;
    Upp_Constant global_type_informations;
};

static C_Generator c_generator;


C_Generator* c_generator_initialize()
{
    C_Generator& result = c_generator;
    for (int i = 0; i < (int)Generator_Section::MAX_ENUM_VALUE; i++) {
        result.sections[i] = string_create_empty(256);
    }
    result.translations = hashtable_create_empty<C_Translation, String>(64, c_translation_hash, c_translation_is_equal);
    result.type_dependencies = dynamic_array_create<C_Type_Dependency*>();
    result.type_to_dependency_mapping = hashtable_create_pointer_empty<Datatype*, C_Type_Dependency*>(32);
    result.name_counter = 0;
    result.text = 0;

    return &result;
}

void c_generator_shutdown()
{
    auto& gen = c_generator;
    for (int i = 0; i < (int)Generator_Section::MAX_ENUM_VALUE; i++) {
        string_destroy(&gen.sections[i]);
    }
    hashtable_destroy(&gen.translations);
    hashtable_destroy(&gen.type_to_dependency_mapping);
    for (int i = 0; i < gen.type_dependencies.size; i++) {
        type_dependency_destroy(&gen.type_dependencies[i]);
    }
    dynamic_array_destroy(&gen.type_dependencies);
}



// IMPLEMENTATION
void c_generator_output_type_reference(Datatype* type);
void c_generator_output_code_block(IR_Code_Block* code_block, int indentation_level, bool registers_in_same_scope);
void c_generator_output_data_access(IR_Data_Access* access, bool add_parenthesis_on_pointer_ops = false);
C_Type_Dependency* get_type_dependency(Datatype* datatype);
void c_generator_output_constant_access(Upp_Constant& constant, bool requires_memory_address, int indentation_level);
void output_memory_as_new_constant(byte* base_memory, Datatype* base_type, bool requires_memory_address, int current_indentation_level);


void string_add_indentation(String* str, int indentation)
{
    for (int i = 0; i < indentation; i++) {
        string_append_formated(str, "    ");
    }
}

void c_generator_generate_struct_content(Struct_Content* content, C_Type_Dependency* type_dependency, int indentation_level)
{
    auto& gen = c_generator;
    String* backup_text = gen.text;
    SCOPE_EXIT(gen.text = backup_text);
    gen.text = &type_dependency->type_definition;

    // Generate members + add potential dependencies
    for (int i = 0; i < content->members.size; i++) 
    {
        auto& member = content->members[i];
        
        string_add_indentation(gen.text, indentation_level);
        c_generator_output_type_reference(member.type);
        string_append_formated(gen.text, " %s;\n", member.id->characters);

        // Add dependencies if necessary
        auto member_type = datatype_get_non_const_type(member.type);
        if (member_type->type == Datatype_Type::SUBTYPE || member_type->type == Datatype_Type::STRUCT || member_type->type == Datatype_Type::ARRAY || member_type->type == Datatype_Type::OPTIONAL_TYPE) 
        {
            member_type = member_type->base_type;
            C_Type_Dependency* member_dependency = get_type_dependency(member_type);
            dynamic_array_push_back(&type_dependency->depends_on, member_dependency);
            dynamic_array_push_back(&member_dependency->dependees, type_dependency);
            type_dependency->dependency_count += 1;
        }
    }

    // Generate subtypes + tag if existing
    if (content->subtypes.size > 0) 
    {
        string_add_indentation(gen.text, indentation_level);
        string_append(gen.text, "union {\n");
        for (int i = 0; i < content->subtypes.size; i++) 
        {
            Struct_Content* child_content = content->subtypes[i];
            string_add_indentation(gen.text, indentation_level + 1);
            string_append(gen.text, "struct {\n");
            c_generator_generate_struct_content(child_content, type_dependency, indentation_level + 2);
            string_add_indentation(gen.text, indentation_level + 1);
            string_append_formated(gen.text, "} %s;", child_content->name->characters);
            string_append(gen.text, "\n");
        }
        string_add_indentation(gen.text, indentation_level);
        string_append(gen.text, "} subtypes_;\n");

        // Add tag member
        string_add_indentation(gen.text, indentation_level);
        c_generator_output_type_reference(content->tag_member.type);
        string_append(gen.text, " tag_;\n");
    }
}

C_Type_Dependency* get_type_dependency(Datatype* datatype)
{
    auto& gen = c_generator;
    // Check if the dependency already exists
    {
        C_Type_Dependency** existing = hashtable_find_element(&gen.type_to_dependency_mapping, datatype);
        if (existing != 0) {
            return *existing;
        }
    }

    C_Type_Dependency* dep = new C_Type_Dependency;
    dep->dependees = dynamic_array_create<C_Type_Dependency*>();
    dep->depends_on = dynamic_array_create<C_Type_Dependency*>();
    dep->dependency_count = 0;
    dep->signature = datatype;
    dep->type_definition = string_create_empty(32);

    dynamic_array_push_back(&gen.type_dependencies, dep);
    hashtable_insert_element(&gen.type_to_dependency_mapping, datatype, dep);
    return dep;
}

void c_generator_output_type_reference(Datatype* type)
{
    auto& gen = c_generator;

    // Check if datatype was already created
    C_Translation translation;
    translation.type = C_Translation_Type::DATATYPE;
    translation.options.datatype = type;
    {
        String* translated = hashtable_find_element(&gen.translations, translation);
        if (translated != 0) {
            string_append(gen.text, translated->characters);
            return;
        }
    }

    // Otherwise generate the type reference
    String constant_string;
    constant_string.size = 0;
    constant_string.capacity = 0;
    SCOPE_EXIT(if (constant_string.capacity != 0) string_destroy(&constant_string));

    String access_name = string_create_empty(32);
    String* backup_text = gen.text;
    SCOPE_EXIT(gen.text = backup_text);

    // Handle pointers and const combinations
    if (type->mods.pointer_level > 0 || type->mods.constant_flags != 0 || type->mods.is_constant)
    {
        // In C, it makes a difference where the const is places...
        // e.g. const int* is not the same as int* const 

        // Get base-type 
        Type_Mods mods = type->mods;
        type = type->base_type;

        // Append type
        gen.text = &access_name;
        if (mods.is_constant) {
            string_append(gen.text, "const ");
        }
        c_generator_output_type_reference(type);
        for (int i = 0; i < mods.pointer_level; i++) {
            string_append(gen.text, "*");
            if (type_mods_pointer_is_constant(mods, i)) {
                string_append(gen.text, " const");
            }
        }

        hashtable_insert_element(&gen.translations, translation, access_name);
        gen.text = backup_text;
        string_append(gen.text, access_name.characters);
        return;
    }

    switch (type->type)
    {
    case Datatype_Type::PRIMITIVE:
    {
        auto primitive = downcast<Datatype_Primitive>(type);
        int type_size = type->memory_info.value.size;
        if (primitive->is_c_char) {
            string_append(&access_name, "char");
            break;
        }

        switch (primitive->primitive_type)
        {
        case Primitive_Type::BOOLEAN:
            string_append(&access_name, "bool");
            break;
        case Primitive_Type::INTEGER:
            switch (type_size)
            {
            case 1: string_append(&access_name, primitive->is_signed ? "i8" : "u8"); break;
            case 2: string_append(&access_name, primitive->is_signed ? "i16" : "u16"); break;
            case 4: string_append(&access_name, primitive->is_signed ? "i32" : "u32"); break;
            case 8: string_append(&access_name, primitive->is_signed ? "i64" : "u64"); break;
            default: panic("HEY");
            }

            break;
        case Primitive_Type::FLOAT:
            switch (type_size)
            {
            case 4: string_append(&access_name, "f32"); break;
            case 8: string_append(&access_name, "f64"); break;
            default: panic("HEY");
            }
            break;
        default: panic("What");
        }
        break;
    }
    case Datatype_Type::TYPE_HANDLE: {
        string_append_formated(&access_name, "Type_Handle_"); // See hardcoded_functions.h for definition
        break;
    }
    case Datatype_Type::STRUCT_INSTANCE_TEMPLATE:
    case Datatype_Type::TEMPLATE_TYPE:
    case Datatype_Type::UNKNOWN_TYPE:
    {
        string_append_formated(&access_name, "UNUSED_TYPE_BACKEND_"); // See hardcoded_functions.h for definition
        break;
    }
    case Datatype_Type::ENUM: 
    {
        auto enum_type = downcast<Datatype_Enum>(type);
        auto& members = enum_type->members;
        string_append_formated(&access_name, "%s_Enum_%d", enum_type->name->characters, gen.name_counter);
        gen.name_counter++;

        String* enum_section = &gen.sections[(int)Generator_Section::ENUM_DECLARATIONS];
        string_append_formated(enum_section, "enum class %s\n{\n", access_name.characters);
        for (int i = 0; i < members.size; i++) {
            auto member = &members[i];
            string_append_formated(enum_section, "    %s = %d,\n", member->name->characters, member->value);
        }
        string_append_formated(enum_section, "};\n");
        break;
    }
    case Datatype_Type::BYTE_POINTER: {
        string_append_formated(&access_name, "byte_pointer_"); // Defined in hardcoded_functions.h as void*
        break;
    }
    case Datatype_Type::SLICE: 
    {
        auto slice_type = downcast<Datatype_Slice>(type);
        string_append_formated(&access_name, "Slice_%d", gen.name_counter);
        gen.name_counter++;

        String* section_prototypes = &gen.sections[(int)Generator_Section::STRUCT_PROTOTYPES];
        String* section_structs = &gen.sections[(int)Generator_Section::STRUCT_AND_ARRAY_DECLARATIONS];
        string_append_formated(section_prototypes, "struct %s;\n", access_name.characters);

        // Temporary c_string is required when calling this function recursively
        String tmp = string_create_empty(32);
        SCOPE_EXIT(string_destroy(&tmp));

        gen.text = &tmp;
        string_append_formated(gen.text, "struct %s {\n    ", access_name.characters);
        c_generator_output_type_reference(slice_type->data_member.type);
        string_append_formated(gen.text, " data;\n    u64 size;\n};\n\n");

        // Now we write to struct section
        gen.text = section_structs;
        string_append(gen.text, tmp.characters);

        break;
    }
    case Datatype_Type::FUNCTION: 
    {
        auto function = downcast<Datatype_Function>(type);
        auto& parameters = function->parameters;
        string_append_formated(&access_name, "fptr_%d", gen.name_counter);
        gen.name_counter++;

        // Temporary c_string is required when calling this function recursively
        String tmp = string_create_empty(32);
        SCOPE_EXIT(string_destroy(&tmp));

        gen.text = &tmp;
        string_append(gen.text, "typedef ");
        if (function->return_type.available) {
            c_generator_output_type_reference(function->return_type.value);
        }
        else {
            string_append(gen.text, "void");
        }

        string_append_formated(gen.text, " (*%s)(", access_name.characters);
        for (int i = 0; i < parameters.size; i++) {
            auto& param = parameters[i];
            c_generator_output_type_reference(param.type);
            string_append_formated(gen.text, " %s", param.name->characters);
            if (i != parameters.size - 1) {
                string_append_formated(gen.text, ", ");
            }
        }
        string_append_formated(gen.text, ");\n\n");

        gen.text = &gen.sections[(int)Generator_Section::TYPE_DECLARATIONS];
        string_append(gen.text, tmp.characters);
        break;
    }
    case Datatype_Type::OPTIONAL_TYPE:
    {
        auto opt = downcast<Datatype_Optional>(type);
        string_append_formated(&access_name, "Optional_%d", gen.name_counter);
        gen.name_counter += 1;
        string_append_formated(&gen.sections[(int)Generator_Section::STRUCT_PROTOTYPES], "struct %s;\n", access_name.characters);

        C_Type_Dependency* dependency = get_type_dependency(type);
        gen.text = &dependency->type_definition;
        string_append_formated(gen.text, "struct %s {\n    ", access_name.characters);
        c_generator_output_type_reference(opt->child_type);
        string_append(gen.text, " value;\n    bool is_available;\n};\n");

        auto member_type = datatype_get_non_const_type(opt->child_type);
        if (member_type->type == Datatype_Type::SUBTYPE || member_type->type == Datatype_Type::STRUCT || member_type->type == Datatype_Type::ARRAY || member_type->type == Datatype_Type::OPTIONAL_TYPE) 
        {
            member_type = member_type->base_type;
            C_Type_Dependency* member_dependency = get_type_dependency(member_type);
            dynamic_array_push_back(&dependency->depends_on, member_dependency);
            dynamic_array_push_back(&member_dependency->dependees, dependency);
            dependency->dependency_count += 1;
        }

        break;
    }
    case Datatype_Type::STRUCT:
    {
        auto structure = downcast<Datatype_Struct>(type);
        auto& members = structure->content.members;

        // Extern structs should be accessible by name alone (And forward definition/definition should be in an included header)
        if (structure->is_extern_struct) {
            string_append_formated(&access_name, structure->content.name->characters);
            break;
        }

        if (structure->struct_type == AST::Structure_Type::STRUCT) {
            string_append_formated(&access_name, "%s_Struct_%d", structure->content.name->characters, gen.name_counter);
        }
        else {
            string_append_formated(&access_name, "%s_Union_%d", structure->content.name->characters, gen.name_counter);
        }
        gen.name_counter += 1;

        // Because structs can contain references to themselves, we need to register the access name before generating the members
        hashtable_insert_element(&gen.translations, translation, access_name);
        if (structure->struct_type == AST::Structure_Type::STRUCT) {
            string_append_formated(&gen.sections[(int)Generator_Section::STRUCT_PROTOTYPES], "struct %s;\n", access_name.characters);
        }
        else {
            string_append_formated(&gen.sections[(int)Generator_Section::STRUCT_PROTOTYPES], "union %s;\n", access_name.characters);
        }

        // Generate struct content
        C_Type_Dependency* dependency = get_type_dependency(type);
        gen.text = &dependency->type_definition;
        string_append(gen.text, structure->struct_type == AST::Structure_Type::STRUCT ? "struct " : "union ");

        // Handle extern structs (No members, but size and alignment can be different
        if (structure->content.members.size == 0 && structure->content.subtypes.size == 0) {
            string_append_formated(
                gen.text, "alignas(%d) %s {\n    u8 values[%d];\n};\n", 
                structure->base.memory_info.value.alignment, access_name.characters, structure->base.memory_info.value.size
            );
        }
        else
        {
            string_append_formated(gen.text, "%s {\n", access_name.characters);
            c_generator_generate_struct_content(&structure->content, dependency, 1);
            string_append(gen.text, "};\n\n");
        }

        // We return early because we don't want to insert into the translation table twice
        gen.text = backup_text;
        string_append(gen.text, access_name.characters);
        return;
    }
    case Datatype_Type::SUBTYPE:
    {
        // In C-Compilation struct subtypes are treated as the struct base type
        gen.text = &access_name;
        c_generator_output_type_reference(downcast<Datatype_Subtype>(type)->base_type);
        break;
    }
    case Datatype_Type::ARRAY: 
    {
        auto array_type = downcast<Datatype_Array>(type);
        string_append_formated(&access_name, "Array_%d", gen.name_counter);
        gen.name_counter++;
        string_append_formated(&gen.sections[(int)Generator_Section::STRUCT_PROTOTYPES], "struct %s;\n", access_name.characters);

        // Similar to structs we insert the names early
        hashtable_insert_element(&gen.translations, translation, access_name);
        string_append_formated(&gen.sections[(int)Generator_Section::STRUCT_PROTOTYPES], "struct %s;\n", access_name.characters);

        // Generate array definition
        C_Type_Dependency* dependency = get_type_dependency(type);
        gen.text = &dependency->type_definition;
        string_append_formated(gen.text, "struct %s {\n", access_name.characters);
        string_add_indentation(gen.text, 1);
        // Note: Here we use the non-const type, because:
        //      In type_system, if the element_type is constant, the array_type is also constant
        //      In C, if a struct is constant, the array in the struct is also automatically constant
        //      By not making the values constant, array_initializers will still work by using a temporary, non-const array
        c_generator_output_type_reference(datatype_get_non_const_type(array_type->element_type));
        string_append_formated(gen.text, " values[%d];\n};\n", array_type->element_count);

        // Add dependency if necessary
        auto member_type = datatype_get_non_const_type(array_type->element_type);
        if (member_type->type == Datatype_Type::SUBTYPE || member_type->type == Datatype_Type::STRUCT || member_type->type == Datatype_Type::ARRAY) 
        {
            member_type = member_type->base_type;
            C_Type_Dependency* member_dependency = get_type_dependency(member_type);
            dynamic_array_push_back(&dependency->depends_on, member_dependency);
            dynamic_array_push_back(&member_dependency->dependees, dependency);
            dependency->dependency_count += 1;
        }

        // We return early because we don't want to insert into the translation table twice
        gen.text = backup_text;
        string_append(gen.text, access_name.characters);
        return;
    }
    case Datatype_Type::POINTER:
    case Datatype_Type::CONSTANT: {
        panic("Should be handled by pointer/constant handling");
        break;
    }
    default: panic("Hey");
    }

    // Insert translation into table and append access to text
    hashtable_insert_element(&gen.translations, translation, access_name);
    gen.text = backup_text;
    string_append(gen.text, access_name.characters);
}

void type_info_append_struct_content(Internal_Type_Struct_Content* content, int indentation_level)
{
    auto& gen = c_generator;
    auto& types = compiler.analysis_data->type_system.predefined_types;

    // Create a new block, because we need to keep the content pointer available to avoid having to use recursive functions for this
    string_add_indentation(gen.text, indentation_level);
    string_append_formated(gen.text, "{\n");
    indentation_level = indentation_level + 1;

    string_add_indentation(gen.text, indentation_level);
    string_append_formated(gen.text, "content->name = ");
    output_memory_as_new_constant((byte*)&content->name, types.c_string, false, 1);
    string_append(gen.text, ";\n");

    // Generate tag member
    if (content->subtypes.size > 0)
    {
        string_add_indentation(gen.text, indentation_level);
        string_append_formated(gen.text, "content->tag_member.name = ");
        output_memory_as_new_constant((byte*)&content->tag_member.name, types.c_string, false, 1);
        string_append(gen.text, ";\n");
        string_add_indentation(gen.text, indentation_level);
        string_append_formated(gen.text, "content->tag_member.type = %d;\n", content->tag_member.type.index);
        string_add_indentation(gen.text, indentation_level);
        string_append_formated(gen.text, "content->tag_member.offset = %d;\n", content->tag_member.offset);
    }

    // Generate members
    string_add_indentation(gen.text, indentation_level);
    string_append_formated(gen.text, "content->members.size = %d;\n", content->members.size);
    if (content->members.size != 0) 
    {
        string_add_indentation(gen.text, indentation_level);
        string_append(gen.text, "content->members.data = new ");
        c_generator_output_type_reference(upcast(types.internal_member_info_type));
        string_append_formated(gen.text, "[%d];\n", content->members.size);
        for (int i = 0; i < content->members.size; i++) {
            auto& member = content->members.data[i];
            string_add_indentation(gen.text, indentation_level);
            string_append_formated(gen.text, "content->members.data[%d].name = ", i);
            output_memory_as_new_constant((byte*)&member.name, types.c_string, false, 1);
            string_append(gen.text, ";\n");

            string_add_indentation(gen.text, indentation_level);
            string_append_formated(gen.text, "content->members.data[%d].type = %d;\n", i, member.type.index);
            string_add_indentation(gen.text, indentation_level);
            string_append_formated(gen.text, "content->members.data[%d].offset = %d;\n", i, member.offset);
        }
    }
    else {
        string_add_indentation(gen.text, indentation_level);
        string_append_formated(gen.text, "content->members.data = nullptr;\n");
    }

    // Generate subtypes
    string_add_indentation(gen.text, indentation_level);
    string_append_formated(gen.text, "content->subtypes.size = %d;\n", content->subtypes.size);
    if (content->subtypes.size > 0)
    {
        string_add_indentation(gen.text, indentation_level);
        c_generator_output_type_reference(upcast(types.internal_struct_content_type));
        string_append(gen.text, "* base_content = content;\n");
        string_add_indentation(gen.text, indentation_level);
        string_append(gen.text, "content->subtypes.data = new ");
        c_generator_output_type_reference(upcast(types.internal_struct_content_type));
        string_append_formated(gen.text, "[%d];\n", content->subtypes.size);
        for (int i = 0; i < content->subtypes.size; i++) {
            string_add_indentation(gen.text, indentation_level);
            string_append_formated(gen.text, "content = &base_content->subtypes.data[%d];\n", i);
            type_info_append_struct_content(&content->subtypes.data[i], indentation_level);
        }
    }
    else {
        string_add_indentation(gen.text, indentation_level);
        string_append_formated(gen.text, "content->subtypes.data = nullptr;\n", content->subtypes.size);
    }

    string_add_indentation(gen.text, indentation_level - 1);
    string_append_formated(gen.text, "}\n");
}

void c_generator_generate()
{
    auto& gen = c_generator;
    IR_Program* program = compiler.ir_generator->program;
    auto& types = compiler.analysis_data->type_system.predefined_types;
    auto& ids = compiler.identifier_pool.predefined_ids;

    // Reset generator data 
    {
        for (int i = 0; i < (int)Generator_Section::MAX_ENUM_VALUE; i++) {
            string_reset(&gen.sections[i]);
        }
        hashtable_reset(&gen.translations);
        hashtable_reset(&gen.type_to_dependency_mapping);
        for (int i = 0; i < gen.type_dependencies.size; i++) {
            type_dependency_destroy(&gen.type_dependencies[i]);
        }
        dynamic_array_reset(&gen.type_dependencies);
        gen.name_counter = 0;
    }

    // Define translations for hardcoded types (Because we don't want to have 2 definitons for those, and they are already defined in the header)
    {
        // Slice_U8
        {
            Datatype* const_u8_slice = upcast(type_system_make_slice(type_system_make_constant(upcast(types.u8_type))));
            C_Translation translation;
            translation.type = C_Translation_Type::DATATYPE;
            translation.options.datatype = const_u8_slice;
            String name = string_create("Slice_U8_"); // See hardcoded_functions.h
            hashtable_insert_element(&gen.translations, translation, name);
        }
        // String
        {
            C_Translation translation;
            translation.type = C_Translation_Type::DATATYPE;
            translation.options.datatype = types.c_string;
            String name = string_create("Upp_String_"); // See hardcoded_functions.h
            hashtable_insert_element(&gen.translations, translation, name);
        }
        // Type_Handle
        {
            C_Translation translation;
            translation.type = C_Translation_Type::DATATYPE;
            translation.options.datatype = types.type_handle;
            String name = string_create("Type_Handle_"); // See hardcoded_functions.h
            hashtable_insert_element(&gen.translations, translation, name);
        }
        // Byte_Pointer
        {
            C_Translation translation;
            translation.type = C_Translation_Type::DATATYPE;
            translation.options.datatype = types.byte_pointer;
            String name = string_create("byte_pointer_"); // See hardcoded_functions.h
            hashtable_insert_element(&gen.translations, translation, name);
        }
        // Any
        {
            C_Translation translation;
            translation.type = C_Translation_Type::DATATYPE;
            translation.options.datatype = upcast(types.any_type);
            String name = string_create("Upp_Any_"); // See hardcoded_functions.h
            hashtable_insert_element(&gen.translations, translation, name);
        }
    }

    // Create globals Translations
    {
        gen.text = &gen.sections[(int)Generator_Section::GLOBALS];
        auto& globals = compiler.analysis_data->program->globals;
        for (int i = 0; i < globals.size; i++) 
        {
            auto global = globals[i];
            auto type = global->type;

            if (global->is_extern) {
                // Extern globals should be defined in included headers
                continue;
            }

            c_generator_output_type_reference(type);
            string_append(gen.text, " ");
            IR_Data_Access access;
            access.type = IR_Data_Access_Type::GLOBAL_DATA;
            access.option.global_index = i;
            c_generator_output_data_access(&access);
            string_append(gen.text, ";\n");
        }
    }

    auto append_function_signature = [&](String* function_access_name, Datatype_Function* signature)
    {
        if (signature->return_type.available) {
            c_generator_output_type_reference(signature->return_type.value);
        }
        else {
            string_append(gen.text, "void");
        }
        string_append_formated(gen.text, " %s(", function_access_name->characters);

        auto& parameters = signature->parameters;
        for (int j = 0; j < parameters.size; j++)
        {
            auto& param = parameters[j];

            c_generator_output_type_reference(param.type);
            string_append(gen.text, " ");

            // Note: This has to be the same name as in output_data_access for parameter access
            string_append_formated(gen.text, "%s_p%d", param.name->characters, j);

            if (j != parameters.size - 1) {
                string_append(gen.text, ", ");
            }
        }
        string_append(gen.text, ")");
    };

    // Create function prototypes (Required before code-generation, as functions calling other functions require the translation)
    {
        for (int i = 0; i < program->functions.size; i++)
        {
            auto function = program->functions[i];

            // Special case for entry-function (Is not called, does not require forward definition)
            if (function == program->entry_function) {
                continue;
            }

            const auto& slot = compiler.analysis_data->function_slots[function->function_slot_index];
            if (slot.modtree_function != 0) {
                assert(slot.modtree_function->function_type != ModTree_Function_Type::EXTERN, "Extern functions should not be generated by ir_code");
            }

            String access_name = string_create();
            if (slot.modtree_function != 0) {
                string_append(&access_name, slot.modtree_function->name->characters);
            }
            else {
                string_append(&access_name, "ir_generated_fn");
            }
            string_append_formated(&access_name, "_%d", gen.name_counter);
            gen.name_counter++;

            C_Translation translation;
            translation.type = C_Translation_Type::FUNCTION;
            translation.options.function_slot_index = function->function_slot_index;
            hashtable_insert_element(&gen.translations, translation, access_name);

            // Generate prototype
            gen.text = &gen.sections[(int)Generator_Section::FUNCTION_PROTOTYPES];
            append_function_signature(&access_name, function->function_type);
            string_append(gen.text, ";\n");
        }

        // Generate extern function translations (Aren't included in ir functions)
        for (int i = 0; i < compiler.analysis_data->extern_sources.extern_functions.size; i++) 
        {
            auto extern_function = compiler.analysis_data->extern_sources.extern_functions[i];
            assert(extern_function->function_type == ModTree_Function_Type::EXTERN, "Should be extern");

            String access_name = string_create();
            string_append(&access_name, extern_function->name->characters);

            C_Translation translation;
            translation.type = C_Translation_Type::FUNCTION;
            translation.options.function_slot_index = extern_function->function_slot_index;
            hashtable_insert_element(&gen.translations, translation, access_name);
        }
    }

    // Create functions
    {
        for (int i = 0; i < program->functions.size; i++)
        {
            IR_Function* function = program->functions[i];
            Datatype_Function* function_signature = function->function_type;
            auto& parameters = function_signature->parameters;

            // Generate function signature into tmp c_string
            gen.text = &gen.sections[(int)Generator_Section::FUNCTION_IMPLEMENTATION];
            {
                if (function != program->entry_function) 
                {
                    C_Translation fn_translation;
                    fn_translation.type = C_Translation_Type::FUNCTION;
                    fn_translation.options.function_slot_index = function->function_slot_index;
                    String* fn_name = hashtable_find_element(&gen.translations, fn_translation);
                    assert(fn_name != 0, "");

                    append_function_signature(fn_name, function->function_type);
                }
                else {
                    String name = string_create_static("upp_entry_");
                    append_function_signature(&name, function->function_type);
                }
            }

            // Generate function body
            string_append(gen.text, "\n");
            c_generator_output_code_block(function->code, 0, false);
            string_append(gen.text, "\n");
        }
    }

    // Create type_info function
    {
        // Create Holder struct
        gen.text = &gen.sections[(int)Generator_Section::CONSTANT_ARRAY_HOLDERS];
        string_append(gen.text, "struct Type_Information_Holder_ {\n    ");
        c_generator_output_type_reference(upcast(types.type_information_type));
        string_append_formated(gen.text, " infos[%d];\n};\n", compiler.analysis_data->type_system.types.size);

        // Create constant
        gen.text = &gen.sections[(int)Generator_Section::CONSTANTS];
        string_append(gen.text, "Type_Information_Holder_ type_infos_;\n");

        // Create initialization function
        gen.text = &gen.sections[(int)Generator_Section::FUNCTION_PROTOTYPES];
        string_append(gen.text, "void inititalize_type_infos_global_();\n");
        gen.text = &gen.sections[(int)Generator_Section::FUNCTION_IMPLEMENTATION];
        string_append(gen.text, "void inititalize_type_infos_global_() {\n");
        string_add_indentation(gen.text, 1);
        c_generator_output_type_reference(upcast(types.type_information_type));
        string_append(gen.text, "* info = nullptr;\n");
        string_add_indentation(gen.text, 1);
        c_generator_output_type_reference(upcast(types.internal_struct_content_type));
        string_append(gen.text, "* content = nullptr;\n\n");


        auto& type_system = compiler.analysis_data->type_system;
        for (int i = 0; i < type_system.types.size; i++) 
        {
            auto type = type_system.types[i];
            assert(type->memory_info.available, "Should be the case at this point");
            auto& memory = type->memory_info.value;

            // Set base info
            string_add_indentation(gen.text, 1);
            string_append_formated(gen.text, "info = &type_infos_.infos[%d];\n", i);
            string_add_indentation(gen.text, 1);
            string_append_formated(gen.text, "info->type      = %d;\n", type->type_handle.index);
            string_add_indentation(gen.text, 1);
            string_append_formated(gen.text, "info->size      = %d;\n", memory.size);
            string_add_indentation(gen.text, 1);
            string_append_formated(gen.text, "info->alignment = %d;\n", memory.alignment);
            string_add_indentation(gen.text, 1);
            string_append(gen.text,          "info->tag_      = ");
            output_memory_as_new_constant((byte*)&type_system.internal_type_infos[i]->tag, types.type_information_type->content.tag_member.type, false, 1);
            string_append(gen.text, ";\n");

            // Set type-specific infos
            string_add_indentation(gen.text, 1);
            switch (type->type)
            {
            case Datatype_Type::PRIMITIVE: {
                auto primitive = downcast<Datatype_Primitive>(type);
                Struct_Content* primitive_info = types.type_information_type->content.subtypes[(int)Datatype_Type::PRIMITIVE - 1];
                string_append(gen.text, "info->subtypes_.Primitive.tag_ = ");
                output_memory_as_new_constant((byte*)&primitive->primitive_type, primitive_info->tag_member.type, false, 1);
                string_append(gen.text, ";\n");
                if (primitive->primitive_type == Primitive_Type::INTEGER) {
                    string_add_indentation(gen.text, 1);
                    string_append_formated(gen.text, "info->subtypes_.Primitive.subtypes_.Integer.is_signed = %s;\n", primitive->is_signed ? "true" : "false");
                }
                break;
            }
            case Datatype_Type::ARRAY: {
                auto array_type = downcast<Datatype_Array>(type);
                Struct_Content* array_info = types.type_information_type->content.subtypes[(int)Datatype_Type::ARRAY - 1];
                string_append_formated(gen.text, "info->subtypes_.Array.element_type = %d;\n", array_type->element_type->type_handle.index);
                string_add_indentation(gen.text, 1);
                string_append_formated(gen.text, "info->subtypes_.Array.size         = %d;\n", array_type->element_count);
                break;
            }
            case Datatype_Type::POINTER: {
                auto pointer = downcast<Datatype_Pointer>(type);
                string_append_formated(gen.text, "info->subtypes_.Pointer.element_type = %d;\n", pointer->element_type->type_handle.index);
                break;
            }
            case Datatype_Type::OPTIONAL_TYPE: {
                auto opt = downcast<Datatype_Optional>(type);
                auto& internal_info = type_system.internal_type_infos[i]->options.optional;
                string_append_formated(gen.text, "info->subtypes_.Optional.child_type = %d;\n", internal_info.child_type.index);
                string_add_indentation(gen.text, 1);
                string_append_formated(gen.text, "info->subtypes_.Optional.available_offset = %d;\n", internal_info.available_offset);
                break;
            }
            case Datatype_Type::SUBTYPE: {
                auto subtype = downcast<Datatype_Subtype>(type);
                auto& internal_info = type_system.internal_type_infos[i]->options.struct_subtype;
                string_append_formated(gen.text, "info->subtypes_.Subtype.base_type = %d;\n", subtype->base_type->type_handle.index);
                string_add_indentation(gen.text, 1);
                string_append(gen.text, "info->subtypes_.Subtype.name = ");
                output_memory_as_new_constant((byte*)&internal_info.subtype_name, types.c_string, false, 1);
                string_append(gen.text, ";\n");
                string_add_indentation(gen.text, 1);
                string_append_formated(gen.text, "info->subtypes_.Subtype.index = %d;\n", subtype->subtype_index);
                break;
            }
            case Datatype_Type::CONSTANT: {
                auto constant = downcast<Datatype_Constant>(type);
                string_append_formated(gen.text, "info->subtypes_.Constant.element_type = %d;\n", constant->element_type->type_handle.index);
                break;
            }

            case Datatype_Type::SLICE: {
                auto slice = downcast<Datatype_Slice>(type);
                string_append_formated(gen.text, "info->subtypes_.Slice.element_type = %d;\n", slice->element_type->type_handle.index);
                break;
            }
            case Datatype_Type::ENUM: 
            {
                auto enumeration = downcast<Datatype_Enum>(type);
                Struct_Content* enum_subtype = types.type_information_type->content.subtypes[(int)Datatype_Type::ENUM - 1];
                auto& internal_info = type_system.internal_type_infos[i]->options.enumeration;
                string_append_formated(gen.text, "info->subtypes_.Enum.name = ");
                output_memory_as_new_constant((byte*)&internal_info.name, types.c_string, false, 1);
                string_append(gen.text, ";\n");

                string_add_indentation(gen.text, 1);
                string_append_formated(gen.text, "info->subtypes_.Enum.members.size = %d;\n", internal_info.members.size);
                string_add_indentation(gen.text, 1);
                if (internal_info.members.size > 0)
                {
                    string_append_formated(gen.text, "info->subtypes_.Enum.members.data = new ");
                    Datatype* member_info_type = downcast<Datatype_Slice>(enum_subtype->members[0].type)->element_type;
                    c_generator_output_type_reference(member_info_type); // Check if this works
                    string_append_formated(gen.text, "[%d];\n", internal_info.members.size);
                    for (int j = 0; j < internal_info.members.size; j++) {
                        auto& member = internal_info.members.data[j];
                        string_add_indentation(gen.text, 1);
                        string_append_formated(gen.text, "info->subtypes_.Enum.members.data[%d].name = ", j);
                        output_memory_as_new_constant((byte*)&member.name, types.c_string, false, 1);
                        string_append(gen.text, ";\n");
                        string_add_indentation(gen.text, 1);
                        string_append_formated(gen.text, "info->subtypes_.Enum.members.data[%d].value = %d;\n", j, member.value);
                    }
                }
                else {
                    string_append_formated(gen.text, "info->subtypes_.Enum.members.data = nullptr;\n");
                }
                break;
            }
            case Datatype_Type::FUNCTION: 
            {
                auto function = downcast<Datatype_Function>(type);

                string_append_formated(gen.text, "info->subtypes_.Function.return_type = %d;\n", function->return_type.available ? function->return_type.value->type_handle.index : -1);
                string_add_indentation(gen.text, 1);
                string_append_formated(gen.text, "info->subtypes_.Function.has_return_type = %s;\n", function->return_type.available ? "true" : "false");
                string_add_indentation(gen.text, 1);
                string_append_formated(gen.text, "info->subtypes_.Function.parameter_types.size = %d;\n", function->parameters.size);
                if (function->parameters.size != 0) {
                    string_add_indentation(gen.text, 1);
                    string_append_formated(gen.text, "info->subtypes_.Function.parameter_types.data = new ", function->parameters.size);
                    c_generator_output_type_reference(types.type_handle); // Check if this works
                    string_append_formated(gen.text, "[%d];\n", function->parameters.size);
                    for (int j = 0; j < function->parameters.size; j++) {
                        string_add_indentation(gen.text, 1);
                        string_append_formated(gen.text, "info->subtypes_.Function.parameter_types.data[%d] = %d;\n", j, function->parameters[j].type->type_handle.index);
                    }
                }
                else {
                    string_add_indentation(gen.text, 1);
                    string_append_formated(gen.text, "info->subtypes_.Function.parameter_types.data = nullptr;\n", function->parameters.size);
                }
                break;
            }
            case Datatype_Type::STRUCT: 
            {
                auto structure = downcast<Datatype_Struct>(type);
                string_append_formated(gen.text, "info->subtypes_.Struct.is_union = %s;\n", structure->struct_type == AST::Structure_Type::UNION ? "true" : "false");
                string_add_indentation(gen.text, 1);
                string_append(gen.text, "content = &info->subtypes_.Struct.content;\n");
                type_info_append_struct_content(&type_system.internal_type_infos[i]->options.structure.content, 1);
                break;
            }

            case Datatype_Type::TYPE_HANDLE:
            case Datatype_Type::BYTE_POINTER:
            case Datatype_Type::UNKNOWN_TYPE:
            case Datatype_Type::TEMPLATE_TYPE:
            case Datatype_Type::STRUCT_INSTANCE_TEMPLATE:
                break; // Nothing to do on these types
            default: panic("");
            }

            string_append(gen.text, "\n");
        }

        string_append(gen.text, "}\n");
    }

    // Resolve Type Dependencies
    {
        // Analyse Dependencies between types
        Dynamic_Array<C_Type_Dependency*> resolved_dependency_queue = dynamic_array_create<C_Type_Dependency*>(gen.type_dependencies.size);
        SCOPE_EXIT(dynamic_array_destroy(&resolved_dependency_queue));
        for (int i = 0; i < gen.type_dependencies.size; i++)
        {
            C_Type_Dependency* dependency = gen.type_dependencies[i];
            assert(dependency->dependency_count == dependency->depends_on.size, "Should be true");
            if (dependency->dependency_count == 0) {
                dynamic_array_push_back(&resolved_dependency_queue, dependency);
            }
        }

        // Resolve dependencies
        while (resolved_dependency_queue.size != 0)
        {
            C_Type_Dependency* dependency = resolved_dependency_queue[resolved_dependency_queue.size - 1];
            dynamic_array_rollback_to_size(&resolved_dependency_queue, resolved_dependency_queue.size - 1);
            assert(dependency->dependency_count == 0, "Resolved dependencies should not have any dependencies anymore");

            // Decrease dependency count of incoming dependencies
            for (int i = 0; i < dependency->dependees.size; i++)
            {
                C_Type_Dependency* dependant = dependency->dependees[i];
                dependant->dependency_count--;
                if (dependant->dependency_count == 0) {
                    dynamic_array_push_back(&resolved_dependency_queue, dependant);
                }
                assert(dependant->dependency_count >= 0, "HEY");
            }

            // Output type definition
            gen.text = &gen.sections[(int)Generator_Section::STRUCT_AND_ARRAY_DECLARATIONS];
            string_append(gen.text, dependency->type_definition.characters);
            string_append(gen.text, "\n");
        }

        // Sanity test, check if everything was resolved
        for (int i = 0; i < gen.type_dependencies.size; i++) {
            C_Type_Dependency* dependency = gen.type_dependencies[i];
            assert(dependency->dependency_count == 0, "Everything should be resolved now!");
        }
    }

    // Finish (Add entry and combine + write all sections to file)
    {
        string_append(
            &gen.sections[(int)Generator_Section::FUNCTION_IMPLEMENTATION],
            "\nint main(int argc, char** argv) {random_initialize(); inititalize_type_infos_global_(); upp_entry_(); return 0;}\n"
        );

        // Combine sections into one program
        String source_code = string_create_empty(4096);
        SCOPE_EXIT(string_destroy(&source_code));
        {
            string_append_formated(&source_code, "/* INTRODUCTION\n----------------*/\n");
            string_append(&source_code, "#pragma once\n#include <cstdlib>\n#include \"../hardcoded/hardcoded_functions.h\"\n#include \"../hardcoded/datatypes.h\"\n\n");
            // string_append_formated(&source_code, "/* EXTERN HEADERS\n----------------*/\n");
            // string_append_string(&source_code, &section_extern_includes);
            // string_append_formated(&source_code, "\n/* STRING_DATA\n----------------*/\n");
            // string_append_string(&source_code, &generator->section_string_data);
            string_append_formated(&source_code, "\n/* ENUMS\n----------------*/\n");
            string_append_string(&source_code, &gen.sections[(int)Generator_Section::ENUM_DECLARATIONS]);
            string_append_formated(&source_code, "\n/* STRUCT_PROTOTYPES\n----------------*/\n");
            string_append_string(&source_code, &gen.sections[(int)Generator_Section::STRUCT_PROTOTYPES]);
            string_append_formated(&source_code, "\n/* TYPE_DECLARATIONS\n------------------*/\n");
            string_append_string(&source_code, &gen.sections[(int)Generator_Section::TYPE_DECLARATIONS]);
            string_append_formated(&source_code, "\n/* STRUCT_IMPLEMENTATIONS\n----------------*/\n");
            string_append_string(&source_code, &gen.sections[(int)Generator_Section::STRUCT_AND_ARRAY_DECLARATIONS]);
            string_append_formated(&source_code, "\n/* ARRAY_HOLDER_SECTION\n----------------*/\n");
            string_append_string(&source_code, &gen.sections[(int)Generator_Section::CONSTANT_ARRAY_HOLDERS]);
            string_append_formated(&source_code, "\n/* FUNCTION PROTOTYPES\n------------------*/\n"); // Need to be declared before constants for function pointers constants to work
            string_append_string(&source_code, &gen.sections[(int)Generator_Section::FUNCTION_PROTOTYPES]);
            string_append_formated(&source_code, "\n/* CONSTANTS\n------------------*/\n");
            string_append_string(&source_code, &gen.sections[(int)Generator_Section::CONSTANTS]);
            string_append_formated(&source_code, "\n/* GLOBALS\n------------------*/\n");
            string_append_string(&source_code, &gen.sections[(int)Generator_Section::GLOBALS]);
            string_append_formated(&source_code, "\n/* FUNCTIONS\n------------------*/\n");
            string_append_string(&source_code, &gen.sections[(int)Generator_Section::FUNCTION_IMPLEMENTATION]);
        }

        file_io_write_file("backend/src/main.cpp", array_create_static((byte*)source_code.characters, source_code.size));
    }
}

// Outputs "{" + the struct content on indentation level + 1 and "}"
void output_struct_content_block_recursive(Struct_Content* content, byte* struct_start_memory, int current_indentation_level)
{
    auto& gen = c_generator;
    auto& members = content->members;
    int block_indentation = current_indentation_level + 1;
    string_append(gen.text, "{\n");
    string_add_indentation(gen.text, block_indentation);
    for (int i = 0; i < members.size; i++)
    {
        auto& member = members[i];
        byte* member_memory = struct_start_memory + member.offset;

        // Generate designator
        string_append_formated(gen.text, ".%s = ", member.id->characters);
        output_memory_as_new_constant(member_memory, member.type, false, block_indentation);
        if (i != members.size - 1) {
            string_append(gen.text, ", \n");
            string_add_indentation(gen.text, block_indentation);
        }
    }

    if (content->subtypes.size > 0)
    {
        int subtype_index = (*(int*)(struct_start_memory + content->tag_member.offset)) - 1;
        assert(subtype_index >= 0 && subtype_index < content->subtypes.size, "");
        Struct_Content* child_content = content->subtypes[subtype_index];
        string_append_formated(gen.text, ".subtypes_ = { .%s = ", child_content->name->characters);
        output_struct_content_block_recursive(child_content, struct_start_memory, block_indentation);
        string_append(gen.text, "}, \n");

        // Set tag
        string_add_indentation(gen.text, block_indentation);
        string_append(gen.text, "tag_ = ");
        byte* tag_memory = struct_start_memory + content->tag_member.offset;
        output_memory_as_new_constant(tag_memory, content->tag_member.type, false, block_indentation);
    }

    string_append(gen.text, "\n");
    string_add_indentation(gen.text, current_indentation_level);
    string_append(gen.text, "}");
}

void c_generator_output_constant_access(Upp_Constant& constant, bool requires_memory_address, int indentation_level)
{
    auto& types = compiler.analysis_data->type_system.predefined_types;
    auto& gen = c_generator;
    String* backup_text = gen.text;
    SCOPE_EXIT(gen.text = backup_text);

    Datatype* type = datatype_get_non_const_type(constant.type);
    if (type->type == Datatype_Type::ARRAY || type->type == Datatype_Type::SLICE ||
        type->type == Datatype_Type::STRUCT || type->type == Datatype_Type::SUBTYPE || type->type == Datatype_Type::OPTIONAL_TYPE) {
        requires_memory_address = true;
    }

    C_Translation constant_translation;
    constant_translation.type = C_Translation_Type::CONSTANT;
    constant_translation.options.constant.index = constant.constant_index;
    constant_translation.options.constant.requires_memory_address = requires_memory_address;
    {
        String* access_name = hashtable_find_element(&gen.translations, constant_translation);
        if (access_name != 0) {
            string_append(gen.text, access_name->characters);
            return;
        }
    }

    // Create access
    String access_name = string_create_empty(12);
    gen.text = &access_name;
    {
        auto& gen = c_generator;
        String* backup_text = gen.text;
        SCOPE_EXIT(gen.text = backup_text);

        Datatype* base_type = type;
        byte* base_memory = constant.memory;

        String constant_string;
        constant_string.size = 0;
        constant_string.capacity = 0;
        SCOPE_EXIT(if (constant_string.capacity != 0) string_destroy(&constant_string));

        if (requires_memory_address)
        {
            constant_string = string_create_empty(32);
            gen.text = &constant_string;

            c_generator_output_type_reference(base_type);
            string_append_formated(gen.text, " const_%d = ", gen.name_counter);
            string_append_formated(backup_text, "const_%d", gen.name_counter);
            gen.name_counter += 1;
        }

        // Generate constant access
        int type_size = type->memory_info.value.size;
        switch (type->type)
        {
        // Simple cases first
        case Datatype_Type::PRIMITIVE:
        {
            auto primitive = downcast<Datatype_Primitive>(type);
            int type_size = type->memory_info.value.size;
            byte* memory = base_memory;
            switch (primitive->primitive_type)
            {
            case Primitive_Type::BOOLEAN: {
                bool* value_ptr = (bool*)memory;
                string_append(gen.text, *value_ptr ? "true" : "false");
                break;
            }
            case Primitive_Type::INTEGER: {
                if (primitive->is_signed) {
                    switch (type_size)
                    {
                    case 1: string_append_formated(gen.text, "%d", (int)(*(i8*)memory)); break;
                    case 2: string_append_formated(gen.text, "%d", (int)(*(i16*)memory)); break;
                    case 4: string_append_formated(gen.text, "%d", (int)(*(i32*)memory)); break;
                    case 8: string_append_formated(gen.text, "%lld", (i64)(*(i64*)memory)); break;
                    default: panic("HEY");
                    }
                }
                else {
                    switch (type_size)
                    {
                    case 1: string_append_formated(gen.text, "%u", (u32)(*(u8*)memory)); break;
                    case 2: string_append_formated(gen.text, "%u", (u32)(*(u16*)memory)); break;
                    case 4: string_append_formated(gen.text, "%u", (u32)(*(u32*)memory)); break;
                    case 8: string_append_formated(gen.text, "%llu", (u64)(*(u64*)memory)); break;
                    default: panic("HEY");
                    }
                }
                break;
            }
            case Primitive_Type::FLOAT:
                switch (type_size)
                {
                case 4: string_append_formated(gen.text, "%f", (double)(*(float*)memory)); break;
                case 8: string_append_formated(gen.text, "%f", (double)(*(double*)memory)); break;
                default: panic("HEY");
                }
                break;
            default: panic("What");
            }
            break;
        }
        case Datatype_Type::TYPE_HANDLE: {
            byte* memory = base_memory;
            string_append_formated(gen.text, "%u", (u32)(*(u32*)memory));
            break;
        }
        case Datatype_Type::ENUM:
        {
            byte* memory = base_memory;

            int enum_value = *(int*)memory;
            Datatype_Enum* enum_type = downcast<Datatype_Enum>(type);
            int member_index = -1;
            for (int i = 0; i < enum_type->members.size; i++) {
                auto& member = enum_type->members[i];
                if (member.value == enum_value) {
                    member_index = i;
                    break;
                }
            }
            assert(member_index != -1, "");

            auto member = enum_type->members[member_index];
            c_generator_output_type_reference(type);
            string_append_formated(gen.text, "::%s", member.name->characters);
            break;
        }
        case Datatype_Type::FUNCTION:
        {
            byte* memory = base_memory;

            int function_index = (int)*(i64*)memory;
            if (function_index == 0) { // Function index 0 == nullptr, otherwise add -1 to get index in functions array
                string_append(gen.text, "nullptr");
            }
            else {
                C_Translation function_translation;
                function_translation.type = C_Translation_Type::FUNCTION;
                function_translation.options.function_slot_index = function_index - 1;
                String* fn_name = hashtable_find_element(&gen.translations, function_translation);
                assert(fn_name != 0, "");
                string_append_formated(gen.text, "(&%s)", fn_name->characters);
            }
            break;
        }
        case Datatype_Type::POINTER:
        case Datatype_Type::BYTE_POINTER:
        {
            byte* memory = base_memory;
            byte* pointer = *(byte**)memory;
            assert(pointer == 0, "Pointers must be null in constant memory");
            string_append(gen.text, "nullptr");
            break;
        }
        case Datatype_Type::SLICE:
        {
            Datatype* element_type = downcast<Datatype_Slice>(type)->element_type;
            Upp_Slice_Base slice = *(Upp_Slice_Base*)base_memory;
            assert(slice.size == 0 && slice.data == nullptr, "");
            string_append_formated(gen.text, "{.data = nullptr, .size = 0}");
            break;
        }
        case Datatype_Type::ARRAY:
        {
            Datatype_Array* array_type = downcast<Datatype_Array>(type);
            assert(array_type->count_known, "");
            string_append(gen.text, "{\n");
            string_add_indentation(gen.text, 1);
            for (int i = 0; i < array_type->element_count; i++) {
                output_memory_as_new_constant(constant.memory + i * array_type->element_type->memory_info.value.size, array_type->element_type, false, 1);
                if (i != array_type->element_count - 1) {
                    string_append(gen.text, ",\n");
                    string_add_indentation(gen.text, 1);
                }
            }
            string_append(gen.text, "\n}");
            break;
        }
        case Datatype_Type::OPTIONAL_TYPE:
        {
            auto opt = downcast<Datatype_Optional>(type);
            bool is_available = *(bool*)(constant.memory + opt->is_available_member.offset);
            string_append(gen.text, "{.value = ");
            output_memory_as_new_constant(constant.memory, opt->child_type, false, 1);
            string_append_formated(gen.text, ", .is_available = %s}", is_available ? "true" : "false");
            break;
        }
        case Datatype_Type::STRUCT:
        case Datatype_Type::SUBTYPE:
        {
            // Handle c_string
            if (types_are_equal(type, types.c_string))
            {
                // Note: Maybe we need something smarter in the future to handle multi-line strings 
                Upp_C_String string = *(Upp_C_String*)base_memory;
                string_append_formated(gen.text, "{.bytes = {.data = (const u8*) \"");

                // Note: I need to escape escape sequences, so this is what i'm doing now...
                String escaped = string_create_empty(16);
                SCOPE_EXIT(string_destroy(&escaped));
                for (int i = 0; i < string.bytes.size; i++) {
                    char c = (char) string.bytes.data[i];
                    switch (c)
                    {
                    case '\n': string_append(&escaped, "\\n"); break;
                    case '\r': string_append(&escaped, "\\r"); break;
                    case '\t': string_append(&escaped, "\\t"); break;
                    case '\\': string_append(&escaped, "\\\\"); break;
                    case '\"': string_append(&escaped, "\\\""); break;
                    case '\'': string_append(&escaped, "\\\'"); break;
                    default: string_append_character(&escaped, c); break;
                    }
                }

                string_append(gen.text, escaped.characters);
                string_append_formated(gen.text, "\", .size = %d} }", string.bytes.size);
                break;
            }

            // Handle structure
            Datatype_Struct* structure = downcast<Datatype_Struct>(type->base_type);
            assert(structure->struct_type != AST::Structure_Type::UNION, "Must not happen, as normal unions cannot get serialized in constant pool");
            output_struct_content_block_recursive(&structure->content, base_memory, indentation_level);

            break;
        }
        case Datatype_Type::STRUCT_INSTANCE_TEMPLATE:
        case Datatype_Type::TEMPLATE_TYPE:
        case Datatype_Type::UNKNOWN_TYPE: {
            panic("Should not happen, this should generate an error beforehand");
            break;
        }
        case Datatype_Type::CONSTANT: {
            panic("Should not happen as we stripped the constant before");
            break;
        }
        default: panic("");
        }

        // Finish declaration
        if (requires_memory_address) {
            string_append(gen.text, ";\n");
            gen.text = &gen.sections[(int)Generator_Section::CONSTANTS];
            string_append(gen.text, constant_string.characters);
        }
    }

    // Store translation
    hashtable_insert_element(&gen.translations, constant_translation, access_name);

    // Append constant access
    gen.text = backup_text;
    string_append(gen.text, access_name.characters);
}

void output_memory_as_new_constant(byte* base_memory, Datatype* base_type, bool requires_memory_address, int current_indentation_level)
{
    auto result = constant_pool_add_constant(base_type, array_create_static<byte>(base_memory, base_type->memory_info.value.size));
    assert(result.success, "Should always work");
    c_generator_output_constant_access(result.options.constant, requires_memory_address, current_indentation_level);
}

void c_generator_output_data_access(IR_Data_Access* access, bool add_parenthesis_on_pointer_ops)
{
    auto& gen = c_generator;
    auto& types = compiler.analysis_data->type_system.predefined_types;

    switch (access->type)
    {
    case IR_Data_Access_Type::REGISTER:
    {
        C_Translation translation;
        translation.type = C_Translation_Type::REGISTER;
        translation.options.register_translation.code_block = access->option.register_access.definition_block;
        translation.options.register_translation.index = access->option.register_access.index;
        {
            String* name = hashtable_find_element(&gen.translations, translation);
            if (name != 0) {
                string_append(gen.text, name->characters);
                break;
            }
        }

        auto& reg = access->option.register_access.definition_block->registers[access->option.register_access.index];
        String new_name = string_create_empty(16);
        if (reg.name.available) {
            string_append(&new_name, reg.name.value->characters);
        }
        else {
            string_append(&new_name, "tmp");
        }
        string_append_formated(&new_name, "%_%d", gen.name_counter);
        gen.name_counter++;

        hashtable_insert_element(&gen.translations, translation, new_name);
        string_append(gen.text, new_name.characters);
        break;
    }
    case IR_Data_Access_Type::PARAMETER:
    {
        auto& param_access = access->option.parameter;
        auto& param = param_access.function->function_type->parameters[param_access.index];
        string_append_formated(gen.text, "%s_p%d", param.name->characters, param_access.index);
        break;
    }
    case IR_Data_Access_Type::GLOBAL_DATA:
    {
        auto global = compiler.analysis_data->program->globals[access->option.global_index];

        if (global->is_extern) {
            string_append(gen.text, global->symbol->id->characters);
        }
        else {
            if (global->symbol != 0) {
                string_append_formated(gen.text, "%s_g%d", global->symbol->id->characters, access->option.global_index);
            }
            else {
                string_append_formated(gen.text, "global_%d", access->option.global_index);
            }
        }
        break;
    }
    case IR_Data_Access_Type::CONSTANT:
    {
        Upp_Constant* constant = &compiler.analysis_data->constant_pool.constants[access->option.constant_index];
        c_generator_output_constant_access(*constant, false, 0);
        break;
    }
    case IR_Data_Access_Type::POINTER_DEREFERENCE: {
        if (add_parenthesis_on_pointer_ops) {
            string_append(gen.text, "(");
        }

        string_append(gen.text, "*");
        c_generator_output_data_access(access->option.pointer_value, false);

        if (add_parenthesis_on_pointer_ops) {
            string_append(gen.text, ")");
        }
        break;
    }
    case IR_Data_Access_Type::ADDRESS_OF_VALUE: {
        if (add_parenthesis_on_pointer_ops) {
            string_append(gen.text, "(");
        }

        string_append(gen.text, "&");
        c_generator_output_data_access(access->option.pointer_value);

        if (add_parenthesis_on_pointer_ops) {
            string_append(gen.text, ")");
        }
        break;
    }
    case IR_Data_Access_Type::MEMBER_ACCESS:
    {
        const Struct_Member& member = access->option.member_access.member;

        c_generator_output_data_access(access->option.member_access.struct_access, true);

        // Handle members of struct subtypes
        Datatype* access_type = access->option.member_access.struct_access->datatype;
        assert(!datatype_is_pointer(access_type), "");
        if (access_type->base_type->type == Datatype_Type::STRUCT)
        {
            Datatype_Struct* structure = downcast<Datatype_Struct>(access_type->base_type);
            Struct_Content* subtype = member.content;
            assert(structure == member.content->structure, "");
            {
                Struct_Content* content = &structure->content;
                for (int i = 0; i < subtype->index->indices.size; i++) {
                    auto index = subtype->index->indices[i].index;
                    string_append_formated(gen.text, ".subtypes_.%s", content->subtypes[index]->name->characters);
                    content = content->subtypes[index];
                }
            }

            if (member.content->subtypes.size > 0) {
                if (member.offset == member.content->tag_member.offset) {
                    string_append_formated(gen.text, ".tag_", member.id->characters);
                    break;
                }
            }
        }

        // Append member access
        string_append_formated(gen.text, ".%s", member.id->characters);

        break;
    }
    case IR_Data_Access_Type::ARRAY_ELEMENT_ACCESS:
    {
        auto array_type = datatype_get_non_const_type(access->option.array_access.array_access->datatype);
        if (array_type->type == Datatype_Type::SLICE)
        {
            if (types_are_equal(array_type, types.bytes)) {
                // Convert to u8
                string_append(gen.text, "((u8*)");
                c_generator_output_data_access(access->option.array_access.array_access, true);
                string_append(gen.text, ".data)[");
            }
            else {
                c_generator_output_data_access(access->option.array_access.array_access, true);
                string_append(gen.text, ".data[");
            }

            c_generator_output_data_access(access->option.array_access.index_access);
            string_append(gen.text, "]");
        }
        else {
            assert(array_type->type == Datatype_Type::ARRAY, "");
            c_generator_output_data_access(access->option.array_access.array_access, true);
            string_append(gen.text, ".values[");
            c_generator_output_data_access(access->option.array_access.index_access);
            string_append(gen.text, "]");
        }
        break;
    }
    case IR_Data_Access_Type::NOTHING:
    {
        panic("Not sure if this should happen");
        break;
    }
    default:
        panic("");
    }

    return;
}

void c_generator_output_cast_if_necessary(IR_Data_Access* write_to_access, Datatype* value_type)
{
    auto& gen = c_generator;

    Datatype* write_to_type = write_to_access->datatype;
    if (types_are_equal(write_to_type, value_type)) return;

    string_append_formated(gen.text, "(");
    c_generator_output_type_reference(write_to_type);
    string_append_formated(gen.text, ") ");
}

// registers_in_same_scope is used for e.g. the while loop condition-block, as this may contain registers
void c_generator_output_code_block(IR_Code_Block* code_block, int indentation_level, bool registers_in_same_scope)
{
    auto& gen = c_generator;

    // Create Register variables
    {
        if (!registers_in_same_scope)
        {
            string_add_indentation(gen.text, indentation_level);
            string_append_formated(gen.text, "{\n");
        }
        for (int i = 0; i < code_block->registers.size; i++)
        {
            auto& reg = code_block->registers[i];
            if (reg.has_definition_instruction) continue;

            if (registers_in_same_scope) {
                string_add_indentation(gen.text, indentation_level);
            }
            else {
                string_add_indentation(gen.text, indentation_level + 1);
            }
            Datatype* sig = code_block->registers[i].type;
            c_generator_output_type_reference(sig);
            string_append_formated(gen.text, " ");

            IR_Data_Access access;
            access.type = IR_Data_Access_Type::REGISTER;
            access.option.register_access.definition_block = code_block;
            access.option.register_access.index = i;
            c_generator_output_data_access(&access);
            string_append_formated(gen.text, ";\n");
        }
        if (registers_in_same_scope) {
            string_add_indentation(gen.text, indentation_level);
            string_append_formated(gen.text, "{\n");
        }
    }

    // Output code
    for (int i = 0; i < code_block->instructions.size; i++)
    {
        IR_Instruction* instr = &code_block->instructions[i];
        if (instr->type != IR_Instruction_Type::BLOCK) {
            string_add_indentation(gen.text, indentation_level + 1);
        }
        switch (instr->type)
        {
        case IR_Instruction_Type::FUNCTION_CALL:
        {
            IR_Instruction_Call* call = &instr->options.call;
            Datatype_Function* function_sig = 0;
            switch (call->call_type) {
            case IR_Instruction_Call_Type::FUNCTION_CALL:
                function_sig = call->options.function->signature;
                break;
            case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL:
                function_sig = downcast<Datatype_Function>(datatype_get_non_const_type(call->options.pointer_access->datatype));
                break;
            case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL:
                function_sig = hardcoded_type_to_signature(call->options.hardcoded);
                break;
            default: panic("hey");
            }
            if (function_sig->return_type.available) {
                c_generator_output_data_access(call->destination);
                string_append_formated(gen.text, " = ");
                c_generator_output_cast_if_necessary(call->destination, function_sig->return_type.value);
            }

            bool call_handled = false;
            switch (call->call_type)
            {
            case IR_Instruction_Call_Type::FUNCTION_CALL: {
                C_Translation fn_translation;
                fn_translation.type = C_Translation_Type::FUNCTION;
                fn_translation.options.function_slot_index = call->options.function->function_slot_index;
                String* fn_name = hashtable_find_element(&gen.translations, fn_translation);
                assert(fn_name != 0, "Hey");
                string_append_formated(gen.text, fn_name->characters);
                break;
            }
            case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL: {
                c_generator_output_data_access(call->options.pointer_access);
                break;
            }
            case IR_Instruction_Call_Type::HARDCODED_FUNCTION_CALL: {
                switch (call->options.hardcoded)
                {
                case Hardcoded_Type::PRINT_I32:
                    string_append_formated(gen.text, "print_i32");
                    break;
                case Hardcoded_Type::PRINT_F32:
                    string_append_formated(gen.text, "print_f32");
                    break;
                case Hardcoded_Type::PRINT_BOOL:
                    string_append_formated(gen.text, "print_bool");
                    break;
                case Hardcoded_Type::PRINT_LINE:
                    string_append_formated(gen.text, "print_line");
                    break;
                case Hardcoded_Type::PRINT_STRING:
                    string_append_formated(gen.text, "print_string");
                    break;
                case Hardcoded_Type::READ_I32:
                    string_append_formated(gen.text, "read_i32");
                    break;
                case Hardcoded_Type::READ_F32:
                    string_append_formated(gen.text, "read_f32");
                    break;
                case Hardcoded_Type::READ_BOOL:
                    string_append_formated(gen.text, "read_bool");
                    break;
                case Hardcoded_Type::RANDOM_I32:
                    string_append_formated(gen.text, "random_i32");
                    break;
                case Hardcoded_Type::MALLOC_SIZE_U64:
                    string_append_formated(gen.text, "malloc_size_i32");
                    break;
                case Hardcoded_Type::FREE_POINTER:
                    string_append_formated(gen.text, "free_pointer");
                    break;
                case Hardcoded_Type::MEMORY_COPY:
                    string_append_formated(gen.text, "memory_copy");
                    break;
                case Hardcoded_Type::MEMORY_COMPARE:
                    string_append_formated(gen.text, "memory_compare");
                    break;
                case Hardcoded_Type::MEMORY_ZERO:
                    string_append_formated(gen.text, "memory_zero");
                    break;
                case Hardcoded_Type::TYPE_INFO:
                {
                    string_append(gen.text, "&type_infos_.infos[");
                    assert(call->arguments.size == 1, "");
                    c_generator_output_data_access(call->arguments[0]);
                    string_append_formated(gen.text, "];\n");
                    call_handled = true;
                    break;
                }
                default: panic("What");
                }
                break;
            }
            default: panic("What");
            }
            if (call_handled) {
                break;
            }

            string_append_formated(gen.text, "(");
            for (int j = 0; j < call->arguments.size; j++)
            {
                // Add cast (Implemented because of signed char/char difference in C) // ? Is this comment outdated ?
                c_generator_output_data_access(call->arguments[j]);
                if (j != call->arguments.size - 1) {
                    string_append_formated(gen.text, ", ");
                }
            }
            string_append_formated(gen.text, ");\n");
            break;
        }
        case IR_Instruction_Type::SWITCH:
        {
            IR_Instruction_Switch* switch_instr = &instr->options.switch_instr;
            string_append_formated(gen.text, "switch ((int) ");
            c_generator_output_data_access(switch_instr->condition_access);
            string_append_formated(gen.text, ")\n");
            string_add_indentation(gen.text, indentation_level);
            string_append_formated(gen.text, "{\n");
            for (int i = 0; i < switch_instr->cases.size; i++)
            {
                IR_Switch_Case* switch_case = &switch_instr->cases[i];
                string_add_indentation(gen.text, indentation_level);
                string_append_formated(gen.text, "case %d: \n", switch_case->value);
                c_generator_output_code_block(switch_case->block, indentation_level + 1, false);
                string_add_indentation(gen.text, indentation_level);
                string_append_formated(gen.text, "break;\n");
            }
            string_add_indentation(gen.text, indentation_level);
            string_append_formated(gen.text, "default:\n");
            c_generator_output_code_block(switch_instr->default_block, indentation_level + 1, false);
            string_add_indentation(gen.text, indentation_level);
            string_append_formated(gen.text, "}\n");
            break;
        }
        case IR_Instruction_Type::IF:
        {
            IR_Instruction_If* if_instr = &instr->options.if_instr;
            string_append_formated(gen.text, "if (");
            c_generator_output_data_access(if_instr->condition);
            string_append_formated(gen.text, ")\n");
            c_generator_output_code_block(if_instr->true_branch, indentation_level + 1, false);
            if (if_instr->false_branch->instructions.size != 0) {
                string_add_indentation(gen.text, indentation_level + 1);
                string_append_formated(gen.text, "else\n");
                c_generator_output_code_block(if_instr->false_branch, indentation_level + 1, false);
            }
            break;
        }
        case IR_Instruction_Type::WHILE:
        {
            IR_Instruction_While* while_instr = &instr->options.while_instr;
            string_append_formated(gen.text, "while(true){\n");
            c_generator_output_code_block(while_instr->condition_code, indentation_level + 2, true);
            string_add_indentation(gen.text, indentation_level + 2);
            string_append_formated(gen.text, "if(!(");
            c_generator_output_data_access(while_instr->condition_access);
            string_append_formated(gen.text, ")) break;\n");
            c_generator_output_code_block(while_instr->code, indentation_level + 2, false);
            string_add_indentation(gen.text, indentation_level + 1);
            string_append_formated(gen.text, "}\n");
            break;
        }
        case IR_Instruction_Type::BLOCK:
        {
            c_generator_output_code_block(instr->options.block, indentation_level + 1, false);
            break;
        }
        case IR_Instruction_Type::GOTO: {
            string_append_formated(gen.text, "goto upp_label_%d;\n", instr->options.label_index);
            break;
        }
        case IR_Instruction_Type::LABEL: {
            string_append_formated(gen.text, "upp_label_%d: {}\n", instr->options.label_index);
            break;
        }
        case IR_Instruction_Type::RETURN:
        {
            IR_Instruction_Return* return_instr = &instr->options.return_instr;
            switch (return_instr->type)
            {
            case IR_Instruction_Return_Type::EXIT: {
                string_append_formated(gen.text, "exit(%d);\n", (i32)return_instr->options.exit_code.type);
                break;
            }
            case IR_Instruction_Return_Type::RETURN_DATA: {
                string_append_formated(gen.text, "return ");
                c_generator_output_cast_if_necessary(return_instr->options.return_value, code_block->function->function_type->return_type.value);
                c_generator_output_data_access(return_instr->options.return_value);
                string_append_formated(gen.text, ";\n");
                break;
            }
            case IR_Instruction_Return_Type::RETURN_EMPTY: {
                string_append_formated(gen.text, "return;\n");
                break;
            }
            default: panic("What");
            }
            break;
        }
        case IR_Instruction_Type::MOVE: {
            c_generator_output_data_access(instr->options.move.destination);
            string_append_formated(gen.text, " = ");
            c_generator_output_cast_if_necessary(instr->options.move.destination, instr->options.move.source->datatype);
            c_generator_output_data_access(instr->options.move.source);
            string_append_formated(gen.text, ";\n");
            break;
        }
        case IR_Instruction_Type::VARIABLE_DEFINITION:
        {
            auto& def = instr->options.variable_definition;

            c_generator_output_type_reference(def.variable_access->datatype);
            string_append_formated(gen.text, " ");
            c_generator_output_data_access(def.variable_access);

            if (def.initial_value.available) {
                string_append(gen.text, " = ");
                c_generator_output_data_access(def.initial_value.value);
            }
            string_append(gen.text, "; \n");
            break;
        }
        case IR_Instruction_Type::CAST:
        {
            IR_Instruction_Cast* cast = &instr->options.cast;
            c_generator_output_data_access(cast->destination);
            string_append_formated(gen.text, " = ");
            c_generator_output_cast_if_necessary(cast->destination, cast->source->datatype);
            c_generator_output_data_access(cast->source);
            string_append_formated(gen.text, ";\n");
            break;
        }
        case IR_Instruction_Type::FUNCTION_ADDRESS:
        {
            IR_Instruction_Function_Address* addr_of = &instr->options.function_address;
            c_generator_output_data_access(addr_of->destination);
            string_append_formated(gen.text, " = ");

            C_Translation fn_translation;
            fn_translation.type = C_Translation_Type::FUNCTION;
            fn_translation.options.function_slot_index = addr_of->function_slot_index;
            String* fn_name = hashtable_find_element(&gen.translations, fn_translation);
            assert(fn_name != 0, "HEY");
            string_append_formated(gen.text, "&%s;\n", fn_name->characters);
            break;
        }
        case IR_Instruction_Type::UNARY_OP:
        {
            IR_Instruction_Unary_OP* unary = &instr->options.unary_op;
            c_generator_output_data_access(unary->destination);
            string_append_formated(gen.text, " = ");
            switch (unary->type)
            {
            case IR_Unop::NEGATE: {
                string_append_formated(gen.text, "-");
                break;
            }
            case IR_Unop::NOT: {
                string_append_formated(gen.text, "!");
                break;
            }
            default: panic("Hey");
            }
            string_append_formated(gen.text, "(");
            c_generator_output_data_access(unary->source);
            string_append_formated(gen.text, ");\n");
            break;
        }
        case IR_Instruction_Type::BINARY_OP:
        {
            IR_Instruction_Binary_OP* binary = &instr->options.binary_op;
            c_generator_output_data_access(binary->destination);
            string_append_formated(gen.text, " = ");
            c_generator_output_data_access(binary->operand_left);
            string_append_formated(gen.text, " ");
            const char* binary_str = "";
            switch (binary->type)
            {
            case IR_Binop::ADDITION:            binary_str = "+"; break;
            case IR_Binop::SUBTRACTION:         binary_str = "-"; break;
            case IR_Binop::DIVISION:            binary_str = "/"; break;
            case IR_Binop::MULTIPLICATION:      binary_str = "*"; break;
            case IR_Binop::MODULO:              binary_str = "%"; break;
            case IR_Binop::AND:                 binary_str = "&&"; break;
            case IR_Binop::OR:                  binary_str = "||"; break;
            case IR_Binop::BITWISE_AND:         binary_str = "&"; break;
            case IR_Binop::BITWISE_OR:          binary_str = "|"; break;
            case IR_Binop::BITWISE_XOR:         binary_str = "~"; break;
            case IR_Binop::BITWISE_SHIFT_LEFT:  binary_str = "<<"; break;
            case IR_Binop::BITWISE_SHIFT_RIGHT: binary_str = ">>"; break;
            case IR_Binop::EQUAL:               binary_str = "=="; break;
            case IR_Binop::NOT_EQUAL:           binary_str = "!="; break;
            case IR_Binop::LESS:                binary_str = "<"; break;
            case IR_Binop::LESS_OR_EQUAL:       binary_str = "<="; break;
            case IR_Binop::GREATER:             binary_str = ">"; break;
            case IR_Binop::GREATER_OR_EQUAL:    binary_str = ">="; break;
            default: panic("Hey");
            }
            string_append_formated(gen.text, "%s ", binary_str);
            c_generator_output_data_access(binary->operand_right);
            string_append_formated(gen.text, ";\n");
            break;
        }
        default: panic("Hey");
        }
    }

    string_add_indentation(gen.text, indentation_level);
    string_append_formated(gen.text, "}\n");
}

