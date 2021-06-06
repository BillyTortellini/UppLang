#include "ast_parser.hpp"
#include "compiler.hpp"

int ast_parser_check_for_undefines(AST_Parser* parser)
{
    int count = 0;
    for (int i = 0; i < parser->next_free_node; i++) {
        if (parser->nodes[i].type == AST_Node_Type::UNDEFINED) {
            //logg("Undefined at %d\n", i);
            count++;
        }
    }
    if (count > 0) {
        logg("wellerman\n");
    }
    return count;
}

int ast_parser_get_next_node_index_no_parent(AST_Parser* parser)
{
    while (parser->next_free_node >= parser->nodes.size) {
        AST_Node node;
        node.type = AST_Node_Type::UNDEFINED;
        node.children = dynamic_array_create_empty<AST_Node_Index>(2);
        node.parent = -1;
        dynamic_array_push_back(&parser->nodes, node);
        dynamic_array_push_back(&parser->token_mapping, token_range_make(-1, -1));
    }

    AST_Node* node = &parser->nodes[parser->next_free_node];
    parser->next_free_node++;
    node->parent = -1;
    dynamic_array_reset(&node->children);
    return parser->next_free_node - 1;
}

int ast_parser_get_next_node_index(AST_Parser* parser, AST_Node_Index parent_index)
{
    int index = ast_parser_get_next_node_index_no_parent(parser);
    AST_Node* node = &parser->nodes[index];
    node->parent = parent_index;
        
    if (parent_index != -1) {
        dynamic_array_push_back(&parser->nodes[parent_index].children, parser->next_free_node - 1);
    }
    return index;
}

void ast_parser_add_parent_child_connection(AST_Parser* parser, AST_Node_Index parent_index, AST_Node_Index child_index)
{
    parser->nodes[child_index].parent = parent_index;
    dynamic_array_push_back(&parser->nodes[parent_index].children, child_index);
}

AST_Parser_Checkpoint ast_parser_checkpoint_make(AST_Parser* parser, AST_Node_Index parent_index)
{
    AST_Parser_Checkpoint result;
    result.parser = parser;
    result.parent_index = parent_index;
    if (parent_index != -1)
        result.parent_child_count = parser->nodes.data[parent_index].children.size;
    else
        result.parent_child_count = 0;
    result.rewind_token_index = parser->index;
    result.next_free_node_index = parser->next_free_node;
    return result;
}

void ast_parser_checkpoint_reset(AST_Parser_Checkpoint checkpoint) 
{
    checkpoint.parser->index = checkpoint.rewind_token_index;
    checkpoint.parser->next_free_node = checkpoint.next_free_node_index;
    if (checkpoint.parent_index != -1) { // This is the case if root
        Dynamic_Array<AST_Node_Index>* parent_childs = &checkpoint.parser->nodes.data[checkpoint.parent_index].children;
        dynamic_array_remove_range_ordered(parent_childs, checkpoint.parent_child_count, parent_childs->size); 
        dynamic_array_rollback_to_size(parent_childs, checkpoint.parent_child_count); 
    }
}

bool ast_parser_test_next_token(AST_Parser* parser, Token_Type type)
{
    if (parser->index >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type) {
        return true;
    }
    return false;
}

bool ast_parser_test_next_2_tokens(AST_Parser* parser, Token_Type type1, Token_Type type2)
{
    if (parser->index + 1 >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type1 && parser->lexer->tokens[parser->index + 1].type == type2) {
        return true;
    }
    return false;
}

bool ast_parser_test_next_3_tokens(AST_Parser* parser, Token_Type type1, Token_Type type2, Token_Type type3)
{
    if (parser->index + 2 >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type1 &&
        parser->lexer->tokens[parser->index + 1].type == type2 &&
        parser->lexer->tokens[parser->index + 2].type == type3) {
        return true;
    }
    return false;
}

bool ast_parser_test_next_4_tokens(AST_Parser* parser, Token_Type type1, Token_Type type2, Token_Type type3,
    Token_Type type4)
{
    if (parser->index + 3 >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type1 &&
        parser->lexer->tokens[parser->index + 1].type == type2 &&
        parser->lexer->tokens[parser->index + 2].type == type3 &&
        parser->lexer->tokens[parser->index + 3].type == type4) 
    {
        return true;
    }
    return false;
}

bool ast_parser_test_next_5_tokens(AST_Parser* parser, Token_Type type1, Token_Type type2, Token_Type type3,
    Token_Type type4, Token_Type type5)
{
    if (parser->index + 4 >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type1 &&
        parser->lexer->tokens[parser->index + 1].type == type2 &&
        parser->lexer->tokens[parser->index + 2].type == type3 &&
        parser->lexer->tokens[parser->index + 3].type == type4 &&
        parser->lexer->tokens[parser->index + 4].type == type5) {
        return true;
    }
    return false;
}

int ast_parser_find_next_token_type(AST_Parser* parser, Token_Type type) 
{
    int index = parser->index;
    while (index < parser->lexer->tokens.size)
    {
        if (parser->lexer->tokens[index].type == type) {
            return index;
        }
        index++;
    }
    return index;
}

int ast_parser_find_next_line_start_token(AST_Parser* parser)
{
    int i = parser->index;
    int line = parser->lexer->tokens[parser->index].position.start.line;
    while (i < parser->lexer->tokens.size) {
        int token_line = parser->lexer->tokens[i].position.start.line;
        if (token_line != line) return i;
        i++;
    }
    return i;
}

int ast_parser_find_parenthesis_ending(AST_Parser* parser, Token_Type open_type, Token_Type closed_type)
{
    int i = parser->index;
    int depth = 0;
    while (i < parser->lexer->tokens.size)
    {
        if (parser->lexer->tokens[i].type == open_type) depth++;
        if (parser->lexer->tokens[i].type == closed_type) {
            depth--;
            if (depth <= 0) {
                return i;
            }
        }
        i++;
    }
    return i;
}

void ast_parser_log_error(AST_Parser* parser, const char* msg, Token_Range range)
{
    Compiler_Error error;
    error.message = msg;
    error.range = range;
    dynamic_array_push_back(&parser->errors, error);
}

/*
Pointers and Dereferencing/referencing:
a: int = 5;
b: *int = *a; // Remember: & is also bitwise AND, * is multiplication
&b = 17;  

I also need a get_reference unary operator in expressions

// So first i need the *int as a type, which means i need to do
// Better type testing --> Type Node in AST,
Type_Node can be TYPE_IDENTIFIER, or TYPE_POINTER_TO with one child

// Maybe at some point i want some implicit casting with pointers
    What are the next things tasks I would like to implement in my language:
        - Sorting an array (Features: Arrays)
        - Creating a list (Features: Pointers + Memory allocation)
        int* a = &b;
        a : int* = &b;
        a : int* = *b;
        a := int[5, 7, 8, 23];
        a := [int: "Frick", "You"];
        if (a == nullptr) // Maybe i want this really to be like null

    b : int[] = [2, 3, 4, 5];
    b.size;
    b[3];
    x := new int; // typeof(x) = int*
    x := new int[]; // typeof(x) = int[];

    I generally want pointers to implement datastructures, and for structs I would like to have 
    b : int[] = [2, 3, 4, 6];
    Parsing reference operator in expressions (New unary operator, only valid on variables), 
    Type-System needs pointer types (Semantic analyser)

    // Do i want something like pointer arithmetic?
*/
bool ast_parser_parse_expression(AST_Parser* parser, int parent_index);
bool ast_parser_parse_type(AST_Parser* parser, AST_Node_Index parent);
bool ast_parser_parse_parameter_block(AST_Parser* parser, AST_Node_Index parent_index, bool is_named_parameter_block);

