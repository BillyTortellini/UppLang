#include "c_backend.hpp"

#include "compilation_data.hpp"
#include "../../utility/file_io.hpp"
#include <cstdlib>
#include <Windows.h>
#include "../../win32/windows_helper_functions.hpp"
#include "../../utility/hash_functions.hpp"
#include "semantic_analyser.hpp"
#include "../../win32/process.hpp"
#include "ir_code.hpp"
#include "symbol_table.hpp"
#include "constant_pool.hpp"

// --------------
// - C_COMPILER -
// --------------

const bool ADD_WAIT_BEFORE_EXIT = false;

struct C_Generator;

void c_generator_output_global_access(C_Generator* generator, int global_index);
void c_generator_output_parameter_access(C_Generator* generator, Upp_Function* function, int parameter_index);

static bool global_compiler_environment_variables_loaded = false;

// ---------------
// - C_GENERATOR -
// ---------------
struct IR_Program;
struct Datatype;
struct IR_Code_Block;
struct Upp_Constant;
struct Upp_Function;

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
        return a.options.function == b.options.function;
    case C_Translation_Type::GLOBAL:
        return a.options.global_index == b.options.global_index;
    case C_Translation_Type::CONSTANT:
        return a.options.constant.index == b.options.constant.index &&
            a.options.constant.requires_memory_address == b.options.constant.requires_memory_address;
    case C_Translation_Type::PARAMETER:
        return a.options.parameter.index == b.options.parameter.index &&
            a.options.parameter.function == b.options.parameter.function;
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
    case C_Translation_Type::GLOBAL:
        hash = hash_combine(hash, hash_i32(&t.options.global_index));
        break;
    case C_Translation_Type::FUNCTION:
        hash = hash_combine(hash, hash_pointer(t.options.function));
        break;
    case C_Translation_Type::CONSTANT:
        hash = hash_combine(hash, hash_i32(&t.options.constant.index));
        hash = hash_bool(hash, t.options.constant.requires_memory_address);
        break;
    case C_Translation_Type::PARAMETER:
        hash = hash_combine(hash, hash_pointer(t.options.parameter.function));
        hash = hash_combine(hash, hash_i32(&t.options.parameter.index));
        break;
    default: panic("");
    }
    return hash;
}



struct C_Type_Dependency
{
    DynArray<C_Type_Dependency*> depends_on;
    DynArray<C_Type_Dependency*> dependees;
    DynString type_definition; // e.g. struct A {int value; B b;} 
    Datatype* signature;
    int dependency_count;
};

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

struct Translation_Char_Info
{
    IR_Code_Block* code_block;
    int instruction_index;
    int char_start;
    int char_end;
};

struct C_Generator
{
    Arena* perma_arena;
    Arena* scratch_arena;
    Compilation_Data* compilation_data;
    DynString sections[(int)Generator_Section::MAX_ENUM_VALUE];
    DynString* text; // Current text that's being worked on
    int name_counter;

    DynTable<Datatype*, C_Type_Dependency*> type_to_dependency_mapping;
    DynArray<C_Type_Dependency*> type_dependencies;

    DynArray<Translation_Char_Info> translation_characters;
    C_Program_Translation program_translation;
};



// IMPLEMENTATION
void c_generator_output_type_reference(C_Generator* generator, Datatype* type);
void c_generator_output_code_block(C_Generator* generator, IR_Code_Block* code_block, int indentation_level, bool registers_in_same_scope);
void c_generator_output_data_access(C_Generator* generator, IR_Data_Access* access, bool add_parenthesis_on_pointer_ops = false);
C_Type_Dependency* get_type_dependency(C_Generator* generator, Datatype* datatype);
void c_generator_output_constant_access(C_Generator* generator, Upp_Constant& constant, bool requires_memory_address, int indentation_level);
void output_memory_as_new_constant(C_Generator* generator, byte* base_memory, Datatype* base_type, bool requires_memory_address, int current_indentation_level);


void string_add_indentation(DynString* str, int indentation)
{
    for (int i = 0; i < indentation; i++) {
        dynstring_append_formated(str, "    ");
    }
}

void c_generator_generate_struct_content(C_Generator* generator, Datatype_Struct* structure, C_Type_Dependency* type_dependency, int indentation_level)
{
    auto& gen = *generator;
    DynString* backup_text = gen.text;
    SCOPE_EXIT(gen.text = backup_text);
    gen.text = &type_dependency->type_definition;

    // Generate members + add potential dependencies
    for (int i = 0; i < structure->members.size; i++)
    {
        auto& member = structure->members[i];

        string_add_indentation(gen.text, indentation_level);
        c_generator_output_type_reference(generator, member.datatype);
        gen.text->append(' ')->append(member.name)->append(";\n");

        // Add dependencies if necessary
        auto member_type = member.datatype;
        if (member_type->type == Datatype_Type::STRUCT || member_type->type == Datatype_Type::ARRAY)
        {
            member_type = datatype_get_undecorated(member_type);
            C_Type_Dependency* member_dependency = get_type_dependency(generator, member_type);
            type_dependency->depends_on.push_back(member_dependency);
            member_dependency->dependees.push_back(type_dependency);
            type_dependency->dependency_count += 1;
        }
    }

    // Generate subtypes + tag if existing
    if (structure->subtypes.size > 0)
    {
        string_add_indentation(gen.text, indentation_level);
        gen.text->append("union {\n");
        for (int i = 0; i < structure->subtypes.size; i++)
        {
            Datatype_Struct* child_content = structure->subtypes[i];
            string_add_indentation(gen.text, indentation_level + 1);
            gen.text->append("struct {\n");
            c_generator_generate_struct_content(generator, child_content, type_dependency, indentation_level + 2);
            string_add_indentation(gen.text, indentation_level + 1);
            gen.text->append("} ")->append(child_content->name)->append(";\n");
        }
        string_add_indentation(gen.text, indentation_level);
        gen.text->append("} subtypes_; \n");

        // Add tag member
        string_add_indentation(gen.text, indentation_level);
        c_generator_output_type_reference(generator, structure->tag_member.datatype);
        gen.text->append(" tag_;\n");
    }
}

C_Type_Dependency* get_type_dependency(C_Generator* generator, Datatype* datatype)
{
    auto& gen = *generator;
    Arena* arena = gen.scratch_arena;

    // Check if the dependency already exists
    {
        C_Type_Dependency** existing = gen.type_to_dependency_mapping.find(datatype);
        if (existing != 0) {
            return *existing;
        }
    }

    C_Type_Dependency* dep = arena->allocate<C_Type_Dependency>();
    dep->dependees  = DynArray<C_Type_Dependency*>::create(arena);
    dep->depends_on = DynArray<C_Type_Dependency*>::create(arena);
    dep->dependency_count = 0;
    dep->signature = datatype;
    dep->type_definition = DynString::create(arena);

    gen.type_dependencies.push_back(dep);
    gen.type_to_dependency_mapping.insert(datatype, dep);
    return dep;
}

