#include "ast_parser.hpp"
#include "compiler.hpp"
#include "../../utility/utils.hpp"

int ast_node_check_for_undefines(AST_Node* node)
{
    if (node == 0) return 0;
    int count = 0;
    if (node->type == AST_Node_Type::UNDEFINED) {
        count++;
        panic("wellerman\n");
    }
    if (node->neighbor == node) panic("HEY");
    count += ast_node_check_for_undefines(node->neighbor);
    count += ast_node_check_for_undefines(node->child_start);
    return count;
}

int count = 0;
AST_Node* ast_parser_make_node_no_parent(AST_Parser* parser)
{
    count++;
    AST_Node* node = stack_allocator_allocate<AST_Node>(&parser->allocator);
    node->type = AST_Node_Type::UNDEFINED;
    node->child_count = 0;
    node->child_start = 0;
    node->child_end = 0;
    node->neighbor = 0;
    node->parent = 0;
    node->id = 0;
    node->token_range = token_range_make(-1, -1);
    node->literal_token = 0;
    node->alloc_index = count;
    return node;
}

void ast_node_add_child(AST_Node* parent, AST_Node* child)
{
    if (parent->child_count == 0) {
        parent->child_start = child;
        parent->child_end = child;
        child->neighbor = 0;
    }
    else {
        if (parent->child_end == child) panic("HEY");
        parent->child_end->neighbor = child;
        parent->child_end = child;
        if (parent->child_end == parent->child_end->neighbor) panic("WHAT");
    }
    child->parent = parent;
    parent->child_count++;
}

AST_Node* ast_parser_make_node_child(AST_Parser* parser, AST_Node* parent)
{
    AST_Node* node = ast_parser_make_node_no_parent(parser);
    ast_node_add_child(parent, node);
    return node;
}

AST_Parser_Checkpoint ast_parser_checkpoint_make(AST_Parser* parser, AST_Node* node)
{
    AST_Parser_Checkpoint result;
    result.parser = parser;
    result.node = node;
    if (node == 0) {
        result.last_child = 0;
        result.node_child_count = 0;
    }
    else 
    {
        result.node_child_count = node->child_count;
        result.last_child = node->child_end;
        if (node->child_count == 0) {
            assert(node->child_start == 0 && node->child_end == 0, "");
        }
        else {
            assert(node->child_start->type != AST_Node_Type::UNDEFINED && node->child_end->type != AST_Node_Type::UNDEFINED, "");
        }
    }
    result.rewind_token_index = parser->index;
    result.stack_checkpoint = stack_checkpoint_make(&parser->allocator);
    return result;
}

void check_node_parent(AST_Node* node)
{
    if (node == 0) return;
    int count = 0;
    AST_Node* child = node->child_start;
    while (child != 0) {
        assert(child->neighbor != child, "");
        count++;
        child = child->neighbor;
    }
    assert(count == node->child_count, "");
    check_node_parent(node->parent);
}

void ast_parser_checkpoint_reset(AST_Parser_Checkpoint checkpoint)
{
    checkpoint.parser->index = checkpoint.rewind_token_index;
    if (checkpoint.node != 0)
    {
        if (checkpoint.node_child_count == 0) {
            checkpoint.node->child_count = 0;

            checkpoint.node->child_start = 0;
            checkpoint.node->child_end = 0;
        }
        else if (checkpoint.node_child_count == 1) {
            checkpoint.node->child_count = checkpoint.node_child_count;
            checkpoint.node->child_start = checkpoint.last_child;

            checkpoint.node->child_end = checkpoint.last_child;
            checkpoint.node->child_end->neighbor = 0;
        }
        else {
            checkpoint.node->child_count = checkpoint.node_child_count;

            checkpoint.node->child_end = checkpoint.last_child;
            checkpoint.node->child_end->neighbor = 0;
        }
    }

    stack_checkpoint_rewind(checkpoint.stack_checkpoint);
    check_node_parent(checkpoint.node);
}

bool ast_parser_test_next_identifier(AST_Parser* parser, String* id)
{
    if (parser->index >= parser->code_source->tokens.size) {
        return false;
    }
    if (parser->code_source->tokens[parser->index].type == Token_Type::IDENTIFIER_NAME) {
        if (parser->code_source->tokens[parser->index].attribute.id == id) return true;
    }
    return false;
}

bool ast_parser_test_next_token(AST_Parser* parser, Token_Type type)
{
    if (parser->index >= parser->code_source->tokens.size) {
        return false;
    }
    if (parser->code_source->tokens[parser->index].type == type) {
        return true;
    }
    return false;
}

bool ast_parser_test_next_2_tokens(AST_Parser* parser, Token_Type type1, Token_Type type2)
{
    if (parser->index + 1 >= parser->code_source->tokens.size) {
        return false;
    }
    if (parser->code_source->tokens[parser->index].type == type1 && parser->code_source->tokens[parser->index + 1].type == type2) {
        return true;
    }
    return false;
}

bool ast_parser_test_next_3_tokens(AST_Parser* parser, Token_Type type1, Token_Type type2, Token_Type type3)
{
    if (parser->index + 2 >= parser->code_source->tokens.size) {
        return false;
    }
    if (parser->code_source->tokens[parser->index].type == type1 &&
        parser->code_source->tokens[parser->index + 1].type == type2 &&
        parser->code_source->tokens[parser->index + 2].type == type3) {
        return true;
    }
    return false;
}

bool ast_parser_test_next_4_tokens(AST_Parser* parser, Token_Type type1, Token_Type type2, Token_Type type3,
    Token_Type type4)
{
    if (parser->index + 3 >= parser->code_source->tokens.size) {
        return false;
    }
    if (parser->code_source->tokens[parser->index].type == type1 &&
        parser->code_source->tokens[parser->index + 1].type == type2 &&
        parser->code_source->tokens[parser->index + 2].type == type3 &&
        parser->code_source->tokens[parser->index + 3].type == type4)
    {
        return true;
    }
    return false;
}

bool ast_parser_test_next_5_tokens(AST_Parser* parser, Token_Type type1, Token_Type type2, Token_Type type3,
    Token_Type type4, Token_Type type5)
{
    if (parser->index + 4 >= parser->code_source->tokens.size) {
        return false;
    }
    if (parser->code_source->tokens[parser->index].type == type1 &&
        parser->code_source->tokens[parser->index + 1].type == type2 &&
        parser->code_source->tokens[parser->index + 2].type == type3 &&
        parser->code_source->tokens[parser->index + 3].type == type4 &&
        parser->code_source->tokens[parser->index + 4].type == type5) {
        return true;
    }
    return false;
}

int ast_parser_find_next_token_type(AST_Parser* parser, Token_Type type)
{
    int index = parser->index;
    while (index < parser->code_source->tokens.size)
    {
        if (parser->code_source->tokens[index].type == type) {
            return index;
        }
        index++;
    }
    return index;
}

int ast_parser_find_next_line_start_token(AST_Parser* parser)
{
    if (parser->index >= parser->code_source->tokens.size) return parser->index;
    int i = parser->index;
    int line = parser->code_source->tokens[parser->index].position.start.line;
    while (i < parser->code_source->tokens.size) {
        int token_line = parser->code_source->tokens[i].position.start.line;
        if (token_line != line) return i;
        i++;
    }
    return i;
}

int ast_parser_find_parenthesis_ending(AST_Parser* parser, int start_index, Token_Type open_type, Token_Type closed_type, bool* depth_negativ)
{
    int i = parser->index;
    int depth = 0;
    while (i < parser->code_source->tokens.size)
    {
        if (parser->code_source->tokens[i].type == open_type) depth++;
        if (parser->code_source->tokens[i].type == closed_type) {
            depth--;
            if (depth <= 0) {
                if (depth == 0) {
                    *depth_negativ = false;
                }
                else {
                    *depth_negativ = true;
                }
                return i;
            }
        }
        i++;
    }
    *depth_negativ = true;
    return i;
}

void ast_parser_log_error(AST_Parser* parser, const char* msg, Token_Range range)
{
    if (parser->errors.size > 0) {
        Compiler_Error last_error = parser->errors[parser->errors.size - 1];
        if (last_error.range.start_index <= range.start_index || last_error.range.end_index >= range.start_index) {
            return; // Skip nested errors
        }
    }
    Compiler_Error error;
    error.message = msg;
    error.range = range;
    dynamic_array_push_back(&parser->errors, error);
}



/*
    PARSING FUNCTIONS
*/
bool ast_parser_parse_definitions(AST_Parser* parser, AST_Node* parent);
bool ast_parser_parse_definition(AST_Parser* parser, AST_Node* parent);
bool ast_parser_parse_expression(AST_Parser* parser, AST_Node* parent);
AST_Node* ast_parser_parse_expression_no_parents(AST_Parser* parser);
bool ast_parser_parse_statement_block(AST_Parser* parser, AST_Node* parent, bool label_allowed);
bool ast_parser_parse_statement(AST_Parser* parser, AST_Node* parent);
bool ast_parser_parse_parameter_block(AST_Parser* parser, AST_Node* parent);

template<Token_Type T>
bool ast_parser_test_single_token(AST_Parser* parser) {
    return ast_parser_test_next_token(parser, T);
}

template<Token_Type T>
bool ast_parser_skip_single_token(AST_Parser* parser)
{
    if (ast_parser_test_next_token(parser, T)) {
        parser->index++;
        return true;
    }
    return false;
}

Optional<int> error_handling_block(AST_Parser* parser)
{
    // Error handling, check if we can continue at next line or after next semicolon
    int next_semi = ast_parser_find_next_token_type(parser, Token_Type::SEMICOLON);
    int next_line = ast_parser_find_next_line_start_token(parser);
    bool depth_negative;
    int next_index = ast_parser_find_parenthesis_ending(parser, parser->index, Token_Type::OPEN_BRACES, Token_Type::CLOSED_BRACES, &depth_negative);
    if (next_semi < next_index && next_line < next_index) {
        next_index = next_semi < next_line ? next_semi + 1 : next_line;
    }
    else {
        if (!depth_negative) {
            next_index = math_minimum(next_index + 1, parser->code_source->tokens.size);
        }
    }
    ast_parser_log_error(parser, "Could not parse block!", token_range_make(parser->index, next_index));
    return optional_make_success(next_index);
}