bool ast_parser_parse_function_signature(AST_Parser* parser, AST_Node_Index parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node_Index node_index = ast_parser_get_next_node_index(parser, parent);

    if (ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) 
    {
        parser->nodes[node_index].type = AST_Node_Type::FUNCTION_SIGNATURE;
        if (!ast_parser_parse_parameter_block(parser, node_index, true)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_test_next_token(parser, Token_Type::ARROW)) {
            parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        parser->index++;
        if (!ast_parser_parse_type(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_type_function_pointer(AST_Parser* parser, AST_Node_Index parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node_Index node_index = ast_parser_get_next_node_index(parser, parent);

    if (ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) 
    {
        parser->nodes[node_index].type = AST_Node_Type::TYPE_FUNCTION_POINTER;
        if (!ast_parser_parse_parameter_block(parser, node_index, false)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_test_next_token(parser, Token_Type::ARROW)) {
            parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        parser->index++;
        if (!ast_parser_parse_type(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_type(AST_Parser* parser, AST_Node_Index parent)
{
    if (ast_parser_parse_type_function_pointer(parser, parent)) {
        return true;
    }

    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node_Index node_index = ast_parser_get_next_node_index(parser, parent);

    if (ast_parser_test_next_token(parser, Token_Type::IDENTIFIER)) {
        parser->nodes[node_index].type = AST_Node_Type::TYPE_IDENTIFIER;
        parser->nodes[node_index].name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        parser->index++;
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_token(parser, Token_Type::OP_STAR)) {
        parser->nodes[node_index].type = AST_Node_Type::TYPE_POINTER_TO;
        parser->index++;
        if (ast_parser_parse_type(parser, node_index)) {
            parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
    }

    if (ast_parser_test_next_token(parser, Token_Type::OPEN_BRACKETS))
    {
        parser->index++;
        parser->nodes[node_index].type = AST_Node_Type::TYPE_ARRAY_UNSIZED;
        if (ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACKETS)) {
            parser->index++;
            if (!ast_parser_parse_type(parser, node_index)) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
            parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }

        parser->nodes[node_index].type = AST_Node_Type::TYPE_ARRAY_SIZED;
        if (!ast_parser_parse_expression(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACKETS)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        parser->index++;
        if (!ast_parser_parse_type(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_argument_block(AST_Parser* parser, AST_Node_Index parent_index)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    if (!ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++;
    if (ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
        parser->index++;
        return true;
    }

    // TODO: Better error handling
    while (!ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS))
    {
        if (!ast_parser_parse_expression(parser, parent_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
            parser->index++;
            return true;
        }
        if (ast_parser_test_next_token(parser, Token_Type::COMMA)) {
            parser->index++;
            continue;
        }
        // Error
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++; // Skip )
    return true;
}

bool ast_parser_parse_expression(AST_Parser* parser, AST_Node_Index parent_index);
AST_Node_Index ast_parser_parse_expression_no_parents(AST_Parser* parser);

AST_Node_Index ast_parser_parse_member_access(AST_Parser* parser, int access_to_index)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, -1);
    int node_index = access_to_index;
    while (ast_parser_test_next_2_tokens(parser, Token_Type::DOT, Token_Type::IDENTIFIER)) 
    {
        int new_node_index = ast_parser_get_next_node_index_no_parent(parser);
        parser->nodes[new_node_index].type = AST_Node_Type::EXPRESSION_MEMBER_ACCESS;
        parser->nodes[new_node_index].name_id = parser->lexer->tokens[parser->index+1].attribute.identifier_number;
        parser->token_mapping[new_node_index] = token_range_make(parser->index, parser->index + 2);
        parser->index += 2;
        ast_parser_add_parent_child_connection(parser, new_node_index, node_index);
        node_index = new_node_index;
    }
    return node_index;
}

AST_Node_Index ast_parser_parse_variable_read(AST_Parser* parser)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, -1);
    int node_index = ast_parser_get_next_node_index_no_parent(parser);
    if (!ast_parser_test_next_token(parser, Token_Type::IDENTIFIER)) {
        ast_parser_checkpoint_reset(checkpoint);
        return -1;
    }
    parser->nodes[node_index].type = AST_Node_Type::EXPRESSION_VARIABLE_READ;
    parser->nodes[node_index].name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
    parser->token_mapping[node_index] = token_range_make(parser->index, parser->index + 1);
    parser->index++;
    return node_index;
}

AST_Node_Index ast_parser_parse_expression_single_value(AST_Parser* parser);
AST_Node_Index ast_parser_parse_array_or_member_access(AST_Parser* parser, AST_Node_Index child_node)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, -1);
    int node_index = child_node;
    while (true)
    {
        node_index = ast_parser_parse_member_access(parser, node_index);
        if (ast_parser_test_next_token(parser, Token_Type::OPEN_BRACKETS))
        {
            int new_node_index = ast_parser_get_next_node_index_no_parent(parser);
            parser->nodes[new_node_index].type = AST_Node_Type::EXPRESSION_ARRAY_ACCESS;
            parser->index++;
            ast_parser_add_parent_child_connection(parser, new_node_index, node_index);
            if (!ast_parser_parse_expression(parser, new_node_index)) {
                ast_parser_checkpoint_reset(checkpoint);
                return -1;
            }
            if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACKETS)) {
                ast_parser_checkpoint_reset(checkpoint);
                return -1;
            }
            parser->index++;
            parser->token_mapping[new_node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
            node_index = new_node_index;
        }
        else {
            return node_index;
        }
    }
}

AST_Node_Index ast_parser_parse_general_access(AST_Parser* parser)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, -1);
    int node_index = ast_parser_get_next_node_index_no_parent(parser);

    if (ast_parser_test_next_2_tokens(parser, Token_Type::CAST, Token_Type::COMPARISON_LESS))
    {
        parser->index += 2;
        parser->nodes[node_index].type = AST_Node_Type::EXPRESSION_CAST;
        if (!ast_parser_parse_type(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return -1;
        }
        if (!ast_parser_test_next_token(parser, Token_Type::COMPARISON_GREATER)) {
            ast_parser_checkpoint_reset(checkpoint);
            return -1;
        }
        parser->index += 1;
        AST_Node_Index child = ast_parser_parse_expression_single_value(parser);
        if (child == -1) {
            ast_parser_checkpoint_reset(checkpoint);
            return -1;
        }
        ast_parser_add_parent_child_connection(parser, node_index, child);
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, checkpoint.rewind_token_index + 1);
        return node_index;
    }
    if (ast_parser_test_next_token(parser, Token_Type::OP_STAR))
    {
        parser->index++;
        parser->nodes[node_index].type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF;
        AST_Node_Index child = ast_parser_parse_general_access(parser);
        if (child == -1) {
            ast_parser_checkpoint_reset(checkpoint);
            return -1;
        }
        ast_parser_add_parent_child_connection(parser, node_index, child);
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, checkpoint.rewind_token_index + 1);
        return node_index;
    }
    if (ast_parser_test_next_token(parser, Token_Type::LOGICAL_BITWISE_AND))
    {
        parser->index++;
        parser->nodes[node_index].type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
        AST_Node_Index child = ast_parser_parse_general_access(parser);
        if (child == -1) {
            ast_parser_checkpoint_reset(checkpoint);
            return -1;
        }
        ast_parser_add_parent_child_connection(parser, node_index, child);
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, checkpoint.rewind_token_index + 1);
        return node_index;
    }
    if (ast_parser_test_next_token(parser, Token_Type::LOGICAL_AND))
    {
        parser->index++;
        parser->nodes[node_index].type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
        AST_Node_Index child_index = ast_parser_get_next_node_index_no_parent(parser);
        parser->nodes[child_index].type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
        AST_Node_Index child_child = ast_parser_parse_general_access(parser);
        if (child_child == -1) {
            ast_parser_checkpoint_reset(checkpoint);
            return -1;
        }
        ast_parser_add_parent_child_connection(parser, child_index, child_child);
        ast_parser_add_parent_child_connection(parser, node_index, child_index);
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, checkpoint.rewind_token_index + 1);
        parser->token_mapping[child_index] = token_range_make(checkpoint.rewind_token_index, checkpoint.rewind_token_index + 1);
        return node_index;
    }

    ast_parser_checkpoint_reset(checkpoint);
    AST_Node_Index expr_index = -1;
    if (ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS))
    {
        ast_parser_checkpoint_reset(checkpoint); // Remove last thing
        parser->index++;
        expr_index = ast_parser_parse_expression_no_parents(parser);
        if (expr_index == -1 || !ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
            ast_parser_checkpoint_reset(checkpoint);
            return -1;
        }
        parser->index++;
    }
    if (expr_index == -1) {
        expr_index = ast_parser_parse_variable_read(parser);
    }
    if (expr_index == -1) {
        ast_parser_checkpoint_reset(checkpoint);
        return -1;
    }
    expr_index = ast_parser_parse_array_or_member_access(parser, expr_index);
    if (expr_index == -1) {
        ast_parser_checkpoint_reset(checkpoint);
        return -1;
    }
    return expr_index;
}

AST_Node_Index ast_parser_parse_expression_single_value(AST_Parser* parser)
{
    /*
        Evaluation order of unary ops? Also what expressions are valid
        a: int[5];
        b := &a[2]; // Obviously the dereference at last
        b := *a[2]; // Also the memory of at last
        b := &a;
        b.x = 5;
        READ <-- MULTIPLE_REFERENCE_DEREFERENCE ARRAY_ACCESSES
        ARRAY_ACCESSES <-- (MEMBER_ACCESS) | (MEMBER_ACCESS [expression])
        MULTIPLE_REFERENCE_DEREFERENCE <-- (* | &) MULTIPLE_REFERENCE_DEREFERENCE

        // Lets just think how to parse this shit

        a.x[5].b = 7 // Member_Access_Parent --> Member_Access_Child --> array_access
        *a[5]; // ADDRESS_OF

        MEMBER_ACCESS <-- VAR_READ . VAR_READ
        MEMBER_ACCESS <-- MEMBER_ACCESS . VAR_READ
    */
    /*
        What is a single value? 
            a       ... Variable read
            true    ... Literal
            (a+b)   ... Parenthesised expression
            a[5]    ... Array_Access
            a()     ... Function Call
            a.m     ... Member access
            *a      ... Reference-Of operator
            &a      ... Dereference operator
            -a      ... Negate operator
            !a      ... Not operator

        Single_Value    <-- Variable_Read | Literal | Parenthesis | Array_Access | Function_Call | Member_Access | Unary_Op
        Variable_Read   <-- Identifier     
        Function_Call

        Watch out for tree order
            a.b.c           --> Member_Access (.c) -> Member_Access(.b) -> Variable_Read(a)
            a[5].b.c[2].d   --> Member_Access (.d) -> Array_Access[2] -> Member_Access(.c) -> Member_Access (.b) -> Array_Access(5) -> Var_Read(a) 
            a(7).b[2]       --> 

        Do While (has member access at the end)
            Do While (has array access or function call)
    */

    // Cases: Function Call, Variable read, Literal Value, Unary Operation
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, -1);
    if (ast_parser_test_next_token(parser, Token_Type::IDENTIFIER))
    {
        int node_index = ast_parser_get_next_node_index_no_parent(parser);
        parser->nodes[node_index].type = AST_Node_Type::EXPRESSION_FUNCTION_CALL;
        parser->nodes[node_index].name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        parser->index++;
        if (ast_parser_parse_argument_block(parser, node_index)) {
            parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
            return node_index;
        }
        ast_parser_checkpoint_reset(checkpoint);
    }
    checkpoint = ast_parser_checkpoint_make(parser, -1);
    {
        int node_index = ast_parser_parse_general_access(parser);
        if (node_index != -1) {
            return node_index;
        }
        ast_parser_checkpoint_reset(checkpoint);
    }

    int node_index = ast_parser_get_next_node_index_no_parent(parser);
    if (ast_parser_test_next_token(parser, Token_Type::INTEGER_LITERAL) ||
        ast_parser_test_next_token(parser, Token_Type::FLOAT_LITERAL) ||
        ast_parser_test_next_token(parser, Token_Type::BOOLEAN_LITERAL) ||
        ast_parser_test_next_token(parser, Token_Type::STRING_LITERAL) ||
        ast_parser_test_next_token(parser, Token_Type::NULLPTR))
    {
        parser->nodes[node_index].type = AST_Node_Type::EXPRESSION_LITERAL;
        parser->index++;
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return node_index;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::OP_MINUS))
    {
        parser->nodes[node_index].type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE;
        parser->index++;
        AST_Node_Index child_index = ast_parser_parse_expression_single_value(parser);
        if (child_index == -1) {
            ast_parser_checkpoint_reset(checkpoint);
            return -1;
        }
        dynamic_array_push_back(&parser->nodes[node_index].children, child_index);
        parser->nodes[child_index].parent = node_index;
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return node_index;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::LOGICAL_NOT))
    {
        parser->nodes[node_index].type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT;
        parser->index++;
        AST_Node_Index child_index = ast_parser_parse_expression_single_value(parser);
        if (child_index == -1) {
            ast_parser_checkpoint_reset(checkpoint);
            return -1;
        }
        dynamic_array_push_back(&parser->nodes[node_index].children, child_index);
        parser->nodes[child_index].parent = node_index;
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return node_index;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::OP_STAR))
    {
        parser->nodes[node_index].type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF;
        parser->index++;
        AST_Node_Index child_index = ast_parser_parse_expression_single_value(parser);
        if (child_index == -1) {
            ast_parser_checkpoint_reset(checkpoint);
            return -1;
        }
        dynamic_array_push_back(&parser->nodes[node_index].children, child_index);
        parser->nodes[child_index].parent = node_index;
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return node_index;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::LOGICAL_BITWISE_AND))
    {
        parser->nodes[node_index].type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
        parser->index++;
        AST_Node_Index child_index = ast_parser_parse_expression_single_value(parser);
        if (child_index == -1) {
            ast_parser_checkpoint_reset(checkpoint);
            return -1;
        }
        dynamic_array_push_back(&parser->nodes[node_index].children, child_index);
        parser->nodes[child_index].parent = node_index;
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return node_index;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::LOGICAL_AND))
    {
        parser->nodes[node_index].type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
        parser->index++;
        {
            AST_Node_Index child_index = ast_parser_get_next_node_index(parser, node_index);
            parser->nodes[child_index].type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
            AST_Node_Index child_child_index = ast_parser_parse_expression_single_value(parser);
            if (child_child_index == -1) {
                ast_parser_checkpoint_reset(checkpoint);
                return -1;
            }
            ast_parser_add_parent_child_connection(parser, child_index, child_child_index);
            parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
            parser->token_mapping[child_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        }
        return node_index;
    }
    ast_parser_checkpoint_reset(checkpoint);
    return -1;
}

