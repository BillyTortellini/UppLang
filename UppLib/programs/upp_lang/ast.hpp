#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../utility/utils.hpp"
#include "../../datastructures/string.hpp"
#include "compiler_misc.hpp"
#include "code_history.hpp"


struct String;

enum class Literal_Type
{
    INTEGER,
    FLOAT_VAL,
    BOOLEAN,
    STRING,
    NULL_VAL,
};

struct Literal_Value
{
    Literal_Type type;
    union {
        String* string;
        i64 int_val;
        f64 float_val;
        bool boolean;
        void* null_ptr;
    } options;
};

namespace AST
{
    struct Expression;
    struct Statement;
    struct Code_Block;
    struct Definition;
    struct Path_Lookup;
    struct Call_Node;
    struct Symbol_Node;
    struct Signature;

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
        NOT,                  // !
        NEGATE,               // -
        ADDRESS_OF,           // -*
        DEREFERENCE,          // -&
        NULL_CHECK,           // ?
        OPTIONAL_DEREFERENCE, // -?&
    };

    enum class Node_Type
    {
        EXPRESSION,
        STATEMENT,
        DEFINITION,

        // Helpers
        CODE_BLOCK,
        SIGNATURE,
        SYMBOL_NODE,           // Just an id, used to attach analysis-info to symbol
        PATH_LOOKUP,           // Possibliy multiple symbol-lookups together (e.g. Utils~Logger~log)
        ENUM_MEMBER,           // ID with or without value-expr
        STRUCT_MEMBER,         // Either a normal member or a struct-subtype
        CALL_NODE,             // Parenthesis (either () or {}) and list of arguments, optional _ and subtype-inititalizer
        SUBTYPE_INITIALIZER,   // Named or unnamed parameter with parenthesis, e.g. .Location = {12, 100}
        PARAMETER,             // Name/Type compination with optional default value + comptime
        ARGUMENT,              // Expression with optional base_name
        GET_OVERLOAD_ARGUMENT, // Either just id, or id = type_expr
        SWITCH_CASE,           // Expression 
    };

    struct Node
    {
        Node_Type type;
        Node* parent;
        Text_Range range;
        Text_Range bounding_range;
    };

    // Note: #get_overload can also specify return_type, in which case the id is set appropriately (see parser.cpp)
    //      e.g. #get_overload foo(a = int, b = float) => int
    struct Get_Overload_Argument
    {
        Node base;
        String* id;
        Optional<Expression*> type_expr;
    };

    struct Symbol_Node
    {
        Node base;
        String* name;
        bool is_root_lookup; // If it is a ~
        bool is_definition;  // Symbol nodes are either definitions or lookups
    };

    struct Path_Lookup
    {
        Node base;
        Array<Symbol_Node*> parts; // Always > 0, because first one may be _root_
        bool is_dot_call_lookup;

        Symbol_Node* last(); // May return nullptr
    };

    enum class Definition_Type
    {
        VARIABLE,
        GLOBAL,
        CONSTANT,
        FUNCTION,
        STRUCT,
        ENUM,
        IMPORT,
        EXTERN,
        MODULE,
        CUSTOM_OPERATOR
    };

    struct Definition_Function
    {
        Symbol_Node* symbol;
        Signature* signature;
        Function_Body body;
    };

    struct Definition_Custom_Operator
    {
        Custom_Operator_Type type;
        Call_Node* call_node; 
    };

    struct Definition_Value
    {
        Symbol_Node* symbol;
        Optional<Expression*> datatype_expr;
        Optional<Expression*> value_expr;
    };
    
    struct Structure_Member_Node
    {
        Node base;
        String* name;
        bool is_expression; // Either expression or subtype_members
        union {
            Expression* expression;
            Array<Structure_Member_Node*> subtype_members;
        } options;
    };

    struct Definition_Struct 
    {
        Symbol_Node* symbol;
        Signature* signature;
        Array<Structure_Member_Node*> members;
        bool is_union;
    };

    struct Definition_Module
    {
        Optional<Symbol_Node*> symbol; // The root modules does not have a definition
        Array<Definition*> definitions;
    };


    enum class Extern_Type
    {
        FUNCTION,
        GLOBAL,
        STRUCT,
        COMPILER_SETTING,
    };

    struct Definition_Extern_Import
    {
        Extern_Type type;
        union 
        {
            struct {
                Symbol_Node* symbol;
                Expression* type_expr;
            } function;
            Path_Lookup* global_lookup;
            Path_Lookup* struct_type_lookup; // This should be a path to an already defined struct
            struct {
                Extern_Compiler_Setting type;
                String* value;
            } setting;
        } options;
    };

    enum class Import_Operator
    {
        SINGLE_SYMBOL,             // import A~a
        MODULE_IMPORT,            // import A~*
        MODULE_IMPORT_TRANSITIVE, // import A~**
        FILE_IMPORT,               // import "filename"
    };

    struct Definition_Import
    {
        Import_Type import_type;
        Import_Operator operator_type;
        Optional<Symbol_Node*> alias_name;
        union {
            Path_Lookup* path;
            struct {
                String* relative_path;
                Compilation_Unit* node_unit;
            } file_import;
        } options;
    };

    struct Enum_Member_Node
    {
        Node base;
        String* name;
        Optional<Expression*> value;
    };

    struct Definition_Enum
    {
        Symbol_Node* symbol;
        Array<Enum_Member_Node*> members;
    };

    struct Definition
    {
        Node base;
        Definition_Type type;
        union {
            Definition_Function function;
            Definition_Struct structure;
            Definition_Value value; // Global, var or const
            Definition_Custom_Operator custom_operator;
            Definition_Import import;
            Definition_Extern_Import extern_import;
            Definition_Enum enumeration;
            Definition_Module module;
        } options;
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
        Optional<String*> name; // If not available, supertype initializer
        Call_Node* call_node;
    };

    struct Call_Node
    {
        Node base;
        Array<Argument*> arguments;
        Array<Subtype_Initializer*> subtype_initializers;
        Array<Expression*> uninitialized_tokens;
    };

    struct Parameter
    {
        Node base;
        Symbol_Node* symbol; // name of parameter, or "!return_type" (See identifier pool)
        Optional<Expression*> type; // Comptime parameters may not have a type...
        bool is_comptime;    // $ at the start
        bool is_return_type;
    };

    struct Signature
    {
        Node base;
        // If we have a return type, it's the last in this array, and the is_return_type bool is set
        Array<Parameter*> parameters;
    };

    struct Code_Block
    {
        Node base;
        Array<Statement*> statements;
        Optional<String*> block_id;
    };

    enum class Expression_Type
    {
        // Value Generation
        BINARY_OPERATION,
        UNARY_OPERATION,
        FUNCTION_CALL,
        CAST,
        ARRAY_INITIALIZER,
        STRUCT_INITIALIZER,
        AUTO_ENUM,
        BAKE,
        INSTANCIATE,
        GET_OVERLOAD,

        // Memory Reads
        PATH_LOOKUP,
        LITERAL_READ,
        ARRAY_ACCESS,
        MEMBER_ACCESS,
        SUBTYPE_ACCESS,   // .>identifier
        BASETYPE_ACCESS, // .<

        // Types/Definitions
        INFERRED_FUNCTION, // .fn a > 17 or .fn { ... }
        PATTERN_VARIABLE, // $T

        ARRAY_TYPE,
        SLICE_TYPE,
        POINTER_TYPE,          // *int or ?*int
        FUNCTION_POINTER_TYPE, // *fn (a:int,b:int)

        ERROR_EXPR,
    };

    struct Expression
    {
        Node base;
        Expression_Type type;
        union
        {
            Symbol_Node* pattern_variable_symbol;
            struct {
                Expression* child_type; // ?int
                bool is_optional;
            } pointer_type;
            struct {
                Expression* left;
                Expression* right;
                Binop type;
            } binop;
            struct {
                Unop type;
                Expression* expr;
            } unop;
            Function_Body bake_body; 
            struct {
                Expression* expr;
                Call_Node* call_node;
                bool is_dot_call;
            } call;
            struct {
                Call_Node* call_node;
                bool is_dot_call;
            } cast;
            struct {
                Expression* expr;
                String* name;
            } subtype_access;
            Expression* basetype_access_expr;
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
            Function_Body inferred_function_body;
            Signature* function_pointer_signature;
            struct {
                Optional<Expression*> type_expr;
                Call_Node* call_node;
            } struct_initializer;
            struct {
                Optional<Expression*> type_expr;
                Array<Expression*> values;
            } array_initializer;
            struct {
                Expression* size_expr;
                Expression* type_expr;
            } array_type;
            Expression* slice_type;
            struct {
                Path_Lookup* path_lookup;
                Call_Node* call_node;
                Optional<AST::Expression*> return_type;
            } instanciate;
            struct {
                Path_Lookup* path; // Is not available if parsing failed!
                Array<Get_Overload_Argument*> arguments;
                bool is_poly;
            } get_overload;
        } options;
    };

    struct Switch_Case
    {
        Node base;
        Optional<Expression*> value; // Default-Case if value not available
        Optional<Symbol_Node*> variable_definition; // case .IPv4 => v4
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
                Expression* left_side;
                Expression* right_side;
            } assignment;
            struct {
                Symbol_Node* loop_variable_definition;
                Optional<Expression*> loop_variable_type;
                Expression* initial_value;
                Expression* condition;
                Statement* increment_statement;
                Code_Block* body_block;
            } for_loop;
            struct {
                Symbol_Node* loop_variable_definition;
                Optional<Symbol_Node*> index_variable_definition;
                Expression* expression;
                Code_Block* body_block;
            } foreach_loop;
            struct {
                Optional<Expression*> condition;
                Code_Block* block;
            } while_statement;
            struct {
                Expression* left_side;
                Expression* right_side;
                Binop binop;
            } binop_assignment;
            Code_Block* defer_block;
            struct {
                Expression* left_side;
                Expression* right_side;
            } defer_restore;
            struct {
                Expression* condition;
                Code_Block* block;
                Optional<Code_Block*> else_block;
            } if_statement;
            struct {
                Expression* condition;
                Array<Switch_Case*> cases;
                Optional<String*> label;
            } switch_statement;
            Optional<String*> break_name;
            Optional<String*> continue_name;
            Optional<Expression*> return_value;
        } options;
    };

    Node* base_get_child(Node* node, int child_index);
    void base_print(Node* node);
    void expression_append_to_string(AST::Expression* expr, String* str);
    void base_append_to_string(Node* base, String* str);

    void custom_operator_type_append_to_string(Custom_Operator_Type type, String* string);
    void path_lookup_append_to_string(Path_Lookup* read, String* string);
    int binop_priority(Binop binop);

    namespace Helpers
    {
        bool type_correct(Definition* base);
        bool type_correct(Get_Overload_Argument* base);
        bool type_correct(Symbol_Node* base);
        bool type_correct(Switch_Case* base);
        bool type_correct(Statement* base);
        bool type_correct(Signature* base);
        bool type_correct(Argument* base);
        bool type_correct(Parameter* base);
        bool type_correct(Expression* base);
        bool type_correct(Enum_Member_Node* base);
        bool type_correct(Path_Lookup* base);
        bool type_correct(Code_Block* base);
        bool type_correct(Structure_Member_Node* base);
        bool type_correct(Call_Node* base);
        bool type_correct(Subtype_Initializer* base);
    }

    template<typename T>
    T* downcast(Node* node)
    {
        T* result = (T*)node;
        assert(Helpers::type_correct(result), "Heyy");
        return result;
    }

    Node* upcast(Definition* node);
    Node* upcast(Get_Overload_Argument* node);
    Node* upcast(Symbol_Node* node);
    Node* upcast(Switch_Case* node);
    Node* upcast(Statement* node);
    Node* upcast(Signature* node);
    Node* upcast(Argument* node);
    Node* upcast(Parameter* node);
    Node* upcast(Expression* node);
    Node* upcast(Enum_Member_Node* node);
    Node* upcast(Path_Lookup* node);
    Node* upcast(Code_Block* node);
    Node* upcast(Structure_Member_Node* node);
    Node* upcast(Call_Node* node);
    Node* upcast(Subtype_Initializer* node);

    Node* upcast(Function_Body node);

    Node* upcast(Definition_Custom_Operator* node);
    Node* upcast(Definition_Function* node);
    Node* upcast(Definition_Struct* node);
    Node* upcast(Definition_Value* node);
    Node* upcast(Definition_Import* node);
    Node* upcast(Definition_Extern_Import* node);
    Node* upcast(Definition_Enum* node);
    Node* upcast(Definition_Module* node);

    Definition* upcast_definition(Definition_Custom_Operator* node);
    Definition* upcast_definition(Definition_Function* node);
    Definition* upcast_definition(Definition_Struct* node);
    Definition* upcast_definition(Definition_Value* node);
    Definition* upcast_definition(Definition_Import* node);
    Definition* upcast_definition(Definition_Extern_Import* node);
    Definition* upcast_definition(Definition_Enum* node);
    Definition* upcast_definition(Definition_Module* node);
}