void c_generator_output_type_reference(C_Generator* generator, Datatype* type)
{
    auto& gen = *generator;
    Arena* arena = gen.scratch_arena;
    Type_System* type_system = gen.compilation_data->type_system;
    auto& types = type_system->predefined_types;

    Type_Modifier_Info mod_info = datatype_get_modifier_info(type);
    type = type_system_make_type_with_modifiers(type_system, mod_info.base_type, mod_info.pointer_level); // Remove optional modifiers and subtype modifiers

    // Check if datatype was already created
    C_Translation translation;
    translation.type = C_Translation_Type::DATATYPE;
    translation.options.datatype = type;
    {
        String* translated = gen.program_translation.name_mapping.find(translation);
        if (translated != 0) {
            gen.text->append(translated);
            return;
        }
    }

    // Otherwise generate the type reference
    DynString constant_string = DynString::create(arena);;
    DynString access_name = dynstring_create(arena);
    DynString* backup_text = gen.text;

    switch (type->type)
    {
    case Datatype_Type::PRIMITIVE:
    {
        auto primitive = downcast<Datatype_Primitive>(type);
        int type_size = type->memory_info.value.size;

        switch (primitive->primitive_type)
        {
        case Primitive_Type::INT:  dynstring_append(&access_name, "i64"); break;
        case Primitive_Type::UINT: dynstring_append(&access_name, "u64"); break;
        case Primitive_Type::I8:  dynstring_append(&access_name, "i8"); break;
        case Primitive_Type::I16: dynstring_append(&access_name, "i16"); break;
        case Primitive_Type::I32: dynstring_append(&access_name, "i32"); break;
        case Primitive_Type::I64: dynstring_append(&access_name, "i64"); break;
        case Primitive_Type::U8:  dynstring_append(&access_name, "u8"); break;
        case Primitive_Type::U16: dynstring_append(&access_name, "u16"); break;
        case Primitive_Type::U32: dynstring_append(&access_name, "u32"); break;
        case Primitive_Type::U64: dynstring_append(&access_name, "u64"); break;
        case Primitive_Type::F32: dynstring_append(&access_name, "f32"); break;
        case Primitive_Type::F64: dynstring_append(&access_name, "f64"); break;
        case Primitive_Type::BOOLEAN: dynstring_append(&access_name, "bool"); break;
        default: break;
        }
        break;
    }
    case Datatype_Type::BUILT_IN:
    {
        auto builtin = downcast<Datatype_Builtin>(type);
        switch (builtin->builtin_type)
        {
        case Builtin_Type::RAWPTR:      dynstring_append(&access_name, "_void_ptr"); break; // See datatypes.h
        case Builtin_Type::TYPE_HANDLE: dynstring_append(&access_name, "Type_Handle_"); break; // See hardcoded_functions.h for definition
        case Builtin_Type::ANY:         dynstring_append(&access_name, "Upp_Any_"); break;
        case Builtin_Type::STRING:      dynstring_append(&access_name, "Upp_String_"); break;
        case Builtin_Type::C_STRING:    dynstring_append(&access_name, "const char*"); break;
        case Builtin_Type::C_CHAR:      dynstring_append(&access_name, "char"); break;
        case Builtin_Type::CODE_POINT:  dynstring_append(&access_name, "u32"); break;
        default: panic("");
        }
        break;
    }
    case Datatype_Type::PATTERN_VARIABLE:
    case Datatype_Type::UNKNOWN_TYPE:
    {
        dynstring_append(&access_name, "UNUSED_TYPE_BACKEND_"); // See hardcoded_functions.h for definition
        break;
    }
    case Datatype_Type::ENUM:
    {
        auto enum_type = downcast<Datatype_Enum>(type);
        auto& members = enum_type->members;
        access_name.append(enum_type->name)->append_formated("_Enum_%d", gen.name_counter);
        gen.name_counter++;

        DynString* enum_section = &gen.sections[(int)Generator_Section::ENUM_DECLARATIONS];
        enum_section->append("enum class ")->append(access_name)->append(" : i64\n{\n");
        for (int i = 0; i < members.size; i++) {
            auto member = &members[i];
            enum_section->append("    ")->append(member->name)->append_formated(" = %d,\n", member->value);
        }
        dynstring_append_formated(enum_section, "};\n");
        break;
    }
    case Datatype_Type::SLICE:
    {
        auto slice_type = downcast<Datatype_Slice>(type);
        dynstring_append_formated(&access_name, "Slice_%d", gen.name_counter);
        gen.name_counter++;

        DynString* section_prototypes = &gen.sections[(int)Generator_Section::STRUCT_PROTOTYPES];
        DynString* section_structs = &gen.sections[(int)Generator_Section::STRUCT_AND_ARRAY_DECLARATIONS];
        section_prototypes->append("struct ")->append(access_name)->append(";\n");

        // Temporary c_string is required when calling this function recursively
        DynString tmp = dynstring_create(arena);

        gen.text = &tmp;
        gen.text->append("struct ")->append(access_name)->append(" {\n    ");
        c_generator_output_type_reference(generator, slice_type->data_member.datatype);
        gen.text->append(" data;\n    u64 size;\n};\n\n");

        // Now we write to struct section
        gen.text = section_structs;
        gen.text->append(tmp);

        break;
    }
    case Datatype_Type::FUNCTION_POINTER:
    {
        auto signature = downcast<Datatype_Function_Pointer>(type)->signature;
        auto& parameters = signature->parameters;
        dynstring_append_formated(&access_name, "fptr_%d", gen.name_counter);
        gen.name_counter++;

        // Temporary c_string is required when calling this function recursively
        DynString tmp = dynstring_create(arena);

        gen.text = &tmp;
        gen.text->append("typedef ");
        if (signature->return_type().available) {
            c_generator_output_type_reference(generator, signature->return_type().value);
        }
        else {
            gen.text->append("void");
        }

        gen.text->append(" (*")->append(access_name)->append(")(");
        bool require_comma = false;
        for (int i = 0; i < parameters.size; i++) {
            auto& param = parameters[i];
            if (i == signature->return_type_index) continue;
            if (require_comma) {
                dynstring_append_formated(gen.text, ", ");
            }
            require_comma = true;
            c_generator_output_type_reference(generator, param.datatype);
            gen.text->append(" ")->append(param.name);
        }
        gen.text->append(");\n\n");

        gen.text = &gen.sections[(int)Generator_Section::TYPE_DECLARATIONS];
        gen.text->append(tmp);
        break;
    }
    case Datatype_Type::STRUCT:
    {
        auto structure = downcast<Datatype_Struct>(type);
        assert(structure->parent == nullptr, "Should be handled at start of this function");
        auto& members = structure->members;

        if (structure->base.contains_pattern) {
            dynstring_append_formated(&access_name, "UNUSED_TYPE_BACKEND_"); // See hardcoded_functions.h for definition
            break;
        }

        // Extern structs should be accessible by name alone (And forward definition/definition should be in an included header)
        if (structure->upp_struct->is_extern_struct) {
            dynstring_append(&access_name, structure->name);
            break;
        }

        // Because structs can contain references to themselves, we need to register the access name before generating the members
        if (structure->upp_struct->is_union) {
            access_name.append(structure->name)->append_formated("_Struct_%d", gen.name_counter);
        }
        else {
            access_name.append(structure->name)->append_formated("_Uniont_%d", gen.name_counter);
        }
        gen.name_counter += 1;
        gen.program_translation.name_mapping.insert(translation, string_allocate_copy(access_name.to_string(), generator->perma_arena->upcast()));
        gen.sections[(int)Generator_Section::STRUCT_PROTOTYPES].append(
            structure->upp_struct->is_union ? "union " : " struct")->append(access_name)->append(";\n");

        // Generate struct content
        C_Type_Dependency* dependency = get_type_dependency(generator, type);
        gen.text = &dependency->type_definition;
        dynstring_append(gen.text, structure->upp_struct->is_union ? "union " : "struct ");

        // Handle extern structs (No members, but size and alignment can be different
        if (structure->members.size == 0 && structure->subtypes.size == 0) {
            gen.text
                ->append_formated("alignas(%d) ", structure->base.memory_info.value.alignment)
                ->append(access_name)
                ->append_formated(" {\n    u8 values[%d];\n};\n", structure->base.memory_info.value.size);
        }
        else
        {
            gen.text->append(access_name)->append(" {\n");
            c_generator_generate_struct_content(generator, structure, dependency, 1);
            gen.text->append("};\n\n");
        }

        // We return early because we don't want to insert into the translation table twice
        gen.text = backup_text;
        gen.text->append(access_name);
        return;
    }
    case Datatype_Type::ARRAY:
    {
        auto array_type = downcast<Datatype_Array>(type);
        dynstring_append_formated(&access_name, "Array_%d", gen.name_counter);
        gen.name_counter++;
        gen.sections[(int)Generator_Section::STRUCT_PROTOTYPES].append("struct ")->append(access_name)->append(";\n");

        // Similar to structs we insert the names early
        gen.program_translation.name_mapping.insert(translation, string_allocate_copy(access_name.to_string(), generator->perma_arena->upcast()));

        // Generate array definition
        C_Type_Dependency* dependency = get_type_dependency(generator, type);
        gen.text = &dependency->type_definition;
        dynstring_append_formated(gen.text, "struct %s {\n", access_name.characters);
        string_add_indentation(gen.text, 1);
        // Note: Here we use the non-const type, because:
        //      In type_system, if the element_type is constant, the array_type is also constant
        //      In C, if a struct is constant, the array in the struct is also automatically constant
        //      By not making the values constant, array_initializers will still work by using a temporary, non-const array
        c_generator_output_type_reference(generator, array_type->element_type);
        dynstring_append_formated(gen.text, " values[%d];\n};\n", array_type->element_count);

        // Add dependency if necessary
        auto member_type = array_type->element_type;
        if (member_type->type == Datatype_Type::STRUCT || member_type->type == Datatype_Type::ARRAY)
        {
            member_type = datatype_get_undecorated(member_type, true, true, true);
            C_Type_Dependency* member_dependency = get_type_dependency(generator, member_type);
            dependency->depends_on.push_back(member_dependency);
            member_dependency->dependees.push_back(dependency);
            dependency->dependency_count += 1;
        }

        // We return early because we don't want to insert into the translation table twice
        gen.text = backup_text;
        gen.text->append(access_name.characters);
        return;
    }
    case Datatype_Type::POINTER: 
    {
        Datatype_Pointer* pointer_type = downcast<Datatype_Pointer>(type);
        Datatype* child_type = pointer_type->element_type;
        gen.text = &access_name;
        c_generator_output_type_reference(generator, child_type);
        access_name.append("*");
        break;
    }
    default: panic("Hey");
    }

    // Insert translation into table and append access to text
    gen.program_translation.name_mapping.insert(translation, string_allocate_copy(access_name.to_string(), generator->perma_arena->upcast()));
    gen.text = backup_text;
    gen.text->append(access_name);
}