bool ast_parser_parse_binary_operation(AST_Parser* parser, AST_Node_Type* op_type, int* op_priority)
{
    /*
        Priority tree:
            0       ---     &&
            1       ---     ||
            2       ---     ==, !=
            3       ---     <, >, <=, >=
            4       ---     +, -
            5       ---     *, /
            6       ---     %
    */
    if (parser->index + 1 >= parser->lexer->tokens.size) return false;
    switch (parser->lexer->tokens[parser->index].type)
    {
    case Token_Type::LOGICAL_AND: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND;
        *op_priority = 0;
        break;
    }
    case Token_Type::LOGICAL_OR: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR;
        *op_priority = 1;
        break;
    }
    case Token_Type::COMPARISON_EQUAL: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL;
        *op_priority = 2;
        break;
    }
    case Token_Type::COMPARISON_NOT_EQUAL: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL;
        *op_priority = 2;
        break;
    }
    case Token_Type::COMPARISON_GREATER: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER;
        *op_priority = 3;
        break;
    }
    case Token_Type::COMPARISON_GREATER_EQUAL: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL;
        *op_priority = 3;
        break;
    }
    case Token_Type::COMPARISON_LESS: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS;
        *op_priority = 3;
        break;
    }
    case Token_Type::COMPARISON_LESS_EQUAL: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL;
        *op_priority = 3;
        break;
    }
    case Token_Type::OP_PLUS: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION;
        *op_priority = 4;
        break;
    }
    case Token_Type::OP_MINUS: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION;
        *op_priority = 4;
        break;
    }
    case Token_Type::OP_STAR: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION;
        *op_priority = 5;
        break;
    }
    case Token_Type::OP_SLASH: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION;
        *op_priority = 5;
        break;
    }
    case Token_Type::OP_PERCENT: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO;
        *op_priority = 6;
        break;
    }
    default: {
        return false;
    }
    }

    parser->index++;
    return true;
}

