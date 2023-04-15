#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../utility/utils.hpp"
#include "../../datastructures/string.hpp"
#include "compiler_misc.hpp"
#include "source_code.hpp"

namespace AST
{
    struct Expression;
    struct Statement;
    struct Code_Block;
    struct Definition;
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

    enum class Cast_Type
    {
        PTR_TO_RAW,
        RAW_TO_PTR,
        TYPE_TO_TYPE,
    };

    enum class Node_Position_Type
    {
        TOKEN_INDEX,
        BLOCK_START,
        BLOCK_END
    };

    struct Node_Position
    {
        Node_Position_Type type;
        union {
            Block_Index block_index;
            Token_Index token_index;
        } options;
    };

    struct Node_Range
    {
        Node_Position start;
        Node_Position end;
    };

    Token_Index node_position_to_token_index(Node_Position pos);
    int node_position_compare(Node_Position a, Node_Position b);
    Node_Position node_position_make_token_index(Token_Index index);
    Node_Position node_position_make_block_end(Block_Index block_index);
    Node_Position node_position_make_block_start(Block_Index block_index);
    Node_Position node_position_make_end_of_line(Line_Index line_index);
    Node_Position node_position_make_start_of_line(Line_Index line_index);

    Node_Range node_range_make(Node_Position a, Node_Position b);
    Node_Range node_range_make(Token_Index a, Token_Index b);
    Node_Range node_range_make_block(Block_Index index);
    Token_Range node_range_to_token_range(Node_Range range);

    enum class Node_Type
    {
        EXPRESSION,
        STATEMENT,
        DEFINITION, // ::, :=, : ... =, ...: ...
        CODE_BLOCK,
        MODULE,

        // Helpers
        ARGUMENT,          // Expression with optional name
        PARAMETER,         // Name/Type compination with optional default value + comptime
        SYMBOL_LOOKUP,     // A single identifier lookup
        PATH_LOOKUP,       // Possibliy multiple symbol-lookups together (e.g. Utils~Logger~log)
        PROJECT_IMPORT,    // Loading a project
        ENUM_MEMBER,       // ID with or without value-expr
        SWITCH_CASE,       // Expression 
    };

    struct Node
    {
        Node_Type type;
        Node* parent;
        Node_Range range; // Note: It would be more memory efficient to just have a line-span + token-span or something along those lines...
        Node_Range bounding_range;
    };

    struct Project_Import
    {
        Node base;
        String* filename;
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
        Dynamic_Array<Project_Import*> imports;
    };

    struct Enum_Member
    {
        Node base;
        String* name;
        Optional<Expression*> value;
    };

    struct Definition
    {
        Node base;
        bool is_comptime; // :: instead of :=
        String* name;
        Optional<Expression*> type;
        Optional<Expression*> value;
    };

    struct Argument
    {
        Node base;
        Optional<String*> name;
        Expression* value;
    };

    struct Parameter
    {
        Node base;
        bool is_comptime; // $ at the start
        String* name;
        Expression* type;
        Optional<Expression*> default_value;
    };

    struct Code_Block
    {
        Node base;
        Dynamic_Array<Statement*> statements;
        Optional<String*> block_id;
    };

    enum class Structure_Type {
        STRUCT = 1,
        UNION,
        C_UNION
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

        // Memory Reads
        PATH_LOOKUP,
        LITERAL_READ,
        ARRAY_ACCESS,
        MEMBER_ACCESS,

        // Types/Definitions
        MODULE,
        FUNCTION,
        FUNCTION_SIGNATURE,

        STRUCTURE_TYPE, // Struct, union, c_union
        ENUM_TYPE,
        ARRAY_TYPE,
        SLICE_TYPE,

        ERROR_EXPR,
    };

    struct Expression
    {
        Node base;
        Expression_Type type;
        union
        {
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
                Dynamic_Array<Argument*> arguments;
            } call;
            struct {
                Expression* type_expr;
                Optional<Expression*> count_expr;
            } new_expr;
            struct {
                Cast_Type type;
                Optional<Expression*> to_type;
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
                Dynamic_Array<Argument*> arguments;
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
            struct {
                Dynamic_Array<Definition*> members;
                Structure_Type type;
            } structure;
            Dynamic_Array<Enum_Member*> enum_members;
        } options;
    };

    struct Switch_Case
    {
        Node base;
        Optional<Expression*> value; // Default-Case if value not available
        Code_Block* block;
    };

    enum class Statement_Type
    {
        DEFINITION,
        BLOCK,
        ASSIGNMENT,
        EXPRESSION_STATEMENT,
        // Keyword Statements
        DEFER,
        IF_STATEMENT,
        WHILE_STATEMENT,
        SWITCH_STATEMENT,
        BREAK_STATEMENT,
        CONTINUE_STATEMENT,
        RETURN_STATEMENT,
        DELETE_STATEMENT,
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
            Code_Block* defer_block;
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
        } options;
    };

    void base_destroy(Node* node);
    Node* base_get_child(Node* node, int child_index);
    void base_enumerate_children(Node* node, Dynamic_Array<Node*>* fill);
    void base_print(Node* node);
    void base_append_to_string(Node* base, String* str);

    void path_lookup_append_to_string(Path_Lookup* read, String* string);
    int binop_priority(Binop binop);

    namespace Helpers
    {
        bool type_correct(Definition* base);
        bool type_correct(Switch_Case* base);
        bool type_correct(Statement* base);
        bool type_correct(Argument* base);
        bool type_correct(Parameter* base);
        bool type_correct(Expression* base);
        bool type_correct(Enum_Member* base);
        bool type_correct(Module* base);
        bool type_correct(Project_Import* base);
        bool type_correct(Path_Lookup* base);
        bool type_correct(Symbol_Lookup* base);
        bool type_correct(Code_Block* base);
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