C_Program_Translation* c_backend_generate_program(Compilation_Data* compilation_data)
{
    Arena* arena = &compilation_data->arena;
    SCRATCH_ARENA_MAKE_SCOPED(arena);

    // Initialize generator
    C_Generator generator_on_stack;
    C_Generator* generator = &generator_on_stack;
    generator->scratch_arena = scratch_arena;
    generator->perma_arena = &compilation_data->arena;
    for (int i = 0; i < (int)Generator_Section::MAX_ENUM_VALUE; i++) {
        generator->sections[i] = DynString::create(scratch_arena);
    }
    generator->compilation_data = compilation_data;
    generator->program_translation.line_infos = DynArray<C_Line_Info>::create(generator->perma_arena);
    generator->program_translation.name_mapping = DynTable<C_Translation, String>::create(generator->perma_arena, c_translation_hash, c_translation_is_equal);
    generator->program_translation.source_code = DynString::create(generator->perma_arena);
    generator->type_dependencies          = DynArray<C_Type_Dependency*>::create(scratch_arena);
    generator->type_to_dependency_mapping = DynTable<Datatype*, C_Type_Dependency*>::create_pointer(scratch_arena);
    generator->translation_characters     = DynArray<Translation_Char_Info>::create(scratch_arena);
    generator->name_counter = 0;
    generator->text = 0;

    auto& gen = *generator;
    Type_System* type_system = gen.compilation_data->type_system;
    auto& types = type_system->predefined_types;
    auto& ids = gen.compilation_data->predefined_ids;
    auto& globals = gen.compilation_data->globals;

    // Create globals Translations
    {
        gen.text = &gen.sections[(int)Generator_Section::GLOBALS];
        for (int i = 0; i < globals.size; i++)
        {
            auto global = globals[i];
            auto type = global->type;

            if (global->is_extern) {
                // Extern globals should be defined in included headers
                continue;
            }

            c_generator_output_type_reference(generator, type);
            gen.text->append(" ");
            c_generator_output_global_access(generator, i);
            gen.text->append(";\n");
        }
    }

    auto append_function_signature = [&](String function_access_name, Upp_Function* function)
    {
        Call_Signature* signature = function->signature;
        if (signature->return_type().available) {
            c_generator_output_type_reference(generator, signature->return_type().value);
        }
        else {
            gen.text->append("void");
        }
        gen.text->append(" ")->append(function_access_name)->append('(');

        auto& parameters = signature->parameters;
        bool require_comma = false;
        for (int j = 0; j < parameters.size; j++)
        {
            auto& param = parameters[j];
            if (j == signature->return_type_index) continue;
            if (require_comma) {
                gen.text->append(", ");
            }
            require_comma = true;

            c_generator_output_type_reference(generator, param.datatype);
            gen.text->append(" ");

            // Note: This has to be the same name as in output_data_access for parameter access
            c_generator_output_parameter_access(generator, function, j);
        }
        gen.text->append(")");
    };

    // Create function prototypes (Required before code-generation, as functions calling other functions require the translation)
    {
        for (int i = 0; i < compilation_data->functions.size; i++)
        {
            Upp_Function* function = compilation_data->functions[i];
            if (function->ir_block == nullptr) {
                continue;
            }
            assert(!function->is_extern, "Extern functions should not be generated by ir_code");

            // Special case for entry-function (Is not called, does not require forward definition)
            if (function == compilation_data->entry_function) {
                continue;
            }

            DynString access_name = dynstring_create(generator->scratch_arena);
            access_name.append(function->name);
            dynstring_append_formated(&access_name, "_%d", gen.name_counter);
            gen.name_counter++;

            C_Translation translation;
            translation.type = C_Translation_Type::FUNCTION;
            translation.options.function = function;
            gen.program_translation.name_mapping.insert(
                translation, string_allocate_copy(access_name.to_string(), generator->perma_arena->upcast())
            );

            // Generate prototype
            gen.text = &gen.sections[(int)Generator_Section::FUNCTION_PROTOTYPES];
            append_function_signature(access_name.to_string(), function);
            gen.text->append(";\n");
        }

        // Generate extern function translations (Aren't included in ir functions)
        auto& extern_fns = gen.compilation_data->extern_sources.extern_functions;
        for (int i = 0; i < extern_fns.size; i++)
        {
            auto extern_function = extern_fns[i];
            assert(extern_function->is_extern, "Should be extern");

            C_Translation translation;
            translation.type = C_Translation_Type::FUNCTION;
            translation.options.function = extern_function;
            gen.program_translation.name_mapping.insert(
                translation, 
                string_allocate_copy(extern_function->name.to_string(), generator->perma_arena->upcast())
            );
        }
    }

    // Create functions
    for (int i = 0; i < compilation_data->functions.size; i++)
    {
        Upp_Function* function = compilation_data->functions[i];
        if (function->ir_block == nullptr) continue;

        Call_Signature* signature = function->signature;
        auto& parameters = signature->parameters;

        // Generate function signature into tmp string
        gen.text = &gen.sections[(int)Generator_Section::FUNCTION_IMPLEMENTATION];
        {
            if (function != compilation_data->entry_function)
            {
                C_Translation fn_translation;
                fn_translation.type = C_Translation_Type::FUNCTION;
                fn_translation.options.function = function;
                String* fn_name = gen.program_translation.name_mapping.find(fn_translation);
                assert(fn_name != 0, "");

                append_function_signature(*fn_name, function);
            }
            else {
                append_function_signature(string_make("upp_entry_"), function);
            }
        }

        // Generate function body
        gen.text->append("\n");
        c_generator_output_code_block(generator, function->ir_block, 0, false);
        gen.text->append("\n");
    }

    // Create type_info init function
    {
        // Create Holder struct
        gen.text = &gen.sections[(int)Generator_Section::CONSTANT_ARRAY_HOLDERS];
        gen.text->append("struct Type_Information_Holder_ {\n    ");
        c_generator_output_type_reference(generator, upcast(types.type_information_type));
        dynstring_append_formated(gen.text, " infos[%d];\n};\n", type_system->types.size);

        // Create constant
        gen.text = &gen.sections[(int)Generator_Section::CONSTANTS];
        gen.text->append("Type_Information_Holder_ type_infos_;\n");

        // Create initialization function
        gen.text = &gen.sections[(int)Generator_Section::FUNCTION_PROTOTYPES];
        gen.text->append("void inititalize_type_infos_global_();\n");
        gen.text = &gen.sections[(int)Generator_Section::FUNCTION_IMPLEMENTATION];
        gen.text->append("void inititalize_type_infos_global_() {\n");
        string_add_indentation(gen.text, 1);
        c_generator_output_type_reference(generator, upcast(types.type_information_type));
        gen.text->append("* info = nullptr;\n");
        string_add_indentation(gen.text, 1);

        for (int i = 0; i < type_system->types.size; i++)
        {
            auto type = type_system->types[i];
            assert(type->memory_info.available, "Should be the case at this point");
            auto& memory = type->memory_info.value;

            // Set base info
            string_add_indentation(gen.text, 1);
            dynstring_append_formated(gen.text, "info = &type_infos_.infos[%d];\n", i);
            string_add_indentation(gen.text, 1);
            dynstring_append_formated(gen.text, "info->type      = %d;\n", type->type_handle.index);
            string_add_indentation(gen.text, 1);
            dynstring_append_formated(gen.text, "info->size      = %d;\n", memory.size);
            string_add_indentation(gen.text, 1);
            dynstring_append_formated(gen.text, "info->alignment = %d;\n", memory.alignment);
            string_add_indentation(gen.text, 1);
            gen.text->append("info->tag_      = ");
            output_memory_as_new_constant(
                generator, 
                (byte*)&type_system->types[i]->internal_info->tag, 
                downcast<Datatype_Struct>(types.type_information_type)->tag_member.datatype, false, 1
            );
            gen.text->append(";\n");

            // Set type-specific infos
            string_add_indentation(gen.text, 1);
            switch (type->type)
            {
            case Datatype_Type::PRIMITIVE: {
                auto primitive = downcast<Datatype_Primitive>(type);
                gen.text->append("info->subtypes_.Primitive.type = ");
                output_memory_as_new_constant(generator, (byte*)&primitive->primitive_type, upcast(types.primitive_type_enum), false, 1);
                gen.text->append(";\n");
                break;
            }
            case Datatype_Type::ARRAY: {
                auto array_type = downcast<Datatype_Array>(type);
                dynstring_append_formated(gen.text, "info->subtypes_.Array.element_type = %d;\n", array_type->element_type->type_handle.index);
                string_add_indentation(gen.text, 1);
                dynstring_append_formated(gen.text, "info->subtypes_.Array.size         = %d;\n", array_type->element_count);
                break;
            }
            case Datatype_Type::BUILT_IN: 
            {
                auto built_in = downcast<Datatype_Builtin>(type);
                gen.text->append("info->subtypes_.Builtin.type = (");
                c_generator_output_type_reference(generator, types.builtin_type_enum);
                dynstring_append_formated(gen.text, ")%d;\n", (int)built_in->builtin_type); // Do i need plus one here?
                string_add_indentation(gen.text, 1);
                break;
            }
            case Datatype_Type::POINTER: {
                auto pointer = downcast<Datatype_Pointer>(type);
                dynstring_append_formated(gen.text, "info->subtypes_.Pointer.element_type = %d;\n", pointer->element_type->type_handle.index);
                break;
            }
            case Datatype_Type::SLICE: {
                auto slice = downcast<Datatype_Slice>(type);
                dynstring_append_formated(gen.text, "info->subtypes_.Slice.element_type = %d;\n", slice->element_type->type_handle.index);
                break;
            }
            case Datatype_Type::ENUM:
            {
                auto enumeration = downcast<Datatype_Enum>(type);
                auto& internal_info = type_system->types[i]->internal_info->options.enumeration;
                dynstring_append_formated(gen.text, "info->subtypes_.Enum.name = ");
                output_memory_as_new_constant(generator, (byte*)&internal_info.name, types.string->upcast(), false, 1);
                gen.text->append(";\n");

                string_add_indentation(gen.text, 1);
                dynstring_append_formated(gen.text, "info->subtypes_.Enum.members.size = %d;\n", internal_info.members.size);
                string_add_indentation(gen.text, 1);
                if (internal_info.members.size > 0)
                {
                    dynstring_append_formated(gen.text, "info->subtypes_.Enum.members.data = new ");
                    c_generator_output_type_reference(generator, upcast(types.internal_enum_member_info_type)); // Check if this works
                    dynstring_append_formated(gen.text, "[%d];\n", internal_info.members.size);
                    for (int j = 0; j < internal_info.members.size; j++) {
                        auto& member = internal_info.members.data[j];
                        string_add_indentation(gen.text, 1);
                        dynstring_append_formated(gen.text, "info->subtypes_.Enum.members.data[%d].name = ", j);
                        output_memory_as_new_constant(generator, (byte*)&member.name, types.string->upcast(), false, 1);
                        gen.text->append(";\n");
                        string_add_indentation(gen.text, 1);
                        dynstring_append_formated(gen.text, "info->subtypes_.Enum.members.data[%d].value = %d;\n", j, member.value);
                    }
                }
                else {
                    dynstring_append_formated(gen.text, "info->subtypes_.Enum.members.data = nullptr;\n");
                }
                break;
            }
            case Datatype_Type::FUNCTION_POINTER:
            {
                auto signature = downcast<Datatype_Function_Pointer>(type)->signature;
                auto parameters = signature->parameters;
                auto return_type = signature->return_type();

                // string_append_formated(gen.text, "info->subtypes_.Function.return_type = %d;\n", 
                //     return_type.available ? return_type.value->type_handle.index : -1);
                // string_add_indentation(gen.text, 1);
                dynstring_append_formated(gen.text, "info->subtypes_.Function_Pointer.has_return_type = %s;\n", return_type.available ? "true" : "false");
                string_add_indentation(gen.text, 1);
                dynstring_append_formated(gen.text, "info->subtypes_.Function_Pointer.parameter_types.size = %d;\n", parameters.size);
                if (parameters.size != 0) 
                {
                    string_add_indentation(gen.text, 1);
                    dynstring_append_formated(gen.text, "info->subtypes_.Function_Pointer.parameter_types.data = new ");
                    c_generator_output_type_reference(generator, upcast(types.type_handle)); // Check if this works
                    dynstring_append_formated(gen.text, "[%d];\n", parameters.size);
                    for (int j = 0; j < parameters.size; j++) {
                        auto& param = parameters[j];
                        string_add_indentation(gen.text, 1);
                        dynstring_append_formated(
                            gen.text, "info->subtypes_.Function_Pointer.parameter_types.data[%d] = %d;\n", j, param.datatype->type_handle.index);
                    }
                }
                else {
                    string_add_indentation(gen.text, 1);
                    dynstring_append_formated(gen.text, "info->subtypes_.Function_Pointer.parameter_types.data = nullptr;\n");
                }
                break;
            }
            case Datatype_Type::STRUCT:
            {
                int indentation_level = 1;
                const char* access_prefix = "info->subtypes_.Struct";

                auto structure = downcast<Datatype_Struct>(type);
                dynstring_append_formated(gen.text, "%s.is_union = %s;\n", access_prefix, structure->upp_struct->is_union ? "true" : "false");
                string_add_indentation(gen.text, indentation_level);

                dynstring_append_formated(gen.text, "%s.name = ", access_prefix);
                Upp_String upp_string = upp_string_from_id(structure->name);
                output_memory_as_new_constant(generator, (byte*)&upp_string, types.string->upcast(), false, 1);
                gen.text->append(";\n");

                // Generate tag member info
                if (structure->subtypes.size > 0)
                {
                    const char* access_prefix = "info->subtypes_.Struct.tag_member";
                    string_add_indentation(gen.text, indentation_level);
                    dynstring_append_formated(gen.text, "%s.name = ", access_prefix);
                    upp_string = upp_string_from_id(structure->tag_member.name);
                    output_memory_as_new_constant(generator, (byte*)&upp_string, types.string->upcast(), false, 1);
                    gen.text->append(";\n");
                    string_add_indentation(gen.text, indentation_level);
                    dynstring_append_formated(gen.text, "%s.type = %d;\n", access_prefix, structure->tag_member.datatype->type_handle.index);
                    string_add_indentation(gen.text, indentation_level);
                    dynstring_append_formated(gen.text, "%s.offset = %d;\n", access_prefix, structure->tag_member.offset);
                }

                // Generate members
                string_add_indentation(gen.text, indentation_level);
                dynstring_append_formated(gen.text, "%s.members.size = %d;\n", access_prefix, structure->members.size);
                if (structure->members.size != 0)
                {
                    string_add_indentation(gen.text, indentation_level);
                    dynstring_append_formated(gen.text, "%s.members.data = new ", access_prefix);
                    c_generator_output_type_reference(generator, upcast(types.internal_member_info_type));
                    dynstring_append_formated(gen.text, "[%d];\n", structure->members.size);
                    for (int i = 0; i < structure->members.size; i++) {
                        auto& member = structure->members[i];
                        string_add_indentation(gen.text, indentation_level);
                        dynstring_append_formated(gen.text, "%s.members.data[%d].name = ", access_prefix, i);
                        upp_string = upp_string_from_id(member.name);
                        output_memory_as_new_constant(generator, (byte*)&upp_string, types.string->upcast(), false, 1);
                        gen.text->append(";\n");

                        string_add_indentation(gen.text, indentation_level);
                        dynstring_append_formated(gen.text, "%s.members.data[%d].type = %d;\n", access_prefix, i, member.datatype->type_handle.index);
                        string_add_indentation(gen.text, indentation_level);
                        dynstring_append_formated(gen.text, "%s.members.data[%d].offset = %d;\n", access_prefix, i, member.offset);
                    }
                }
                else { // Extern c struct i guess?
                    string_add_indentation(gen.text, indentation_level);
                    dynstring_append_formated(gen.text, "%s.members.data = nullptr;\n", access_prefix);
                }

                // Generate subtypes
                string_add_indentation(gen.text, indentation_level);
                dynstring_append_formated(gen.text, "%s.subtypes.size = %d;\n", access_prefix, structure->subtypes.size);
                if (structure->subtypes.size > 0)
                {
                    string_add_indentation(gen.text, indentation_level);
                    dynstring_append_formated(gen.text, "%s.subtypes.data = new ", access_prefix);
                    c_generator_output_type_reference(generator, upcast(types.type_handle));
                    dynstring_append_formated(gen.text, "[%d];\n", structure->subtypes.size);
                    for (int i = 0; i < structure->subtypes.size; i++) {
                        string_add_indentation(gen.text, indentation_level);
                        dynstring_append_formated(gen.text, "%s.subtypes.data[%d] = %d;\n", access_prefix, i, structure->subtypes[i]->base.type_handle.index);
                    }
                }
                else {
                    string_add_indentation(gen.text, indentation_level);
                    dynstring_append_formated(gen.text, "%s.subtypes.data = nullptr;\n", access_prefix);
                }

                break;
            }
            case Datatype_Type::UNKNOWN_TYPE:
            case Datatype_Type::PATTERN_VARIABLE:
            case Datatype_Type::VOID_TYPE:
                break; // Nothing to do on these types
            default: panic("");
            }

            gen.text->append("\n");
        }

        gen.text->append("}\n");
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
            gen.text->append(dependency->type_definition);
            gen.text->append("\n");
        }

        // Sanity test, check if everything was resolved
        for (int i = 0; i < gen.type_dependencies.size; i++) {
            C_Type_Dependency* dependency = gen.type_dependencies[i];
            assert(dependency->dependency_count == 0, "Everything should be resolved now!");
        }
    }

    // Combine all sections + entry into one string 
    auto& source_code = gen.program_translation.source_code;
    dynstring_reset(&source_code);
    int function_implementation_char_index = -1;
    {
        gen.sections[(int)Generator_Section::FUNCTION_IMPLEMENTATION].append(
            "\nint main(int argc, char** argv) {\n    inititalize_type_infos_global_(); \n    upp_entry_();\n"
        );
        if (ADD_WAIT_BEFORE_EXIT) {
            gen.sections[(int)Generator_Section::FUNCTION_IMPLEMENTATION].append("    printf(\"\\n\\nEND OF PROGRAM\");\n");
            gen.sections[(int)Generator_Section::FUNCTION_IMPLEMENTATION].append("    std::cin.ignore();\n");
        }
        gen.sections[(int)Generator_Section::FUNCTION_IMPLEMENTATION].append("    return 0;\n}\n\n");

        // Combine sections into one program
        dynstring_append_formated(&source_code, "/* INTRODUCTION\n----------------*/\n");
        source_code.append("#pragma once\n#include <cstdlib>\n#include \"../hardcoded/hardcoded_functions.h\"\n#include \"../hardcoded/datatypes.h\"\n\n");
        source_code.append("#include <iostream>\n#include <cstdio>\n");
        // string_append_formated(&source_code, "/* EXTERN HEADERS\n----------------*/\n");
        // string_append_string(&source_code, &section_extern_includes);
        // string_append_formated(&source_code, "\n/* STRING_DATA\n----------------*/\n");
        // string_append_string(&source_code, &generator->section_string_data);
        dynstring_append_formated(&source_code, "\n/* ENUMS\n----------------*/\n");
        source_code.append(gen.sections[(int)Generator_Section::ENUM_DECLARATIONS]);
        dynstring_append_formated(&source_code, "\n/* STRUCT_PROTOTYPES\n----------------*/\n");
        source_code.append(gen.sections[(int)Generator_Section::STRUCT_PROTOTYPES]);
        dynstring_append_formated(&source_code, "\n/* TYPE_DECLARATIONS\n------------------*/\n");
        source_code.append(gen.sections[(int)Generator_Section::TYPE_DECLARATIONS]);
        dynstring_append_formated(&source_code, "\n/* STRUCT_IMPLEMENTATIONS\n----------------*/\n");
        source_code.append(gen.sections[(int)Generator_Section::STRUCT_AND_ARRAY_DECLARATIONS]);
        dynstring_append_formated(&source_code, "\n/* ARRAY_HOLDER_SECTION\n----------------*/\n");
        source_code.append(gen.sections[(int)Generator_Section::CONSTANT_ARRAY_HOLDERS]);
        dynstring_append_formated(&source_code, "\n/* FUNCTION PROTOTYPES\n------------------*/\n"); // Need to be declared before constants for function pointers constants to work
        source_code.append(gen.sections[(int)Generator_Section::FUNCTION_PROTOTYPES]);
        dynstring_append_formated(&source_code, "\n/* CONSTANTS\n------------------*/\n");
        source_code.append(gen.sections[(int)Generator_Section::CONSTANTS]);
        dynstring_append_formated(&source_code, "\n/* GLOBALS\n------------------*/\n");
        source_code.append(gen.sections[(int)Generator_Section::GLOBALS]);
        dynstring_append_formated(&source_code, "\n/* FUNCTIONS\n------------------*/\n");
        function_implementation_char_index = source_code.size;
        source_code.append(gen.sections[(int)Generator_Section::FUNCTION_IMPLEMENTATION]);
    }

    // Write to file
    file_io_write_text_file(string_make("backend/src/main.cpp"), source_code.to_string());

    // Calculate line-translations
    {
        // Count lines before function implementation starts
        {
            int line_offset = 0;
            for (int i = 0; i < function_implementation_char_index; i++) {
                auto c = source_code.characters[i];
                if (c == '\n') {
                    line_offset += 1;
                }
            }
            gen.program_translation.line_offset = line_offset;
        }

        int last_line_start = function_implementation_char_index;
        int next_char_translation_index = 0;
        Dynamic_Array<int> active_translations_indices = dynamic_array_create<int>(4);
        SCOPE_EXIT(dynamic_array_destroy(&active_translations_indices));
        for (int char_index = function_implementation_char_index; char_index < source_code.size + 1; char_index++)
        {
            char c;
            if (char_index != source_code.size) {
                c = source_code[char_index];
            }
            else {
                c = '\n';
            }
            if (c != '\n') {
                continue;
            }

            int line_start = last_line_start;
            int line_end = char_index;
            last_line_start = char_index + 1;

            // Activate all translations which we moved over
            while (next_char_translation_index < gen.translation_characters.size)
            {
                auto& translation = gen.translation_characters[next_char_translation_index];
                if (translation.char_start <= line_end - function_implementation_char_index) {
                    dynamic_array_push_back(&active_translations_indices, next_char_translation_index);
                    next_char_translation_index += 1;
                }
                else {
                    break;
                }
            }

            // Remove all non-active translations
            for (int i = 0; i < active_translations_indices.size; i++) {
                int translation_index = active_translations_indices[i];
                auto& translation = gen.translation_characters[translation_index];
                // Check if translation was already passed by
                if (translation.char_end <= line_start - function_implementation_char_index) {
                    dynamic_array_swap_remove(&active_translations_indices, i);
                    i -= 1;
                }
            }

            C_Line_Info line_info;
            line_info.ir_block = nullptr;
            line_info.instruction_index = -1;
            line_info.line_start_index = line_start;
            line_info.line_end_index = line_end;

            // Find smallest active translation
            int smallest_index = -1;
            int smallest_char_length = 100000000;
            for (int i = 0; i < active_translations_indices.size; i++) {
                auto& translation = gen.translation_characters[active_translations_indices[i]];
                int length = translation.char_end - translation.char_start;
                if (length < smallest_char_length) {
                    smallest_index = i;
                    smallest_char_length = length;
                }
            }

            if (smallest_index != -1) {
                auto& translation = gen.translation_characters[active_translations_indices[smallest_index]];
                line_info.ir_block = translation.code_block;
                line_info.instruction_index = translation.instruction_index;
            }

            gen.program_translation.line_infos.push_back(line_info);
        }
    }

    C_Program_Translation* result = compilation_data->arena.allocate<C_Program_Translation>();
    *result = generator->program_translation;
    return result;
}

