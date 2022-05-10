#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../utility/utils.hpp"
#include "../../datastructures/string.hpp"

struct Symbol;
struct Symbol_Table;

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
        ADDRESS_OF, // &
    };

    enum class Cast_Type
    {
        PTR_TO_RAW,
        RAW_TO_PTR,
        TYPE_TO_TYPE,
    };

    enum class Literal_Type
    {
        STRING,
        NUMBER,
        BOOLEAN,
    };

    enum class Base_Type
    {
        EXPRESSION,
        STATEMENT,
        DEFINITION, // ::, :=, : ... =, ...: ...
        CODE_BLOCK,
        MODULE,

        // Helpers
        ARGUMENT,    // Expression with optional name
        PARAMETER,   // Name/Type compination with optional default value + comptime
        SYMBOL_READ, // A symbol read
    };

    struct Base
    {
        Base_Type type;
        Base* parent;
        int allocation_index;
    };

    struct Symbol_Read
    {
        Base base;
        String* name;
        Optional<Symbol_Read*> path_child;
        Symbol* resolved_symbol;
    };

    struct Module
    {
        Base base;
        Dynamic_Array<Definition*> definitions;
        Symbol_Table* symbol_table;
    };

    struct Definition
    {
        Base base;
        Symbol* symbol;
        bool is_comptime; // :: instead of :=
        String* name;
        Optional<Expression*> type;
        Optional<Expression*> value;
    };

    struct Argument
    {
        Base base;
        Optional<String*> name;
        Expression* value;
    };

    struct Parameter
    {
        Base base;
        Symbol* symbol;
        bool is_comptime; // $ at the start
        String* name;
        Expression* type;
        Optional<Expression*> default_value;
    };

    struct Code_Block
    {
        Base base;
        Symbol_Table* symbol_table;
        Dynamic_Array<Statement*> statements;
        Optional<String*> block_id;
    };

    enum class Structure_Type {
        STRUCT,
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
        SYMBOL_READ,
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
        Base base;
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
            Symbol_Read* symbol_read;
            String* auto_enum;
            struct {
                Literal_Type type;
                union {
                    String* string;
                    int number;
                    bool boolean;
                } options;
            } literal_read;
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
                Symbol_Table* symbol_table;
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
            Dynamic_Array<String*> enum_members;
        } options;
    };

    struct Switch_Case
    {
        Optional<Expression*> value;
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
        Base base;
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
                Dynamic_Array<Switch_Case> cases;
            } switch_statement;
            String* break_name;
            String* continue_name;
            Optional<Expression*> return_value;
            Expression* delete_expr;
        } options;
    };

    void base_destroy(Base* node);
    Base* base_get_child(Base* node, int child_index);
    void base_enumerate_children(Base* node, Dynamic_Array<Base*>* fill);
    void base_print(Base* node);
}