Optional<int> error_handling_switch_case_block(AST_Parser* parser)
{
    bool unused;
    int next_brace = ast_parser_find_parenthesis_ending(parser, parser->index, Token_Type::OPEN_BRACES, Token_Type::CLOSED_BRACES, &unused);
    int next_semi = ast_parser_find_next_token_type(parser, Token_Type::SEMICOLON);
    int next_line = ast_parser_find_next_line_start_token(parser);
    int next_switch_case = math_minimum(
        ast_parser_find_next_token_type(parser, Token_Type::CASE), ast_parser_find_next_token_type(parser, Token_Type::DEFAULT)
    );

    int next_index;
    if (next_brace < next_switch_case && next_brace < next_semi && next_brace < next_line) {
        next_index = next_brace;
    }
    else {
        if (next_switch_case < next_semi && next_switch_case < next_line) {
            next_index = next_brace;
        }
        else {
            if (next_semi < next_line) {
                next_index = next_semi + 1;
            }
            else {
                next_index = next_line;
            }
        }
    }

    ast_parser_log_error(parser, "Could not parse statements", token_range_make(parser->index, next_index));
    return optional_make_success(next_index);
}

Optional<int> error_handling_switch_node(AST_Parser* parser)
{
    bool unused;
    int next_brace = ast_parser_find_parenthesis_ending(parser, parser->index, Token_Type::OPEN_BRACES, Token_Type::CLOSED_BRACES, &unused);
    int next_case = ast_parser_find_next_token_type(parser, Token_Type::CASE);
    if (next_case == parser->index) next_case = parser->code_source->tokens.size;
    int next_default = ast_parser_find_next_token_type(parser, Token_Type::DEFAULT);
    if (next_default == parser->index) next_case = parser->code_source->tokens.size;
    int recover_index = next_brace;
    if (next_case < next_default && next_case < next_brace) {
        recover_index = next_case;
    }
    if (next_default < next_case && next_default < next_brace) {
        recover_index = next_default;
    }
    ast_parser_log_error(parser, "Could not parse switch case", token_range_make(parser->index, recover_index));
    return optional_make_success(recover_index);
}

typedef bool (*list_item_function)(AST_Parser* parser, AST_Node* parent);
typedef bool(*list_finished_function)(AST_Parser* parser);
typedef Optional<int>(*list_error_function)(AST_Parser* parser); // If continuation is possible, returns the continuation index
typedef bool(*list_seperator_function)(AST_Parser* parser);

bool ast_parser_parse_list_items(
    AST_Parser* parser, AST_Node* parent,
    list_item_function item_fn,
    list_seperator_function seperator_fn,
    list_finished_function finished_fn,
    list_error_function error_fn
)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    while (!finished_fn(parser))
    {
        if (parser->index >= parser->code_source->tokens.size) {
            ast_parser_log_error(parser, "Unexpected end of tokens", token_range_make(checkpoint.rewind_token_index, parser->index));
            return true;
        }
        AST_Parser_Checkpoint recoverable_checkpoint = ast_parser_checkpoint_make(parser, parent);
        if (item_fn(parser, parent)) {
            if (finished_fn(parser)) break;
            if (seperator_fn(parser)) continue;
        }

        ast_parser_checkpoint_reset(recoverable_checkpoint);
        Optional<int> recoverable_index = error_fn(parser);
        if (!recoverable_index.available) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (recoverable_index.value == parser->index) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        parser->index = recoverable_index.value;
    }

    return true;
}