// Outputs "{" + the struct content on indentation level + 1 and "}"
void output_struct_content_block_recursive(C_Generator* generator, Datatype_Struct* structure, byte* struct_start_memory, int current_indentation_level)
{
    auto& gen = *generator;
    auto& members = structure->members;
    int block_indentation = current_indentation_level + 1;
    gen.text->append("{\n");
    string_add_indentation(gen.text, block_indentation);
    for (int i = 0; i < members.size; i++)
    {
        auto& member = members[i];
        byte* member_memory = struct_start_memory + member.offset;

        // Generate designator
        gen.text->append(".")->append(member.name)->append(" = ");
        output_memory_as_new_constant(generator, member_memory, member.datatype, false, block_indentation);
        if (i != members.size - 1) {
            gen.text->append(", \n");
            string_add_indentation(gen.text, block_indentation);
        }
    }

    if (structure->subtypes.size > 0)
    {
        int subtype_index = (*(int*)(struct_start_memory + structure->tag_member.offset)) - 1;
        assert(subtype_index >= 0 && subtype_index < structure->subtypes.size, "");
        Datatype_Struct* child_structure = structure->subtypes[subtype_index];
        gen.text->append(".subtypes_ = { .")->append(child_structure->name)->append(" = ");
        output_struct_content_block_recursive(generator, child_structure, struct_start_memory, block_indentation);
        gen.text->append("}, \n");

        // Set tag
        string_add_indentation(gen.text, block_indentation);
        gen.text->append("tag_ = ");
        byte* tag_memory = struct_start_memory + structure->tag_member.offset;
        output_memory_as_new_constant(generator, tag_memory, structure->tag_member.datatype, false, block_indentation);
    }

    gen.text->append("\n");
    string_add_indentation(gen.text, current_indentation_level);
    gen.text->append("}");
}