int ast_parser_parse_expression_priority(AST_Parser* parser, AST_Node_Index node_index, int min_priority)
{
    int start_point = parser->index;
    int rewind_point = parser->index;

    bool first_run = true;
    int max_priority = 999;
    while (true)
    {
        AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parser->nodes[node_index].parent);

        int first_op_priority;
        int first_op_index = parser->index;
        AST_Node_Type first_op_type;
        if (!ast_parser_parse_binary_operation(parser, &first_op_type, &first_op_priority)) {
            break;
        }
        if (first_op_priority < max_priority) {
            max_priority = first_op_priority;
        }
        if (first_op_priority < min_priority) {
            parser->index = rewind_point; // Undo the binary operation, maybe just do -1
            break;
        }

        AST_Node_Index operator_node = ast_parser_get_next_node_index_no_parent(parser);
        parser->nodes[operator_node].type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND; // This is just so that we dont have any undefines
        AST_Node_Index right_operand_index = ast_parser_parse_expression_single_value(parser);
        if (right_operand_index == -1) {
            ast_parser_checkpoint_reset(checkpoint);
            break;
        }
        rewind_point = parser->index;

        int second_op_priority;
        AST_Node_Type second_op_type;
        bool second_op_exists = ast_parser_parse_binary_operation(parser, &second_op_type, &second_op_priority);
        if (second_op_exists)
        {
            parser->index--;
            if (second_op_priority > max_priority) {
                right_operand_index = ast_parser_parse_expression_priority(parser, right_operand_index, second_op_priority);
            }
        }

        parser->nodes[node_index].parent = operator_node;
        dynamic_array_push_back(&parser->nodes[operator_node].children, node_index);
        parser->nodes[right_operand_index].parent = operator_node;
        dynamic_array_push_back(&parser->nodes[operator_node].children, right_operand_index);
        parser->nodes[operator_node].type = first_op_type;
        /*
        parser->token_mapping[operator_node] = token_range_make(
            parser->token_mapping[parser->nodes[operator_node].children[0]].start_index,
            parser->token_mapping[parser->nodes[operator_node].children[1]].end_index
        );
        */
        parser->token_mapping[operator_node] = token_range_make(first_op_index, first_op_index + 1);

        node_index = operator_node;
        if (!second_op_exists) break;
    }

    return node_index;
}

AST_Node_Index ast_parser_parse_expression_no_parents(AST_Parser* parser)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, -1);
    int single_value_index = ast_parser_parse_expression_single_value(parser);
    if (single_value_index == -1) {
        ast_parser_checkpoint_reset(checkpoint);
        return -1;
    }
    AST_Node_Index op_tree_root_index = ast_parser_parse_expression_priority(parser, single_value_index, 0);
    return op_tree_root_index;
}