bool ast_parser_parse_identifier_or_path(AST_Parser* parser, AST_Node* parent)
{
    if (!ast_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME)) {
        return false;
    }

    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);

    node->id = parser->code_source->tokens[parser->index].attribute.id;
    node->type = AST_Node_Type::IDENTIFIER_NAME;
    parser->index++;

    if (ast_parser_test_next_token(parser, Token_Type::TILDE))
    {
        parser->index++; 
        node->type = AST_Node_Type::IDENTIFIER_PATH;
        if (ast_parser_parse_identifier_or_path(parser, node)) {
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    return true;
}

bool ast_parser_parse_arguments(AST_Parser* parser, AST_Node* parent, Token_Type start_token, list_finished_function finished_fn)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);
    node->type = AST_Node_Type::ARGUMENTS;

    if (!ast_parser_test_next_token(parser, start_token)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++;

    list_item_function argument_parse_fn = [](AST_Parser* parser, AST_Node* parent) -> bool
    {
        AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
        AST_Node* argument_node = ast_parser_make_node_child(parser, parent);
        argument_node->type = AST_Node_Type::ARGUMENT_UNNAMED;
        if (ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::OP_ASSIGNMENT)) {
            argument_node->type = AST_Node_Type::ARGUMENT_NAMED;
            argument_node->id = parser->code_source->tokens[parser->index].attribute.id;
            parser->index += 2;
        }
        if (!ast_parser_parse_expression(parser, argument_node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        argument_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    };

    if (!ast_parser_parse_list_items(
        parser, node,
        argument_parse_fn,
        ast_parser_skip_single_token<Token_Type::COMMA>,
        finished_fn,
        [](AST_Parser* parser)->Optional<int> {return optional_make_failure<int>(); }))
    {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    return true;
}

AST_Node* ast_parser_parse_expression_single_value(AST_Parser* parser)
{
    /*
        Prefix Operations:
            Negate  - a          X
            Not     ! a          X
            Cast    cast<u64> a  X
            Deref   & a          X
            Pointer * a          X
            Array   [expr]a      X
            Slice   []a          X
            Fn-Type () [-> expr] X
        Operands:
            Expr    (a+...)      X
            Lit     true         X
            Read    a            X
        Postfix Operations
            Mem     a.
            Array   a[]
            Call    a()
    */
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, 0);

    // Parse operand
    AST_Node* node = 0;
    if (ast_parser_test_next_2_tokens(parser, Token_Type::OPEN_PARENTHESIS, Token_Type::CLOSED_PARENTHESIS) ||
        ast_parser_test_next_3_tokens(parser, Token_Type::OPEN_PARENTHESIS, Token_Type::IDENTIFIER_NAME, Token_Type::COLON) ||
        ast_parser_test_next_3_tokens(parser, Token_Type::OPEN_PARENTHESIS, Token_Type::DOLLAR, Token_Type::IDENTIFIER_NAME)  )
    {
        AST_Node* signature_node = ast_parser_make_node_no_parent(parser);
        signature_node->type = AST_Node_Type::FUNCTION_SIGNATURE;
        {
            if (!ast_parser_parse_parameter_block(parser, signature_node)) {
                ast_parser_checkpoint_reset(checkpoint);
                return 0;
            }
            if (ast_parser_test_next_token(parser, Token_Type::ARROW))
            {
                parser->index++;
                if (!ast_parser_parse_expression(parser, signature_node)) {
                    ast_parser_checkpoint_reset(checkpoint);
                    return 0;
                }
            }
            signature_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        }

        if (!ast_parser_test_next_token(parser, Token_Type::OPEN_BRACES)) {
            node = signature_node;
        }
        else
        {
            AST_Node* function_node = ast_parser_make_node_no_parent(parser);
            function_node->type = AST_Node_Type::FUNCTION;
            ast_node_add_child(function_node, signature_node);
            if (!ast_parser_parse_statement_block(parser, function_node, false)) {
                ast_parser_checkpoint_reset(checkpoint);
                return 0;
            };
            function_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            node = function_node;
        }
    }
    else if (ast_parser_test_next_token(parser, Token_Type::ENUM))
    {
        AST_Node* enum_node = ast_parser_make_node_no_parent(parser);
        enum_node->type = AST_Node_Type::ENUM;

        // Parse Enum name
        if (!ast_parser_test_next_2_tokens(parser, Token_Type::ENUM, Token_Type::OPEN_BRACES)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        parser->index += 2;

        list_item_function enum_member_function = [](AST_Parser* parser, AST_Node* parent) -> bool
        {
            AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
            AST_Node* member_node = ast_parser_make_node_child(parser, parent);
            member_node->type = AST_Node_Type::ENUM_MEMBER;

            if (!ast_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME)) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
            member_node->id = parser->code_source->tokens[parser->index].attribute.id;
            parser->index++;

            if (ast_parser_test_next_token(parser, Token_Type::DOUBLE_COLON))
            {
                parser->index++;
                if (!ast_parser_parse_expression(parser, member_node)) {
                    ast_parser_checkpoint_reset(checkpoint);
                    return false;
                }
            }
            if (!ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
            parser->index++;

            member_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        };

        if (!ast_parser_parse_list_items(
            parser, enum_node,
            enum_member_function,
            [](AST_Parser* parser) -> bool {return true; },
            ast_parser_skip_single_token<Token_Type::CLOSED_BRACES>,
            error_handling_block
        ))
        {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }

        enum_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        node = enum_node;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::STRUCT) ||
        ast_parser_test_next_token(parser, Token_Type::C_UNION) ||
        ast_parser_test_next_token(parser, Token_Type::UNION))
    {
        AST_Node* struct_node = ast_parser_make_node_no_parent(parser);
        // Parse Struct name
        if (ast_parser_test_next_2_tokens(parser, Token_Type::STRUCT, Token_Type::OPEN_BRACES)) {
            struct_node->type = AST_Node_Type::STRUCT;
        }
        else if (ast_parser_test_next_2_tokens(parser, Token_Type::UNION, Token_Type::OPEN_BRACES)) {
            struct_node->type = AST_Node_Type::UNION;
        }
        else if (ast_parser_test_next_2_tokens(parser, Token_Type::C_UNION, Token_Type::OPEN_BRACES)) {
            struct_node->type = AST_Node_Type::C_UNION;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        parser->index += 2;

        list_item_function variable_definition_function = [](AST_Parser* parser, AST_Node* parent) -> bool
        {
            if (ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::COLON))
            {
                AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
                AST_Node* node = ast_parser_make_node_child(parser, parent);
                parser->index += 2;
                if (!ast_parser_parse_expression(parser, node)) {
                    ast_parser_checkpoint_reset(checkpoint);
                    return false;
                }
                if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
                    node->type = AST_Node_Type::VARIABLE_DEFINITION;
                    node->id = parser->code_source->tokens[checkpoint.rewind_token_index].attribute.id;
                    parser->index += 1;
                    node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
                    return true;
                }
            }
            return false;
        };

        if (!ast_parser_parse_list_items(
            parser, struct_node,
            variable_definition_function,
            [](AST_Parser* parser) -> bool {return true; },
            ast_parser_skip_single_token<Token_Type::CLOSED_BRACES>,
            error_handling_block
        )) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }

        struct_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        node = struct_node;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::MODULE))
    {
        AST_Node* module_node = ast_parser_make_node_no_parent(parser);
        AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, module_node);
        module_node->type = AST_Node_Type::MODULE;
        parser->index += 1;
        module_node->id = parser->code_source->tokens[parser->index - 1].attribute.id;

        if (!ast_parser_test_next_token(parser, Token_Type::OPEN_BRACES)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        parser->index++;

        if (!ast_parser_parse_definitions(parser, module_node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }

        if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        parser->index++;

        module_node->token_range.start_index = checkpoint.rewind_token_index;
        module_node->token_range.end_index = math_clamp(parser->index, 0, parser->code_source->tokens.size);
        node = module_node;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) // Parenthesized expression
    {
        parser->index++;
        node = ast_parser_parse_expression_no_parents(parser);
        if (node == 0 || !ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        parser->index++;
    }
    else if (ast_parser_test_next_identifier(parser, parser->id_type_info) ||
        ast_parser_test_next_identifier(parser, parser->id_type_of))
    {
        node = ast_parser_make_node_no_parent(parser);
        if (ast_parser_test_next_identifier(parser, parser->id_type_info)) {
            node->type = AST_Node_Type::EXPRESSION_TYPE_INFO;
        }
        else {
            node->type = AST_Node_Type::EXPRESSION_TYPE_OF;
        }
        parser->index++;
        if (!ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        parser->index++;
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        parser->index++;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    }
    else if (ast_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME)) // Variable read
    {
        node = ast_parser_make_node_no_parent(parser);
        if (!ast_parser_parse_identifier_or_path(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        node->type = AST_Node_Type::EXPRESSION_IDENTIFIER;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    }
    else if (ast_parser_test_next_token(parser, Token_Type::INTEGER_LITERAL) || // Literal read
        ast_parser_test_next_token(parser, Token_Type::FLOAT_LITERAL) ||
        ast_parser_test_next_token(parser, Token_Type::BOOLEAN_LITERAL) ||
        ast_parser_test_next_token(parser, Token_Type::STRING_LITERAL) ||
        ast_parser_test_next_token(parser, Token_Type::NULLPTR))
    {
        node = ast_parser_make_node_no_parent(parser);
        node->type = AST_Node_Type::EXPRESSION_LITERAL;
        node->literal_token = &parser->code_source->tokens[parser->index];
        parser->index++;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    }
    else if (ast_parser_test_next_token(parser, Token_Type::NEW))
    {
        node = ast_parser_make_node_no_parent(parser);
        node->type = AST_Node_Type::EXPRESSION_NEW;
        parser->index++;
        if (ast_parser_test_next_token(parser, Token_Type::OPEN_BRACKETS) &&
            !ast_parser_test_next_2_tokens(parser, Token_Type::OPEN_BRACKETS, Token_Type::CLOSED_BRACKETS))
        {
            node->type = AST_Node_Type::EXPRESSION_NEW_ARRAY;
            parser->index++;
            if (!ast_parser_parse_expression(parser, node)) {
                ast_parser_log_error(parser, "Invalid array-size expression in new", token_range_make(checkpoint.rewind_token_index, parser->index));
                ast_parser_checkpoint_reset(checkpoint);
                return 0;
            }
            if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACKETS)) {
                ast_parser_log_error(parser, "Missing closing brackets in array new", token_range_make(checkpoint.rewind_token_index, parser->index));
                ast_parser_checkpoint_reset(checkpoint);
                return 0;
            }
            parser->index++;
        }
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    }
    else if (ast_parser_test_next_token(parser, Token_Type::DOT))
    {
        parser->index++;
        node = ast_parser_make_node_no_parent(parser);
        if (ast_parser_test_next_token(parser, Token_Type::OPEN_BRACES))
        {
            node->type = AST_Node_Type::EXPRESSION_AUTO_STRUCT_INITIALIZER;
            if (!ast_parser_parse_arguments(parser, node, Token_Type::OPEN_BRACES, ast_parser_skip_single_token<Token_Type::CLOSED_BRACES>)) {
                ast_parser_checkpoint_reset(checkpoint);
                return 0;
            }
        }
        else if (ast_parser_test_next_token(parser, Token_Type::OPEN_BRACKETS))
        {
            node->type = AST_Node_Type::EXPRESSION_AUTO_ARRAY_INITIALIZER;
            parser->index++;
            if (!ast_parser_parse_list_items(
                parser, node,
                ast_parser_parse_expression,
                ast_parser_skip_single_token<Token_Type::COMMA>,
                ast_parser_skip_single_token<Token_Type::CLOSED_BRACKETS>,
                [](AST_Parser* parser) -> Optional<int> { return optional_make_failure<int>(); }
            )) {
                ast_parser_checkpoint_reset(checkpoint);
                return 0;
            }
        }
        else if (ast_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME)) {
            node->type = AST_Node_Type::EXPRESSION_AUTO_ENUM;
            node->id = parser->code_source->tokens[parser->index].attribute.id;
            parser->index += 1;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    }
    else if (ast_parser_test_next_token(parser, Token_Type::HASHTAG))
    {
        parser->index++;
        if (!ast_parser_test_next_identifier(parser, parser->id_bake)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        parser->index++;

        node = ast_parser_make_node_no_parent(parser);
        node->type = AST_Node_Type::EXPRESSION_BAKE;
        if (!ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        parser->index++;

        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }

        if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        parser->index++;

        if (!ast_parser_parse_statement_block(parser, node, false)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    }

    // Parse post operators
    if (node != 0)
    {
        while (true)
        {
            if (ast_parser_test_next_token(parser, Token_Type::DOT))
            {
                AST_Node* new_node = ast_parser_make_node_no_parent(parser);
                new_node->token_range.start_index = parser->index;
                ast_node_add_child(new_node, node);
                parser->index++;
                if (ast_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME)) // Member access
                {
                    new_node->type = AST_Node_Type::EXPRESSION_MEMBER_ACCESS;
                    new_node->id = parser->code_source->tokens[parser->index].attribute.id;
                    parser->index++;
                }
                else if (ast_parser_test_next_token(parser, Token_Type::OPEN_BRACES)) // Struct Initializer
                {
                    new_node->type = AST_Node_Type::EXPRESSION_STRUCT_INITIALIZER;
                    if (!ast_parser_parse_arguments(parser, new_node, Token_Type::OPEN_BRACES, ast_parser_skip_single_token<Token_Type::CLOSED_BRACES>)) {
                        ast_parser_checkpoint_reset(checkpoint);
                        return 0;
                    }
                }
                else if (ast_parser_test_next_token(parser, Token_Type::OPEN_BRACKETS)) // Array Initializer
                {
                    new_node->type = AST_Node_Type::EXPRESSION_ARRAY_INITIALIZER;
                    parser->index++;
                    if (!ast_parser_parse_list_items(
                        parser, new_node,
                        ast_parser_parse_expression,
                        ast_parser_skip_single_token<Token_Type::COMMA>,
                        ast_parser_skip_single_token<Token_Type::CLOSED_BRACKETS>,
                        [](AST_Parser* parser) -> Optional<int> { return optional_make_failure<int>(); }
                    )) {
                        ast_parser_checkpoint_reset(checkpoint);
                        return 0;
                    }
                }
                else {
                    ast_parser_checkpoint_reset(checkpoint);
                    return 0;
                }
                new_node->token_range.end_index = parser->index;
                node = new_node;
            }
            else if (ast_parser_test_next_token(parser, Token_Type::OPEN_BRACKETS)) // Array access
            {
                AST_Node* new_node = ast_parser_make_node_no_parent(parser);
                new_node->type = AST_Node_Type::EXPRESSION_ARRAY_ACCESS;
                ast_node_add_child(new_node, node);
                parser->index++;
                if (!ast_parser_parse_expression(parser, new_node)) {
                    ast_parser_checkpoint_reset(checkpoint);
                    return 0;
                }
                if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACKETS)) {
                    ast_parser_checkpoint_reset(checkpoint);
                    return 0;
                }
                parser->index++;
                new_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
                node = new_node;
            }
            else if (ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) // Function call
            {
                AST_Node* new_node = ast_parser_make_node_no_parent(parser);
                new_node->type = AST_Node_Type::EXPRESSION_FUNCTION_CALL;
                ast_node_add_child(new_node, node);
                if (!ast_parser_parse_arguments(parser, new_node, Token_Type::OPEN_PARENTHESIS, ast_parser_skip_single_token<Token_Type::CLOSED_PARENTHESIS>)) {
                    ast_parser_checkpoint_reset(checkpoint);
                    return 0;
                }
                new_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
                node = new_node;
            }
            else {
                return node;
            }
        }
    }

    // Parse Pre-Operators
    node = ast_parser_make_node_no_parent(parser);
    if (ast_parser_test_next_token(parser, Token_Type::OPEN_BRACKETS))
    {
        parser->index++;
        if (ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACKETS)) {
            node->type = AST_Node_Type::EXPRESSION_SLICE_TYPE;
            parser->index++;
        }
        else {
            node->type = AST_Node_Type::EXPRESSION_ARRAY_TYPE;
            if (!ast_parser_parse_expression(parser, node)) {
                ast_parser_checkpoint_reset(checkpoint);
                return 0;
            }
            if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACKETS)) {
                ast_parser_checkpoint_reset(checkpoint);
                return 0;
            }
            parser->index++;
        }
    }
    else if (ast_parser_test_next_token(parser, Token_Type::CAST_RAW))
    {
        parser->index++;
        node->type = AST_Node_Type::EXPRESSION_CAST_RAW;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::CAST) || ast_parser_test_next_token(parser, Token_Type::CAST_PTR))
    {
        if (ast_parser_test_next_token(parser, Token_Type::CAST)) {
            node->type = AST_Node_Type::EXPRESSION_CAST;
        }
        else {
            node->type = AST_Node_Type::EXPRESSION_CAST_PTR;
        }
        parser->index += 1;
        if (ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS))
        {
            parser->index++;
            if (!ast_parser_parse_expression(parser, node)) {
                ast_parser_checkpoint_reset(checkpoint);
                return 0;
            }
            if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
                ast_parser_checkpoint_reset(checkpoint);
                return 0;
            }
            parser->index += 1;
        }
    }
    else if (ast_parser_test_next_token(parser, Token_Type::OP_MINUS)) {
        node->type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE;
        parser->index++;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::LOGICAL_NOT)) {
        node->type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT;
        parser->index++;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::OP_STAR)) {
        node->type = AST_Node_Type::EXPRESSION_POINTER;
        parser->index++;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::LOGICAL_BITWISE_AND)) {
        node->type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
        parser->index++;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::LOGICAL_AND))
    {
        node->type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
        parser->index++;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);

        AST_Node* child_node = ast_parser_make_node_child(parser, node);
        child_node->type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
        child_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);

        AST_Node* operand_node = ast_parser_parse_expression_single_value(parser);
        if (operand_node == 0) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        ast_node_add_child(child_node, operand_node);
        return node;
    }
    else {
        ast_parser_checkpoint_reset(checkpoint);
        return 0;
    }

    node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index); // TODO: Think what token mappings should include
    AST_Node* child_node = ast_parser_parse_expression_single_value(parser);
    if (child_node == 0) {
        ast_parser_checkpoint_reset(checkpoint);
        return 0;
    }
    ast_node_add_child(node, child_node);
    return node;
}