void c_generator_output_constant_access(C_Generator* generator, Upp_Constant& constant, bool requires_memory_address, int indentation_level)
{
    auto& gen = *generator;
    auto& types = gen.compilation_data->type_system->predefined_types;
    DynString* backup_text = gen.text;
    SCOPE_EXIT(gen.text = backup_text);

    Datatype* type = constant.type;
    if (type->type == Datatype_Type::ARRAY || 
        type->type == Datatype_Type::SLICE ||
        type->type == Datatype_Type::STRUCT ||
        datatype_is_builtin_type(type, Builtin_Type::STRING) || 
        datatype_is_builtin_type(type, Builtin_Type::ANY)) 
    {
        requires_memory_address = true;
    }

    C_Translation constant_translation;
    constant_translation.type = C_Translation_Type::CONSTANT;
    constant_translation.options.constant.index = constant.constant_index;
    constant_translation.options.constant.requires_memory_address = requires_memory_address;
    {
        String* access_name = gen.program_translation.name_mapping.find(constant_translation);
        if (access_name != 0) {
            gen.text->append(access_name);
            return;
        }
    }

    // Create access
    DynString access_name = dynstring_create(gen.scratch_arena);
    gen.text = &access_name;
    {
        DynString* backup_text = gen.text;
        SCOPE_EXIT(gen.text = backup_text);

        Datatype* base_type = type;
        byte* base_memory = constant.memory;

        DynString constant_string = DynString::create(gen.scratch_arena);

        if (requires_memory_address)
        {
            gen.text = &constant_string;
            c_generator_output_type_reference(generator, base_type);
            dynstring_append_formated(gen.text, " const_%d = ", gen.name_counter);
            dynstring_append_formated(backup_text, "const_%d", gen.name_counter);
            gen.name_counter += 1;
        }

        // Generate constant access
        int type_size = type->memory_info.value.size;
        switch (type->type)
        {
        case Datatype_Type::BUILT_IN:
        {
            Datatype_Builtin* builtin = downcast<Datatype_Builtin>(type);
            switch (builtin->builtin_type)
            {
            case Builtin_Type::RAWPTR: {
                byte* pointer = *(byte**)base_memory;
                assert(pointer == 0, "Pointers must be null in constant memory");
                gen.text->append("nullptr");
                break;
            }
            case Builtin_Type::TYPE_HANDLE: {
                dynstring_append_formated(gen.text, "%u", *(u32*)base_memory);
                break;
            }
            case Builtin_Type::ANY: 
            {
                // Any in constant-pool only works
                Upp_Any* any = (Upp_Any*)base_memory;
                assert(any->data == nullptr, "Any in constnat pool cannot work with pointers");
                dynstring_append_formated(gen.text, "upp_any_make_(nullptr, %u)", any->type.index);
                break;
            }
            case Builtin_Type::STRING: 
            {
                // Note: Maybe we need something smarter in the future to handle multi-line strings 
                Upp_String string = *(Upp_String*)base_memory;
                dynstring_append_formated(gen.text, "{.data = (void*) \"");

                // Note: I need to escape escape sequences, so this is what i'm doing now...
                DynString escaped = dynstring_create(gen.scratch_arena);
                for (int i = 0; i < string.size; i++) {
                    char c = ((const char*)string.data)[i];
                    switch (c)
                    {
                    case '\n': escaped.append("\\n"); break;
                    case '\r': escaped.append("\\r"); break;
                    case '\t': escaped.append("\\t"); break;
                    case '\\': escaped.append("\\\\"); break;
                    case '\"': escaped.append("\\\""); break;
                    case '\'': escaped.append("\\\'"); break;
                    default: escaped.append(c); break;
                    }
                }

                gen.text->append(escaped);
                dynstring_append_formated(gen.text, "\", .size = %d }", string.size);
                break;
            }
            case Builtin_Type::C_STRING: {
                panic("C-strings should not end up in constant pool");
                break;
            }
            case Builtin_Type::C_CHAR: {
                u8* value = (u8*)base_memory;
                dynstring_append_formated(gen.text, "((char)%d)", *value);
                break;
            }
            case Builtin_Type::CODE_POINT: {
                dynstring_append_formated(gen.text, "%u", *(u32*)base_memory);
                break;
            }
            default: panic("");
            }
            break;
        }
        case Datatype_Type::PRIMITIVE:
        {
            auto primitive = downcast<Datatype_Primitive>(type);
            byte* memory = base_memory;
            switch (primitive->primitive_type)
            {
            case Primitive_Type::INT:  dynstring_append_formated(gen.text, "%lld", (*(i64*)memory)); break;
            case Primitive_Type::UINT: dynstring_append_formated(gen.text, "%llu", (*(u64*)memory)); break;
            case Primitive_Type::I8:  dynstring_append_formated(gen.text, "%d", (int)(*(i8*)memory)); break;
            case Primitive_Type::I16: dynstring_append_formated(gen.text, "%d", (int)(*(i16*)memory)); break;
            case Primitive_Type::I32: dynstring_append_formated(gen.text, "%d", (int)(*(i32*)memory)); break;
            case Primitive_Type::I64: dynstring_append_formated(gen.text, "%lld", (i64)(*(i64*)memory)); break;
            case Primitive_Type::U8:  dynstring_append_formated(gen.text, "%u", (u32)(*(u8*)memory)); break;
            case Primitive_Type::U16: dynstring_append_formated(gen.text, "%u", (u32)(*(u16*)memory)); break;
            case Primitive_Type::U32: dynstring_append_formated(gen.text, "%u", (u32)(*(u32*)memory)); break;
            case Primitive_Type::U64: dynstring_append_formated(gen.text, "%llu", (u64)(*(u64*)memory)); break;
            case Primitive_Type::F32: dynstring_append_formated(gen.text, "%f", (double)(*(float*)memory)); break;
            case Primitive_Type::F64: dynstring_append_formated(gen.text, "%f", (double)(*(double*)memory)); break;
            case Primitive_Type::BOOLEAN: gen.text->append(((*(bool*)memory) ? "true" : "false")); break;
            default: panic("What");
            }
            break;
        }
        case Datatype_Type::ENUM:
        {
            byte* memory = base_memory;

            assert(DEFAULT_ENUM_SIZE == 8, "");
            i64 enum_value = *(i64*)memory;
            Datatype_Enum* enum_type = downcast<Datatype_Enum>(type);
            int member_index = -1;
            for (int i = 0; i < enum_type->members.size; i++) {
                auto& member = enum_type->members[i];
                if ((i64)member.value == enum_value) {
                    member_index = i;
                    break;
                }
            }
            assert(member_index != -1, "");

            auto member = enum_type->members[member_index];
            c_generator_output_type_reference(generator, type);
            gen.text->append("::")->append(member.name);
            break;
        }
        case Datatype_Type::FUNCTION_POINTER:
        {
            byte* memory = base_memory;

            int function_index = (int)*(i64*)memory;
            if (function_index == 0) { // Function index 0 == nullptr, otherwise add -1 to get index in functions array
                gen.text->append("nullptr");
            }
            else {
                C_Translation function_translation;
                function_translation.type = C_Translation_Type::FUNCTION;
                function_translation.options.function = gen.compilation_data->functions[function_index - 1];
                String* fn_name = gen.program_translation.name_mapping.find(function_translation);
                assert(fn_name != 0, "");
                gen.text->append("(&")->append(fn_name)->append(")");
            }
            break;
        }
        case Datatype_Type::POINTER:
        {
            byte* memory = base_memory;
            byte* pointer = *(byte**)memory;
            assert(pointer == 0, "Pointers must be null in constant memory");
            gen.text->append("nullptr");
            break;
        }
        case Datatype_Type::SLICE:
        {
            Datatype* element_type = downcast<Datatype_Slice>(type)->element_type;
            Upp_Slice_Base slice = *(Upp_Slice_Base*)base_memory;
            assert(slice.size == 0 && slice.data == nullptr, "");
            dynstring_append_formated(gen.text, "{.data = nullptr, .size = 0}");
            break;
        }
        case Datatype_Type::ARRAY:
        {
            Datatype_Array* array_type = downcast<Datatype_Array>(type);
            assert(array_type->count_known, "");
            gen.text->append("{\n");
            string_add_indentation(gen.text, 1);
            for (int i = 0; i < array_type->element_count; i++) {
                output_memory_as_new_constant(
                    generator, constant.memory + i * array_type->element_type->memory_info.value.size, array_type->element_type, false, 1
                );
                if (i != array_type->element_count - 1) {
                    gen.text->append(",\n");
                    string_add_indentation(gen.text, 1);
                }
            }
            gen.text->append("\n}");
            break;
        }
        case Datatype_Type::STRUCT:
        {
            Datatype_Struct* structure = downcast<Datatype_Struct>(type);
            assert(!structure->upp_struct->is_union, "Must not happen, as normal unions cannot get serialized in constant pool");
            output_struct_content_block_recursive(generator, structure, base_memory, indentation_level);
            break;
        }
        case Datatype_Type::PATTERN_VARIABLE:
        case Datatype_Type::UNKNOWN_TYPE: {
            panic("Should not happen, this should generate an error beforehand");
            break;
        }
        default: panic("");
        }

        // Finish declaration
        if (requires_memory_address) {
            gen.text->append(";\n");
            gen.text = &gen.sections[(int)Generator_Section::CONSTANTS];
            gen.text->append(constant_string);
        }
    }

    // Store translation
    gen.program_translation.name_mapping.insert(
        constant_translation,  
        string_allocate_copy(access_name.to_string(), gen.perma_arena->upcast())
    );

    // Append constant access
    gen.text = backup_text;
    gen.text->append(access_name);
}

void output_memory_as_new_constant(C_Generator* generator, byte* base_memory, Datatype* base_type, bool requires_memory_address, int current_indentation_level)
{
    Constant_Pool* pool = generator->compilation_data->constant_pool;
    auto result = constant_pool_add_constant(pool, base_type, array_create_static<byte>(base_memory, base_type->memory_info.value.size));
    assert(result.success, "Should always work");
    c_generator_output_constant_access(generator, result.options.constant, requires_memory_address, current_indentation_level);
}

void c_generator_output_parameter_access(C_Generator* generator, Upp_Function* function, int parameter_index)
{
    auto& gen = *generator;
    auto& types = gen.compilation_data->type_system->predefined_types;

    C_Translation translation;
    translation.type = C_Translation_Type::PARAMETER;
    translation.options.parameter.function = function;
    translation.options.parameter.index = parameter_index;
    {
        String* name = gen.program_translation.name_mapping.find(translation);
        if (name != 0) {
            gen.text->append(name);
            return;
        }
    }

    DynString new_name = dynstring_create(generator->scratch_arena);
    auto& param = function->signature->parameters[parameter_index];
    new_name.append(param.name)->append_formated("_%d", gen.name_counter);
    gen.name_counter++;

    gen.program_translation.name_mapping.insert(translation, string_allocate_copy(new_name.to_string(), gen.perma_arena->upcast()));
    gen.text->append(new_name.characters);
}

void c_generator_output_global_access(C_Generator* generator, int global_index)
{
    auto& gen = *generator;
    auto& types = gen.compilation_data->type_system->predefined_types;

    Upp_Global* global = gen.compilation_data->globals[global_index];

    C_Translation translation;
    translation.type = C_Translation_Type::GLOBAL;
    translation.options.global_index = global_index;
    {
        String* name = gen.program_translation.name_mapping.find(translation);
        if (name != 0) {
            gen.text->append(name);
            return;
        }
    }

    DynString new_name = dynstring_create(generator->scratch_arena);
    if (global->is_extern) {
        new_name.append(global->symbol->id);
    }
    else {
        if (global->symbol != 0) {
            new_name.append(global->symbol->id)->append_formated("_%d", gen.name_counter);
        }
        else {
            dynstring_append_formated(&new_name, "global_%d", gen.name_counter);
        }
    }
    gen.name_counter++;

    gen.program_translation.name_mapping.insert(translation, string_allocate_copy(new_name.to_string(), gen.perma_arena->upcast()));
    gen.text->append(new_name);
}

