#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../utility/utils.hpp"
#include "../../datastructures/string.hpp"
#include "compiler_misc.hpp"
#include "code_history.hpp"


struct String;

namespace AST
{
    struct Expression;
    struct Statement;
    struct Code_Block;
    struct Definition;
    struct Extern_Import;
    struct Path_Lookup;
    struct Arguments;

    enum class Binop
    {
        ADDITION,
        SUBTRACTION,
        DIVISION,
        MULTIPLICATION,
        MODULO,
        AND,
        OR,
        EQUAL,
        NOT_EQUAL,
        LESS,
        LESS_OR_EQUAL,
        GREATER,
        GREATER_OR_EQUAL,
        POINTER_EQUAL,
        POINTER_NOT_EQUAL,
        INVALID,
    };

    enum class Unop
    {
        NOT, // !
        NEGATE, // -
        POINTER, // *
        DEREFERENCE, // &
    };

    enum class Assignment_Type
    {
        DEREFERENCE, // Dereferences pointers on the left side (e.g. writes to the pointer-value), ip = 15
        POINTER, // Writes to the pointer on the left side, ip =* x
        RAW, // Expects the exact type on the left, ip =~
    };

    enum class Node_Type
    {
        EXPRESSION,
        STATEMENT,
        DEFINITION_SYMBOL, // Just an id
        DEFINITION, // ::, :=, : ... =, ...: ...
        CODE_BLOCK,
        MODULE,

        // Helpers
        ARGUMENTS,           // Parenthesis (either () or {}) and list of arguments, optional _ and subtype-inititalizer
        ARGUMENT,            // Expression with optional base_name
        SUBTYPE_INITIALIZER, // Named or unnamed parameter with parenthesis, e.g. .Location = {12, 100}
        PARAMETER,           // Name/Type compination with optional default value + comptime
        SYMBOL_LOOKUP,       // A single identifier lookup
        PATH_LOOKUP,         // Possibliy multiple symbol-lookups together (e.g. Utils~Logger~log)
        IMPORT,              // Aliases/Symbol-Import/File-Loading
        ENUM_MEMBER,         // ID with or without value-expr
        STRUCT_MEMBER,       // Either a normal member or a struct-subtype
        SWITCH_CASE,         // Expression 
        CONTEXT_CHANGE,      // Changing some operator context
        EXTERN_IMPORT,
    };

    struct Node
    {
        Node_Type type;
        Node* parent;
        Text_Range range;
        Text_Range bounding_range;
    };

    enum class Import_Type
    {
        SINGLE_SYMBOL,             // import A~a
        MODULE_SYMBOLS,            // import A~*
        MODULE_SYMBOLS_TRANSITIVE, // import A~**
        FILE                       // import "../something"
    };

    struct Import
    {
        Node base;
        Import_Type type;
        String* alias_name; // May be null if no alias is given
        // Depending on import type one of those is set, the other will be 0
        Path_Lookup* path;
        String* file_name;
    };

    enum class Context_Change_Type
    {
        BINARY_OPERATOR,
        UNARY_OPERATOR,
        ARRAY_ACCESS,
        CAST,
        DOT_CALL,
        ITERATOR,
        CAST_OPTION,
        IMPORT,
        INVALID, // Not a valid context change
        MAX_ENUM_VALUE
    };

    struct Argument;
    struct Context_Change
    {
        Node base;
        Context_Change_Type type;
        union 
        {
            Path_Lookup* import_path;
            Arguments* arguments; 
        } options;
    };

    struct Symbol_Lookup
    {
        Node base;
        String* name;
    };

    struct Path_Lookup
    {
        Node base;
        Dynamic_Array<Symbol_Lookup*> parts;
        // NOTE: The last node is only a convenient pointer to the end of parts, but the
        //       node is also inside parts, e.g. parts[parts.size-1] == last
        Symbol_Lookup* last;
    };

    struct Module
    {
        Node base;
        Dynamic_Array<Definition*> definitions;
        Dynamic_Array<Context_Change*> context_changes;
        Dynamic_Array<Import*> import_nodes;
        Dynamic_Array<Extern_Import*> extern_imports;
    };

    struct Enum_Member_Node
    {
        Node base;
        String* name;
        Optional<Expression*> value;
    };

    struct Definition_Symbol
    {
        Node base;
        String* name;
    };