bool ast_parser_parse_binary_operator(AST_Parser* parser, AST_Node_Type* op_type, int* op_priority)
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
    if (parser->index + 1 >= parser->code_source->tokens.size) return false;
    switch (parser->code_source->tokens[parser->index].type)
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
    case Token_Type::COMPARISON_POINTER_EQUAL: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_POINTER_EQUAL;
        *op_priority = 2;
        break;
    }
    case Token_Type::COMPARISON_POINTER_NOT_EQUAL: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_POINTER_NOT_EQUAL;
        *op_priority = 2;
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

AST_Node* ast_parser_parse_binary_expression(AST_Parser* parser, AST_Node* node, int min_priority)
{
    int start_point = parser->index;
    int rewind_point = parser->index;

    bool first_run = true;
    int max_priority = 999;
    while (true)
    {
        AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, node);

        int first_op_priority;
        int first_op_index = parser->index;
        AST_Node_Type first_op_type;
        if (!ast_parser_parse_binary_operator(parser, &first_op_type, &first_op_priority)) {
            break;
        }
        if (first_op_priority < max_priority) {
            max_priority = first_op_priority;
        }
        if (first_op_priority < min_priority) {
            parser->index = rewind_point; // Undo the binary operation, maybe just do 0
            break;
        }

        AST_Node* operator_node = ast_parser_make_node_no_parent(parser);
        operator_node->type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND; // This is just so that we dont have any undefines
        AST_Node* right_operand_node = ast_parser_parse_expression_single_value(parser);
        if (right_operand_node == 0) {
            ast_parser_checkpoint_reset(checkpoint);
            break;
        }
        rewind_point = parser->index;

        int second_op_priority;
        AST_Node_Type second_op_type;
        bool second_op_exists = ast_parser_parse_binary_operator(parser, &second_op_type, &second_op_priority);
        if (second_op_exists)
        {
            parser->index--;
            if (second_op_priority > max_priority) {
                right_operand_node = ast_parser_parse_binary_expression(parser, right_operand_node, second_op_priority);
            }
        }

        ast_node_add_child(operator_node, node);
        ast_node_add_child(operator_node, right_operand_node);
        operator_node->type = first_op_type;
        operator_node->token_range = token_range_make(
            operator_node->child_start->token_range.start_index,
            operator_node->child_end->token_range.end_index
        );

        node = operator_node;
        if (!second_op_exists) break;
    }

    return node;
}

AST_Node* ast_parser_parse_expression_no_parents(AST_Parser* parser)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, 0);
    AST_Node* single_value_node = ast_parser_parse_expression_single_value(parser);
    if (single_value_node == 0) {
        ast_parser_checkpoint_reset(checkpoint);
        return 0;
    }
    return ast_parser_parse_binary_expression(parser, single_value_node, 0);
}

bool ast_parser_parse_expression(AST_Parser* parser, AST_Node* parent)
{
    AST_Node* op_tree_root_index = ast_parser_parse_expression_no_parents(parser);
    if (op_tree_root_index == 0) { return false; }
    ast_node_add_child(parent, op_tree_root_index);
    return true;
}