void c_generator_output_data_access(C_Generator* generator, IR_Data_Access* access, bool add_parenthesis_on_pointer_ops)
{
    auto& gen = *generator;
    auto& types = gen.compilation_data->type_system->predefined_types;

    switch (access->type)
    {
    case IR_Data_Access_Type::REGISTER:
    {
        C_Translation translation;
        translation.type = C_Translation_Type::REGISTER;
        translation.options.register_translation.code_block = access->option.register_access.definition_block;
        translation.options.register_translation.index = access->option.register_access.index;
        {
            String* name = gen.program_translation.name_mapping.find(translation);
            if (name != 0) {
                gen.text->append(name);
                break;
            }
        }

        auto& reg = access->option.register_access.definition_block->registers[access->option.register_access.index];
        DynString new_name = dynstring_create(gen.scratch_arena);
        if (reg.name.available) {
            new_name.append(reg.name.value);
        }
        else {
            new_name.append("tmp");
        }
        dynstring_append_formated(&new_name, "_%d", gen.name_counter);
        gen.name_counter++;

        gen.program_translation.name_mapping.insert(translation, string_allocate_copy(new_name.to_string(), gen.perma_arena->upcast()));
        gen.text->append(new_name);
        break;
    }
    case IR_Data_Access_Type::PARAMETER:
    {
        c_generator_output_parameter_access(generator, access->option.parameter.function, access->option.parameter.index);
        break;
    }
    case IR_Data_Access_Type::GLOBAL_DATA:
    {
        c_generator_output_global_access(generator, access->option.global_index);
        break;
    }
    case IR_Data_Access_Type::CONSTANT:
    {
        Upp_Constant* constant = &gen.compilation_data->constant_pool->constants[access->option.constant_index];
        c_generator_output_constant_access(generator, *constant, false, 0);
        break;
    }
    case IR_Data_Access_Type::POINTER_DEREFERENCE: {
        if (add_parenthesis_on_pointer_ops) {
            gen.text->append("(");
        }

        gen.text->append("*");
        c_generator_output_data_access(generator, access->option.pointer_value, false);

        if (add_parenthesis_on_pointer_ops) {
            gen.text->append(")");
        }
        break;
    }
    case IR_Data_Access_Type::ADDRESS_OF_VALUE: {
        if (add_parenthesis_on_pointer_ops) {
            gen.text->append("(");
        }

        gen.text->append("&");
        c_generator_output_data_access(generator, access->option.pointer_value);

        if (add_parenthesis_on_pointer_ops) {
            gen.text->append(")");
        }
        break;
    }
    case IR_Data_Access_Type::MEMBER_ACCESS:
    {
        const Struct_Member& member = access->option.member_access.member;

        c_generator_output_data_access(generator, access->option.member_access.struct_access, true);

        // Handle members of struct subtypes
        Datatype* access_type = access->option.member_access.struct_access->datatype;
        assert(!datatype_is_pointer(access_type, true, true, true), "");
        if (access_type->type == Datatype_Type::STRUCT)
        {
            Datatype_Struct* structure = downcast<Datatype_Struct>(access_type);
            int child_depth = 0;
            Datatype_Struct* iter = structure;
            while (iter->parent != nullptr) {
                iter = iter->parent;
                child_depth += 1;
            }

            for (int deref_count = child_depth; deref_count > 0; deref_count -= 1)
            {
                iter = structure;
                for (int j = 0; j < deref_count; j++) {
                    iter = iter->parent;
                }
                gen.text->append(".subtypes_.")->append(iter->name);
            }
        }

        // Append member access
        gen.text->append(".")->append(member.name);

        break;
    }
    case IR_Data_Access_Type::ARRAY_ELEMENT_ACCESS:
    {
        auto array_type = access->option.array_access.array_access->datatype;
        if (array_type->type == Datatype_Type::SLICE)
        {
            c_generator_output_data_access(generator, access->option.array_access.array_access, true);
            gen.text->append(".data[");
            c_generator_output_data_access(generator, access->option.array_access.index_access);
            gen.text->append("]");
        }
        else {
            assert(array_type->type == Datatype_Type::ARRAY, "");
            c_generator_output_data_access(generator, access->option.array_access.array_access, true);
            gen.text->append(".values[");
            c_generator_output_data_access(generator, access->option.array_access.index_access);
            gen.text->append("]");
        }
        break;
    }
    case IR_Data_Access_Type::NON_DESTRUCTIVE_CAST:
    {
        auto& infos = access->option.non_destructive_cast;
        dynstring_append_formated(gen.text, "(");
        c_generator_output_type_reference(generator, access->datatype);
        dynstring_append_formated(gen.text, ") ");
        c_generator_output_data_access(generator, infos.value_access, add_parenthesis_on_pointer_ops);
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

void c_generator_output_cast_if_necessary(C_Generator* generator, Datatype* dst_type, Datatype* src_type)
{
    auto& gen = *generator;

    if (types_are_equal(dst_type, src_type)) return;

    dynstring_append_formated(gen.text, "(");
    c_generator_output_type_reference(generator, dst_type);
    dynstring_append_formated(gen.text, ") ");
}

// define_registers_in_outer_scope is used for e.g. the while loop condition-block
void c_generator_output_code_block(C_Generator* generator, IR_Code_Block* code_block, int indentation_level, bool define_registers_in_outer_scope)
{
    auto& gen = *generator;
    auto& types = gen.compilation_data->type_system->predefined_types;

    // Create Register variables
    {
        if (!define_registers_in_outer_scope)
        {
            string_add_indentation(gen.text, indentation_level);
            dynstring_append_formated(gen.text, "{\n");
        }
        for (int i = 0; i < code_block->registers.size; i++)
        {
            auto& reg = code_block->registers[i];
            if (reg.has_definition_instruction) continue;

            if (define_registers_in_outer_scope) {
                string_add_indentation(gen.text, indentation_level);
            }
            else {
                string_add_indentation(gen.text, indentation_level + 1);
            }
            Datatype* sig = code_block->registers[i].type;
            c_generator_output_type_reference(generator, sig);
            dynstring_append_formated(gen.text, " ");

            IR_Data_Access access;
            access.type = IR_Data_Access_Type::REGISTER;
            access.option.register_access.definition_block = code_block;
            access.option.register_access.index = i;
            c_generator_output_data_access(generator, &access);
            dynstring_append_formated(gen.text, ";\n");
        }
        if (define_registers_in_outer_scope) {
            string_add_indentation(gen.text, indentation_level);
            dynstring_append_formated(gen.text, "{\n");
        }
    }

    // Output code
    for (int i = 0; i < code_block->instructions.size; i++)
    {
        IR_Instruction* instr = &code_block->instructions[i];

        // Add instruction to char mapping
        bool add_char_info = gen.text == &gen.sections[(int)Generator_Section::FUNCTION_IMPLEMENTATION];
        Translation_Char_Info char_info;
        char_info.code_block = code_block;
        char_info.instruction_index = i;
        char_info.char_start = gen.text->size;
        char_info.char_end = char_info.char_start;
        int char_info_index = gen.translation_characters.size;
        if (add_char_info) {
            gen.translation_characters.push_back(char_info);
        }
        SCOPE_EXIT(
            if (add_char_info && gen.text == &gen.sections[(int)Generator_Section::FUNCTION_IMPLEMENTATION]) {
                gen.translation_characters[char_info_index].char_end = gen.text->size;
            }
        );

        // Generate instruction
        if (instr->type != IR_Instruction_Type::BLOCK) {
            string_add_indentation(gen.text, indentation_level + 1);
        }
        switch (instr->type)
        {
        case IR_Instruction_Type::FUNCTION_CALL:
        {
            IR_Instruction_Call* call = &instr->options.call;
            Call_Signature* signature = 0;
            switch (call->call_type) {
            case IR_Instruction_Call_Type::FUNCTION_CALL:
                signature = call->options.function->signature;
                break;
            case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL:
                signature = downcast<Datatype_Function_Pointer>(call->options.pointer_access->datatype)->signature;
                break;
            case IR_Instruction_Call_Type::BUILTIN_CALL:
                signature = generator->compilation_data->hardcoded_function_signatures[
                    (int)ir_builtin_fn_to_hardcoded_type(call->options.builtin_fn)
                ];
                break;
            default: panic("hey");
            }
            if (signature->return_type().available) {
                c_generator_output_data_access(generator, call->destination);
                dynstring_append_formated(gen.text, " = ");
                c_generator_output_cast_if_necessary(generator, call->destination->datatype, signature->return_type().value);
            }

            bool call_handled = false;
            switch (call->call_type)
            {
            case IR_Instruction_Call_Type::FUNCTION_CALL: 
            {
                C_Translation fn_translation;
                fn_translation.type = C_Translation_Type::FUNCTION;
                fn_translation.options.function = call->options.function;
                String* fn_name = gen.program_translation.name_mapping.find(fn_translation);
                assert(fn_name != 0, "Hey");
                dynstring_append_formated(gen.text, fn_name->characters);
                break;
            }
            case IR_Instruction_Call_Type::FUNCTION_POINTER_CALL: {
                c_generator_output_data_access(generator, call->options.pointer_access);
                break;
            }
            case IR_Instruction_Call_Type::BUILTIN_CALL: 
            {
                const char* impl_name = nullptr;
                switch (call->options.builtin_fn)
                {
                case IR_Builtin_Function::TYPE_INFO:
                {
                    gen.text->append("&type_infos_.infos[");
                    assert(call->arguments.size == 1, "");
                    c_generator_output_data_access(generator, call->arguments[0]);
                    dynstring_append_formated(gen.text, "];\n");
                    call_handled = true;
                    break;
                }
                case IR_Builtin_Function::MEMORY_COPY: impl_name = "memory_copy"; break;
                case IR_Builtin_Function::MEMORY_COPY_NO_OVERLAP: impl_name = "memory_copy_no_overlap"; break;
                case IR_Builtin_Function::MEMORY_ZERO: impl_name = "memory_zero"; break;
                case IR_Builtin_Function::MEMORY_COMPARE: impl_name = "memory_compare"; break;
                case IR_Builtin_Function::SYSTEM_ALLOC: impl_name = "malloc_size"; break;
                case IR_Builtin_Function::SYSTEM_FREE: impl_name = "free_pointer"; break;
                case IR_Builtin_Function::PRINT_INT: impl_name = "print_upp_int"; break;
                case IR_Builtin_Function::PRINT_FLOAT: impl_name = "print_f32"; break;
                case IR_Builtin_Function::PRINT_STRING: impl_name = "print_string"; break;
                default: panic("");
                }
                if (!call_handled) {
                    gen.text->append(impl_name);
                }

                break;
            }
            default: panic("What");
            }
            if (call_handled) {
                break;
            }

            dynstring_append_formated(gen.text, "(");
            for (int j = 0; j < call->arguments.size; j++)
            {
                // Add cast (Implemented because of signed char/char difference in C) // ? Is this comment outdated ?
                c_generator_output_data_access(generator, call->arguments[j]);
                if (j != call->arguments.size - 1) {
                    dynstring_append_formated(gen.text, ", ");
                }
            }
            dynstring_append_formated(gen.text, ");\n");
            break;
        }
        case IR_Instruction_Type::MATCH:
        {
            IR_Instruction_Switch* switch_instr = &instr->options.switch_instr;
            dynstring_append_formated(gen.text, "switch ((int) ");
            c_generator_output_data_access(generator, switch_instr->condition_access);
            dynstring_append_formated(gen.text, ")\n");
            string_add_indentation(gen.text, indentation_level);
            dynstring_append_formated(gen.text, "{\n");
            for (int i = 0; i < switch_instr->cases.size; i++)
            {
                IR_Switch_Case* switch_case = &switch_instr->cases[i];
                string_add_indentation(gen.text, indentation_level);
                dynstring_append_formated(gen.text, "case %d: \n", switch_case->value);
                c_generator_output_code_block(generator, switch_case->block, indentation_level + 1, false);
                string_add_indentation(gen.text, indentation_level);
                dynstring_append_formated(gen.text, "break;\n");
            }
            string_add_indentation(gen.text, indentation_level);
            dynstring_append_formated(gen.text, "default:\n");
            c_generator_output_code_block(generator, switch_instr->default_block, indentation_level + 1, false);
            string_add_indentation(gen.text, indentation_level);
            dynstring_append_formated(gen.text, "}\n");
            break;
        }
        case IR_Instruction_Type::IF:
        {
            IR_Instruction_If* if_instr = &instr->options.if_instr;
            dynstring_append_formated(gen.text, "if (");
            c_generator_output_data_access(generator, if_instr->condition);
            dynstring_append_formated(gen.text, ")\n");
            c_generator_output_code_block(generator, if_instr->true_branch, indentation_level + 1, false);
            if (if_instr->false_branch->instructions.size != 0) {
                string_add_indentation(gen.text, indentation_level + 1);
                dynstring_append_formated(gen.text, "else\n");
                c_generator_output_code_block(generator, if_instr->false_branch, indentation_level + 1, false);
            }
            break;
        }
        case IR_Instruction_Type::WHILE:
        {
            IR_Instruction_While* while_instr = &instr->options.while_instr;
            dynstring_append_formated(gen.text, "while(true){\n");
            c_generator_output_code_block(generator, while_instr->condition_code, indentation_level + 2, true);
            string_add_indentation(gen.text, indentation_level + 2);
            dynstring_append_formated(gen.text, "if(!(");
            c_generator_output_data_access(generator, while_instr->condition_access);
            dynstring_append_formated(gen.text, ")) break;\n");
            c_generator_output_code_block(generator, while_instr->code, indentation_level + 2, false);
            string_add_indentation(gen.text, indentation_level + 1);
            dynstring_append_formated(gen.text, "}\n");
            break;
        }
        case IR_Instruction_Type::BLOCK:
        {
            c_generator_output_code_block(generator, instr->options.block, indentation_level + 1, false);
            break;
        }
        case IR_Instruction_Type::GOTO: {
            dynstring_append_formated(gen.text, "goto upp_label_%d;\n", instr->options.label_index);
            break;
        }
        case IR_Instruction_Type::LABEL: {
            dynstring_append_formated(gen.text, "upp_label_%d: {}\n", instr->options.label_index);
            break;
        }
        case IR_Instruction_Type::RETURN:
        {
            IR_Instruction_Return* return_instr = &instr->options.return_instr;
            switch (return_instr->type)
            {
            case IR_Instruction_Return_Type::EXIT: {
                if (return_instr->options.exit_code.type == Exit_Code_Type::SUCCESS && ADD_WAIT_BEFORE_EXIT) {
                    gen.text->append("printf(\"\\n\\nEND OF PROGRAM\");\n");
                    string_add_indentation(gen.text, indentation_level);
                    gen.text->append("std::cin.ignore();\n");
                    string_add_indentation(gen.text, indentation_level);
                }
                dynstring_append_formated(gen.text, "exit(%d);\n", (i32)return_instr->options.exit_code.type);
                break;
            }
            case IR_Instruction_Return_Type::RETURN_DATA: {
                dynstring_append_formated(gen.text, "return ");
                c_generator_output_cast_if_necessary(generator, return_instr->options.return_value->datatype, code_block->function->signature->return_type().value);
                c_generator_output_data_access(generator, return_instr->options.return_value);
                dynstring_append_formated(gen.text, ";\n");
                break;
            }
            case IR_Instruction_Return_Type::RETURN_EMPTY: {
                dynstring_append_formated(gen.text, "return;\n");
                break;
            }
            default: panic("What");
            }
            break;
        }
        case IR_Instruction_Type::VARIABLE_DEFINITION:
        {
            auto& def = instr->options.variable_definition;

            c_generator_output_type_reference(generator, def.variable_access->datatype);
            dynstring_append_formated(gen.text, " ");
            c_generator_output_data_access(generator, def.variable_access);

            if (def.initial_value.available) {
                gen.text->append(" = ");
                c_generator_output_data_access(generator, def.initial_value.value);
            }
            gen.text->append("; \n");
            break;
        }
        case IR_Instruction_Type::FUNCTION_ADDRESS:
        {
            IR_Instruction_Function_Address* addr_of = &instr->options.function_address;
            c_generator_output_data_access(generator, addr_of->destination);
            dynstring_append_formated(gen.text, " = ");

            C_Translation fn_translation;
            fn_translation.type = C_Translation_Type::FUNCTION;
            fn_translation.options.function = addr_of->function;
            String* fn_name = gen.program_translation.name_mapping.find(fn_translation);
            assert(fn_name != 0, "HEY");
            dynstring_append_formated(gen.text, "&%s;\n", fn_name->characters);
            break;
        }
        case IR_Instruction_Type::MOVE:
        {
            auto& move = instr->options.move;
            c_generator_output_data_access(generator, move.destination);
            dynstring_append_formated(gen.text, " = ");
            c_generator_output_cast_if_necessary(generator, move.destination->datatype, move.source->datatype);
            c_generator_output_data_access(generator, move.source);
            dynstring_append_formated(gen.text, ";\n");
            break;
        }
        case IR_Instruction_Type::OPERATION:
        {
            auto& operation = instr->options.operation;

            bool op_handled = false;
            // Bitwise-ops on signed ints are implementation defined in C++.
            // In upp, we handle them like unsigned ints (Assuming two's-complement)
            bool handle_params_as_unsigned = false;
            const char* binop_str = nullptr;
            const char* unop_str  = nullptr;
            const char* float_fn_name = nullptr;
            bool append_f_for_f32_version = true;
            switch (operation.type)
            {
            case Primitive_Operation::PRIMITIVE_CAST: 
            {
                op_handled = true;
                c_generator_output_data_access(generator, operation.destination);
                dynstring_append_formated(gen.text, " = ");
                c_generator_output_cast_if_necessary(generator, operation.destination->datatype, operation.operand_1->datatype);
                c_generator_output_data_access(generator, operation.operand_1);
                dynstring_append_formated(gen.text, ";\n");
                break;
            }
            case Primitive_Operation::HIGHEST_SET_BIT:
            case Primitive_Operation::LOWEST_SET_BIT:
            {
                op_handled = true;
                assert(datatype_is_integer(operation.operand_1->datatype, true), "");
                handle_params_as_unsigned = true;

                const char* fn_name = operation.type == Primitive_Operation::HIGHEST_SET_BIT ? "highest_set_bit_" : "lowest_set_bit_";
                const char* postfix = "";
                Datatype* function_value_datatype = types.u32_type->upcast();
                switch (operation.operand_1->datatype->memory_info.value.size)
                {
                case 1:
                case 2:
                case 4: postfix = "u32"; function_value_datatype = types.u32_type->upcast(); break;
                case 8: postfix = "u64"; function_value_datatype = types.u64_type->upcast(); break;
                default: panic("");
                }

                c_generator_output_data_access(generator, operation.destination);
                gen.text->append(" = ");
                c_generator_output_cast_if_necessary(generator, operation.destination->datatype, function_value_datatype);
                dynstring_append_formated(gen.text, "%s%s(", fn_name, postfix);
                c_generator_output_cast_if_necessary(generator, operation.operand_1->datatype, function_value_datatype);
                c_generator_output_data_access(generator, operation.operand_1);
                dynstring_append_formated(gen.text, ");\n");
                break;
            }

            case Primitive_Operation::ADDITION:               binop_str = "+"; break;
            case Primitive_Operation::SUBTRACTION:            binop_str = "-"; break;
            case Primitive_Operation::DIVISION:               binop_str = "/"; break;
            case Primitive_Operation::MULTIPLICATION:         binop_str = "*"; break;
            case Primitive_Operation::MODULO:                 binop_str = "%"; break;
            case Primitive_Operation::NEGATE:                 unop_str = "-"; break;
            case Primitive_Operation::EQUAL:                  binop_str = "=="; break;
            case Primitive_Operation::NOT_EQUAL:              binop_str = "!="; break;
            case Primitive_Operation::LESS:                   binop_str = "<"; break;
            case Primitive_Operation::LESS_OR_EQUAL:          binop_str = "<="; break;
            case Primitive_Operation::GREATER:                binop_str = ">"; break;
            case Primitive_Operation::GREATER_OR_EQUAL:       binop_str = ">="; break;
            case Primitive_Operation::AND:                    binop_str = "&&"; break;
            case Primitive_Operation::OR:                     binop_str = "||"; break;
            case Primitive_Operation::NOT:                    unop_str = "!"; break;
            case Primitive_Operation::BITWISE_NOT:            binop_str = "~";  handle_params_as_unsigned = true; break;
            case Primitive_Operation::BITWISE_AND:            binop_str = "&";  handle_params_as_unsigned = true; break;
            case Primitive_Operation::BITWISE_OR:             binop_str = "|";  handle_params_as_unsigned = true; break;
            case Primitive_Operation::BITWISE_XOR:            binop_str = "^";  handle_params_as_unsigned = true; break;
            case Primitive_Operation::BITWISE_SHIFT_LEFT:     binop_str = "<<"; handle_params_as_unsigned = true; break;
            case Primitive_Operation::BITWISE_SHIFT_RIGHT:    binop_str = ">>"; handle_params_as_unsigned = true; break;

            case Primitive_Operation::FLOAT_ABS:            float_fn_name = "fabsf"; break;
            case Primitive_Operation::FLOAT_MODULO:         float_fn_name = "fmod";  break;
            case Primitive_Operation::FLOAT_REMAINDER:      float_fn_name = "remainder"; break;
            case Primitive_Operation::ROUND_UP:             float_fn_name = "ceil"; break;
            case Primitive_Operation::ROUND_DOWN:           float_fn_name = "floor"; break;
            case Primitive_Operation::ROUND_TOWARDS_ZERO:   float_fn_name = "trunc"; break;
            case Primitive_Operation::ROUND_NEAREST:        float_fn_name = "round"; break;

            case Primitive_Operation::EXP:                  float_fn_name = "exp"; break;
            case Primitive_Operation::LN:                   float_fn_name = "log"; break;
            case Primitive_Operation::LOG10:                float_fn_name = "log10"; break;
            case Primitive_Operation::LOG2:                 float_fn_name = "log2"; break;
            case Primitive_Operation::POW:                  float_fn_name = "pow"; break;
            case Primitive_Operation::SQUARE_ROOT:          float_fn_name = "sqrt"; break;
            case Primitive_Operation::CUBE_ROOT:            float_fn_name = "cbrt"; break;

            case Primitive_Operation::SIN:                  float_fn_name = "sin"; break;
            case Primitive_Operation::COS:                  float_fn_name = "cos"; break;
            case Primitive_Operation::TAN:                  float_fn_name = "tan"; break;
            case Primitive_Operation::ASIN:                 float_fn_name = "asin"; break;
            case Primitive_Operation::ACOS:                 float_fn_name = "acos"; break;
            case Primitive_Operation::ATAN:                 float_fn_name = "atan"; break;
            case Primitive_Operation::ATAN2:                float_fn_name = "atan2"; break;

            case Primitive_Operation::SINH:                 float_fn_name = "sinh"; break;
            case Primitive_Operation::COSH:                 float_fn_name = "cosh"; break;
            case Primitive_Operation::TANH:                 float_fn_name = "tanh"; break;
            case Primitive_Operation::ASINH:                float_fn_name = "asinh"; break;
            case Primitive_Operation::ACOSH:                float_fn_name = "acosh"; break;
            case Primitive_Operation::ATANH:                float_fn_name = "atanh"; break;

            case Primitive_Operation::IS_NAN:               float_fn_name = "isnan"; append_f_for_f32_version = false; break;
            case Primitive_Operation::IS_FINITE:            float_fn_name = "isinf"; append_f_for_f32_version = false; break;
            case Primitive_Operation::IS_INFINITE:          float_fn_name = "isfinite"; append_f_for_f32_version = false; break;
            default: panic("");
            }

            if (op_handled) {
                break;
            }
            else if (binop_str != nullptr)
            {
                auto helper_signed_to_unsigned_if_required = [&](Datatype* datatype)-> Datatype*
                {
                    if (!handle_params_as_unsigned || !datatype_is_signed_int(datatype)) return datatype;
                    switch (operation.operand_1->datatype->memory_info.value.size)
                    {
                    case 1: return types.u8_type->upcast();
                    case 2: return types.u16_type->upcast();
                    case 4: return types.u32_type->upcast();
                    case 8: return types.u64_type->upcast();
                    default: panic("");
                    }
                    return datatype;
                };

                c_generator_output_data_access(generator, operation.destination);
                gen.text->append(" = ");
                gen.text->append("(");
                c_generator_output_cast_if_necessary(
                    generator, helper_signed_to_unsigned_if_required(operation.operand_1->datatype), operation.operand_1->datatype
                );
                c_generator_output_data_access(generator, operation.operand_1);
                gen.text->append(")");

                dynstring_append_formated(gen.text, " %s ", binop_str);

                gen.text->append("(");
                c_generator_output_cast_if_necessary(
                    generator, helper_signed_to_unsigned_if_required(operation.operand_2->datatype), operation.operand_2->datatype
                );
                c_generator_output_data_access(generator, operation.operand_2);
                gen.text->append(")");

                dynstring_append_formated(gen.text, ";\n");
            }
            else if (unop_str != nullptr)
            {
                c_generator_output_data_access(generator, operation.destination);
                dynstring_append_formated(gen.text, " = ");
                dynstring_append_formated(gen.text, "%s", unop_str);
                c_generator_output_data_access(generator, operation.operand_1);
                dynstring_append_formated(gen.text, ";\n");
            }
            else if (float_fn_name != nullptr)
            {
                int param_count = ir_operation_parameter_count(operation.type);
                c_generator_output_data_access(generator, operation.destination);
                dynstring_append_formated(gen.text, " = ");
                dynstring_append_formated(gen.text, "%s");
                if (append_f_for_f32_version && downcast<Datatype_Primitive>(operation.operand_1->datatype)->primitive_type == Primitive_Type::F32)
                {
                    dynstring_append_formated(gen.text, "f");
                }
                gen.text->append("(");
                c_generator_output_data_access(generator, operation.operand_1);
                if (param_count == 2) {
                    gen.text->append(", ");
                    c_generator_output_data_access(generator, operation.operand_2);
                }
                dynstring_append_formated(gen.text, ");\n");
            }
            else {
                panic("operation was not handled");
            }

            break;
        }
        default: panic("Hey");
        }
    }

    string_add_indentation(gen.text, indentation_level);
    dynstring_append_formated(gen.text, "}\n");
}



// COMPILER PART
void load_compiler_environment_variables()
{
    if (global_compiler_environment_variables_loaded) {
        return;
    }
    global_compiler_environment_variables_loaded = true;

    SCRATCH_ARENA_MAKE_SCOPED(nullptr);

    // Load system vars (Required to use cl.exe and link.exe)
    // These are generated once with the batch script in some misc folder, and then loaded from that file.
    //  Otherwise the initialization takes multiple seconds (Thanks, microsoft...)
    DynString file_content = DynString::create(scratch_arena);
    DynString value_null_terminated = DynString::create(scratch_arena);
    DynString name_null_terminated = DynString::create(scratch_arena);
    bool file_loaded = file_io_load_text_file(string_make("backend/misc/env_vars.txt"), &file_content);
    if (!file_loaded) {
        // TODO: maybe we should run the script to generate the vars here...
        panic("System-Variables file for compilation (Pre-generated) were not available!");
        return;
    }

    Array<String> lines = string_split(file_content.to_string(), '\n', scratch_arena);
    for (int i = 0; i < lines.size; i++)
    {
        String line = lines[i];
        Array<String> parts = string_split(line, '=', scratch_arena);
        if (parts.size != 2) continue; // Maybe log error

        String val_name = string_get_whitespace_trimmed(parts[0]);
        String value    = string_get_whitespace_trimmed(parts[1]);
        if (val_name.size == 0 || value.size == 0) {
            continue;
        }
        SetEnvironmentVariableA(
            name_null_terminated.reset()->append(val_name)->to_cstring(),
            value_null_terminated.reset()->append(value)->to_cstring()
        );
    }
}

void c_backend_compile_program(Compilation_Data* compilation_data)
{
    load_compiler_environment_variables();

    compilation_data->c_program_was_compiled = false;
    if (compilation_data->c_program_translation == nullptr) {
        logg("Cannot compile c-code, no c-program translation available\n");
        return;
    }

    // Create compilation command
    SCRATCH_ARENA_MAKE_SCOPED(&compilation_data->arena);
    DynString command = dynstring_create(scratch_arena);
    {
        /*
        Used Compiler Switches:
            MDd     --> Multithreaded debug version of runtime library
            EHsc    --> Exception handling thing
            Zi      --> Generate PDB file (ZI would be with Edit-And-Continue Features)
            std:    --> C++ Standard to use, c++latest is used for designated struct inititalizers
            Fd      --> Debug-Filename output path (PDB filename)
            Fo      --> Object-File output directory
        */
        // Note: MDd should be replaced between debug and optimized build
        String compiler_options = string_make("/MDd /EHsc /Zi /std:c++latest /Fobackend/build/ /Fdbackend/build/main.pdb /Febackend/build/main.exe");
        dynstring_append_formated(&command, "\"cl\" "); // Not sure why we need those Quotations, maybe for CreateProcess?
        command.append(compiler_options);

        auto& extern_sources = compilation_data->extern_sources;
        // Defines
        auto defines = extern_sources.compiler_settings[(int)Extern_Compiler_Setting::DEFINITION];
        for (int i = 0; i < defines.size; i++) {
            command.append(" /D ")->append_in_quotation(defines[i].to_string());
        }

        // Include directories
        auto includes = extern_sources.compiler_settings[(int)Extern_Compiler_Setting::INCLUDE_DIRECTORY];
        for (int i = 0; i < includes.size; i++) {
            command.append(" /I ")->append_in_quotation(includes[i].to_string());
        }

        // Forced includes (Header files)
        auto header_files = extern_sources.compiler_settings[(int)Extern_Compiler_Setting::HEADER_FILE];
        for (int i = 0; i < header_files.size; i++) {
            command.append(" /FI ")->append_in_quotation(header_files[i].to_string());
        }

        // Source files
        command.append(" backend/src/main.cpp backend/hardcoded/hardcoded_functions.cpp");
        auto source_files = extern_sources.compiler_settings[(int)Extern_Compiler_Setting::SOURCE_FILE];
        for (int i = 0; i < source_files.size; i++) {
            command.append(" ")->append_in_quotation(source_files[i].to_string());
        }

        // LINKER
        command.append(" /link");

        // Library directories
        auto lib_dirs = extern_sources.compiler_settings[(int)Extern_Compiler_Setting::LIBRARY_DIRECTORY];
        for (int i = 0; i < lib_dirs.size; i++) {
            command.append(" /LIBPATH:")->append_in_quotation(lib_dirs[i].to_string());
        }

        // Libraries 
        auto lib_files = extern_sources.compiler_settings[(int)Extern_Compiler_Setting::LIBRARY];
        for (int i = 0; i < lib_files.size; i++) {
            command.append(" ")->append_in_quotation(lib_files[i].to_string());
        }
    }

    logg("Compile command:\n%s\n", command.to_cstring());
    DynString output = DynString::create(scratch_arena);
    Process_Result result = process_start(command.to_string(), true, &output);

    if (result.process_started) {
        compilation_data->c_program_was_compiled = result.exit_code == 0;
        if (!compilation_data->c_program_was_compiled) {
            logg("C_COMPILATION FAILED! We should always generate valid C-Code in backend!\n");
        }
        logg("Compiler output: \n--------------\n%s\n", output.to_cstring());
    }
    else {
        logg("\n\n COULD NOT START COMPILE PROCESS? \n\n");
        compilation_data->c_program_was_compiled = false;
    }
}

Exit_Code c_backend_execute_program(Compilation_Data* compilation_data)
{
    if (compilation_data->c_program_translation == nullptr) {
        return exit_code_make(Exit_Code_Type::COMPILATION_FAILED, "No c-program was generated");
    }
    if (!compilation_data->c_program_was_compiled) {
        return exit_code_make(Exit_Code_Type::COMPILATION_FAILED, "C-Program was not compiled (Compilation failed or compile was never called)");
    }

    Process_Result result = process_start(string_make("backend/build/main.exe"), true, nullptr);
    if (!result.process_started) {
        return exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Could not start program");
    }
    if (result.exit_code < 0 || result.exit_code >= (int)Exit_Code_Type::MAX_ENUM_VALUE) {
        return exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Exit code value from program execution was invalid");
    }

    return exit_code_make((Exit_Code_Type)result.exit_code);
}