    struct Definition
    {
        Node base;
        bool is_comptime; // :: instead of :=
        Assignment_Type assignment_type; // :=, :=* or :=~
        Dynamic_Array<Definition_Symbol*> symbols;
        Dynamic_Array<Expression*> types;
        Dynamic_Array<Expression*> values;
    };

    struct Argument
    {
        Node base;
        Optional<String*> name;
        Expression* value;
    };

    struct Subtype_Initializer
    {
        Node base;
        Optional<String*> name;
        Arguments* arguments;
    };

    struct Arguments
    {
        Node base;
        Dynamic_Array<Argument*> arguments;
        Dynamic_Array<Subtype_Initializer*> subtype_initializers;
        Dynamic_Array<Expression*> uninitialized_tokens;
    };

    enum class Extern_Type
    {
        FUNCTION,
        GLOBAL,
        STRUCT,
        COMPILER_SETTING,
        INVALID, // If there was an error during parsing
    };

    struct Extern_Import
    {
        Node base;
        Extern_Type type;
        union {
            struct {
                String* id;
                Expression* type_expr;
            } function;
            struct {
                String* id;
                Expression* type_expr;
            } global;
            AST::Expression* struct_type_expr; // This should normally be a path_lookup to an existing struct.
            struct {
                Extern_Compiler_Setting type;
                String* value;
            } setting;
        } options;
    };

    struct Parameter
    {
        Node base;
        bool is_comptime; // $ at the start
        bool is_mutable; // mut at the start
        String* name;
        Expression* type;
        Optional<Expression*> default_value;
    };

    struct Code_Block
    {
        Node base;
        Dynamic_Array<Statement*> statements;
        Dynamic_Array<Context_Change*> context_changes;
        Optional<String*> block_id;
    };

    enum class Structure_Type {
        STRUCT = 1,
        UNION,
    };

    struct Structure_Member_Node
    {
        Node base;
        String* name;
        bool is_expression; // Either expression or subtype_members
        union {
            Expression* expression;
            Dynamic_Array<Structure_Member_Node*> subtype_members;
        } options;
    };

    enum class Expression_Type
    {
        // Value Generation
        BINARY_OPERATION,
        UNARY_OPERATION,
        FUNCTION_CALL,
        NEW_EXPR,
        CAST,
        ARRAY_INITIALIZER,
        STRUCT_INITIALIZER,
        AUTO_ENUM,
        BAKE_EXPR,
        BAKE_BLOCK,
        INSTANCIATE,
        OPTIONAL_CHECK, // x?

        // Memory Reads
        PATH_LOOKUP,
        LITERAL_READ,
        ARRAY_ACCESS,
        MEMBER_ACCESS,

        // Types/Definitions
        MODULE,
        FUNCTION,
        FUNCTION_SIGNATURE,
        TEMPLATE_PARAMETER, // $T

        STRUCTURE_TYPE, // Struct, union, c_union
        ENUM_TYPE,
        ARRAY_TYPE,
        SLICE_TYPE,
        CONST_TYPE,
        OPTIONAL_TYPE,     // ? [type-expr]
        OPTIONAL_POINTER,  // ?* [type-expr]

        ERROR_EXPR,
    };

    struct Expression
    {
        Node base;
        Expression_Type type;
        union
        {
            String* polymorphic_symbol_id;
            Expression* optional_child_type;
            Expression* optional_pointer_child_type;
            Expression* optional_check_value;
            struct {
                Expression* left;
                Expression* right;
                Binop type;
            } binop;
            struct {
                Unop type;
                Expression* expr;
            } unop;
            Expression* bake_expr;
            Code_Block* bake_block;
            struct {
                Expression* expr;
                Arguments* arguments;
            } call;
            struct {
                Expression* type_expr;
                Optional<Expression*> count_expr;
            } new_expr;
            struct {
                Optional<Expression*> to_type;
                bool is_pointer_cast;
                Expression* operand;
            } cast;
            Path_Lookup* path_lookup;
            String* auto_enum;
            Literal_Value literal_read;
            struct {
                Expression* array_expr;
                Expression* index_expr;
            } array_access;
            struct {
                String* name;
                Expression* expr;
            } member_access;
            Module* module;
            struct {
                Expression* signature;
                Code_Block* body;
            } function;
            struct {
                Dynamic_Array<Parameter*> parameters;
                Optional<Expression*> return_value;
            } function_signature;
            struct {
                Optional<Expression*> type_expr;
                Arguments* arguments;
            } struct_initializer;
            struct {
                Optional<Expression*> type_expr;
                Dynamic_Array<Expression*> values;
            } array_initializer;
            struct {
                Expression* size_expr;
                Expression* type_expr;
            } array_type;
            Expression* slice_type;
            Expression* const_type;
            struct {
                Dynamic_Array<Parameter*> parameters;
                Dynamic_Array<Structure_Member_Node*> members;
                Structure_Type type;
            } structure;
            Expression* instanciate_expr; // Should be a function call...
            Dynamic_Array<Enum_Member_Node*> enum_members;
        } options;
    };