bool ast_parser_parse_switch_case(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* case_node = ast_parser_make_node_child(parser, parent);
    case_node->token_range.start_index = parser->index;
    if (ast_parser_test_next_token(parser, Token_Type::CASE))
    {
        case_node->type = AST_Node_Type::SWITCH_CASE;
        parser->index++;
        if (!ast_parser_parse_expression(parser, case_node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_test_next_token(parser, Token_Type::COLON)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        parser->index++;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::DEFAULT))
    {
        // This needs to be done in two parts, otherwise the error handling will just go back to the same index
        parser->index++;
        if (ast_parser_test_next_token(parser, Token_Type::COLON)) {
            parser->index++;
            case_node->type = AST_Node_Type::SWITCH_DEFAULT_CASE;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }
    else {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    AST_Node* block_node = ast_parser_make_node_child(parser, case_node);
    block_node->type = AST_Node_Type::STATEMENT_BLOCK;
    block_node->token_range.start_index = parser->index;
    if (!ast_parser_parse_list_items(
        parser, block_node,
        ast_parser_parse_statement,
        [](AST_Parser* parser) -> bool {return true; },
        [](AST_Parser* parser) -> bool {
            return ast_parser_test_next_token(parser, Token_Type::CASE) ||
                ast_parser_test_next_token(parser, Token_Type::DEFAULT) ||
                ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES);
        },
        error_handling_switch_case_block))
    {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
        block_node->token_range.end_index = parser->index;
        block_node->id = parent->id;
        case_node->token_range.end_index = parser->index;

        return true;
}

bool ast_parser_parse_statement_or_block(AST_Parser* parser, AST_Node* parent, bool name_allowed)
{
    if (ast_parser_test_next_token(parser, Token_Type::OPEN_BRACES)) {
        return ast_parser_parse_statement_block(parser, parent, name_allowed);
    }
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);
    node->type = AST_Node_Type::STATEMENT_BLOCK;
    if (!ast_parser_parse_statement(parser, node)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    return node;
}

bool ast_parser_parse_statement(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    if (ast_parser_parse_definition(parser, parent)) {
        return true;
    }
    if (ast_parser_parse_statement_block(parser, parent, true)) {
        return true;
    }

    AST_Node* node = ast_parser_make_node_child(parser, parent);
    if (ast_parser_parse_expression(parser, node))
    {
        node->type = AST_Node_Type::STATEMENT_EXPRESSION;
        if (ast_parser_test_next_token(parser, Token_Type::OP_ASSIGNMENT))
        {
            node->type = AST_Node_Type::STATEMENT_ASSIGNMENT;
            parser->index++;
            if (!ast_parser_parse_expression(parser, node)) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
        }

        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    if (ast_parser_test_next_token(parser, Token_Type::SWITCH))
    {
        node->type = AST_Node_Type::STATEMENT_SWITCH;
        parser->index++;
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::COLON)) {
            node->id = parser->code_source->tokens[parser->index].attribute.id;
            parser->index += 2;
        }

        if (!ast_parser_test_next_token(parser, Token_Type::OPEN_BRACES)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        parser->index += 1;

        if (!ast_parser_parse_list_items(
            parser, node,
            ast_parser_parse_switch_case,
            [](AST_Parser* parser) -> bool {return true; },
            ast_parser_skip_single_token<Token_Type::CLOSED_BRACES>,
            error_handling_switch_node))
        {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }

        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }


    if (ast_parser_test_next_token(parser, Token_Type::WHILE))
    {
        node->type = AST_Node_Type::STATEMENT_WHILE;
        parser->index++;
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_parse_statement_block(parser, node, true)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_token(parser, Token_Type::DEFER))
    {
        parser->index++;
        node->type = AST_Node_Type::STATEMENT_DEFER;
        if (!ast_parser_parse_statement_or_block(parser, node, false)) {
            ast_parser_log_error(parser, "Invalid statement after defer keyword", token_range_make(checkpoint.rewind_token_index, parser->index));
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_token(parser, Token_Type::DELETE_TOKEN))
    {
        node->type = AST_Node_Type::STATEMENT_DELETE;
        parser->index++;
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_log_error(parser, "Invalid expression after delete", token_range_make(checkpoint.rewind_token_index, parser->index));
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    if (ast_parser_test_next_token(parser, Token_Type::IF))
    {
        node->type = AST_Node_Type::STATEMENT_IF;
        parser->index++;
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_parse_statement_block(parser, node, true)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }

        if (ast_parser_test_next_token(parser, Token_Type::ELSE))
        {
            node->type = AST_Node_Type::STATEMENT_IF_ELSE;
            parser->index++;
            if (ast_parser_test_next_token(parser, Token_Type::IF)) {
                if (!ast_parser_parse_statement_or_block(parser, node, true)) {
                    ast_parser_checkpoint_reset(checkpoint);
                    return false;
                }
            }
            else {
                if (!ast_parser_parse_statement_block(parser, node, true)) {
                    ast_parser_checkpoint_reset(checkpoint);
                    return false;
                }
            }
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_3_tokens(parser, Token_Type::BREAK, Token_Type::IDENTIFIER_NAME, Token_Type::SEMICOLON))
    {
        node->type = AST_Node_Type::STATEMENT_BREAK;
        node->id = parser->code_source->tokens[parser->index + 1].attribute.id;
        parser->index += 3;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_3_tokens(parser, Token_Type::CONTINUE, Token_Type::IDENTIFIER_NAME, Token_Type::SEMICOLON))
    {
        node->type = AST_Node_Type::STATEMENT_CONTINUE;
        node->id = parser->code_source->tokens[parser->index + 1].attribute.id;
        parser->index += 3;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_token(parser, Token_Type::RETURN))
    {
        node->type = AST_Node_Type::STATEMENT_RETURN;
        parser->index++;
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
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

bool ast_parser_parse_statement_block(AST_Parser* parser, AST_Node* parent, bool label_allowed)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* block_node = ast_parser_make_node_child(parser, parent);
    block_node->type = AST_Node_Type::STATEMENT_BLOCK;

    if (label_allowed && ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::COLON)) {
        block_node->id = parser->code_source->tokens[parser->index].attribute.id;
        parser->index += 2;
    }
    if (!ast_parser_test_next_token(parser, Token_Type::OPEN_BRACES)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++;

    if (!ast_parser_parse_list_items(
        parser, block_node,
        ast_parser_parse_statement,
        [](AST_Parser* parser) -> bool {return true; },
        ast_parser_skip_single_token<Token_Type::CLOSED_BRACES>,
        error_handling_block))
    {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    block_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    return true;
}

bool ast_parser_parse_parameter_block(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);
    node->type = AST_Node_Type::PARAMETER_BLOCK;

    if (!ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++;
    if (ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
        parser->index++;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    list_item_function parameter_function = [](AST_Parser* parser, AST_Node* parent) -> bool
    {
        AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
        AST_Node* node = ast_parser_make_node_child(parser, parent);
        node->type = AST_Node_Type::PARAMETER;
        if (ast_parser_test_next_token(parser, Token_Type::DOLLAR)) {
            node->type = AST_Node_Type::PARAMETER_COMPTIME;
            parser->index++;
        }
        if (!ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::COLON)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        node->id = parser->code_source->tokens[parser->index].attribute.id;
        parser->index += 2;
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    };

    if (!ast_parser_parse_list_items(
        parser, node,
        parameter_function,
        ast_parser_skip_single_token<Token_Type::COMMA>,
        ast_parser_skip_single_token<Token_Type::CLOSED_PARENTHESIS>,
        [](AST_Parser* parser)->Optional<int> {return optional_make_failure<int>(); }))
    {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    return true;
}

bool ast_parser_parse_extern_source_declarations(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);

    if (ast_parser_test_next_2_tokens(parser, Token_Type::HASHTAG, Token_Type::IDENTIFIER_NAME))
    {
        if (parser->code_source->tokens[parser->index + 1].attribute.id = parser->id_load)
        {
            parser->index += 2;
            if (ast_parser_test_next_2_tokens(parser, Token_Type::STRING_LITERAL, Token_Type::SEMICOLON)) {
                AST_Node* node = ast_parser_make_node_child(parser, parent);
                node->type = AST_Node_Type::LOAD_FILE;
                node->id = parser->code_source->tokens[parser->index].attribute.id;
                parser->index += 2;
                node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
                return true;
            }
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    if (!ast_parser_test_next_token(parser, Token_Type::EXTERN)) {
        return false;
    }
    parser->index += 1;

    if (ast_parser_test_next_2_tokens(parser, Token_Type::STRING_LITERAL, Token_Type::OPEN_BRACES))
    {
        AST_Node* node = ast_parser_make_node_child(parser, parent);
        node->type = AST_Node_Type::EXTERN_HEADER_IMPORT;
        node->id = parser->code_source->tokens[parser->index].attribute.id;
        parser->index += 2;
        while (true)
        {
            if (ast_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME)) {
                AST_Node* child_node = ast_parser_make_node_child(parser, node);
                child_node->type = AST_Node_Type::IDENTIFIER_NAME;
                child_node->id = parser->code_source->tokens[parser->index].attribute.id;
                child_node->token_range = token_range_make(parser->index, parser->index + 1);
                parser->index++;
            }
            else if (ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
                parser->index++;
                break;
            }
            else {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
        }

        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_3_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::STRING_LITERAL, Token_Type::SEMICOLON))
    {
        String* id1 = parser->code_source->tokens[parser->index].attribute.id;
        String* id2 = parser->code_source->tokens[parser->index + 1].attribute.id;
        if (id1 != parser->id_lib) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        AST_Node* node = ast_parser_make_node_child(parser, parent);
        node->type = AST_Node_Type::EXTERN_LIB_IMPORT;
        node->id = id2;
        parser->index += 3;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (!ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::DOUBLE_COLON)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    AST_Node* node = ast_parser_make_node_child(parser, parent);
    node->type = AST_Node_Type::EXTERN_FUNCTION_DECLARATION;
    node->id = parser->code_source->tokens[parser->index].attribute.id;
    parser->index += 2;

    if (!ast_parser_parse_expression(parser, node)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    if (!ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index += 1;
    node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    return true;
}

bool ast_parser_parse_definitions(AST_Parser* parser, AST_Node* parent)
{
    int start_index = parser->index;
    AST_Node* node = ast_parser_make_node_child(parser, parent);
    node->type = AST_Node_Type::DEFINITIONS;

    while (parser->index < parser->code_source->tokens.size)
    {
        if (ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
            break;
        }

        AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, node);
        if (ast_parser_parse_definition(parser, node)) {
            continue;
        }
        if (ast_parser_parse_extern_source_declarations(parser, node)) {
            continue;
        }

        // Error Recovery
        ast_parser_checkpoint_reset(checkpoint);
        bool exit_braces_found;
        int next_closing_braces = ast_parser_find_parenthesis_ending(
            parser, checkpoint.rewind_token_index, Token_Type::OPEN_BRACES, Token_Type::CLOSED_BRACES, &exit_braces_found
        );
        int next_line = ast_parser_find_next_line_start_token(parser);
        if (next_line < next_closing_braces) {
            ast_parser_log_error(parser, "Could not parse Definitions", token_range_make(parser->index, next_line));
            parser->index = next_line;
            continue;
        }

        ast_parser_log_error(parser, "Could not parse Definitions", token_range_make(parser->index, next_closing_braces + 1));
        parser->index = next_closing_braces + 1;
        if (exit_braces_found) {
            break;
        }
    }

    node->token_range.start_index = start_index;
    node->token_range.end_index = math_clamp(parser->index, 0, parser->code_source->tokens.size);
    return true;
}

bool ast_parser_parse_definition(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    if (!ast_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME)) {
        return false;
    }

    AST_Node* node = ast_parser_make_node_child(parser, parent);
    node->token_range.start_index = parser->index;
    node->id = parser->code_source->tokens[parser->index].attribute.id;
    parser->index++;
    if (ast_parser_test_next_token(parser, Token_Type::COLON)) // id: ...
    {
        parser->index++;
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }

        if (ast_parser_test_next_token(parser, Token_Type::COLON)) { // id: type_expr: ... 
            node->type = AST_Node_Type::COMPTIME_DEFINE_ASSIGN;
            parser->index++;
            if (!ast_parser_parse_expression(parser, node)) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
        }
        else if (ast_parser_test_next_token(parser, Token_Type::OP_ASSIGNMENT)) { // id: type_expr = ...
            node->type = AST_Node_Type::VARIABLE_DEFINE_ASSIGN;
            parser->index++;
            if (!ast_parser_parse_expression(parser, node)) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
        }
        else {
            node->type = AST_Node_Type::VARIABLE_DEFINITION;
        }
    }
    else if (ast_parser_test_next_token(parser, Token_Type::DOUBLE_COLON)) // id :: ...
    {
        parser->index++;
        node->type = AST_Node_Type::COMPTIME_DEFINE_INFER;
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }
    else if (ast_parser_test_next_token(parser, Token_Type::INFER_ASSIGN)) // id := ...
    {
        parser->index++;
        node->type = AST_Node_Type::VARIABLE_DEFINE_INFER;
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }
    else {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    bool requires_semicolon;
    switch (node->child_end->type)
    {
    case AST_Node_Type::FUNCTION:
    case AST_Node_Type::STRUCT:
    case AST_Node_Type::C_UNION:
    case AST_Node_Type::UNION:
    case AST_Node_Type::ENUM:
    case AST_Node_Type::MODULE:
        requires_semicolon = false;
        break;
    default:
        requires_semicolon = true;
        break;
    }

    if (requires_semicolon) {
        if (!ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        parser->index++;
    }
    return true;
}

void ast_parser_parse_root(AST_Parser* parser)
{
    parser->code_source->root_node = ast_parser_make_node_no_parent(parser);
    parser->code_source->root_node->type = AST_Node_Type::ROOT;
    ast_parser_parse_definitions(parser, parser->code_source->root_node);
    parser->code_source->root_node->token_range = token_range_make(0, math_maximum(0, parser->code_source->tokens.size - 1));
}

void ast_parser_check_sanity(AST_Parser* parser, AST_Node* node)
{
    while (node != 0)
    {
        SCOPE_EXIT(node = node->neighbor;);

        // Verify child count and parent relation
        {
            int count = 0;
            AST_Node* child = node->child_start;
            while (child != 0)
            {
                assert(child->parent == node, "HEY");
                count++;
                if (count == node->child_count) {
                    assert(node->child_end == child, "");
                    assert(node->child_end->neighbor == 0, "");
                }
                child = child->neighbor;
            }
            assert(count == node->child_count, "");
        }
        // Check sanity of children
        ast_parser_check_sanity(parser, node->child_start);

        // Check parent indices
        if (node->parent == 0) {
            assert(node->type == AST_Node_Type::ROOT, "HEY");
        }

        // Check if token mappings are only 0 if Definitions or Root
        if (parser->code_source->tokens.size != 0)
        {
            int start = node->token_range.start_index;
            int end = node->token_range.end_index;
            if (start < 0 || end < 0 || start >= parser->code_source->tokens.size || end > parser->code_source->tokens.size) {
                logg("Should not happen: range: %d-%d\n", start, end);
                panic("Should not happen!");
            }
            if (start == end) {
                if (node->type != AST_Node_Type::ROOT && node->type != AST_Node_Type::DEFINITIONS && node->type != AST_Node_Type::STATEMENT_BLOCK) {
                    logg("Should not happen: range: %d-%d\n", start, end);
                    logg("Node_Type::%s\n", ast_node_type_to_string(node->type).characters);
                    panic("Should not happen!");
                }
            }
        }

        // Check if child types are allowed for given node type
        AST_Node* child = node->child_start;
        switch (node->type)
        {
        case AST_Node_Type::ROOT:
        case AST_Node_Type::MODULE:
            assert(node->child_count == 1, "");
            assert(child->type == AST_Node_Type::DEFINITIONS, "");
            break;
        case AST_Node_Type::ENUM: {
            while (child != 0) {
                assert(child->type == AST_Node_Type::ENUM_MEMBER, "");
                child = child->neighbor;
            }
            break;
        }
        case AST_Node_Type::ENUM_MEMBER:
            assert(node->child_count <= 1, "");
            if (child != 0) {
                assert(ast_node_type_is_expression(child->type) || child->type == AST_Node_Type::VARIABLE_DEFINITION, "");
            }
            break;
        case AST_Node_Type::DEFINITIONS:
            while (child != 0)
            {
                AST_Node_Type child_type = child->type;
                assert(child_type == AST_Node_Type::FUNCTION ||
                    child_type == AST_Node_Type::COMPTIME_DEFINE_ASSIGN ||
                    child_type == AST_Node_Type::COMPTIME_DEFINE_INFER ||
                    child_type == AST_Node_Type::MODULE ||
                    child_type == AST_Node_Type::VARIABLE_DEFINE_ASSIGN ||
                    child_type == AST_Node_Type::VARIABLE_DEFINITION ||
                    child_type == AST_Node_Type::VARIABLE_DEFINE_INFER ||
                    child_type == AST_Node_Type::EXTERN_FUNCTION_DECLARATION ||
                    child_type == AST_Node_Type::EXTERN_LIB_IMPORT ||
                    child_type == AST_Node_Type::EXTERN_HEADER_IMPORT ||
                    child_type == AST_Node_Type::LOAD_FILE, "");
                child = child->neighbor;
            }
            break;
        case AST_Node_Type::COMPTIME_DEFINE_ASSIGN:
            assert(node->child_count == 2, "");
            assert(ast_node_type_is_expression(node->child_start->type), "");
            assert(ast_node_type_is_expression(node->child_end->type), "");
            break;
        case AST_Node_Type::COMPTIME_DEFINE_INFER:
            assert(node->child_count == 1, "");
            assert(ast_node_type_is_expression(node->child_start->type), "");
            break;
        case AST_Node_Type::UNION:
        case AST_Node_Type::C_UNION:
        case AST_Node_Type::STRUCT:
            while (child != 0) {
                assert(child->type == AST_Node_Type::VARIABLE_DEFINITION, "");
                child = child->neighbor;
            }
            break;
        case AST_Node_Type::EXTERN_FUNCTION_DECLARATION:
            assert(node->child_count == 1, "");
            assert(ast_node_type_is_expression(child->type), "");
            break;
        case AST_Node_Type::EXTERN_HEADER_IMPORT: {
            while (child != 0) {
                assert(child->type == AST_Node_Type::IDENTIFIER_NAME, "");
                child = child->neighbor;
            }
            break;
        }
        case AST_Node_Type::LOAD_FILE:
        case AST_Node_Type::EXTERN_LIB_IMPORT:
        case AST_Node_Type::IDENTIFIER_NAME:
            assert(node->child_count == 0, "");
            break;
        case AST_Node_Type::IDENTIFIER_PATH:
            assert(node->child_count == 1, "");
            if (child->type != AST_Node_Type::IDENTIFIER_NAME &&
                child->type != AST_Node_Type::IDENTIFIER_PATH) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::FUNCTION:
            assert(node->child_count == 2, "");
            if (child->type != AST_Node_Type::FUNCTION_SIGNATURE ||
                child->neighbor->type != AST_Node_Type::STATEMENT_BLOCK) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::PARAMETER_BLOCK:
            while (child != 0) {
                assert(child->type == AST_Node_Type::PARAMETER ||
                    child->type == AST_Node_Type::PARAMETER_COMPTIME, "");
                child = child->neighbor;
            }
            break;
        case AST_Node_Type::FUNCTION_SIGNATURE:
            assert(node->child_count == 1 || node->child_count == 2, "");
            assert(child->type == AST_Node_Type::PARAMETER_BLOCK, "");
            if (node->child_count == 2) {
                assert(ast_node_type_is_expression(child->neighbor->type), "");
            }
            break;
        case AST_Node_Type::EXPRESSION_SLICE_TYPE:
        case AST_Node_Type::PARAMETER:
        case AST_Node_Type::PARAMETER_COMPTIME:
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_ARRAY_TYPE:
            assert(node->child_count == 2, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::SWITCH_CASE:
            assert(node->child_count == 2, "");
            assert(ast_node_type_is_expression(node->child_start->type), "");
            assert(node->child_end->type == AST_Node_Type::STATEMENT_BLOCK, "");
            break;
        case AST_Node_Type::SWITCH_DEFAULT_CASE:
            assert(node->child_count == 1, "");
            assert(node->child_end->type == AST_Node_Type::STATEMENT_BLOCK, "");
            break;
        case AST_Node_Type::STATEMENT_SWITCH:
            assert(node->child_count >= 1, "");
            assert(ast_node_type_is_expression(child->type), "");
            child = child->neighbor;
            while (child != 0) {
                assert(child->type == AST_Node_Type::SWITCH_CASE || child->type == AST_Node_Type::SWITCH_DEFAULT_CASE, "");
                child = child->neighbor;
            }
            break;
        case AST_Node_Type::STATEMENT_BLOCK:
            while (child != 0) {
                assert(ast_node_type_is_statement(child->type), "");
                child = child->neighbor;
            }
            break;
        case AST_Node_Type::STATEMENT_WHILE:
        case AST_Node_Type::STATEMENT_IF:
            assert(node->child_count == 2, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            if (child->neighbor->type != AST_Node_Type::STATEMENT_BLOCK) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_IF_ELSE:
            assert(node->child_count == 3, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            if (child->neighbor->type != AST_Node_Type::STATEMENT_BLOCK) {
                panic("Should not happen");
            }
            if (child->neighbor->neighbor->type != AST_Node_Type::STATEMENT_BLOCK) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_DEFER:
            assert(node->child_count == 1, "");
            if (child->type != AST_Node_Type::STATEMENT_BLOCK) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_BREAK:
        case AST_Node_Type::STATEMENT_CONTINUE:
            assert(node->child_count == 0, "");
            break;
        case AST_Node_Type::STATEMENT_EXPRESSION:
        case AST_Node_Type::STATEMENT_RETURN:
            if (node->child_count == 0) return;
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_ASSIGNMENT:
            assert(node->child_count == 2, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(child->neighbor->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::VARIABLE_DEFINITION:
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::VARIABLE_DEFINE_ASSIGN:
            assert(node->child_count == 2, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(child->neighbor->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::VARIABLE_DEFINE_INFER:
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_DELETE:
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::ARGUMENT_NAMED: {
            assert(node->child_count == 1, "");
            assert(ast_node_type_is_expression(child->type), "");
            break;
        }
        case AST_Node_Type::ARGUMENT_UNNAMED: {
            assert(node->child_count == 1, "");
            assert(ast_node_type_is_expression(child->type), "");
            break;
        }
        case AST_Node_Type::ARGUMENTS: {
            while (child != 0) {
                assert(child->type == AST_Node_Type::ARGUMENT_NAMED || child->type == AST_Node_Type::ARGUMENT_UNNAMED, "");
                child = child->neighbor;
            }
            break;
        }
        case AST_Node_Type::EXPRESSION_NEW:
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_NEW_ARRAY:
            assert(node->child_count == 2, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(child->neighbor->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_LITERAL:
            assert(node->child_count == 0, "");
            break;
        case AST_Node_Type::EXPRESSION_FUNCTION_CALL:
        {
            assert(node->child_count == 2, "");
            AST_Node_Type child_type_0 = child->type;
            AST_Node_Type child_type_1 = child->neighbor->type;
            if (!ast_node_type_is_expression(child_type_0)) {
                panic("Should not happen");
            }
            if (child_type_1 != AST_Node_Type::ARGUMENTS) {
                panic("Should not happen");
            }
            break;
        }
        case AST_Node_Type::EXPRESSION_IDENTIFIER:
            assert(node->child_count == 1, "");
            if (child->type != AST_Node_Type::IDENTIFIER_NAME &&
                child->type != AST_Node_Type::IDENTIFIER_PATH) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_AUTO_ARRAY_INITIALIZER:
            while (child != 0) {
                assert(ast_node_type_is_expression(child->type), "");
                child = child->neighbor;
            }
            break;
        case AST_Node_Type::EXPRESSION_AUTO_STRUCT_INITIALIZER:
            assert(child->type == AST_Node_Type::ARGUMENTS, "");
            child = child->neighbor;
            break;
        case AST_Node_Type::EXPRESSION_STRUCT_INITIALIZER: {
            assert(node->child_count == 2, "");
            assert(ast_node_type_is_expression(child->type), "");
            assert(child->neighbor->type == AST_Node_Type::ARGUMENTS, "");
            break;
        }
        case AST_Node_Type::EXPRESSION_ARRAY_INITIALIZER:
            assert(node->child_count >= 1, "");
            while (child != 0) {
                assert(ast_node_type_is_expression(child->type), "");
                child = child->neighbor;
            }
            break;
        case AST_Node_Type::EXPRESSION_ARRAY_ACCESS:
            assert(node->child_count == 2, "");
            while (child != 0) {
                assert(ast_node_type_is_expression(child->type), "");
                child = child->neighbor;
            }
            break;
        case AST_Node_Type::EXPRESSION_MEMBER_ACCESS:
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_TYPE_INFO:
            assert(node->child_count == 1, "");
            assert(ast_node_type_is_expression(node->child_start->type), "");
            break;
        case AST_Node_Type::EXPRESSION_TYPE_OF:
            assert(node->child_count == 1, "");
            assert(ast_node_type_is_expression(node->child_start->type), "");
            break;
        case AST_Node_Type::EXPRESSION_BAKE:
            assert(node->child_count == 2, "");
            assert(ast_node_type_is_expression(node->child_start->type), "");
            assert(node->child_end->type == AST_Node_Type::STATEMENT_BLOCK, "");
            break;
        case AST_Node_Type::EXPRESSION_CAST_RAW:
            assert(node->child_count == 1, "");
            assert(ast_node_type_is_expression(child->type), "");
            break;
        case AST_Node_Type::EXPRESSION_CAST:
        case AST_Node_Type::EXPRESSION_CAST_PTR:
            assert(node->child_count == 2 || node->child_count == 1, "");
            if (node->child_count == 2) {
                if (!ast_node_type_is_expression(child->type)) {
                    panic("Should not happen");
                }
                if (!ast_node_type_is_expression(child->neighbor->type)) {
                    panic("Should not happen");
                }
            }
            else {
                if (!ast_node_type_is_expression(child->type)) {
                    panic("Should not happen");
                }
            }
            break;
        case AST_Node_Type::EXPRESSION_AUTO_ENUM:
            assert(node->child_count == 0, "");
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
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_POINTER_EQUAL:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_POINTER_NOT_EQUAL:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL:
            assert(node->child_count == 2, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(child->neighbor->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE:
        case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT:
        case AST_Node_Type::EXPRESSION_POINTER:
        case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE:
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_expression(child->type)) {
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

Token_Range adjust_token_range(AST_Parser* parser, AST_Node* node)
{
    Token_Range range = node->token_range;
    AST_Node* child = node->child_start;
    while (child != 0)
    {
        Token_Range child_range = adjust_token_range(parser, child);
        if (child_range.start_index < range.start_index) {
            range.start_index = child_range.start_index;
        }
        if (child_range.end_index > range.end_index) {
            range.end_index = child_range.end_index;
        }
        child = child->neighbor;
    }
    node->token_range = range;
    return range;
}

void ast_parser_reset(AST_Parser* parser, Identifier_Pool* id_pool)
{
    count = 0; // Debugging
    stack_allocator_reset(&parser->allocator);
    parser->id_lib = identifier_pool_add(id_pool, string_create_static("lib"));
    parser->id_load = identifier_pool_add(id_pool, string_create_static("load"));
    parser->id_bake = identifier_pool_add(id_pool, string_create_static("bake"));
    parser->id_type_info = identifier_pool_add(id_pool, string_create_static("type_info"));
    parser->id_type_of = identifier_pool_add(id_pool, string_create_static("type_of"));
    dynamic_array_reset(&parser->errors);
}

void ast_parser_parse(AST_Parser* parser, Code_Source* source)
{
    // Reset parser data
    parser->code_source = source;
    parser->index = 0;

    // Parse
    ast_parser_parse_root(parser);

    // Check sanity
    ast_node_check_for_undefines(source->root_node);
    ast_parser_check_sanity(parser, source->root_node);

    // Cleanup token mapping, which I am still not sure if it is necessary
    adjust_token_range(parser, source->root_node);
}

AST_Parser ast_parser_create()
{
    AST_Parser parser;
    parser.index = 0;
    parser.allocator = stack_allocator_create_empty(sizeof(AST_Node) * 2048);
    parser.errors = dynamic_array_create_empty<Compiler_Error>(64);
    return parser;
}

void ast_parser_destroy(AST_Parser* parser) {
    stack_allocator_destroy(&parser->allocator);
    dynamic_array_destroy(&parser->errors);
}




/*
    STRING FUNCTIONS
*/
String ast_node_type_to_string(AST_Node_Type type)
{
    switch (type)
    {
    case AST_Node_Type::ROOT: return string_create_static("ROOT");
    case AST_Node_Type::STRUCT: return string_create_static("STRUCT");
    case AST_Node_Type::UNION: return string_create_static("UNION");
    case AST_Node_Type::ENUM: return string_create_static("ENUM");
    case AST_Node_Type::ENUM_MEMBER: return string_create_static("ENUM_MEMBER");
    case AST_Node_Type::DEFINITIONS: return string_create_static("DEFINITIONS");
    case AST_Node_Type::MODULE: return string_create_static("MODULE");
    case AST_Node_Type::COMPTIME_DEFINE_ASSIGN: return string_create_static("COMPTIME_DEFINE_ASSIGN");
    case AST_Node_Type::COMPTIME_DEFINE_INFER: return string_create_static("COMPTIME_DEFINE_INFER");
    case AST_Node_Type::EXTERN_FUNCTION_DECLARATION: return string_create_static("EXTERN_FUNCTION_DECLARATION");
    case AST_Node_Type::EXTERN_LIB_IMPORT: return string_create_static("EXTERN_LIB_IMPORT");
    case AST_Node_Type::EXTERN_HEADER_IMPORT: return string_create_static("EXTERN_HEADER_IMPORT");
    case AST_Node_Type::LOAD_FILE: return string_create_static("LOAD_FILE");
    case AST_Node_Type::FUNCTION: return string_create_static("FUNCTION");
    case AST_Node_Type::IDENTIFIER_NAME: return string_create_static("IDENTIFIER_NAME");
    case AST_Node_Type::IDENTIFIER_PATH: return string_create_static("IDENTIFIER_PATH");
    case AST_Node_Type::ARGUMENTS: return string_create_static("ARGUMENTS");
    case AST_Node_Type::ARGUMENT_NAMED: return string_create_static("ARGUMENT_NAMED");
    case AST_Node_Type::ARGUMENT_UNNAMED: return string_create_static("ARGUMENT_UNNAMED");
    case AST_Node_Type::SWITCH_CASE: return string_create_static("SWITCH_CASE");
    case AST_Node_Type::SWITCH_DEFAULT_CASE: return string_create_static("SWITCH_DEFAULT_CASE");
    case AST_Node_Type::FUNCTION_SIGNATURE: return string_create_static("FUNCTION_SIGNATURE");
    case AST_Node_Type::EXPRESSION_ARRAY_TYPE: return string_create_static("EXPRESSION_ARRAY_TYPE");
    case AST_Node_Type::EXPRESSION_SLICE_TYPE: return string_create_static("EXPRESSION_SLICE_TYPE");
    case AST_Node_Type::PARAMETER_BLOCK: return string_create_static("PARAMETER_BLOCK");
    case AST_Node_Type::PARAMETER: return string_create_static("PARAMETER");
    case AST_Node_Type::PARAMETER_COMPTIME: return string_create_static("PARAMETER_COMPTIME");
    case AST_Node_Type::STATEMENT_DEFER: return string_create_static("STATEMENT_DEFER");
    case AST_Node_Type::STATEMENT_BLOCK: return string_create_static("STATEMENT_BLOCK");
    case AST_Node_Type::STATEMENT_IF: return string_create_static("STATEMENT_IF");
    case AST_Node_Type::STATEMENT_IF_ELSE: return string_create_static("STATEMENT_IF_ELSE");
    case AST_Node_Type::STATEMENT_SWITCH: return string_create_static("STATEMENT_SWITCH");
    case AST_Node_Type::STATEMENT_WHILE: return string_create_static("STATEMENT_WHILE");
    case AST_Node_Type::STATEMENT_BREAK: return string_create_static("STATEMENT_BREAK");
    case AST_Node_Type::STATEMENT_CONTINUE: return string_create_static("STATEMENT_CONTINUE");
    case AST_Node_Type::STATEMENT_RETURN: return string_create_static("STATEMENT_RETURN");
    case AST_Node_Type::STATEMENT_EXPRESSION: return string_create_static("STATEMENT_EXPRESSION");
    case AST_Node_Type::STATEMENT_ASSIGNMENT: return string_create_static("STATEMENT_ASSIGNMENT");
    case AST_Node_Type::VARIABLE_DEFINITION: return string_create_static("VARIABLE_DEFINITION");
    case AST_Node_Type::VARIABLE_DEFINE_ASSIGN: return string_create_static("VARIABLE_DEFINE_ASSIGN");
    case AST_Node_Type::VARIABLE_DEFINE_INFER: return string_create_static("VARIABLE_DEFINE_INFER");
    case AST_Node_Type::STATEMENT_DELETE: return string_create_static("STATEMENT_DELETE");
    case AST_Node_Type::EXPRESSION_STRUCT_INITIALIZER: return string_create_static("EXPRESSION_STRUCT_INITIALIZER");
    case AST_Node_Type::EXPRESSION_ARRAY_INITIALIZER: return string_create_static("EXPRESSION_ARRAY_INITIALIZER");
    case AST_Node_Type::EXPRESSION_AUTO_ARRAY_INITIALIZER: return string_create_static("EXPRESSION_AUTO_ARRAY_INITIALIZER");
    case AST_Node_Type::EXPRESSION_AUTO_STRUCT_INITIALIZER: return string_create_static("EXPRESSION_AUTO_STRUCT_INITIALIZER");
    case AST_Node_Type::EXPRESSION_AUTO_ENUM: return string_create_static("EXPRESSION_AUTO_ENUM");
    case AST_Node_Type::EXPRESSION_ARRAY_ACCESS: return string_create_static("EXPRESSION_ARRAY_ACCESS");
    case AST_Node_Type::EXPRESSION_MEMBER_ACCESS: return string_create_static("EXPRESSION_MEMBER_ACCESS");
    case AST_Node_Type::EXPRESSION_CAST: return string_create_static("EXPRESSION_CAST");
    case AST_Node_Type::EXPRESSION_CAST_RAW: return string_create_static("EXPRESSION_CAST_RAW");
    case AST_Node_Type::EXPRESSION_CAST_PTR: return string_create_static("EXPRESSION_CAST_PTR");
    case AST_Node_Type::EXPRESSION_BAKE: return string_create_static("EXPRESSION_BAKE");
    case AST_Node_Type::EXPRESSION_TYPE_INFO: return string_create_static("EXPRESSION_TYPE_INFO");
    case AST_Node_Type::EXPRESSION_TYPE_OF: return string_create_static("EXPRESSION_TYPE_OF");
    case AST_Node_Type::EXPRESSION_LITERAL: return string_create_static("EXPRESSION_LITERAL");
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL: return string_create_static("EXPRESSION_FUNCTION_CALL");
    case AST_Node_Type::EXPRESSION_IDENTIFIER: return string_create_static("EXPRESSION_IDENTIFIER");
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
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_POINTER_EQUAL: return string_create_static("EXPRESSION_BINARY_OPERATION_POINTER_EQUAL");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_POINTER_NOT_EQUAL: return string_create_static("EXPRESSION_BINARY_OPERATION_POINTER_NOT_EQUAL");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS: return string_create_static("EXPRESSION_BINARY_OPERATION_LESS");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL: return string_create_static("EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER: return string_create_static("EXPRESSION_BINARY_OPERATION_GREATER");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL: return string_create_static("EXPRESSION_BINARY_OPERATIONGREATER_OR_EQUAL");
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE: return string_create_static("EXPRESSION_UNARY_OPERATION_NEGATE");
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT: return string_create_static("EXPRESSION_UNARY_OPERATION_NOT");
    case AST_Node_Type::EXPRESSION_POINTER: return string_create_static("EXPRESSION_POINTER");
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE: return string_create_static("EXPRESSION_UNARY_DEREFERENCE");
    case AST_Node_Type::UNDEFINED: return string_create_static("UNDEFINED");
    }
    panic("Should not happen");
    return string_create_static("SHOULD NOT FUCKING HAPPEN MOTHERFUCKER");
}

bool ast_node_type_is_identifier_node(AST_Node_Type type) {
    return type == AST_Node_Type::IDENTIFIER_NAME || type == AST_Node_Type::IDENTIFIER_PATH;
}
bool ast_node_type_is_expression(AST_Node_Type type) {
    return type >= AST_Node_Type::EXPRESSION_POINTER && type <= AST_Node_Type::ENUM;
}
bool ast_node_type_is_binary_operation(AST_Node_Type type) {
    return type >= AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION && type <= AST_Node_Type::EXPRESSION_BINARY_OPERATION_POINTER_NOT_EQUAL;
}
bool ast_node_type_is_statement(AST_Node_Type type) {
    return type >= AST_Node_Type::STATEMENT_BLOCK && type <= AST_Node_Type::VARIABLE_DEFINE_INFER;
}

void ast_node_identifer_or_path_append_to_string(AST_Node* node, String* string)
{
    string_append(string, node->id->characters);
    if (node->type == AST_Node_Type::IDENTIFIER_PATH) {
        string_append(string, "::");
        ast_node_identifer_or_path_append_to_string(node->child_start, string);
    }
}

void ast_node_expression_append_to_string(Code_Source* code_source, AST_Node* node, String* string);
void ast_node_arguments_append_to_string(Code_Source* code_source, AST_Node* node, String* string)
{
    string_append(string, "(");
    AST_Node* child = node->child_start;
    while (child != 0) {
        ast_node_expression_append_to_string(code_source, child, string);
        if (child->neighbor != 0) {
            string_append(string, ",");
        }
        child = child->neighbor;
    }
    string_append(string, ")");
}

void ast_node_expression_append_to_string(Code_Source* code_source, AST_Node* node, String* string)
{
    bool bin_op = false;
    bool unary_op = false;
    const char* bin_op_str = "asfd";
    switch (node->type)
    {
    case AST_Node_Type::EXPRESSION_LITERAL: {
        Token t = *node->literal_token;
        switch (t.type) {
        case Token_Type::BOOLEAN_LITERAL: string_append_formated(string, t.attribute.bool_value ? "TRUE" : "FALSE"); break;
        case Token_Type::INTEGER_LITERAL: string_append_formated(string, "%d", t.attribute.integer_value); break;
        case Token_Type::FLOAT_LITERAL: string_append_formated(string, "%3.2f", t.attribute.float_value); break;
        case Token_Type::STRING_LITERAL: string_append_formated(string, "\"%s\"", t.attribute.id->characters); break;
        }
        return;
    }
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL:
        ast_node_expression_append_to_string(code_source, node->child_start, string);
        ast_node_arguments_append_to_string(code_source, node->child_start->neighbor, string);
        return;
    case AST_Node_Type::EXPRESSION_IDENTIFIER:
        ast_node_identifer_or_path_append_to_string(node->child_start, string);
        return;
    case AST_Node_Type::EXPRESSION_AUTO_ARRAY_INITIALIZER: {
        string_append_formated(string, "[");
        AST_Node* child_node = node->child_start;
        while (child_node != 0) {
            ast_node_expression_append_to_string(code_source, child_node, string);
            child_node = child_node->neighbor;
            if (child_node != 0) {
                string_append_formated(string, ", ");
            }
        }
        string_append_formated(string, "]");
        break;
    }
    case AST_Node_Type::EXPRESSION_ARRAY_ACCESS: {
        ast_node_expression_append_to_string(code_source, node->child_start, string);
        string_append_formated(string, "[");
        AST_Node* child_node = node->child_start->neighbor;
        while (child_node != 0) {
            ast_node_expression_append_to_string(code_source, child_node, string);
            child_node = child_node->neighbor;
            if (child_node != 0) {
                string_append_formated(string, ", ");
            }
        }
        string_append_formated(string, "]");
        return;
    }
    case AST_Node_Type::MODULE:
        string_append_formated(string, "Module");
        return;
    case AST_Node_Type::STRUCT:
        string_append_formated(string, "Struct");
        return;
    case AST_Node_Type::UNION:
        string_append_formated(string, "Union");
        return;
    case AST_Node_Type::C_UNION:
        string_append_formated(string, "C_Union");
        return;
    case AST_Node_Type::ENUM:
        string_append_formated(string, "Enum");
        return;
    case AST_Node_Type::FUNCTION:
        string_append_formated(string, "Function");
        return;
    case AST_Node_Type::EXPRESSION_MEMBER_ACCESS:
        ast_node_expression_append_to_string(code_source, node->child_start, string);
        string_append_formated(string, ".%s", node->id->characters);
        return;
    case AST_Node_Type::EXPRESSION_TYPE_INFO:
        string_append_formated(string, "type_info(");
        ast_node_expression_append_to_string(code_source, node->child_start, string);
        string_append_formated(string, ")");
        return;
    case AST_Node_Type::EXPRESSION_TYPE_OF:
        string_append_formated(string, "type_of(");
        ast_node_expression_append_to_string(code_source, node->child_start, string);
        string_append_formated(string, ")");
        return;
    case AST_Node_Type::EXPRESSION_BAKE:
        string_append_formated(string, "#bake");
        return;
    case AST_Node_Type::EXPRESSION_CAST_RAW:
        string_append_formated(string, "cast_raw ");
        ast_node_expression_append_to_string(code_source, node->child_start, string);
        return;
    case AST_Node_Type::EXPRESSION_CAST_PTR:
        if (node->child_count == 1) {
            string_append_formated(string, "cast_ptr ");
            ast_node_expression_append_to_string(code_source, node->child_start, string);
        }
        else {
            string_append_formated(string, "cast_ptr(");
            ast_node_expression_append_to_string(code_source, node->child_start, string);
            string_append_formated(string, ") ");
            ast_node_expression_append_to_string(code_source, node->child_end, string);
        }
        return;
    case AST_Node_Type::EXPRESSION_CAST:
        if (node->child_count == 1) {
            string_append_formated(string, "cast ");
            ast_node_expression_append_to_string(code_source, node->child_start, string);
        }
        else {
            string_append_formated(string, "cast(");
            ast_node_expression_append_to_string(code_source, node->child_start, string);
            string_append_formated(string, ") ");
            ast_node_expression_append_to_string(code_source, node->child_end, string);
        }
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
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_POINTER_EQUAL: bin_op = true, bin_op_str = "*!="; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_POINTER_NOT_EQUAL: bin_op = true, bin_op_str = "*!="; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS: bin_op = true, bin_op_str = "<"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL: bin_op = true, bin_op_str = "<="; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER: bin_op = true, bin_op_str = ">"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL: bin_op = true, bin_op_str = ">="; break;
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE: unary_op = true, bin_op_str = "-"; break;
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT:unary_op = true, bin_op_str = "!"; break;
    case AST_Node_Type::EXPRESSION_POINTER:unary_op = true, bin_op_str = "*"; break;
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE:unary_op = true, bin_op_str = "&"; break;
    default: return;
    }
    if (bin_op)
    {
        string_append_formated(string, "(");
        ast_node_expression_append_to_string(code_source, node->child_start, string);
        string_append_formated(string, " %s ", bin_op_str);
        ast_node_expression_append_to_string(code_source, node->child_start->neighbor, string);
        string_append_formated(string, ")");
        return;
    }
    else if (unary_op)
    {
        string_append_formated(string, bin_op_str);
        ast_node_expression_append_to_string(code_source, node->child_start, string);
    }
}

void ast_node_append_to_string(Code_Source* code_source, AST_Node* node, String* string, int indentation_lvl)
{
    for (int j = 0; j < indentation_lvl; j++) {
        string_append_formated(string, "  ");
    }
    string_append_formated(string, "#%d ", node->alloc_index);
    String type_str = ast_node_type_to_string(node->type);
    string_append_string(string, &type_str);
    if (ast_node_type_is_expression(node->type)) {
        string_append_formated(string, ": ");
        ast_node_expression_append_to_string(code_source, node, string);
        //string_append_formated(string, "\n");
    }
    if (code_source->tokens.size > 0) {
        /*
        int start_index = node->token_range.start_index;
        int end_index = node->token_range.end_index;
        if (end_index == code_source->tokens.size) {
            end_index = code_source->tokens.size - 1;
        }
        string_append_formated(string, " Line-Range: %d-%d, Character-Range: %d-%d ",
            code_source->tokens[start_index].position.start.line,
            code_source->tokens[end_index].position.end.line,
            code_source->tokens[start_index].position.start.character,
            code_source->tokens[end_index].position.end.character
        );
        */
    }
    string_append_formated(string, "\n");
    AST_Node* child = node->child_start;
    while (child != 0) {
        ast_node_append_to_string(code_source, child, string, indentation_lvl + 1);
        child = child->neighbor;
    }
}