bool ast_parser_parse_single_statement_expression(AST_Parser* parser, int parent_index)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    AST_Node_Index node_index = ast_parser_get_next_node_index(parser, parent_index);
    if (ast_parser_test_next_token(parser, Token_Type::NEW)) 
    {
        parser->nodes[node_index].type = AST_Node_Type::EXPRESSION_NEW;
        parser->index++;
        if (ast_parser_test_next_2_tokens(parser, Token_Type::OPEN_BRACKETS, Token_Type::CLOSED_BRACKETS)) {
            ast_parser_log_error(parser, "Cannot have new with empty brackets", token_range_make(checkpoint.rewind_token_index, parser->index));
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::OPEN_BRACKETS))
        {
            parser->nodes[node_index].type = AST_Node_Type::EXPRESSION_NEW_ARRAY;
            parser->index++;
            if (!ast_parser_parse_expression(parser, node_index)) {
                ast_parser_log_error(parser, "Invalid array-size expression in new", token_range_make(checkpoint.rewind_token_index, parser->index));
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
            if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACKETS)) {
                ast_parser_log_error(parser, "Missing closing brackets in array new", token_range_make(checkpoint.rewind_token_index, parser->index));
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
            parser->index++;
        }
        if (!ast_parser_parse_type(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }
    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_expression(AST_Parser* parser, int parent_index)
{
    if (ast_parser_parse_single_statement_expression(parser, parent_index)) {
        return true;
    }

    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    AST_Node_Index op_tree_root_index = ast_parser_parse_expression_no_parents(parser);

    if (op_tree_root_index == -1) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    dynamic_array_push_back(&parser->nodes[parent_index].children, op_tree_root_index);
    parser->nodes[op_tree_root_index].parent = parent_index;
    return true;
}

bool ast_parser_parse_statement_block(AST_Parser* parser, AST_Node_Index parent_index);
bool ast_parser_parse_statement(AST_Parser* parser, AST_Node_Index parent_index);

bool ast_parser_parse_single_statement_or_block(AST_Parser* parser, AST_Node_Index parent_index)
{
    if (ast_parser_parse_statement_block(parser, parent_index)) {
        return true;
    }

    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    int node_index = ast_parser_get_next_node_index(parser, parent_index);
    if (!ast_parser_parse_statement(parser, node_index)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->nodes[node_index].type = AST_Node_Type::STATEMENT_BLOCK;
    parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);

    return true;
}

bool ast_parser_parse_single_variable_definition(AST_Parser* parser, AST_Node_Index parent_index)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    int node_index = ast_parser_get_next_node_index(parser, parent_index);
    if (ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER, Token_Type::COLON))
    {
        parser->index += 2;
        if (!ast_parser_parse_type(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->nodes[node_index].type = AST_Node_Type::STATEMENT_VARIABLE_DEFINITION;
            parser->nodes[node_index].name_id = parser->lexer->tokens[checkpoint.rewind_token_index].attribute.identifier_number;
            parser->index += 1;
            parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
    }
    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_variable_creation_statement(AST_Parser* parser, AST_Node_Index parent_index)
{
    if (ast_parser_parse_single_variable_definition(parser, parent_index)) {
        return true;
    }

    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    int node_index = ast_parser_get_next_node_index(parser, parent_index);

    if (ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER, Token_Type::COLON))
    {
        parser->index += 2;
        if (!ast_parser_parse_type(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::OP_ASSIGNMENT))
        {
            parser->nodes[node_index].type = AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN;
            parser->nodes[node_index].name_id = parser->lexer->tokens[checkpoint.rewind_token_index].attribute.identifier_number;
            parser->index += 1;
            if (!ast_parser_parse_expression(parser, node_index)) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
            if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
                parser->index++;
                parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
                return true;
            }
            else {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
        }
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    if (ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER, Token_Type::INFER_ASSIGN))
    {
        parser->nodes[node_index].type = AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER;
        parser->nodes[node_index].name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        parser->index += 2;
        if (!ast_parser_parse_expression(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_statement(AST_Parser* parser, AST_Node_Index parent_index)
{
    if (ast_parser_parse_statement_block(parser, parent_index)) {
        return true;
    }
    if (ast_parser_parse_variable_creation_statement(parser, parent_index)) {
        return true;
    }

    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    int node_index = ast_parser_get_next_node_index(parser, parent_index);

    if (ast_parser_parse_expression(parser, node_index))
    {
        parser->nodes[node_index].type = AST_Node_Type::STATEMENT_EXPRESSION;
        if (ast_parser_test_next_token(parser, Token_Type::OP_ASSIGNMENT))
        {
            parser->nodes[node_index].type = AST_Node_Type::STATEMENT_ASSIGNMENT;
            parser->index++;
            if (!ast_parser_parse_expression(parser, node_index)) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
        }

        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    if (ast_parser_test_next_token(parser, Token_Type::DELETE_TOKEN))
    {
        parser->nodes[node_index].type = AST_Node_Type::STATEMENT_DELETE;
        parser->index++;
        if (!ast_parser_parse_expression(parser, node_index)) {
            ast_parser_log_error(parser, "Invalid expression after delete", token_range_make(checkpoint.rewind_token_index, parser->index));
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    if (ast_parser_test_next_token(parser, Token_Type::IF))
    {
        parser->nodes[node_index].type = AST_Node_Type::STATEMENT_IF;
        parser->index++;
        if (!ast_parser_parse_expression(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_parse_single_statement_or_block(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }

        if (ast_parser_test_next_token(parser, Token_Type::ELSE))
        {
            parser->nodes[node_index].type = AST_Node_Type::STATEMENT_IF_ELSE;
            parser->index++;
            if (!ast_parser_parse_single_statement_or_block(parser, node_index)) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
        }
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_token(parser, Token_Type::WHILE))
    {
        parser->nodes[node_index].type = AST_Node_Type::STATEMENT_WHILE;
        parser->index++;
        if (!ast_parser_parse_expression(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_parse_single_statement_or_block(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_2_tokens(parser, Token_Type::BREAK, Token_Type::SEMICOLON))
    {
        parser->nodes[node_index].type = AST_Node_Type::STATEMENT_BREAK;
        parser->index += 2;
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_2_tokens(parser, Token_Type::CONTINUE, Token_Type::SEMICOLON))
    {
        parser->nodes[node_index].type = AST_Node_Type::STATEMENT_CONTINUE;
        parser->index += 2;
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_token(parser, Token_Type::RETURN))
    {
        parser->nodes[node_index].type = AST_Node_Type::STATEMENT_RETURN;
        parser->index++;
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        if (!ast_parser_parse_expression(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_statement_block(AST_Parser* parser, AST_Node_Index parent_index)
{
    int start_token_index = parser->index;
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    int node_index = ast_parser_get_next_node_index(parser, parent_index);

    parser->nodes[node_index].type = AST_Node_Type::STATEMENT_BLOCK;
    if (!ast_parser_test_next_token(parser, Token_Type::OPEN_BRACES)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++;

    while (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES))
    {
        if (parser->index >= parser->lexer->tokens.size) {
            ast_parser_log_error(parser, "Statement block did not end!", token_range_make(checkpoint.rewind_token_index, parser->index));
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        checkpoint = ast_parser_checkpoint_make(parser, node_index);
        if (ast_parser_parse_statement(parser, node_index)) {
            continue;
        }
        ast_parser_checkpoint_reset(checkpoint);
        // Error handling, goto next ; or next line or end of {} block
        int next_semi = ast_parser_find_next_token_type(parser, Token_Type::SEMICOLON);
        int next_closing_braces = ast_parser_find_parenthesis_ending(parser, Token_Type::OPEN_BRACES, Token_Type::CLOSED_BRACES);
        int next_line = ast_parser_find_next_line_start_token(parser);
        if (next_line < next_semi && next_line < next_closing_braces) {
            ast_parser_log_error(parser, "Could not parse statement", token_range_make(parser->index, next_line - 1));
            parser->index = next_line;
            continue;
        }
        if (next_semi < next_closing_braces) {
            ast_parser_log_error(parser, "Could not parse statement", token_range_make(parser->index, next_semi));
            parser->index = next_semi + 1;
            continue;
        }
        ast_parser_log_error(parser, "Could not parse statement", token_range_make(parser->index, next_closing_braces));
        parser->index = next_closing_braces;
    }
    parser->index++;

    parser->token_mapping[node_index] = token_range_make(start_token_index, parser->index);
    if (parser->nodes[node_index].type != AST_Node_Type::STATEMENT_BLOCK) {
        logg("Wath");
    }
    if (parser->token_mapping[node_index].start_index == 0) {
        logg("What");
    }
    return true;
}

bool ast_parser_parse_parameter_block(AST_Parser* parser, AST_Node_Index parent_index, bool is_named_parameter_block)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    int block_index = ast_parser_get_next_node_index(parser, parent_index);

    if (is_named_parameter_block) {
        parser->nodes[block_index].type = AST_Node_Type::PARAMETER_BLOCK_NAMED;
    }
    else {
        parser->nodes[block_index].type = AST_Node_Type::PARAMETER_BLOCK_UNNAMED;
    }

    if (!ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) {
        return false;
    }
    parser->index++;
    if (ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
        parser->index++;
        parser->token_mapping[block_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    while (true)
    {
        AST_Parser_Checkpoint recoverable_checkpoint = ast_parser_checkpoint_make(parser, block_index);
        bool success = true;
        if (is_named_parameter_block)
        {
            if (ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER, Token_Type::COLON))
            {
                AST_Node_Index parameter_index = ast_parser_get_next_node_index(parser, block_index);
                parser->index += 2;

                success = ast_parser_parse_type(parser, parameter_index);
                if (success)
                {
                    parser->nodes[parameter_index].type = AST_Node_Type::NAMED_PARAMETER;
                    parser->nodes[parameter_index].name_id = parser->lexer->tokens[recoverable_checkpoint.rewind_token_index].attribute.identifier_number;
                    parser->token_mapping[parameter_index].start_index = recoverable_checkpoint.rewind_token_index;
                    parser->token_mapping[parameter_index].end_index = parser->index;
                }
            }
        }
        else {
            success = ast_parser_parse_type(parser, block_index);
        }

        if (ast_parser_test_next_token(parser, Token_Type::COMMA)) {
            parser->index++;
            continue;
        }
        else if (ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
            parser->index++;
            parser->token_mapping[block_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        else {
            success = false;
        }

        if (!success)
        {
            ast_parser_checkpoint_reset(recoverable_checkpoint);
            // Error handling: find next ), or next Comma, or next  report error and do further error handling
            int next_closed_braces = ast_parser_find_next_token_type(parser, Token_Type::CLOSED_BRACES);
            int next_closed_parenthesis = ast_parser_find_parenthesis_ending(parser, Token_Type::OPEN_PARENTHESIS, Token_Type::CLOSED_PARENTHESIS);
            int next_comma = ast_parser_find_next_token_type(parser, Token_Type::COMMA);
            if (next_comma < next_closed_parenthesis && next_comma < next_closed_braces) {
                ast_parser_log_error(parser, "Could not parse function parameter", token_range_make(parser->index, next_comma));
                parser->index = next_comma + 1;
                continue;
            }
            if (next_closed_parenthesis < next_closed_braces) {
                ast_parser_log_error(parser, "Could not parse parameters", token_range_make(parser->index, next_closed_parenthesis));
                parser->index = next_closed_parenthesis + 1;
                parser->token_mapping[block_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
                return true;
            }
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    panic("Should not happen!\n");
    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_struct_members(AST_Parser* parser, AST_Node_Index parent_index)
{
    while (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES))
    {
        AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
        bool success = ast_parser_parse_single_variable_definition(parser, parent_index);
        // Do error handling here (Goto next semicolon or Closed Braces)
        if (!success)
        {
            ast_parser_checkpoint_reset(checkpoint);
            int next_semicolon = ast_parser_find_next_token_type(parser, Token_Type::SEMICOLON);
            int next_closing_braces = ast_parser_find_next_token_type(parser, Token_Type::CLOSED_BRACES);
            if (next_semicolon < next_closing_braces) {
                ast_parser_log_error(parser, "Variable definition invalid!", token_range_make(checkpoint.rewind_token_index, next_semicolon));
                parser->index = next_semicolon + 1;
                continue;
            }
            ast_parser_log_error(parser, "Variable definition invalid!", token_range_make(checkpoint.rewind_token_index, next_closing_braces));
            parser->index = next_closing_braces;
            break;
        }
    }

    return true;
}

bool ast_parser_parse_struct(AST_Parser* parser, AST_Node_Index parent_index)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    int node_index = ast_parser_get_next_node_index(parser, parent_index);
    parser->nodes[node_index].type = AST_Node_Type::STRUCT;

    // Parse Struct name
    if (!ast_parser_test_next_4_tokens(parser, Token_Type::IDENTIFIER, Token_Type::DOUBLE_COLON, Token_Type::STRUCT, Token_Type::OPEN_BRACES)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->nodes[node_index].name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
    parser->index += 4;
    ast_parser_parse_struct_members(parser, node_index);

    if (ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
        parser->index++;
        parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }
    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_function(AST_Parser* parser, AST_Node_Index parent_index)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    int node_index = ast_parser_get_next_node_index(parser, parent_index);
    parser->nodes[node_index].type = AST_Node_Type::FUNCTION;

    // Parse Function start
    if (!ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER, Token_Type::DOUBLE_COLON)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->nodes[node_index].name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
    parser->index += 2;

    if (!ast_parser_parse_function_signature(parser, node_index)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    if (!ast_parser_parse_statement_block(parser, node_index)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    };

    parser->token_mapping[node_index] = token_range_make(checkpoint.rewind_token_index, parser->index);
    return true;
}

void ast_parser_parse_root(AST_Parser* parser)
{
    int root_index = ast_parser_get_next_node_index(parser, -1);
    parser->nodes[root_index].type = AST_Node_Type::ROOT;
    while (true)
    {
        AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, root_index);
        if (parser->index >= parser->lexer->tokens.size) break;
        if (ast_parser_parse_function(parser, root_index)) {
            continue;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
        }
        if (ast_parser_parse_struct(parser, root_index)) {
            continue;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
        }
        if (ast_parser_parse_variable_creation_statement(parser, root_index)) {
            continue;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
        }

        // TODO: Better error handling: Skip through each line (not in parenthesis) and try parsing function or struct
        int next_closing_braces = ast_parser_find_parenthesis_ending(parser, Token_Type::OPEN_BRACES, Token_Type::CLOSED_BRACES);
        ast_parser_checkpoint_reset(checkpoint);
        ast_parser_log_error(parser, "Could not parse function", token_range_make(parser->index, next_closing_braces));
        parser->index = next_closing_braces + 1;
    }
    parser->token_mapping[root_index].start_index = 0;
    parser->token_mapping[root_index].end_index = math_maximum(0, parser->lexer->tokens.size - 1);
}

AST_Parser ast_parser_create()
{
    AST_Parser parser;
    parser.index = 0;
    parser.nodes = dynamic_array_create_empty<AST_Node>(1024);
    parser.token_mapping = dynamic_array_create_empty<Token_Range>(1024);
    parser.errors = dynamic_array_create_empty<Compiler_Error>(64);
    parser.next_free_node = 0;
    return parser;
}

void ast_parser_check_sanity(AST_Parser* parser)
{
    // Check for undefines
    int count = ast_parser_check_for_undefines(parser);
    logg("Undefined count: %d\n", count);
    count = 0;

    // Check parent indices
    for (int i = 0; i < parser->nodes.size; i++)
    {
        int parent_index = parser->nodes[i].parent;
        if (parent_index == -1) {
            if (parser->nodes[i].type == AST_Node_Type::ROOT) {
                continue;
            }
            panic("Should not happen");
        }
        if (parent_index < 0 || parent_index >= parser->nodes.size) {
            panic("Should not happen");
        }
    }

    // Check if token mappings arent 0 sized
    for (int i = 0; i < parser->token_mapping.size; i++)
    {
        int start = parser->token_mapping[i].start_index;
        int end = parser->token_mapping[i].end_index;
        if (parser->lexer->tokens.size != 0)
        {
            if (start < 0 || end < 0 || start >= parser->lexer->tokens.size || end > parser->lexer->tokens.size) {
                AST_Node* node = &parser->nodes[i];
                logg("Should not happen: range: %d-%d, index: %d\n", start, end, i);
                panic("Should not happen!");
            }
            if (start == end) {
                AST_Node* node = &parser->nodes[i];
                if (node->type != AST_Node_Type::ROOT) {
                    logg("Should not happen: range: %d-%d, index: %d\n", start, end, i);
                    logg("Node_Type::%s\n", ast_node_type_to_string(node->type).characters);
                    panic("Should not happen!");
                }
            }
        }
    }

    // Check child types of nodes
    for (int j = 0; j < parser->nodes.size; j++)
    {
        AST_Node* node = &parser->nodes[j];
        for (int i = 0; i < node->children.size; i++) {
            int index = node->children[i];
            if (index < 0 || index >= parser->nodes.size) {
                panic("Should not happen\n");
            }
        }
        switch (node->type)
        {
        case AST_Node_Type::ROOT:
            for (int i = 0; i < node->children.size; i++)
            {
                AST_Node_Type child_type = parser->nodes[node->children[i]].type;
                if (child_type != AST_Node_Type::FUNCTION &&
                    child_type != AST_Node_Type::STRUCT &&
                    child_type != AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN &&
                    child_type != AST_Node_Type::STATEMENT_VARIABLE_DEFINITION &&
                    child_type != AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER)
                {
                    panic("Should not happen");
                }
            }
            break;
        case AST_Node_Type::STRUCT:
            for (int i = 0; i < node->children.size; i++) {
                AST_Node_Type child_type = parser->nodes[node->children[i]].type;
                if (child_type != AST_Node_Type::STATEMENT_VARIABLE_DEFINITION) {
                    panic("Should not happen");
                }
            }
            break;
        case AST_Node_Type::FUNCTION:
            if (node->children.size != 2) {
                panic("Should not happen");
            }
            if (parser->nodes[node->children[0]].type != AST_Node_Type::FUNCTION_SIGNATURE ||
                parser->nodes[node->children[1]].type != AST_Node_Type::STATEMENT_BLOCK) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::PARAMETER_BLOCK_NAMED:
            for (int i = 0; i < node->children.size; i++) {
                AST_Node_Type child_type = parser->nodes[node->children[i]].type;
                if (child_type != AST_Node_Type::NAMED_PARAMETER) {
                    panic("Should not happen");
                }
            }
            break;
        case AST_Node_Type::PARAMETER_BLOCK_UNNAMED:
            for (int i = 0; i < node->children.size; i++) {
                AST_Node_Type child_type = parser->nodes[node->children[i]].type;
                if (!ast_node_type_is_type(child_type)) {
                    panic("Should not happen");
                }
            }
            break;
        case AST_Node_Type::FUNCTION_SIGNATURE:
            if (node->children.size != 1 && node->children.size != 2) {
                panic("Should not happen");
            }
            {
                AST_Node* child = &parser->nodes[node->children[0]];
                if (child->type != AST_Node_Type::PARAMETER_BLOCK_NAMED) {
                    panic("Should not happen");
                }
            }
            if (node->children.size == 2) {
                AST_Node* child = &parser->nodes[node->children[1]];
                if (!ast_node_type_is_type(child->type)) {
                    panic("Should not happen");
                }
            }
            break;
        case AST_Node_Type::TYPE_FUNCTION_POINTER:
            if (node->children.size != 1 && node->children.size != 2) {
                panic("Should not happen");
            }
            {
                AST_Node* child = &parser->nodes[node->children[0]];
                if (child->type != AST_Node_Type::PARAMETER_BLOCK_UNNAMED) {
                    panic("Should not happen");
                }
            }
            if (node->children.size == 2) {
                AST_Node* child = &parser->nodes[node->children[1]];
                if (!ast_node_type_is_type(child->type)) {
                    panic("Should not happen");
                }
            }
            break;
        case AST_Node_Type::TYPE_ARRAY_UNSIZED:
        case AST_Node_Type::TYPE_POINTER_TO:
        case AST_Node_Type::NAMED_PARAMETER:
            if (node->children.size != 1) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_type(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::TYPE_ARRAY_SIZED:
            if (node->children.size != 2) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_BLOCK:
            for (int i = 0; i < node->children.size; i++) {
                AST_Node_Type child_type = parser->nodes[node->children[i]].type;
                if (!ast_node_type_is_statement(child_type)) {
                    panic("Should not happen");
                }
            }
            break;
        case AST_Node_Type::STATEMENT_WHILE:
        case AST_Node_Type::STATEMENT_IF:
            if (node->children.size != 2) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            if (parser->nodes[node->children[1]].type != AST_Node_Type::STATEMENT_BLOCK) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_IF_ELSE:
            if (node->children.size != 3) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            if (parser->nodes[node->children[1]].type != AST_Node_Type::STATEMENT_BLOCK) {
                panic("Should not happen");
            }
            if (parser->nodes[node->children[2]].type != AST_Node_Type::STATEMENT_BLOCK) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_BREAK:
        case AST_Node_Type::STATEMENT_CONTINUE:
        case AST_Node_Type::TYPE_IDENTIFIER:
            if (node->children.size != 0) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_EXPRESSION:
        case AST_Node_Type::STATEMENT_RETURN:
            if (node->children.size == 0) return;
            if (node->children.size != 1) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_ASSIGNMENT:
            if (node->children.size != 2) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[1]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION:
            if (node->children.size != 1) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_type(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
            if (node->children.size != 2) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_type(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[1]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER:
            if (node->children.size != 1) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_DELETE:
            if (node->children.size != 1) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_NEW:
            if (node->children.size != 1) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_type(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_NEW_ARRAY:
            if (node->children.size != 2) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_type(parser->nodes[node->children[1]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_LITERAL:
            if (node->children.size != 0) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_FUNCTION_CALL:
            for (int i = 0; i < node->children.size; i++) {
                AST_Node_Type child_type = parser->nodes[node->children[i]].type;
                if (!ast_node_type_is_expression(child_type)) {
                    panic("Should not happen");
                }
            }
            break;
        case AST_Node_Type::EXPRESSION_VARIABLE_READ:
            if (node->children.size != 0) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_ARRAY_ACCESS:
            if (node->children.size != 2) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[1]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_MEMBER_ACCESS:
            if (node->children.size != 1) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_CAST:
            if (node->children.size != 2) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_type(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[1]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL:
            if (node->children.size != 2) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[1]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE:
        case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT:
        case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF:
        case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE:
            if (node->children.size != 1) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(parser->nodes[node->children[0]].type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::UNDEFINED:
            panic("Should not happen!");
            break;
        default:
            panic("Should not happen!");
            break;
        }
    }
}

void ast_parser_parse(AST_Parser* parser, Lexer* lexer)
{
    parser->index = 0;
    parser->next_free_node = 0;
    parser->lexer = lexer;
    dynamic_array_reset(&parser->errors);
    for (int i = 0; i < parser->nodes.size; i++) {
        dynamic_array_destroy(&parser->nodes[i].children);
    }
    dynamic_array_reset(&parser->nodes);
    dynamic_array_reset(&parser->token_mapping);

    ast_parser_parse_root(parser);
    for (int i = parser->next_free_node; i < parser->nodes.size; i++) {
        dynamic_array_destroy(&parser->nodes[i].children);
    }
    dynamic_array_rollback_to_size(&parser->nodes, parser->next_free_node);
    dynamic_array_rollback_to_size(&parser->token_mapping, parser->next_free_node);

    ast_parser_check_sanity(parser);
}

void ast_parser_destroy(AST_Parser* parser)
{
    for (int i = 0; i < parser->nodes.size; i++) {
        dynamic_array_destroy(&parser->nodes[i].children);
    }
    dynamic_array_destroy(&parser->nodes);
    dynamic_array_destroy(&parser->token_mapping);
}

String ast_node_type_to_string(AST_Node_Type type)
{
    switch (type)
    {
    case AST_Node_Type::ROOT: return string_create_static("ROOT");
    case AST_Node_Type::STRUCT: return string_create_static("STRUCT");
    case AST_Node_Type::FUNCTION: return string_create_static("FUNCTION");
    case AST_Node_Type::FUNCTION_SIGNATURE: return string_create_static("FUNCTION_SIGNATURE");
    case AST_Node_Type::TYPE_FUNCTION_POINTER: return string_create_static("TYPE_FUNCTION_POINTER");
    case AST_Node_Type::TYPE_IDENTIFIER: return string_create_static("TYPE_IDENTIFIER");
    case AST_Node_Type::TYPE_POINTER_TO: return string_create_static("TYPE_POINTER_TO");
    case AST_Node_Type::TYPE_ARRAY_SIZED: return string_create_static("TYPE_ARRAY_SIZED");
    case AST_Node_Type::TYPE_ARRAY_UNSIZED: return string_create_static("TYPE_ARRAY_UNSIZED");
    case AST_Node_Type::PARAMETER_BLOCK_UNNAMED: return string_create_static("PARAMETER_BLOCK_UNNAMED");
    case AST_Node_Type::PARAMETER_BLOCK_NAMED: return string_create_static("PARAMETER_BLOCK_NAMED");
    case AST_Node_Type::NAMED_PARAMETER: return string_create_static("PARAMETER");
    case AST_Node_Type::STATEMENT_BLOCK: return string_create_static("STATEMENT_BLOCK");
    case AST_Node_Type::STATEMENT_IF: return string_create_static("STATEMENT_IF");
    case AST_Node_Type::STATEMENT_IF_ELSE: return string_create_static("STATEMENT_IF_ELSE");
    case AST_Node_Type::STATEMENT_WHILE: return string_create_static("STATEMENT_WHILE");
    case AST_Node_Type::STATEMENT_BREAK: return string_create_static("STATEMENT_BREAK");
    case AST_Node_Type::STATEMENT_CONTINUE: return string_create_static("STATEMENT_CONTINUE");
    case AST_Node_Type::STATEMENT_RETURN: return string_create_static("STATEMENT_RETURN");
    case AST_Node_Type::STATEMENT_EXPRESSION: return string_create_static("STATEMENT_EXPRESSION");
    case AST_Node_Type::STATEMENT_ASSIGNMENT: return string_create_static("STATEMENT_ASSIGNMENT");
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION: return string_create_static("STATEMENT_VARIABLE_DEFINITION");
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN: return string_create_static("STATEMENT_VARIABLE_DEFINE_ASSIGN");
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER: return string_create_static("STATEMENT_VARIABLE_DEFINE_INFER");
    case AST_Node_Type::STATEMENT_DELETE: return string_create_static("STATEMENT_DELETE");
    case AST_Node_Type::EXPRESSION_ARRAY_ACCESS: return string_create_static("EXPRESSION_ARRAY_INDEX");
    case AST_Node_Type::EXPRESSION_MEMBER_ACCESS: return string_create_static("EXPRESSION_MEMBER_ACCESS");
    case AST_Node_Type::EXPRESSION_CAST: return string_create_static("EXPRESSION_CAST");
    case AST_Node_Type::EXPRESSION_LITERAL: return string_create_static("EXPRESSION_LITERAL");
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL: return string_create_static("EXPRESSION_FUNCTION_CALL");
    case AST_Node_Type::EXPRESSION_VARIABLE_READ: return string_create_static("EXPRESSION_VARIABLE_READ");
    case AST_Node_Type::EXPRESSION_NEW: return string_create_static("EXPRESSION_NEW");
    case AST_Node_Type::EXPRESSION_NEW_ARRAY: return string_create_static("EXPRESSION_NEW_ARRAY");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION: return string_create_static("EXPRESSION_BINARY_OPERATION_ADDITION");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION: return string_create_static("EXPRESSION_BINARY_OPERATION_SUBTRACTION");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION: return string_create_static("EXPRESSION_BINARY_OPERATION_DIVISION");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION: return string_create_static("EXPRESSION_BINARY_OPERATION_MULTIPLICATION");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO: return string_create_static("EXPRESSION_BINARY_OPERATION_MODULO");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND: return string_create_static("EXPRESSION_BINARY_OPERATION_AND");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR: return string_create_static("EXPRESSION_BINARY_OPERATION_OR");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL: return string_create_static("EXPRESSION_BINARY_OPERATION_EQUAL");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL: return string_create_static("EXPRESSION_BINARY_OPERATION_NOT_EQUAL");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS: return string_create_static("EXPRESSION_BINARY_OPERATION_LESS");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL: return string_create_static("EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER: return string_create_static("EXPRESSION_BINARY_OPERATION_GREATER");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL: return string_create_static("EXPRESSION_BINARY_OPERATIONGREATER_OR_EQUAL");
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE: return string_create_static("EXPRESSION_UNARY_OPERATION_NEGATE");
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT: return string_create_static("EXPRESSION_UNARY_OPERATION_NOT");
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF: return string_create_static("EXPRESSION_UNARY_ADDRESS_OF");
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE: return string_create_static("EXPRESSION_UNARY_DEREFERENCE");
    case AST_Node_Type::UNDEFINED: return string_create_static("UNDEFINED");
    }
    panic("Should not happen");
    return string_create_static("SHOULD NOT FUCKING HAPPEN MOTHERFUCKER");
}

bool ast_node_type_is_binary_expression(AST_Node_Type type) {
    return type >= AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION && type <= AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL;
}
bool ast_node_type_is_unary_expression(AST_Node_Type type) {
    return type >= AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE && type <= AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
}
bool ast_node_type_is_expression(AST_Node_Type type) {
    return type >= AST_Node_Type::EXPRESSION_NEW && type <= AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
}
bool ast_node_type_is_statement(AST_Node_Type type) {
    return type >= AST_Node_Type::STATEMENT_BLOCK && type <= AST_Node_Type::STATEMENT_DELETE;
}
bool ast_node_type_is_type(AST_Node_Type type) {
    return type >= AST_Node_Type::FUNCTION_SIGNATURE && type <= AST_Node_Type::TYPE_ARRAY_UNSIZED;
}

void ast_node_expression_append_to_string(AST_Parser* parser, AST_Node_Index node_index, String* string)
{
    AST_Node* node = &parser->nodes[node_index];
    bool bin_op = false;
    bool unary_op = false;
    const char* bin_op_str = "asfd";
    switch (node->type)
    {
    case AST_Node_Type::EXPRESSION_LITERAL:
        Token t = parser->lexer->tokens[parser->token_mapping[node_index].start_index];
        switch (t.type) {
        case Token_Type::BOOLEAN_LITERAL: string_append_formated(string, t.attribute.bool_value ? "TRUE" : "FALSE"); break;
        case Token_Type::INTEGER_LITERAL: string_append_formated(string, "%d", t.attribute.integer_value); break;
        case Token_Type::FLOAT_LITERAL: string_append_formated(string, "%3.2f", t.attribute.float_value); break;
        case Token_Type::STRING_LITERAL: string_append_formated(string, "\"%s\"",
            lexer_identifer_to_string(parser->lexer, t.attribute.identifier_number).characters); break;
        }
        return;
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL:
        string_append_formated(string, "%s(", lexer_identifer_to_string(parser->lexer, node->name_id).characters);
        for (int i = 0; i < node->children.size; i++) {
            ast_node_expression_append_to_string(parser, node->children[i], string);
            string_append_formated(string, ", ");
        }
        string_append_formated(string, ") ", lexer_identifer_to_string(parser->lexer, node->name_id).characters);
        return;
    case AST_Node_Type::EXPRESSION_VARIABLE_READ:
        string_append_formated(string, "%s", lexer_identifer_to_string(parser->lexer, node->name_id).characters);
        return;
    case AST_Node_Type::EXPRESSION_ARRAY_ACCESS:
        ast_node_expression_append_to_string(parser, node->children[0], string);
        string_append_formated(string, "[");
        ast_node_expression_append_to_string(parser, node->children[1], string);
        string_append_formated(string, "]");
        return;
    case AST_Node_Type::EXPRESSION_MEMBER_ACCESS:
        ast_node_expression_append_to_string(parser, node->children[0], string);
        string_append_formated(string, ".%s", lexer_identifer_to_string(parser->lexer, node->name_id).characters);
        return;
    case AST_Node_Type::EXPRESSION_CAST:
        string_append_formated(string, "cast(...)");
        ast_node_expression_append_to_string(parser, node->children[1], string);
        return;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION: bin_op = true, bin_op_str = "+"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION: bin_op = true, bin_op_str = "-"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION: bin_op = true, bin_op_str = "/"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION: bin_op = true, bin_op_str = "*"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO: bin_op = true, bin_op_str = "%"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND: bin_op = true, bin_op_str = "&&"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR: bin_op = true, bin_op_str = "||"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL: bin_op = true, bin_op_str = "=="; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL: bin_op = true, bin_op_str = "!="; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS: bin_op = true, bin_op_str = "<"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL: bin_op = true, bin_op_str = "<="; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER: bin_op = true, bin_op_str = ">"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL: bin_op = true, bin_op_str = ">="; break;
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE: unary_op = true, bin_op_str = "-"; break;
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT:unary_op = true, bin_op_str = "!"; break;
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF:unary_op = true, bin_op_str = "*"; break;
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE:unary_op = true, bin_op_str = "&"; break;
    default: return;
    }
    if (bin_op)
    {
        string_append_formated(string, "(");
        ast_node_expression_append_to_string(parser, node->children[0], string);
        string_append_formated(string, " %s ", bin_op_str);
        ast_node_expression_append_to_string(parser, node->children[1], string);
        string_append_formated(string, ")");
        return;
    }
    else if (unary_op)
    {
        string_append_formated(string, bin_op_str);
        ast_node_expression_append_to_string(parser, node->children[0], string);
    }
}

void ast_node_append_to_string(AST_Parser* parser, int node_index, String* string, int indentation_lvl)
{
    AST_Node* node = &parser->nodes[node_index];
    for (int j = 0; j < indentation_lvl; j++) {
        string_append_formated(string, "  ");
    }
    string_append_formated(string, "#%d ", node_index);
    String type_str = ast_node_type_to_string(node->type);
    string_append_string(string, &type_str);
    if (ast_node_type_is_expression(node->type)) {
        string_append_formated(string, ": ");
        ast_node_expression_append_to_string(parser, node_index, string);
        //string_append_formated(string, "\n");
    }
    if (parser->lexer->tokens.size > 0) {
        int start_index = parser->token_mapping[node_index].start_index;
        int end_index = parser->token_mapping[node_index].end_index;
        if (end_index == parser->lexer->tokens.size) {
            end_index = parser->lexer->tokens.size - 1;
        }
        string_append_formated(string, " Line-Range: %d-%d, Character-Range: %d-%d ",
            parser->lexer->tokens[start_index].position.start.line,
            parser->lexer->tokens[end_index].position.end.line,
            parser->lexer->tokens[start_index].position.start.character,
            parser->lexer->tokens[end_index].position.end.character
        );
    }
    {
        string_append_formated(string, "\n");
        for (int i = 0; i < node->children.size; i++) {
            ast_node_append_to_string(parser, node->children[i], string, indentation_lvl + 1);
        }
    }
}

void ast_parser_append_to_string(AST_Parser* parser, String* string) {
    ast_node_append_to_string(parser, 0, string, 0);
}

int ast_parser_get_closest_node_to_text_position(AST_Parser* parser, Text_Position pos, Dynamic_Array<String> text)
{
    int closest_index = 0;
    AST_Node* closest = &parser->nodes[0];
    while (true)
    {
        bool continue_search = true;
        for (int i = 0; i < closest->children.size && continue_search; i++)
        {
            int child_index = closest->children[i];
            Token* token_start, * token_end;
            {
                int min = 0;
                int max = parser->lexer->tokens.size;
                int start_index = parser->token_mapping[child_index].start_index;
                int end_index = parser->token_mapping[child_index].end_index;
                if (start_index == -1 || end_index == -1) continue;
                start_index = math_clamp(start_index, min, max);
                end_index = math_clamp(end_index, min, max);
                token_start = &parser->lexer->tokens[start_index];
                token_end = &parser->lexer->tokens[end_index - 1];
            }

            Text_Slice node_slice = text_slice_make(token_start->position.start, token_end->position.end);
            if (text_slice_contains_position(node_slice, pos, text)) {
                closest_index = child_index;
                closest = &parser->nodes[closest_index];
                continue_search = false;
            }
        }
        if (continue_search) break;
    }
    return closest_index;

    /*
    AST_Node_Index min_node_index = 0;
    int min_slice_size = 10000;
    for (int i = 0; i < parser->token_mapping.size; i++)
    {
        int min = 0;
        int max = parser->lexer->tokens.size - 1;
        int start_index = parser->token_mapping[i].start_index;
        int end_index = parser->token_mapping[i].end_index;
        if (start_index == -1 || end_index == -1) continue;
        start_index = math_clamp(start_index, min, max);
        end_index = math_clamp(end_index, min, max);
        Token* token_start = &parser->lexer->tokens[start_index];
        Token* token_end = &parser->lexer->tokens[end_index - 1];
        Text_Slice node_slice = text_slice_make(token_start->position.start, token_end->position.end);

        if (text_slice_contains_position(node_slice, pos, text))
        {
            int slice_size;
            if (node_slice.start.line == node_slice.end.line) slice_size = node_slice.end.character - node_slice.start.character;
            else slice_size = (node_slice.end.line - node_slice.start.line) * 100;
            if (slice_size <= min_slice_size) {
                min_slice_size = slice_size;
                min_node_index = i;
            }
        }
    }
    return min_node_index;
    */
}