    struct Switch_Case
    {
        Node base;
        Optional<Expression*> value; // Default-Case if value not available
        Optional<Definition_Symbol*> variable_definition; // case .IPv4 -> v4
        Code_Block* block;
    };

    enum class Statement_Type
    {
        DEFINITION,
        BLOCK,
        ASSIGNMENT,
        BINOP_ASSIGNMENT,
        EXPRESSION_STATEMENT,
        // Keyword Statements
        IMPORT,
        DEFER,
        DEFER_RESTORE,
        IF_STATEMENT,
        WHILE_STATEMENT,
        FOR_LOOP,
        FOREACH_LOOP,
        SWITCH_STATEMENT,
        BREAK_STATEMENT,
        CONTINUE_STATEMENT,
        RETURN_STATEMENT,
        DELETE_STATEMENT
    };

    struct Statement
    {
        Node base;
        Statement_Type type;
        union
        {
            Expression* expression;
            Code_Block* block;
            Definition* definition;
            struct {
                Dynamic_Array<Expression*> left_side;
                Dynamic_Array<Expression*> right_side;
                Assignment_Type type;
            } assignment;
            struct {
                Definition_Symbol* loop_variable_definition;
                Optional<Expression*> loop_variable_type;
                Expression* initial_value;
                Expression* condition;
                Statement* increment_statement;
                Code_Block* body_block;
            } for_loop;
            struct {
                Definition_Symbol* loop_variable_definition;
                Optional<Definition_Symbol*> index_variable_definition;
                Expression* expression;
                Code_Block* body_block;
            } foreach_loop;
            struct {
                Expression* left_side;
                Expression* right_side;
                Binop binop;
            } binop_assignment;
            Code_Block* defer_block;
            struct {
                Expression* left_side;
                Expression* right_side;
                Assignment_Type assignment_type;
            } defer_restore;
            struct {
                Expression* condition;
                Code_Block* block;
                Optional<Code_Block*> else_block;
            } if_statement;
            struct {
                Expression* condition;
                Code_Block* block;
            } while_statement;
            struct {
                Expression* condition;
                Dynamic_Array<Switch_Case*> cases;
                Optional<String*> label;
            } switch_statement;
            String* break_name;
            String* continue_name;
            Optional<Expression*> return_value;
            Expression* delete_expr;
            Import* import_node;
        } options;
    };

    void base_destroy(Node* node);
    Node* base_get_child(Node* node, int child_index);
    void base_enumerate_children(Node* node, Dynamic_Array<Node*>* fill);
    void base_print(Node* node);
    void base_append_to_string(Node* base, String* str);

    void context_change_type_append_to_string(Context_Change_Type type, String* string);
    void path_lookup_append_to_string(Path_Lookup* read, String* string);
    int binop_priority(Binop binop);

    namespace Helpers
    {
        bool type_correct(Definition* base);
        bool type_correct(Definition_Symbol* base);
        bool type_correct(Switch_Case* base);
        bool type_correct(Statement* base);
        bool type_correct(Argument* base);
        bool type_correct(Parameter* base);
        bool type_correct(Expression* base);
        bool type_correct(Enum_Member_Node* base);
        bool type_correct(Module* base);
        bool type_correct(Import* base);
        bool type_correct(Path_Lookup* base);
        bool type_correct(Symbol_Lookup* base);
        bool type_correct(Code_Block* base);
        bool type_correct(Context_Change* base);
        bool type_correct(Structure_Member_Node* base);
        bool type_correct(Arguments* base);
        bool type_correct(Subtype_Initializer* base);
        bool type_correct(Extern_Import* base);
    }

    template<typename T>
    T* downcast(Node* node)
    {
        T* result = (T*)node;
        assert(Helpers::type_correct(result), "Heyy");
        return result;
    }

    template<typename T>
    AST::Node* upcast(T* node) {
        return &node->base;
    }
}

