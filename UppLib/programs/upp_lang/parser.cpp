#include "parser.hpp"

#include "ast.hpp"
#include "syntax_editor.hpp"
#include "compiler.hpp"
#include "code_history.hpp"

namespace Parser
{
    // Types
    using namespace AST;

    struct Binop_Link
    {
        Binop binop;
        Token_Index token_index;
        Expression* expr;
    };

    struct Parse_State
    {
        Token_Index pos;
        Source_Line* line;
        int allocated_count;
        int error_count;
    };

    struct Parser
    {
        Source_Code* code;
        Compilation_Unit* unit;
        Parse_State state;
        // Dynamic_Array<Error_Message> new_error_messages; // Required in incremental parsing (We need a distinction between old and new errors)
    };


    // Globals
    static Parser parser;

    // Parser Functions
    void parser_rollback(Parse_State checkpoint)
    {
        auto& nodes = parser.unit->allocated_nodes;
        auto& errors = parser.unit->parser_errors;

        assert(checkpoint.allocated_count <= nodes.size, "");
        for (int i = checkpoint.allocated_count; i < nodes.size; i++) {
            AST::base_destroy(nodes[i]);
        }
        dynamic_array_rollback_to_size(&nodes, checkpoint.allocated_count);
        dynamic_array_rollback_to_size(&errors, checkpoint.error_count);
        parser.state = checkpoint;
    }

    template<typename T>
    T* allocate_base(Node* parent, Node_Type type)
    {
        // PERF: A block allocator could probably be used here
        auto result = new T;
        memory_zero(result);
        Node* base = &result->base;
        base->parent = parent;
        base->type = type;
        base->range.start = parser.state.pos;
        base->range.end = base->range.start;

        return result;
    }



    // Error reporting
    void log_error(const char* msg, Token_Range range)
    {
        Error_Message err;
        err.msg = msg;
        err.range = range;
        dynamic_array_push_back(&parser.unit->parser_errors, err);
        parser.state.error_count = parser.unit->parser_errors.size;
    }

    void log_error_to_pos(const char* msg, Token_Index pos) {
        log_error(msg, token_range_make(parser.state.pos, pos));
    }

    void log_error_range_offset_with_start(const char* msg, Token_Index start, int token_offset) {
        log_error(msg, token_range_make_offset(parser.state.pos, token_offset));
    }

    int remaining_line_tokens();
    void log_error_range_offset(const char* msg, int token_offset) {
        if (remaining_line_tokens() <= 0) {
            token_offset = 0;
        }
        log_error_range_offset_with_start(msg, parser.state.pos, token_offset);
    }



    // Positional Stuff
    Token* get_token(int offset) {
        return &parser.state.line->tokens[parser.state.pos.token + offset];
    }

    Token* get_token() {
        return &parser.state.line->tokens[parser.state.pos.token];
    }

    int remaining_line_tokens() {
        if (parser.state.line == 0) return 0;
        auto& tokens = parser.state.line->tokens;
        int remaining = tokens.size - parser.state.pos.token;
        if (tokens.size != 0 && tokens[tokens.size - 1].type == Token_Type::COMMENT) {
            remaining -= 1;
        }
        return remaining;
    }

    bool test_parenthesis(char c);
    bool on_follow_block() {
        auto& pos = parser.state.pos;
        auto code = parser.code;

        if (test_parenthesis('{')) return true;
        if (pos.line + 1 >= code->line_count) return false;
        if (remaining_line_tokens() > 0) return false;
        auto next_line = source_code_get_line(code, pos.line + 1);
        return next_line->indentation > parser.state.line->indentation;
    }

    void advance_token() {
        parser.state.pos.token += 1;
    }



    // Token Testing functions
    bool test_token_offset(Token_Type type, int offset) {
        if (offset >= remaining_line_tokens()) return false;
        auto token = get_token(offset);
        return token->type == type;
    }

    bool test_token(Token_Type type) {
        return test_token_offset(type, 0);
    }

    bool test_token_2(Token_Type t0, Token_Type t1) {
        return test_token_offset(t0, 0) && test_token_offset(t1, 1);
    }

    bool test_token_3(Token_Type t0, Token_Type t1, Token_Type t2) {
        return test_token_offset(t0, 0) && test_token_offset(t1, 1) && test_token_offset(t2, 2);
    }

    bool test_token_4(Token_Type t0, Token_Type t1, Token_Type t2, Token_Type t3) {
        return test_token_offset(t0, 0) && test_token_offset(t1, 1) && test_token_offset(t2, 2) && test_token_offset(t3, 3);
    }

    bool test_operator_offset(Operator op, int offset) {
        if (!test_token_offset(Token_Type::OPERATOR, offset))
            return false;
        return get_token(offset)->options.op == op;
    }

    bool test_operator(Operator op) {
        return test_operator_offset(op, 0);
    }

    bool test_keyword_offset(Keyword keyword, int offset) {
        if (!test_token_offset(Token_Type::KEYWORD, offset))
            return false;
        return get_token(offset)->options.keyword == keyword;
    }

    bool test_keyword(Keyword keyword) {
        return test_keyword_offset(keyword, 0);
    }

    bool test_parenthesis_offset(char c, int offset) {
        Parenthesis p = char_to_parenthesis(c);
        if (!test_token_offset(Token_Type::PARENTHESIS, offset))
            return false;
        auto given = get_token(offset)->options.parenthesis;
        return given.is_open == p.is_open && given.type == p.type;
    }

    bool test_parenthesis(char c) {
        return test_parenthesis_offset(c, 0);
    }

    void node_calculate_bounding_range(AST::Node* node)
    {
        auto& bounding_range = node->bounding_range;
        bounding_range = node->range;
        int index = 0;
        auto child = AST::base_get_child(node, index);
        while (child != 0) 
        {
            auto child_range = child->bounding_range;
            if (token_index_compare(bounding_range.start, child_range.start) < 0) {
                bounding_range.start = child_range.start;
            }
            if (token_index_compare(child_range.end, bounding_range.end) < 0) {
                bounding_range.end = child_range.end;
            }

            index += 1;
            child = AST::base_get_child(node, index);
        }
    }

    void node_finalize_range(AST::Node* node) 
    {
        // NOTE: This function did more in the past with hierarchical text-structure
        // Now it does 3 things: 
        //      * Sanity checks
        //      * Sets the end of the node
        //      * Calcualtes bounding-ranges (for editor)
        auto code = parser.code;

        // Set end of node
        auto& range = node->range;
        range.end = parser.state.pos;
        if (range.end.line >= code->line_count) {
            range.end = token_index_make_line_end(code, code->line_count - 1);
        }
        else if (range.start.line != range.end.line && range.end.token == 0 && range.end.line > 0) {
            // Here we have the difference between next line token zero or end of line last token
            range.end = token_index_make_line_end(code, range.end.line - 1);
        }
        
        // Calculate bounding range
        node_calculate_bounding_range(node);

        // Sanity-Check that start/end is in order
        int order = token_index_compare(range.start, range.end);
        assert(order != -1, "Ranges must be in order");
    }




    // Parsing Helpers
#define CHECKPOINT_SETUP \
        if (parser.state.pos.line >= parser.code->line_count) {return 0;}\
        if (remaining_line_tokens() == 0 && !on_follow_block()) {return 0;}\
        auto checkpoint = parser.state;\
        bool _error_exit = false;\
        SCOPE_EXIT(if (_error_exit) parser_rollback(checkpoint););

#define CHECKPOINT_EXIT {_error_exit = true; return 0;}
#define SET_END_RANGE(val) {node_finalize_range(&val->base);}
#define PARSE_SUCCESS(val) { \
        node_finalize_range(&val->base); \
        return val; \
    }

    int source_line_get_non_comment_token_count(Source_Line* line)
    {
        int token_count = line->tokens.size;
        if (line->tokens.size > 0 && line->tokens[line->tokens.size - 1].type == Token_Type::COMMENT) {
            token_count -= 1;
        }
        return token_count;
    }

    // Searches next eligible token, e.g. tokens that aren't in some other list, like () or {} or something
    typedef bool(*token_predicate_fn)(Token* token, void* userdata);
    Optional<Token_Index> search_token(Source_Code* code, Token_Index start, token_predicate_fn predicate, void* user_data)
    {
        // IDEA: Maybe I can move this whole function to source_code, since it could be used outside of parsing
        // PERF: The parenthesis stack can be held in the parser, and just be resetted here
        Dynamic_Array<Parenthesis> parenthesis_stack = dynamic_array_create<Parenthesis>();
        SCOPE_EXIT(dynamic_array_destroy(&parenthesis_stack));

        Token_Index pos = start;
        Source_Line* line = source_code_get_line(code, pos.line);
        int token_count = source_line_get_non_comment_token_count(line);
        int start_indentation = line->indentation;

        while (true)
        {
            // Normal token handling
            if (pos.token < token_count) 
            {
                auto token = &line->tokens[pos.token];

                // If we aren't in other parenthesis, check the token
                if (parenthesis_stack.size == 0) {
                    if (predicate(token, user_data)) {
                        return optional_make_success(pos);
                    }
                }

                // Otherwise check for parenthesis
                if (token->type == Token_Type::PARENTHESIS)
                {
                    auto parenthesis = token->options.parenthesis;
                    if (parenthesis.is_open) {
                        dynamic_array_push_back(&parenthesis_stack, parenthesis);
                    }
                    else if (parenthesis_stack.size > 0) {
                        auto last = dynamic_array_last(&parenthesis_stack);
                        if (last.type == parenthesis.type) {
                            dynamic_array_rollback_to_size(&parenthesis_stack, parenthesis_stack.size - 1);
                        }
                    }
                }

                pos.token += 1;
                continue;
            }

            // Otherwise we are at the end of the line
            // Check if the next line is a follow block
            if (pos.line + 1 >= code->line_count) { // End of code
                return optional_make_failure<Token_Index>();
            }
            Source_Line* next_line = source_code_get_line(code, pos.line + 1);
            if (next_line->indentation <= start_indentation) {
                return optional_make_failure<Token_Index>(); // No follow block
            }

            // If it is a follow block, we skip all lines until we hit the next valid line
            pos.line = pos.line + 2;
            while (true)
            {
                if (pos.line >= code->line_count) {
                    return optional_make_failure<Token_Index>(); // Ran out of lines
                }
                next_line = source_code_get_line(code, pos.line);
                if (next_line->indentation < start_indentation) {
                    return optional_make_failure<Token_Index>(); // No follow block
                }
                else if (next_line->indentation == start_indentation) {
                    break;
                }

                pos.line += 1;
            }

            line = next_line;
            pos.token = 0;
            token_count = source_line_get_non_comment_token_count(line);
        }
    }

    typedef bool (*is_list_stop_token_fn)(Token& token, void* userdata);
    typedef Node* (*list_item_parse_fn)(Node* parent);
    typedef void (*add_list_item_to_parent_fn)(Node* parent, Node* child);

    struct Stopper
    {
        bool found_stop;
        is_list_stop_token_fn stop_fn;
        void* stop_userdata;
        Operator seperator;
    };

    // Parses seperated items until end of line or a stop-token is found
    void parse_list_single_line(
        Node* parent, list_item_parse_fn parse_fn, add_list_item_to_parent_fn add_to_parent_fn, 
        is_list_stop_token_fn stop_fn, void* stop_userdata, Operator seperator)
    {
        // Parse Items
        while (true)
        {
            auto line_before_item = parser.state.pos.line;
            auto item = parse_fn(parent);
            if (item != 0)
            {
                add_to_parent_fn(parent, item);
                if (test_operator(seperator)) {
                    advance_token();
                    continue;
                }

                // Check if we are finished with item parsing
                if (parser.state.pos.line >= parser.code->line_count) {
                    return;
                }
                if (remaining_line_tokens() == 0) { // Reached end of line
                    return;
                }
                if (stop_fn != 0) { // Reached stop
                    if (stop_fn(*get_token(), stop_userdata)) {
                        return;
                    }
                }

                // If we are at the start of a new line, we also want to quit here (As this function only parses one line) (E.g. defining a struct)
                auto& pos = parser.state.pos;
                if (pos.token == 0 && pos.line != line_before_item) {
                    return;
                }
            }
            
            // Otherwise try to find next comma or stop
            Stopper stopper;
            stopper.found_stop = false;
            stopper.stop_fn = stop_fn;
            stopper.seperator = seperator;
            stopper.stop_userdata = stop_userdata;
            auto search_fn = [](Token* token, void* userdata) -> bool {
                Stopper* stopper = (Stopper*)userdata;
                auto stop_function = stopper->stop_fn;
                if (stop_function != 0) {
                    if (stop_function(*token, stopper->stop_userdata)) {
                        stopper->found_stop = true;
                        return true;
                    }
                }
                return token->type == Token_Type::OPERATOR && token->options.op == stopper->seperator;
            };
            auto recovery_point_opt = search_token(parser.code, parser.state.pos, search_fn, &stopper);

            // Check if we reached stop, found comma or none of both
            if (!recovery_point_opt.available) {
                return;
            }
            log_error_to_pos("Couldn't parse item", recovery_point_opt.value);
            parser.state.pos = recovery_point_opt.value;

            if (stopper.found_stop) {
                // Otherwise we found the stop point
                return;
            }

            // Otherwise we found a comma, so we skip it and parse the next item
            advance_token();
        }
    }

    void code_block_add_child(AST::Node* parent, AST::Node* child);
    Node* wrapper_parse_statement_or_context_change(AST::Node* parent);

    // Expects to be on the starting line of this block
    // Ends on the next line after the block
    void parse_block_of_items(
        Node* parent, list_item_parse_fn parse_fn, add_list_item_to_parent_fn add_to_parent_fn, Operator seperator, int block_indent, bool anonymous_blocks_allowed)
    {
        auto& pos = parser.state.pos;
        auto& code = parser.code;

        Source_Line* line = parser.state.line;
        while (true)
        {
            if (pos.line >= code->line_count) {
                return;
            }

            line = parser.state.line;
            // Break if we exit the block
            if (line->is_comment) {
                // Skip line if it's a comment...
            }
            else if (line->indentation < block_indent) {
                assert(parser.state.pos.token == 0, "");
                return;
            }
            else if (line->indentation > block_indent) 
            {
                if (anonymous_blocks_allowed) 
                {
                    auto statement = allocate_base<Statement>(parent, Node_Type::STATEMENT);
                    statement->type = Statement_Type::BLOCK;

                    auto& code_block = statement->options.block;
                    code_block = allocate_base<Code_Block>(AST::upcast(statement), Node_Type::CODE_BLOCK);
                    code_block->statements = dynamic_array_create<Statement*>(1);
                    code_block->context_changes = dynamic_array_create<Context_Change*>(1);
                    code_block->block_id = optional_make_failure<String*>();

                    int block_start_line = pos.line;
                    parse_block_of_items(
                        upcast(code_block), wrapper_parse_statement_or_context_change, code_block_add_child, Operator::SEMI_COLON,
                        block_indent + 1, true
                    );

                    Token_Range range = token_range_make(token_index_make(block_start_line, 0), token_index_make_line_end(code, parser.state.pos.line-1));
                    code_block->base.range = range;
                    code_block->base.bounding_range = range;
                    statement->base.range = range;
                    statement->base.bounding_range = range;

                    add_to_parent_fn(parent, upcast(statement));
                    continue;
                }
                else {
                    log_error_range_offset("Unexpected anonymous block", 0);
                    // Skip all lines afterwards with same indent
                    while (pos.line < code->line_count) {
                        parser.state.line = source_code_get_line(code, pos.line);
                        pos.token = 0;
                        if (parser.state.line->indentation <= block_indent) {
                            break;
                        }
                        pos.line += 1;
                    }
                    if (pos.line >= code->line_count) {
                        parser.state.line = 0;
                        return;
                    }
                    continue;
                }
            }
            else
            {
                // Otherwise parse line item if line isn't a comment or empty
                if (!line->tokens.size == 0)
                {
                    int line_before_index = pos.line;
                    parse_list_single_line(parent, parse_fn, add_to_parent_fn, nullptr, nullptr, seperator);

                    if (pos.line != line_before_index) {
                        if (pos.line >= code->line_count) {
                            parser.state.line = nullptr;
                            return;
                        }
                        if (pos.token == 0) { // Don't advance line if it has already been done
                            continue; 
                        }
                    }

                    if (remaining_line_tokens() > 0) {
                        log_error_to_pos("Cannot parse remaining line items", token_index_make(pos.line, line->tokens.size - 1));
                        pos.token = line->tokens.size;
                    }
                }
            }

            // Advance line
            if (pos.line + 1 >= code->line_count) {
                pos.line = code->line_count;
                parser.state.line = nullptr;
                return;
            }
            pos.line += 1;
            pos.token = 0;
            parser.state.line = source_code_get_line(code, pos.line);
        }
    }

    void parse_list_of_items(
        Node* parent, list_item_parse_fn parse_fn, add_list_item_to_parent_fn add_to_parent_fn, 
        Parenthesis_Type parenthesis, Operator seperator, bool valid_without_parenthesis, bool anonymous_blocks_allowed)
    {
        auto& pos = parser.state.pos;
        auto& code = parser.code;

        bool multi_line_valid = false;
        bool expecting_closing_parenthesis_after_block = false;
        int block_indentation = parser.state.line->indentation + 1;

        // Check for correct List-Start (Either on parenthesis, follow block or root?)
        {
            bool expecting_follow_block = false;
            int remaining_tokens = remaining_line_tokens();
            if (remaining_tokens == 0)
            {
                if (!valid_without_parenthesis) {
                    return;
                }
                else {
                    expecting_follow_block = true;
                    multi_line_valid = true;
                    expecting_closing_parenthesis_after_block = false;
                }
            }
            else
            {
                // Check if we are on parenthesis
                Parenthesis p;
                p.type = parenthesis;
                p.is_open = true;
                if (!test_parenthesis_offset(parenthesis_to_char(p), 0)) return;
                advance_token();

                if (remaining_tokens == 1) { // In this case the parenthesis was the last token
                    expecting_follow_block = true;
                    multi_line_valid = true;
                    expecting_closing_parenthesis_after_block = true;
                }
                else {
                    expecting_follow_block = false;
                    multi_line_valid = false;
                    expecting_closing_parenthesis_after_block = false;
                }
            }

            // Check if follow block is required
            if (expecting_follow_block)
            {
                if (pos.line + 1 >= code->line_count) {
                    log_error_range_offset("Expected follow block", 0);
                    return;
                }
                Source_Line* next_line = source_code_get_line(code, pos.line + 1);
                if (next_line->indentation < parser.state.line->indentation + 1) {
                    log_error_range_offset("Expected follow block", 0);
                    return;
                }

                parser.state.line = next_line;
                pos.line += 1;
                pos.token = 0;
            }
        }

        // Handle single/multiple lines seperately
        if (!multi_line_valid)
        {
            Parenthesis p;
            p.type = parenthesis;
            p.is_open = false;
            char closing = parenthesis_to_char(p);
            if (test_parenthesis(closing)) {
                advance_token();
                return;
            }

            auto stop_fn = [](Token& token, void* userdata) -> bool {
                if (token.type != Token_Type::PARENTHESIS) return false;
                if (token.options.parenthesis.is_open) return false;
                Parenthesis_Type type = *(Parenthesis_Type*)userdata;
                return token.options.parenthesis.type == type;
            };
            parse_list_single_line(parent, parse_fn, add_to_parent_fn, stop_fn, &parenthesis, seperator);

            // Check that we are on closing parentheschis
            if (test_parenthesis(closing)) {
                advance_token();
            }
            else {
                log_error_range_offset("Expected closing parenthesis on this line", 0);
            }
            return;
        }
        else
        {
            parse_block_of_items(parent, parse_fn, add_to_parent_fn, seperator, block_indentation, anonymous_blocks_allowed);

            if (expecting_closing_parenthesis_after_block) {
                Parenthesis p;
                p.type = parenthesis;
                p.is_open = false;
                if (test_parenthesis(parenthesis_to_char(p))) {
                    advance_token();
                }
                else {
                    log_error_range_offset("Expected closing parenthesis", 0);
                }
            }
        }
    }


    template<Parenthesis_Type type>
    bool finish_parenthesis()
    {
        // Check if we are on the parenthesis exit
        Parenthesis closing;
        closing.type = type;
        closing.is_open = false;
        if (test_parenthesis(parenthesis_to_char(closing))) {
            advance_token();
            return true;
        }

        // Check if we can find the parenthesis exit
        auto parenthesis_pos = search_token(
            parser.code,
            parser.state.pos,
            [](Token* t, void* _unused) -> bool
            { return t->type == Token_Type::PARENTHESIS &&
            !t->options.parenthesis.is_open && t->options.parenthesis.type == type; }, nullptr
        );
        if (!parenthesis_pos.available) {
            return false;
        }
        log_error_to_pos("Parenthesis context was already finished, unexpected tokens afterwards", parenthesis_pos.value);
        parser.state.pos = parenthesis_pos.value;
        advance_token();
        return true;
    }



    // Prototypes
    Optional<String*> parse_block_label_or_use_related_node_id(AST::Node* related_node);
    Code_Block* parse_code_block(Node* parent, AST::Node* related_node);
    Path_Lookup* parse_path_lookup(Node* parent);
    Expression* parse_expression(Node* parent);
    Expression* parse_expression_or_error_expr(Node* parent);
    Expression* parse_single_expression(Node* parent);
    Expression* parse_single_expression_or_error(Node* parent);
    Argument* parse_argument(Node* parent);
    Subtype_Initializer* parse_subtype_initializer(Node* parent);
    Parameter* parse_parameter(Node* parent);

    // Returns an error-expression if uninitialized
    Node* parse_argument_or_subtype_initializer_or_uninitialzed(AST::Node* parent) 
    {
        if (test_operator(Operator::UNINITIALIZED)) {
            auto error_expr = allocate_base<AST::Expression>(parent, AST::Node_Type::EXPRESSION);
            error_expr->type = Expression_Type::ERROR_EXPR;

            advance_token();
            node_finalize_range(upcast(error_expr));
            return upcast(error_expr);
        }

        Node* result = upcast(parse_subtype_initializer(parent));
        if (result != 0) return result;

        return upcast(parse_argument(parent));
    }

    // Always returns node, even if no arguments were found...
    // So one should always test for ( or { before calling this...
    Arguments* parse_arguments(Node* parent, Parenthesis_Type parenthesis)
    {
        auto args = allocate_base<Arguments>(parent, Node_Type::ARGUMENTS);
        args->arguments = dynamic_array_create<Argument*>();
        args->subtype_initializers = dynamic_array_create<Subtype_Initializer*>();
        args->uninitialized_tokens = dynamic_array_create<Expression*>();

        {
            Parenthesis p;
            p.type = parenthesis;
            p.is_open = true;
            if (!test_parenthesis(parenthesis_to_char(p))) {
                log_error_range_offset("Expected parenthesis for parameters", 0);
                PARSE_SUCCESS(args);
            }
        }

        auto add_to_arguments = [](Node* parent, Node* child) 
        {
            auto args = downcast<Arguments>(parent);
            if (child->type == Node_Type::ARGUMENT) {
                dynamic_array_push_back(&args->arguments, downcast<Argument>(child));
            }
            else if (child->type == Node_Type::SUBTYPE_INITIALIZER) {
                dynamic_array_push_back(&args->subtype_initializers, downcast<Subtype_Initializer>(child));
            }
            else if (child->type == Node_Type::EXPRESSION) {
                auto expr = downcast<Expression>(child);
                assert(expr->type == Expression_Type::ERROR_EXPR, "");
                dynamic_array_push_back(&args->uninitialized_tokens, expr);
            }
            else {
                panic("");
            }
        };
        parse_list_of_items(
            upcast(args), 
            parse_argument_or_subtype_initializer_or_uninitialzed, 
            add_to_arguments, parenthesis, Operator::COMMA, false, false
        );

        PARSE_SUCCESS(args);
    }

    namespace Block_Items
    {
        Node* parse_module_item(AST::Node* parent);
        Enum_Member_Node* parse_enum_member(AST::Node* parent);
        Switch_Case* parse_switch_case(AST::Node* parent);
        Structure_Member_Node* parse_struct_member(AST::Node* parent);
        Definition_Symbol* parse_definition_symbol(AST::Node* parent);
        Node* parse_statement_or_context_change(AST::Node* parent);
    };

    Node* wrapper_parse_module_item(AST::Node* parent) { return Block_Items::parse_module_item(parent); }
    Node* wrapper_parse_enum_member(AST::Node* parent) { return upcast(Block_Items::parse_enum_member(parent)); }
    Node* wrapper_parse_switch_case(AST::Node* parent) { return upcast(Block_Items::parse_switch_case(parent)); }
    Node* wrapper_parse_struct_member(AST::Node* parent) { return upcast(Block_Items::parse_struct_member(parent)); }
    Node* wrapper_parse_definition_symbol(AST::Node* parent) { return upcast(Block_Items::parse_definition_symbol(parent)); }
    Node* wrapper_parse_statement_or_context_change(AST::Node* parent) { return Block_Items::parse_statement_or_context_change(parent); }
    Node* wrapper_parse_expression_or_error(AST::Node* parent) { return upcast(parse_expression_or_error_expr(parent)); }
    Node* wrapper_parse_parameter(AST::Node* parent) { return upcast(parse_parameter(parent)); }

    void code_block_add_child(AST::Node* parent, AST::Node* child) 
    {
        AST::Code_Block* block = downcast<AST::Code_Block>(parent);
        if (child->type == AST::Node_Type::STATEMENT) {
            dynamic_array_push_back(&block->statements, downcast<AST::Statement>(child));
        }
        else if (child->type == AST::Node_Type::CONTEXT_CHANGE) {
            dynamic_array_push_back(&block->context_changes, downcast<AST::Context_Change>(child));
        }
        else {
            panic("");
        }
    }

    void module_add_child(AST::Node* parent, AST::Node* child) 
    {
        AST::Module* module = downcast<AST::Module>(parent);
        if (child->type == AST::Node_Type::DEFINITION) {
            dynamic_array_push_back(&module->definitions, downcast<AST::Definition>(child));
        }
        else if (child->type == AST::Node_Type::IMPORT) {
            dynamic_array_push_back(&module->import_nodes, downcast<Import>(child));
        }
        else if (child->type == AST::Node_Type::EXTERN_IMPORT) {
            dynamic_array_push_back(&module->extern_imports, downcast<Extern_Import>(child));
        }
        else if (child->type == AST::Node_Type::CONTEXT_CHANGE) {
            dynamic_array_push_back(&module->context_changes, downcast<Context_Change>(child));
        }
        else {
            panic("");
        }
    }

    bool stop_at_semicolon(Token& token, void* unused) {
        return token.type == Token_Type::OPERATOR && token.options.op == Operator::SEMI_COLON;
    }
    



    // Block Parsing
    namespace Block_Items
    {
        Definition_Symbol* parse_definition_symbol(Node* parent) {
            if (!test_token(Token_Type::IDENTIFIER)) {
                return 0;
            }
            auto node = allocate_base<Definition_Symbol>(parent, Node_Type::DEFINITION_SYMBOL);
            node->name = get_token()->options.identifier;
            advance_token();
            PARSE_SUCCESS(node);
        }

        AST::Extern_Import* parse_extern_import(AST::Node* parent)
        {
            auto start = parser.state.pos;
            if (!test_keyword(Keyword::EXTERN)) {
                return 0;
            }

            CHECKPOINT_SETUP;
            auto result = allocate_base<Extern_Import>(parent, Node_Type::EXTERN_IMPORT);
            advance_token();

            if (test_token(Token_Type::IDENTIFIER))
            {
                auto& ids = compiler.identifier_pool.predefined_ids;
                String* id = get_token()->options.identifier;
                if (id == ids.function) {
                    result->type = Extern_Type::FUNCTION;
                }
                else if (id == ids.global) {
                    result->type = Extern_Type::GLOBAL;
                }
                else if (id == ids.lib) {
                    result->type = Extern_Type::COMPILER_SETTING;
                    result->options.setting.type = Extern_Compiler_Setting::LIBRARY;
                }
                else if (id == ids.lib_dir) {
                    result->type = Extern_Type::COMPILER_SETTING;
                    result->options.setting.type = Extern_Compiler_Setting::LIBRARY_DIRECTORY;
                }
                else if (id == ids.source) {
                    result->type = Extern_Type::COMPILER_SETTING;
                    result->options.setting.type = Extern_Compiler_Setting::SOURCE_FILE;
                }
                else if (id == ids.header) {
                    result->type = Extern_Type::COMPILER_SETTING;
                    result->options.setting.type = Extern_Compiler_Setting::HEADER_FILE;
                }
                else if (id == ids.header_dir) {
                    result->type = Extern_Type::COMPILER_SETTING;
                    result->options.setting.type = Extern_Compiler_Setting::INCLUDE_DIRECTORY;
                }
                else if (id == ids.definition) {
                    result->type = Extern_Type::COMPILER_SETTING;
                    result->options.setting.type = Extern_Compiler_Setting::DEFINITION;
                }
                else {
                    log_error_range_offset("Identifier after extern must be one of: function, global, source, lib, lib_dir", 1);
                    result->type = Extern_Type::INVALID;
                    return result;
                }
            }
            else if (test_keyword(Keyword::STRUCT)) {
                result->type = Extern_Type::STRUCT;
            }
            else {
                log_error_range_offset_with_start("Expected extern-type after extern keyword!", start, 1);
                result->type = Extern_Type::INVALID;
                return result;
            }
            advance_token();

            switch (result->type)
            {
            case Extern_Type::FUNCTION: 
            case Extern_Type::GLOBAL: 
            {
                if (!test_token(Token_Type::IDENTIFIER)) {
                    log_error_range_offset("Expected identifier", 1);
                    result->type = Extern_Type::INVALID;
                    return result;
                }
                String* id = get_token()->options.identifier;
                advance_token();

                if (!test_operator(Operator::COLON)) {
                    log_error_range_offset("Expected : after identifier", 1);
                    result->type = Extern_Type::INVALID;
                    return result;
                }
                advance_token();

                AST::Expression* expr = parse_expression_or_error_expr(upcast(result));

                if (result->type == Extern_Type::FUNCTION) {
                    result->options.function.id = id;
                    result->options.function.type_expr = expr;
                }
                else {
                    result->options.global.id = id;
                    result->options.global.type_expr = expr;
                }
                break;
            }
            case Extern_Type::STRUCT: {
                result->options.struct_type_expr = parse_expression_or_error_expr(upcast(result));
                break;
            }
            case Extern_Type::COMPILER_SETTING:
            {
                if (!test_token(Token_Type::LITERAL)) {
                    log_error_range_offset("Expected literal", 1);
                    result->type = Extern_Type::INVALID;
                    return result;
                }
                if (get_token()->options.literal_value.type != Literal_Type::STRING) {
                    log_error_range_offset("Expected string_literal", 1);
                    result->type = Extern_Type::INVALID;
                    return result;
                }
                String* path = get_token()->options.literal_value.options.string;
                advance_token();
                result->options.setting.value = path;
                break;
            }
            default: panic("");
            }

            return result;
        }

        // Block Item functions
        Import* parse_import(Node* parent)
        {
            CHECKPOINT_SETUP;
            if (!test_keyword_offset(Keyword::IMPORT, 0)) {
                CHECKPOINT_EXIT;
                return 0;
            }

            auto result = allocate_base<Import>(parent, Node_Type::IMPORT);
            result->alias_name = 0;
            result->file_name = 0;
            result->path = 0;
            advance_token();

            // Check if its' a file import
            if (test_token(Token_Type::LITERAL) && get_token()->options.literal_value.type == Literal_Type::STRING) {
                result->type = Import_Type::FILE;
                result->file_name = get_token()->options.literal_value.options.string;
                advance_token();
            }
            else {
                result->type = Import_Type::SINGLE_SYMBOL;
                result->path = parse_path_lookup(upcast(result));
                if (result->path == 0) {
                    CHECKPOINT_EXIT;
                }

                if (test_operator(Operator::TILDE_STAR)) {
                    result->type = Import_Type::MODULE_SYMBOLS;
                    advance_token();
                }
                else if (test_operator(Operator::TILDE_STAR_STAR)) {
                    result->type = Import_Type::MODULE_SYMBOLS_TRANSITIVE;
                    advance_token();
                }
            }

            if (test_keyword(Keyword::AS) && test_token_offset(Token_Type::IDENTIFIER, 1)) {
                result->alias_name = get_token(1)->options.identifier;
                advance_token();
                advance_token();
            }
            PARSE_SUCCESS(result);
        }

        Context_Change* parse_context_change(Node* parent)
        {
            auto& ids = compiler.identifier_pool.predefined_ids;

            CHECKPOINT_SETUP;
            if (!test_keyword_offset(Keyword::CONTEXT, 0)) {
                CHECKPOINT_EXIT;
            }

            auto result = allocate_base<Context_Change>(parent, Node_Type::CONTEXT_CHANGE);
            advance_token();
            result->type = Context_Change_Type::INVALID;

            // Check if import
            if (test_keyword(Keyword::IMPORT)) {
                result->type = Context_Change_Type::IMPORT;
                advance_token();

                auto path = parse_path_lookup(upcast(result));
                if (path == 0) {
                    CHECKPOINT_EXIT;
                }

                result->options.import_path = path;
                PARSE_SUCCESS(result);
            }


            // Check for identifier
            Context_Change_Type change_type = Context_Change_Type::INVALID;
            if (test_token(Token_Type::IDENTIFIER)) 
            {
                auto& ids = compiler.identifier_pool.predefined_ids;
                auto id = get_token()->options.identifier;

                if (id == ids.add_array_access) { change_type = Context_Change_Type::ARRAY_ACCESS; }
                else if (id == ids.add_binop) { change_type = Context_Change_Type::BINARY_OPERATOR; }
                else if (id == ids.add_unop) { change_type = Context_Change_Type::UNARY_OPERATOR; }
                else if (id == ids.add_iterator) { change_type = Context_Change_Type::ITERATOR; }
                else if (id == ids.add_cast) { change_type = Context_Change_Type::CAST; }
                else if (id == ids.add_dot_call) { change_type = Context_Change_Type::DOT_CALL; }
                else if (id == ids.set_cast_option) { change_type = Context_Change_Type::CAST_OPTION; }
                else {
                    log_error_range_offset("Identifier is not a valid context operation", 1);
                }
                advance_token();
            }
            else {
                log_error_range_offset("Expected identifier", 0);
            }

            result->type = change_type;
            result->options.arguments = parse_arguments(upcast(result), Parenthesis_Type::PARENTHESIS);
            PARSE_SUCCESS(result);
        }

        Definition* parse_definition(Node* parent)
        {
            CHECKPOINT_SETUP;
            if (!test_token(Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;

            auto result = allocate_base<Definition>(parent, AST::Node_Type::DEFINITION);
            result->is_comptime = false;
            result->assignment_type = AST::Assignment_Type::DEREFERENCE;
            result->symbols = dynamic_array_create<Definition_Symbol*>(1);
            result->types = dynamic_array_create<AST::Expression*>(1);
            result->values = dynamic_array_create<AST::Expression*>(1);

            // Parse comma seperated list of identifiers
            auto found_definition_operator = [](Token& token, void* unused) -> bool {
                return token.type == Token_Type::OPERATOR &&
                    (token.options.op == Operator::COLON ||
                        token.options.op == Operator::DEFINE_COMPTIME ||
                        token.options.op == Operator::DEFINE_INFER ||
                        token.options.op == Operator::DEFINE_INFER_POINTER ||
                        token.options.op == Operator::DEFINE_INFER_RAW ||
                        token.options.op == Operator::SEMI_COLON);
            };
            auto definition_add_symbol = [](Node* parent, Node* child) {
                dynamic_array_push_back(&downcast<Definition>(parent)->symbols, downcast<Definition_Symbol>(child));
            };
            auto definition_add_type = [](Node* parent, Node* child) {
                dynamic_array_push_back(&downcast<Definition>(parent)->types, downcast<Expression>(child));
            };
            auto definition_add_value = [](Node* parent, Node* child) {
                dynamic_array_push_back(&downcast<Definition>(parent)->values, downcast<Expression>(child));
            };
            parse_list_single_line(upcast(result), wrapper_parse_definition_symbol, definition_add_symbol, found_definition_operator, nullptr, Operator::COMMA);

            // Check if there is a colon :, or a := or an :: or :=*
            if (test_operator(Operator::COLON))
            {
                advance_token();

                // Note: Parse types (At least one value is guaranteed by parse_expression or error)
                auto found_value_start = [](Token& token, void* unused) -> bool {
                    return token.type == Token_Type::OPERATOR &&
                        (token.options.op == Operator::COLON ||
                         token.options.op == Operator::ASSIGN ||
                         token.options.op == Operator::ASSIGN_POINTER ||
                         token.options.op == Operator::ASSIGN_RAW ||
                         token.options.op == Operator::SEMI_COLON);
                };
                parse_list_single_line(upcast(result), wrapper_parse_expression_or_error, definition_add_type, found_value_start, nullptr, Operator::COMMA);

                if (test_operator(Operator::ASSIGN)) { // : ... =
                    result->is_comptime = false;
                    advance_token();
                }
                else if (test_operator(Operator::COLON)) { // : ... :
                    result->is_comptime = true;
                    advance_token();
                }
                else if (test_operator(Operator::ASSIGN_POINTER)) { // : ... =*
                    result->is_comptime = false;
                    result->assignment_type = Assignment_Type::POINTER;
                    advance_token();
                }
                else if (test_operator(Operator::ASSIGN_RAW)) { // : ... =~
                    result->is_comptime = false;
                    result->assignment_type = Assignment_Type::RAW;
                    advance_token();
                }
                else {
                    result->assignment_type = Assignment_Type::RAW; // If no values exist, then the definition is raw, e.g. x: *int
                    PARSE_SUCCESS(result);
                }
            }
            else if (test_operator(Operator::DEFINE_COMPTIME)) { // x :: 
                advance_token();
                result->is_comptime = true;
                result->assignment_type = Assignment_Type::DEREFERENCE;
            }
            else if (test_operator(Operator::DEFINE_INFER)) { // x :=
                advance_token();
                result->is_comptime = false;
                result->assignment_type = Assignment_Type::DEREFERENCE;
            }
            else if (test_operator(Operator::DEFINE_INFER_POINTER)) { // x:=*
                advance_token();
                result->is_comptime = false;
                result->assignment_type = Assignment_Type::POINTER;
            }
            else if (test_operator(Operator::DEFINE_INFER_RAW)) { // x:=-
                advance_token();
                result->is_comptime = false;
                result->assignment_type = Assignment_Type::RAW;
            }
            else {
                CHECKPOINT_EXIT;
            }
            
            // Parse values (Or add at least one error expression) 
            // Note: Not sure if I want to stop on semicolon, but I think I usually want to stop there
            parse_list_single_line(upcast(result), wrapper_parse_expression_or_error, definition_add_value, stop_at_semicolon, nullptr, Operator::COMMA);

            PARSE_SUCCESS(result);
        }

        Structure_Member_Node* parse_struct_member(Node* parent)
        {
            CHECKPOINT_SETUP;
            if (!test_token(Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;

            auto result = allocate_base<Structure_Member_Node>(parent, AST::Node_Type::STRUCT_MEMBER);
            result->is_expression = true;
            result->options.expression = nullptr;
            result->name = get_token()->options.identifier;
            advance_token();

            if (!test_operator(Operator::COLON)) {
                log_error_range_offset("Expected follow block or : after struct member name!", 0);
                auto error_expr = allocate_base<AST::Expression>(upcast(result), AST::Node_Type::EXPRESSION);
                error_expr->type = Expression_Type::ERROR_EXPR;
                node_finalize_range(upcast(error_expr));
                result->options.expression = error_expr;
                PARSE_SUCCESS(result);
            }
            advance_token();

            if (on_follow_block()) {
                result->is_expression = false;
                result->options.subtype_members = dynamic_array_create<Structure_Member_Node*>();
                auto add_to_subtype = [](Node* parent, Node* child) {
                    dynamic_array_push_back(&downcast<Structure_Member_Node>(parent)->options.subtype_members, downcast<Structure_Member_Node>(child));
                };
                parse_list_of_items(upcast(result), wrapper_parse_struct_member, add_to_subtype, Parenthesis_Type::BRACES, Operator::SEMI_COLON, true, false);
                PARSE_SUCCESS(result);
            }

            result->options.expression = parse_expression_or_error_expr(upcast(result));
            PARSE_SUCCESS(result);
        }

        AST::Node* parse_module_item(AST::Node* parent)
        {
            auto definition = parse_definition(parent);
            if (definition != 0) {
                return AST::upcast(definition);
            }
            auto import_node = parse_import(parent);
            if (import_node != 0) {
                return AST::upcast(import_node);
            }
            auto context_change = parse_context_change(parent);
            if (context_change != 0) {
                return AST::upcast(context_change);
            }
            auto extern_import = parse_extern_import(parent);
            if (extern_import != 0) {
                return AST::upcast(extern_import);
            }
            return nullptr;
        }

        Switch_Case* parse_switch_case(Node* parent)
        {
            auto result = allocate_base<Switch_Case>(parent, Node_Type::SWITCH_CASE);
            result->value = optional_make_failure<Expression*>();
            result->variable_definition = optional_make_failure<AST::Definition_Symbol*>();

            // Check for default case
            if (test_keyword(Keyword::DEFAULT)) {
                advance_token();
            }
            else
            {
                result->value.available = true;
                result->value.value = parse_expression_or_error_expr(upcast(result));
                if (test_operator(Operator::ARROW)) 
                {
                    advance_token();
                    if (test_token(Token_Type::IDENTIFIER)) {
                        result->variable_definition = optional_make_success(parse_definition_symbol(upcast(result)));
                    }
                    else {
                        log_error_range_offset("Expected identifier after arrow", 0);
                    }
                }
            }

            result->block = parse_code_block(&result->base, 0);
            // Set block label (Switch cases need special treatment because they 'Inherit' the label from the switch
            assert(parent->type == Node_Type::STATEMENT && ((Statement*)parent)->type == Statement_Type::SWITCH_STATEMENT, "");
            result->block->block_id = ((Statement*)parent)->options.switch_statement.label;
            PARSE_SUCCESS(result);
        }

        // This function exists because for loops only allow assignments (+ binop assign) + expression in their last part...
        AST::Statement* parse_assignment_or_expression_statement(Node* parent)
        {
            CHECKPOINT_SETUP;
            auto result = allocate_base<Statement>(parent, Node_Type::STATEMENT);

            auto expr = parse_expression(&result->base);
            if (expr == 0) {
                CHECKPOINT_EXIT;
            }

            auto add_to_left_side = [](Node* parent, Node* child) {
                dynamic_array_push_back(&downcast<Statement>(parent)->options.assignment.left_side, downcast<Expression>(child));
            };
            auto add_to_right_side = [](Node* parent, Node* child) {
                dynamic_array_push_back(&downcast<Statement>(parent)->options.assignment.right_side, downcast<Expression>(child));
            };

            if (test_operator(Operator::COMMA))
            {
                // Assume that it's a multi-assignment
                result->type = Statement_Type::ASSIGNMENT;
                result->options.assignment.left_side = dynamic_array_create<Expression*>(1);
                result->options.assignment.right_side = dynamic_array_create<Expression*>(1);
                result->options.assignment.type = Assignment_Type::DEREFERENCE;
                dynamic_array_push_back(&result->options.assignment.left_side, expr);
                advance_token();

                // Parse remaining left_side expressions
                auto is_assign = [](Token& token, void* userdata) -> bool {
                    return token.type == Token_Type::OPERATOR && 
                        (token.options.op == Operator::ASSIGN || token.options.op == Operator::ASSIGN_POINTER || token.options.op == Operator::ASSIGN_RAW || token.options.op == Operator::SEMI_COLON); 
                };
                parse_list_single_line(upcast(result), wrapper_parse_expression_or_error, add_to_left_side, is_assign, nullptr, Operator::COMMA);

                // Check if assignment found, otherwise error
                if (test_operator(Operator::ASSIGN)) {
                    result->options.assignment.type = Assignment_Type::DEREFERENCE;
                }
                else if (test_operator(Operator::ASSIGN_POINTER)) {
                    result->options.assignment.type = Assignment_Type::POINTER;
                }
                else if (test_operator(Operator::ASSIGN_RAW)) {
                    result->options.assignment.type = Assignment_Type::RAW;
                }
                else {
                    CHECKPOINT_EXIT;
                }
                advance_token();

                // Parse right side
                parse_list_single_line(upcast(result), wrapper_parse_expression_or_error, add_to_right_side, stop_at_semicolon, nullptr, Operator::COMMA);
                PARSE_SUCCESS(result);
            }
            else if (test_operator(Operator::ASSIGN) || test_operator(Operator::ASSIGN_POINTER) || test_operator(Operator::ASSIGN_RAW))
            {
                result->type = Statement_Type::ASSIGNMENT;
                result->options.assignment.left_side = dynamic_array_create<Expression*>(1);
                result->options.assignment.right_side = dynamic_array_create<Expression*>(1);
                if (test_operator(Operator::ASSIGN)) {
                    result->options.assignment.type = Assignment_Type::DEREFERENCE;
                }
                else if (test_operator(Operator::ASSIGN_POINTER)) {
                    result->options.assignment.type = Assignment_Type::POINTER;
                }
                else if (test_operator(Operator::ASSIGN_RAW)) {
                    result->options.assignment.type = Assignment_Type::RAW;
                }
                else {
                    panic("If should have handled this case");
                }
                dynamic_array_push_back(&result->options.assignment.left_side, expr);
                advance_token();

                // Parse right side
                parse_list_single_line(upcast(result), wrapper_parse_expression_or_error, add_to_right_side, stop_at_semicolon, nullptr, Operator::COMMA);
                PARSE_SUCCESS(result);
            }
            else if (test_operator(Operator::ASSIGN_ADD) || test_operator(Operator::ASSIGN_SUB) ||
                test_operator(Operator::ASSIGN_MULT) || test_operator(Operator::ASSIGN_DIV) || test_operator(Operator::ASSIGN_MODULO))
            {
                result->type = Statement_Type::BINOP_ASSIGNMENT;
                auto& assign = result->options.binop_assignment;
                if (test_operator(Operator::ASSIGN_ADD)) {
                    assign.binop = Binop::ADDITION;
                }
                else if (test_operator(Operator::ASSIGN_SUB)) {
                    assign.binop = Binop::SUBTRACTION;
                }
                else if (test_operator(Operator::ASSIGN_MULT)) {
                    assign.binop = Binop::MULTIPLICATION;
                }
                else if (test_operator(Operator::ASSIGN_DIV)) {
                    assign.binop = Binop::DIVISION;
                }
                else if (test_operator(Operator::ASSIGN_MODULO)) {
                    assign.binop = Binop::MODULO;
                }
                else {
                    panic("");
                }
                advance_token();

                assign.left_side = expr;
                assign.right_side = parse_expression_or_error_expr(upcast(result));
                PARSE_SUCCESS(result);
            }

            result->type = Statement_Type::EXPRESSION_STATEMENT;
            result->options.expression = expr;
            PARSE_SUCCESS(result);
        }

        AST::Statement* parse_statement(Node * parent)
        {
            CHECKPOINT_SETUP;
            auto result = allocate_base<Statement>(parent, Node_Type::STATEMENT);

            // Check for import
            if (test_keyword(Keyword::IMPORT)) {
                auto import = parse_import(upcast(result));
                if (import == 0) {
                    CHECKPOINT_EXIT;
                }
                result->type = Statement_Type::IMPORT;
                result->options.import_node = import;
                PARSE_SUCCESS(result);
            }

            {
                // Check for Anonymous Blocks with identifiers
                // INFO: This needs to be done before definition
                auto code = parser.code;
                auto& pos = parser.state.pos;

                bool next_line_has_indent = false;
                if (pos.line + 1 < code->line_count) {
                    if (source_code_get_line(code, pos.line + 1)->indentation > parser.state.line->indentation) {
                        next_line_has_indent = true;
                    }
                }

                if (test_token(Token_Type::IDENTIFIER) && test_operator_offset(Operator::COLON, 1) && next_line_has_indent)
                {
                    auto block_id = optional_make_success(get_token()->options.identifier);
                    Code_Block* block = nullptr;
                    if (remaining_line_tokens() == 2) {
                        block = parse_code_block(&result->base, 0);
                    }
                    else if (test_parenthesis_offset('{', 2)) {
                        advance_token();
                        advance_token();
                        block = parse_code_block(&result->base, 0);
                    }

                    if (block != nullptr) {
                        result->type = Statement_Type::BLOCK;
                        result->options.block = block;
                        result->options.block->block_id = block_id;
                        PARSE_SUCCESS(result);
                    }
                }
            }

            {
                auto definition = parse_definition(&result->base);
                if (definition != 0) {
                    result->type = Statement_Type::DEFINITION;
                    result->options.definition = definition;
                    PARSE_SUCCESS(result);
                }
            }

            if (test_token(Token_Type::KEYWORD))
            {
                switch (get_token(0)->options.keyword)
                {
                case Keyword::IF:
                {
                    advance_token();
                    result->type = Statement_Type::IF_STATEMENT;
                    auto& if_stat = result->options.if_statement;
                    if_stat.condition = parse_expression_or_error_expr(&result->base);
                    if_stat.block = parse_code_block(&result->base, upcast(if_stat.condition));
                    if_stat.else_block.available = false;

                    auto last_if_stat = result;
                    // Parse else-if chain
                    while (test_keyword_offset(Keyword::ELSE, 0) && test_keyword_offset(Keyword::IF, 1))
                    {
                        auto implicit_else_block = allocate_base<AST::Code_Block>(&last_if_stat->base, Node_Type::CODE_BLOCK);
                        implicit_else_block->statements = dynamic_array_create<Statement*>(1);
                        implicit_else_block->block_id = optional_make_failure<String*>();

                        auto new_if_stat = allocate_base<AST::Statement>(&last_if_stat->base, Node_Type::STATEMENT);
                        advance_token();
                        advance_token();
                        new_if_stat->type = Statement_Type::IF_STATEMENT;
                        auto& new_if = new_if_stat->options.if_statement;
                        new_if.condition = parse_expression_or_error_expr(&new_if_stat->base);
                        new_if.block = parse_code_block(&last_if_stat->base, upcast(new_if.condition));
                        new_if.else_block.available = false;
                        SET_END_RANGE(implicit_else_block);
                        SET_END_RANGE(new_if_stat);

                        dynamic_array_push_back(&implicit_else_block->statements, new_if_stat);
                        last_if_stat->options.if_statement.else_block = optional_make_success(implicit_else_block);
                        last_if_stat = new_if_stat;

                    }
                    if (test_keyword_offset(Keyword::ELSE, 0))
                    {
                        advance_token();
                        last_if_stat->options.if_statement.else_block = optional_make_success(parse_code_block(&last_if_stat->base, 0));
                    }
                    // NOTE: Here we return result, not last if stat
                    PARSE_SUCCESS(result);
                }
                case Keyword::WHILE:
                {
                    advance_token();
                    result->type = Statement_Type::WHILE_STATEMENT;
                    auto& loop = result->options.while_statement;
                    loop.condition = parse_expression_or_error_expr(&result->base);
                    loop.block = parse_code_block(&result->base, upcast(loop.condition));
                    PARSE_SUCCESS(result);
                }
                case Keyword::FOR:
                {
                    advance_token();
                    result->type = Statement_Type::FOR_LOOP;
                    bool error_logged = false;

                    // Parse variable name
                    AST::Definition_Symbol* loop_variable = parse_definition_symbol(upcast(result));
                    if (loop_variable == 0) {
                        auto definition_node = allocate_base<Definition_Symbol>(parent, Node_Type::DEFINITION_SYMBOL);
                        definition_node->name = compiler.identifier_pool.predefined_ids.invalid_symbol_name;
                        node_finalize_range(upcast(definition_node));
                        loop_variable = definition_node;
                        if (!error_logged) {
                            log_error_range_offset("Expected loop variable identifier", 0);
                            error_logged = true;
                        }
                    }

                    // Seperate for-each and normal for loop
                    if (test_operator(Operator::COMMA) || test_keyword(Keyword::IN_KEYWORD)) 
                    {
                        result->type = Statement_Type::FOREACH_LOOP;
                        auto& loop = result->options.foreach_loop;
                        loop.loop_variable_definition = loop_variable;

                        // Parse index variable
                        loop.index_variable_definition = optional_make_failure<AST::Definition_Symbol*>();
                        if (test_operator(Operator::COMMA)) 
                        {
                            loop.index_variable_definition.available = true;
                            advance_token();
                            loop.index_variable_definition.value = parse_definition_symbol(upcast(result));
                            if (loop.index_variable_definition.value == 0) {
                                auto definition_node = allocate_base<Definition_Symbol>(parent, Node_Type::DEFINITION_SYMBOL);
                                definition_node->name = compiler.identifier_pool.predefined_ids.invalid_symbol_name;
                                node_finalize_range(upcast(definition_node));
                                loop.index_variable_definition.value = definition_node;
                                if (!error_logged) {
                                    log_error_range_offset("Expected index variable identifier", 0);
                                    error_logged = true;
                                }
                            }
                        }

                        if (test_keyword(Keyword::IN_KEYWORD)) {
                            advance_token();
                        }
                        else {
                            if (!error_logged) {
                                log_error_range_offset("Expected in keyword for foreach loop", 0);
                                error_logged = true;
                            }
                        }

                        loop.expression = parse_expression_or_error_expr(upcast(result));

                        // Parse body
                        loop.body_block = parse_code_block(upcast(result), upcast(loop_variable));
                        if (!loop.body_block->block_id.available) {
                            loop.body_block->block_id = optional_make_success(loop.loop_variable_definition->name);
                        }
                        PARSE_SUCCESS(result);
                    }
                    else
                    {
                        auto& loop = result->options.for_loop;
                        loop.loop_variable_definition = loop_variable;

                        // Returns true if semicolon was found (Otherwise no semicolon until end of line...)
                        auto to_next_semicolon = [&]()->bool {
                            if (test_operator(Operator::SEMI_COLON)) {
                                return true;
                            }
                            auto pos_opt = search_token(
                                parser.code,
                                parser.state.pos,
                                [](Token* token, void* userdata) -> bool {
                                    return token->type == Token_Type::OPERATOR && token->options.op == Operator::SEMI_COLON;
                                },
                                nullptr
                            );
                            if (!pos_opt.available) return false;
                            log_error_to_pos("Un-parsable tokens until expected semicolon", pos_opt.value);
                            error_logged = true;
                            parser.state.pos = pos_opt.value;
                            return true;
                        };

                        // Parse initial value
                        if (test_operator(Operator::DEFINE_INFER)) {
                            loop.loop_variable_type.available = false;
                            advance_token();
                            loop.initial_value = parse_expression_or_error_expr(upcast(result));
                        }
                        else if (test_operator(Operator::COLON)) {
                            advance_token();
                            loop.loop_variable_type.available = true;
                            loop.loop_variable_type.value = parse_expression_or_error_expr(upcast(result));

                            if (test_operator(Operator::ASSIGN)) {
                                advance_token();
                                loop.initial_value = parse_expression_or_error_expr(upcast(result));
                            }
                            else {
                                if (!error_logged) {
                                    log_error_range_offset("Expected initial value assignment (e.g. = 5) in for loop", 0);
                                    error_logged = true;
                                }
                                auto error_expr = allocate_base<AST::Expression>(parent, AST::Node_Type::EXPRESSION);
                                error_expr->type = Expression_Type::ERROR_EXPR;
                                node_finalize_range(upcast(error_expr));
                                loop.initial_value = error_expr;
                            }
                        }
                        else {
                            // Set initial value to error-expr, find next semicolon or exit...
                            auto error_expr = allocate_base<AST::Expression>(parent, AST::Node_Type::EXPRESSION);
                            error_expr->type = Expression_Type::ERROR_EXPR;
                            node_finalize_range(upcast(error_expr));
                            loop.loop_variable_type.available = false;
                            loop.initial_value = error_expr;
                        }

                        // Parse loop condition
                        if (to_next_semicolon()) {
                            advance_token();
                            loop.condition = parse_expression_or_error_expr(upcast(result));
                        }
                        else {
                            if (!error_logged) {
                                log_error_range_offset("Expected Semicolon", 0);
                                error_logged = true;
                            }
                            auto error_expr = allocate_base<AST::Expression>(parent, AST::Node_Type::EXPRESSION);
                            error_expr->type = Expression_Type::ERROR_EXPR;
                            node_finalize_range(upcast(error_expr));
                            loop.condition = error_expr;
                        }

                        // Parse iteration step
                        if (to_next_semicolon()) {
                            advance_token();
                            loop.increment_statement = parse_assignment_or_expression_statement(upcast(result));
                        }
                        else {
                            loop.increment_statement = 0;
                            if (!error_logged) {
                                log_error_range_offset("Expected Semicolon", 0);
                                error_logged = true;
                            }
                        }

                        if (loop.increment_statement == 0) {
                            if (!error_logged) {
                                log_error_range_offset("Expected expression or assignment as for-loop increment", 0);
                                error_logged = true;
                            }
                            auto error_expr = allocate_base<AST::Expression>(parent, AST::Node_Type::EXPRESSION);
                            error_expr->type = Expression_Type::ERROR_EXPR;
                            node_finalize_range(upcast(error_expr));

                            auto error_statement = allocate_base<AST::Statement>(parent, AST::Node_Type::STATEMENT);
                            error_statement->type = Statement_Type::EXPRESSION_STATEMENT;
                            error_statement->options.expression = error_expr;
                            node_finalize_range(upcast(error_statement));

                            loop.increment_statement = error_statement;
                        }

                        // Parse body
                        loop.body_block = parse_code_block(upcast(result), upcast(loop_variable));
                        if (!loop.body_block->block_id.available) {
                            loop.body_block->block_id = optional_make_success(loop.loop_variable_definition->name);
                        }
                        PARSE_SUCCESS(result);
                    }
                    panic("");
                    CHECKPOINT_EXIT;
                }
                case Keyword::DEFER:
                {
                    advance_token();
                    result->type = Statement_Type::DEFER;
                    result->options.defer_block = parse_code_block(&result->base, 0);
                    PARSE_SUCCESS(result);
                }
                case Keyword::DEFER_RESTORE:
                {
                    advance_token();
                    result->type = Statement_Type::DEFER_RESTORE;
                    result->options.defer_restore.left_side = parse_expression_or_error_expr(upcast(result));
                    Assignment_Type assignment_type = (Assignment_Type) -1;
                    if (test_operator(Operator::ASSIGN_POINTER)) {
                        assignment_type = Assignment_Type::POINTER;
                    }
                    else if (test_operator(Operator::ASSIGN)) {
                        assignment_type = Assignment_Type::DEREFERENCE;
                    }
                    else if (test_operator(Operator::ASSIGN_RAW)) {
                        assignment_type = Assignment_Type::RAW;
                    }

                    if ((int)assignment_type == -1) {
                        auto error_expr = allocate_base<AST::Expression>(upcast(result), AST::Node_Type::EXPRESSION);
                        error_expr->type = Expression_Type::ERROR_EXPR;
                        node_finalize_range(upcast(error_expr));
                        result->options.defer_restore.assignment_type = Assignment_Type::RAW;
                        result->options.defer_restore.right_side = error_expr;
                        PARSE_SUCCESS(result);
                    }

                    advance_token();
                    result->options.defer_restore.assignment_type = assignment_type;
                    result->options.defer_restore.right_side = parse_expression_or_error_expr(upcast(result));
                    PARSE_SUCCESS(result);
                }
                case Keyword::SWITCH:
                {
                    advance_token();
                    result->type = Statement_Type::SWITCH_STATEMENT;
                    auto& switch_stat = result->options.switch_statement;
                    switch_stat.condition = parse_expression_or_error_expr(&result->base);
                    switch_stat.cases = dynamic_array_create<Switch_Case*>(1);
                    switch_stat.label = parse_block_label_or_use_related_node_id(upcast(switch_stat.condition));
                    auto add_to_switch = [](Node* parent, Node* child) {
                        dynamic_array_push_back(&downcast<Statement>(parent)->options.switch_statement.cases, downcast<Switch_Case>(child));
                    };
                    parse_list_of_items(upcast(result), wrapper_parse_switch_case, add_to_switch, Parenthesis_Type::BRACES, Operator::SEMI_COLON, true, false);
                    PARSE_SUCCESS(result);
                }
                case Keyword::DELETE_KEYWORD: {
                    advance_token();
                    result->type = Statement_Type::DELETE_STATEMENT;
                    result->options.delete_expr = parse_expression_or_error_expr(&result->base);
                    PARSE_SUCCESS(result);
                }
                case Keyword::RETURN: {
                    advance_token();
                    result->type = Statement_Type::RETURN_STATEMENT;
                    auto expr = parse_expression(&result->base);
                    result->options.return_value.available = false;
                    if (expr != 0) {
                        result->options.return_value = optional_make_success(expr);
                    }
                    PARSE_SUCCESS(result);
                }
                case Keyword::CONTINUE: {
                    advance_token();
                    result->type = Statement_Type::CONTINUE_STATEMENT;
                    if (!test_token(Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;
                    result->options.continue_name = get_token(0)->options.identifier;
                    advance_token();
                    PARSE_SUCCESS(result);
                }
                case Keyword::BREAK: {
                    advance_token();
                    result->type = Statement_Type::BREAK_STATEMENT;
                    if (!test_token(Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;
                    result->options.break_name = get_token(0)->options.identifier;
                    advance_token();
                    PARSE_SUCCESS(result);
                }
                }
            }

            // Otherwise try to parse expression or assignment statement
            {
                // Delete result, which is an allocated statement node
                result->type = Statement_Type::IMPORT; // So that rollback doesn't read uninitialized values
                parser_rollback(checkpoint);

                return parse_assignment_or_expression_statement(parent);
            }
        }

        AST::Node* parse_statement_or_context_change(Node* parent)
        {
            // Check for context change
            auto context_change = parse_context_change(parent);
            if (context_change != 0) {
                return upcast(context_change);
            }

            return upcast(parse_statement(parent));
        }

        Enum_Member_Node* parse_enum_member(Node* parent)
        {
            if (!test_token(Token_Type::IDENTIFIER)) {
                return 0;
            }

            CHECKPOINT_SETUP;
            auto result = allocate_base<Enum_Member_Node>(parent, Node_Type::ENUM_MEMBER);
            result->name = get_token()->options.identifier;
            advance_token();
            if (test_operator(Operator::DEFINE_COMPTIME)) {
                advance_token();
                result->value = optional_make_success(parse_expression_or_error_expr(&result->base));
            }
            PARSE_SUCCESS(result);
        }
    };

    // Parsing
    Optional<String*> parse_block_label_or_use_related_node_id(AST::Node* related_node)
    {
        Optional<String*> result = optional_make_failure<String*>();
        if (test_token(Token_Type::IDENTIFIER), test_operator_offset(Operator::COLON, 1)) {
            result = optional_make_success(get_token(0)->options.identifier);
            advance_token();
            advance_token();
        }
        else if (related_node != 0) 
        {
            if (related_node->type == AST::Node_Type::EXPRESSION) {
                auto expr = downcast<AST::Expression>(related_node);
                if (expr->type == AST::Expression_Type::PATH_LOOKUP) {
                    result = optional_make_success<String*>(expr->options.path_lookup->last->name);
                }
            }
            else if (related_node->type == AST::Node_Type::DEFINITION_SYMBOL) {
                result = optional_make_success<String*>(downcast<AST::Definition_Symbol>(related_node)->name);
            }
        }
        return result;
    }

    // Always returns success, but if there is no follow block, there are errors
    Code_Block* parse_code_block(Node* parent, AST::Node* related_node)
    {
        auto result = allocate_base<Code_Block>(parent, Node_Type::CODE_BLOCK);
        result->statements = dynamic_array_create<Statement*>();
        result->context_changes = dynamic_array_create<Context_Change*>();
        result->block_id = parse_block_label_or_use_related_node_id(related_node);
        parse_list_of_items(
            upcast(result), wrapper_parse_statement_or_context_change, code_block_add_child, Parenthesis_Type::BRACES, Operator::SEMI_COLON, true, true
        );
        PARSE_SUCCESS(result);
    }

    Argument* parse_argument(Node* parent)
    {
        CHECKPOINT_SETUP;
        auto result = allocate_base<Argument>(parent, Node_Type::ARGUMENT);
        if (test_token(Token_Type::IDENTIFIER) && test_operator_offset(Operator::ASSIGN, 1)) {
            result->name = optional_make_success(get_token()->options.identifier);
            advance_token();
            advance_token();
            result->value = parse_expression_or_error_expr(&result->base);
            PARSE_SUCCESS(result);
        }
        result->value = parse_expression_or_error_expr(&result->base);
        // Note: This only works because parse arguments is only called in once in arguments node
        // result->value = parse_expression(&result->base);
        // if (result->value == 0) CHECKPOINT_EXIT;
        PARSE_SUCCESS(result);
    }

    Subtype_Initializer* parse_subtype_initializer(Node* parent)
    {
        if (!(test_operator(Operator::DOT) &&
            ((test_token_offset(Token_Type::IDENTIFIER, 1) && test_operator_offset(Operator::ASSIGN, 2)) ||
             (test_operator_offset(Operator::ASSIGN, 1))))) 
        {
            return 0;
        }

        CHECKPOINT_SETUP;
        auto result = allocate_base<Subtype_Initializer>(parent, Node_Type::SUBTYPE_INITIALIZER);
        advance_token();

        // Parser name if available
        result->name.available = false;
        if (test_token(Token_Type::IDENTIFIER)) {
            result->name = optional_make_success(get_token(0)->options.identifier);
            advance_token();
        }

        assert(test_operator(Operator::ASSIGN), "Should be true after previous if"); 
        advance_token();  // Skip =

        result->arguments = parse_arguments(upcast(result), Parenthesis_Type::BRACES);
        PARSE_SUCCESS(result);
    }

    Parameter* parse_parameter(Node* parent)
    {
        CHECKPOINT_SETUP;
        auto result = allocate_base<Parameter>(parent, Node_Type::PARAMETER);
        result->is_comptime = false;
        result->default_value.available = false;
        result->is_mutable = false;
        if (test_operator(Operator::DOLLAR)) {
            result->is_comptime = true;
            advance_token();
        }
        else if (test_keyword(Keyword::MUTABLE)) {
            result->is_mutable = true;
            advance_token();
        }

        if (!test_token(Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;
        result->name = get_token(0)->options.identifier;
        advance_token();

        if (!test_operator(Operator::COLON)) CHECKPOINT_EXIT;
        advance_token();
        result->type = parse_expression_or_error_expr((Node*)result);

        if (test_operator(Operator::ASSIGN)) {
            advance_token();
            result->default_value.value = parse_expression_or_error_expr((Node*)result);
            if (result->default_value.value != 0) {
                result->default_value.available = true;
            }
        }
        PARSE_SUCCESS(result);
    }

    Path_Lookup* parse_path_lookup(Node* parent)
    {
        CHECKPOINT_SETUP;
        if (!test_token(Token_Type::IDENTIFIER)) {
            CHECKPOINT_EXIT;
        }

        Path_Lookup* path = allocate_base<Path_Lookup>(parent, Node_Type::PATH_LOOKUP);
        path->parts = dynamic_array_create<Symbol_Lookup*>(1);

        while (true)
        {
            // Add symbol-lookup Node
            Symbol_Lookup* lookup = allocate_base<Symbol_Lookup>(upcast(path), Node_Type::SYMBOL_LOOKUP);
            dynamic_array_push_back(&path->parts, lookup);

            // Check if we actually have an identifier, or if it's an 'empty' path, like "A~B~"
            // INFO: We have this empty path so that the syntax editor is able to show that a node is missing here!
            if (test_token(Token_Type::IDENTIFIER)) {
                lookup->name = get_token()->options.identifier;
                advance_token();
            }
            else {
                log_error("Expected identifier", token_range_make_offset(parser.state.pos, -1)); // Put error on the ~
                lookup->name = compiler.identifier_pool.predefined_ids.empty_string;
            }
            SET_END_RANGE(lookup);

            if (test_operator(Operator::TILDE)) {
                advance_token();
            }
            else {
                break;
            }
        }

        path->last = path->parts[path->parts.size - 1];
        PARSE_SUCCESS(path);
    }

    Expression* parse_single_expression_no_postop(Node* parent)
    {
        // DOCU: Parses Pre-Ops + Bases, but no postops
        // --*x   -> is 3 pre-ops + base x
        CHECKPOINT_SETUP;
        auto result = allocate_base<Expression>(parent, Node_Type::EXPRESSION);

        // Unops
        if (test_token(Token_Type::OPERATOR))
        {
            if (test_operator(Operator::QUESTION_MARK)) {
                advance_token();
                result->type = Expression_Type::OPTIONAL_TYPE;
                result->options.optional_child_type= parse_single_expression_or_error(&result->base);
                PARSE_SUCCESS(result);
            }
            else if (test_operator(Operator::OPTIONAL_POINTER)) {
                advance_token();
                result->type = Expression_Type::OPTIONAL_POINTER;
                result->options.optional_pointer_child_type = parse_single_expression_or_error(&result->base);
                PARSE_SUCCESS(result);
            }

            Unop unop;
            bool valid = true;
            switch (get_token(0)->options.op)
            {
            case Operator::SUBTRACTION: unop = Unop::NEGATE; break;
            case Operator::NOT: unop = Unop::NOT; break;
            case Operator::AMPERSAND: unop = Unop::DEREFERENCE; break;
            case Operator::MULTIPLY: unop = Unop::POINTER; break;
            default: valid = false;
            }
            if (valid) {
                advance_token();
                result->type = Expression_Type::UNARY_OPERATION;
                result->options.unop.type = unop;
                result->options.unop.expr = parse_single_expression_or_error(&result->base);
                PARSE_SUCCESS(result);
            }
        }

        if (test_keyword_offset(Keyword::BAKE, 0))
        {
            advance_token();
            if (on_follow_block()) {
                result->type = Expression_Type::BAKE_BLOCK;
                result->options.bake_block = parse_code_block(&result->base, 0);
                PARSE_SUCCESS(result);
            }
            result->type = Expression_Type::BAKE_EXPR;
            result->options.bake_expr = parse_single_expression_or_error(&result->base);
            PARSE_SUCCESS(result);
        }

        if (test_keyword(Keyword::INSTANCIATE))
        {
            advance_token();
            result->type = Expression_Type::INSTANCIATE;
            result->options.instanciate_expr = parse_expression_or_error_expr(upcast(result));
            PARSE_SUCCESS(result);
        }

        // Casts
        if (test_keyword(Keyword::CAST) || test_keyword(Keyword::CAST_POINTER))
        {
            result->type = Expression_Type::CAST;
            auto& cast = result->options.cast;
            cast.is_pointer_cast = test_keyword(Keyword::CAST_POINTER);
            advance_token();
            cast.to_type.available = false;
            if (test_parenthesis_offset('{', 0))
            {
                advance_token();
                cast.to_type = optional_make_success(parse_single_expression_or_error(&result->base));
                if (!finish_parenthesis<Parenthesis_Type::BRACES>()) CHECKPOINT_EXIT;
            }

            cast.operand = parse_single_expression_or_error(&result->base);
            PARSE_SUCCESS(result);
        }

        // Array/Slice
        if (test_parenthesis_offset('[', 0))
        {
            advance_token();
            if (test_parenthesis_offset(']', 0)) {
                advance_token();
                result->type = Expression_Type::SLICE_TYPE;
                result->options.slice_type = parse_single_expression_or_error(&result->base);
                PARSE_SUCCESS(result);
            }

            result->type = Expression_Type::ARRAY_TYPE;
            result->options.array_type.size_expr = parse_expression_or_error_expr(&result->base);
            if (!finish_parenthesis<Parenthesis_Type::BRACKETS>()) CHECKPOINT_EXIT;
            result->options.array_type.type_expr = parse_single_expression_or_error(&result->base);
            PARSE_SUCCESS(result);
        }

        if (test_keyword(Keyword::CONST_KEYWORD)) {
            advance_token();
            result->type = Expression_Type::CONST_TYPE;
            result->options.const_type = parse_expression_or_error_expr(&result->base);
            PARSE_SUCCESS(result);
        }

        // Path/Identifier
        if (test_token(Token_Type::IDENTIFIER))
        {
            result->type = Expression_Type::PATH_LOOKUP;
            result->options.path_lookup = parse_path_lookup(upcast(result)); // Note: parsing path cannot fail in this case
            PARSE_SUCCESS(result);
        }

        if (test_operator(Operator::DOLLAR) && test_token_offset(Token_Type::IDENTIFIER, 1)) {
            result->type = Expression_Type::TEMPLATE_PARAMETER;
            advance_token();
            result->options.polymorphic_symbol_id = get_token()->options.identifier;
            advance_token();
            PARSE_SUCCESS(result);
        }

        // Auto operators
        if (test_operator(Operator::DOT))
        {
            advance_token();
            if (test_parenthesis_offset('{', 0)) // Struct Initializer
            {
                result->type = Expression_Type::STRUCT_INITIALIZER;
                auto& init = result->options.struct_initializer;
                init.type_expr = optional_make_failure<Expression*>();
                init.arguments = parse_arguments(upcast(result), Parenthesis_Type::BRACES);
                PARSE_SUCCESS(result);
            }
            else if (test_parenthesis_offset('[', 0)) // Array Initializer
            {
                result->type = Expression_Type::ARRAY_INITIALIZER;
                auto& init = result->options.array_initializer;
                init.type_expr = optional_make_failure<Expression*>();
                init.values = dynamic_array_create<Expression*>(1);
                auto add_to_values = [](Node* parent, Node* child) {
                    dynamic_array_push_back(&downcast<Expression>(parent)->options.array_initializer.values, downcast<Expression>(child));
                };
                parse_list_of_items(upcast(result), wrapper_parse_expression_or_error, add_to_values, Parenthesis_Type::BRACKETS, Operator::COMMA, false, false);
                PARSE_SUCCESS(result);
            }
            else
            {
                result->type = Expression_Type::AUTO_ENUM;
                if (test_token(Token_Type::IDENTIFIER)) {
                    result->options.auto_enum = get_token(0)->options.identifier;
                    advance_token();
                }
                else {
                    log_error("Missing member name", token_range_make_offset(parser.state.pos, -1));
                    result->options.auto_enum = compiler.identifier_pool.predefined_ids.empty_string;
                }
                PARSE_SUCCESS(result);
            }
            CHECKPOINT_EXIT;
        }

        // Literals
        if (test_token(Token_Type::LITERAL))
        {
            result->type = Expression_Type::LITERAL_READ;
            result->options.literal_read = get_token(0)->options.literal_value;
            advance_token();
            PARSE_SUCCESS(result);
        }

        // Parse functions + signatures
        if (test_parenthesis_offset('(', 0) && (
            test_parenthesis_offset(')', 1) ||
            (test_token_offset(Token_Type::IDENTIFIER, 1) && test_operator_offset(Operator::COLON, 2)) ||
            (test_operator_offset(Operator::DOLLAR, 1) && test_token_offset(Token_Type::IDENTIFIER, 2)) ||
            (test_keyword_offset(Keyword::MUTABLE, 1) && test_token_offset(Token_Type::IDENTIFIER, 2))
            ))
        {
            result->type = Expression_Type::FUNCTION_SIGNATURE;
            auto& signature = result->options.function_signature;
            signature.parameters = dynamic_array_create<Parameter*>(1);
            signature.return_value.available = false;
            auto add_parameter = [](Node* parent, Node* child) {
                dynamic_array_push_back(&downcast<Expression>(parent)->options.function_signature.parameters, downcast<Parameter>(child));
            };
            parse_list_of_items(
                upcast(result), wrapper_parse_parameter, add_parameter, Parenthesis_Type::PARENTHESIS, Operator::COMMA, false, false
            );


            // Parse Return value
            if (test_operator(Operator::ARROW)) {
                advance_token();
                signature.return_value = optional_make_success(parse_expression_or_error_expr((Node*)result));
            }

            // Check if its a function or just a function signature
            if (!on_follow_block()) {
                PARSE_SUCCESS(result);
            }

            auto signature_expr = result;
            SET_END_RANGE(signature_expr);

            result = allocate_base<Expression>(parent, Node_Type::EXPRESSION);
            result->base.range.start = signature_expr->base.range.start;
            result->type = Expression_Type::FUNCTION;
            auto& function = result->options.function;
            function.body = parse_code_block(&result->base, 0);
            function.signature = signature_expr;
            signature_expr->base.parent = (Node*)result;
            PARSE_SUCCESS(result);
        }

        if (test_parenthesis_offset('(', 0))
        {
            parser_rollback(checkpoint); // This is pretty stupid, but needed to reset result
            advance_token();
            result = parse_expression_or_error_expr(parent);
            if (!finish_parenthesis<Parenthesis_Type::PARENTHESIS>()) CHECKPOINT_EXIT;
            PARSE_SUCCESS(result);
        }

        // Keyword expressions
        if (test_keyword_offset(Keyword::NEW, 0))
        {
            result->type = Expression_Type::NEW_EXPR;
            result->options.new_expr.count_expr.available = false;
            advance_token();
            if (test_parenthesis_offset('(', 0)) {
                advance_token();
                result->options.new_expr.count_expr = optional_make_success(parse_expression_or_error_expr(&result->base));
                if (!finish_parenthesis<Parenthesis_Type::PARENTHESIS>()) CHECKPOINT_EXIT;
            }
            result->options.new_expr.type_expr = parse_expression_or_error_expr(&result->base);
            PARSE_SUCCESS(result);
        }
        if (test_keyword_offset(Keyword::STRUCT, 0) || test_keyword_offset(Keyword::UNION, 0))
        {
            result->type = Expression_Type::STRUCTURE_TYPE;
            result->options.structure.members = dynamic_array_create<Structure_Member_Node*>();
            result->options.structure.parameters = dynamic_array_create<Parameter*>();
            if (test_keyword_offset(Keyword::STRUCT, 0)) {
                result->options.structure.type = AST::Structure_Type::STRUCT;
            }
            else {
                result->options.structure.type = AST::Structure_Type::UNION;
            }
            advance_token();

            // Parse struct parameters
            if (test_parenthesis('(')) {
                auto add_parameter = [](Node* parent, Node* child) {
                    dynamic_array_push_back(&downcast<Expression>(parent)->options.structure.parameters, downcast<Parameter>(child));
                };
                parse_list_of_items(
                    upcast(result), wrapper_parse_parameter, add_parameter, Parenthesis_Type::PARENTHESIS, Operator::COMMA, false, false
                );
            }

            auto add_member = [](Node* parent, Node* child) {
                dynamic_array_push_back(&downcast<Expression>(parent)->options.structure.members, downcast<Structure_Member_Node>(child));
            };
            parse_list_of_items(
                upcast(result), wrapper_parse_struct_member, add_member, Parenthesis_Type::BRACES, Operator::SEMI_COLON, true, false
            );
            PARSE_SUCCESS(result);
        }
        if (test_keyword_offset(Keyword::ENUM, 0)) {
            result->type = Expression_Type::ENUM_TYPE;
            result->options.enum_members = dynamic_array_create<Enum_Member_Node*>();
            advance_token();
            auto add_member = [](Node* parent, Node* child) {
                dynamic_array_push_back(&downcast<Expression>(parent)->options.enum_members, downcast<Enum_Member_Node>(child));
            };
            parse_list_of_items(
                upcast(result), wrapper_parse_enum_member, add_member, Parenthesis_Type::BRACES, Operator::SEMI_COLON, true, false
            );
            PARSE_SUCCESS(result);
        }
        if (test_keyword_offset(Keyword::MODULE, 0)) 
        {
            auto module = allocate_base<Module>(&result->base, Node_Type::MODULE);
            module->definitions = dynamic_array_create<Definition*>(1);
            module->import_nodes = dynamic_array_create<Import*>(1);
            module->context_changes = dynamic_array_create<Context_Change*>(1);
            advance_token();
            parse_list_of_items(
                upcast(module), wrapper_parse_module_item, module_add_child, Parenthesis_Type::BRACES, Operator::SEMI_COLON, true, false
            );
            node_finalize_range(AST::upcast(module));

            result->type = Expression_Type::MODULE;
            result->options.module = module;
            PARSE_SUCCESS(result);
        }

        CHECKPOINT_EXIT;
    }

    Expression* parse_post_operator_internal(Expression* child)
    {
        // DOCU: Internal means that we don't add the result of this expression to the parameter child,
        //       but rather to the parent of the child
        CHECKPOINT_SETUP;
        // Post operators
        auto result = allocate_base<Expression>(child->base.parent, Node_Type::EXPRESSION);
        if (test_operator(Operator::DOT))
        {
            advance_token();
            if (test_parenthesis_offset('{', 0)) // Struct Initializer
            {
                result->type = Expression_Type::STRUCT_INITIALIZER;
                auto& init = result->options.struct_initializer;
                init.type_expr = optional_make_success(child);
                init.arguments = parse_arguments(upcast(result), Parenthesis_Type::BRACES);
                PARSE_SUCCESS(result);
            }
            else if (test_parenthesis_offset('[', 0)) // Array Initializer
            {
                result->type = Expression_Type::ARRAY_INITIALIZER;
                auto& init = result->options.array_initializer;
                init.type_expr = optional_make_success(child);
                init.values = dynamic_array_create<Expression*>(1);
                auto add_to_values = [](Node* parent, Node* child) {
                    dynamic_array_push_back(&downcast<Expression>(parent)->options.array_initializer.values, downcast<Expression>(child));
                };
                parse_list_of_items(upcast(result), wrapper_parse_expression_or_error, add_to_values, Parenthesis_Type::BRACKETS, Operator::COMMA, false, false);
                PARSE_SUCCESS(result);
            }
            else
            {
                result->type = Expression_Type::MEMBER_ACCESS;
                result->options.member_access.expr = child;
                if (test_token(Token_Type::IDENTIFIER)) {
                    result->options.member_access.name = get_token(0)->options.identifier;
                    advance_token();
                }
                else {
                    log_error("Missing member name", token_range_make_offset(parser.state.pos, -1));
                    result->options.member_access.name = compiler.identifier_pool.predefined_ids.empty_string;
                }
                PARSE_SUCCESS(result);
            }
            CHECKPOINT_EXIT;
        }
        else if (test_parenthesis_offset('[', 0)) // Array access
        {
            advance_token();
            result->type = Expression_Type::ARRAY_ACCESS;
            result->options.array_access.array_expr = child;
            result->options.array_access.index_expr = parse_expression_or_error_expr(&result->base);
            if (!finish_parenthesis<Parenthesis_Type::BRACKETS>()) CHECKPOINT_EXIT;
            PARSE_SUCCESS(result);
        }
        else if (test_operator(Operator::QUESTION_MARK)) 
        {
            advance_token();
            result->type = Expression_Type::OPTIONAL_CHECK;
            result->options.optional_check_value = child;
            PARSE_SUCCESS(result);
        }
        else if (test_parenthesis_offset('(', 0)) // Function call
        {
            result->type = Expression_Type::FUNCTION_CALL;
            auto& call = result->options.call;
            call.expr = child;
            call.arguments = parse_arguments(upcast(result), Parenthesis_Type::PARENTHESIS);
            PARSE_SUCCESS(result);
        }
        CHECKPOINT_EXIT;
    }

    Expression* parse_single_expression(Node* parent)
    {
        // DOCU: This function parses: Pre-Ops + Node + Post-Op
        Expression* child = parse_single_expression_no_postop(parent);
        if (child == 0) return child;
        Expression* post_op = parse_post_operator_internal(child);
        while (post_op != 0) {
            child->base.parent = &post_op->base;
            child = post_op;
            post_op = parse_post_operator_internal(child);
        }
        return child;
    }

    Expression* parse_single_expression_or_error(Node* parent)
    {
        auto expr = parse_single_expression(parent);
        if (expr != 0) {
            return expr;
        }
        log_error_range_offset("Expected Single Expression", 0);
        expr = allocate_base<AST::Expression>(parent, AST::Node_Type::EXPRESSION);
        expr->type = Expression_Type::ERROR_EXPR;
        PARSE_SUCCESS(expr);
    }

    Expression* parse_priority_level(Expression* expr, int priority_level, Dynamic_Array<Binop_Link>* links, int* index)
    {
        while (*index < links->size)
        {
            auto& link = (*links)[*index];
            auto op_prio = binop_priority(link.binop);
            if (op_prio > priority_level) {
                expr = parse_priority_level(expr, priority_level + 1, links, index);
            }
            else if (op_prio == priority_level) {
                *index = *index + 1;
                Expression* result = allocate_base<Expression>(0, Node_Type::EXPRESSION);
                result->type = Expression_Type::BINARY_OPERATION;
                result->options.binop.type = link.binop;
                result->options.binop.left = expr;
                result->options.binop.right = parse_priority_level(link.expr, priority_level + 1, links, index);
                result->options.binop.left->base.parent = &result->base;
                result->options.binop.right->base.parent = &result->base;
                expr = result;

                auto& range = result->base.range;
                range.start = link.token_index; 
                range.end = link.token_index;
                range.end.token += 1;
                node_calculate_bounding_range(AST::upcast(result->options.binop.left));
                node_calculate_bounding_range(AST::upcast(result));
            }
            else {
                break;
            }
        }
        return expr;
    }

    Expression* parse_expression(Node* parent)
    {
        CHECKPOINT_SETUP;
        Expression* start_expr = parse_single_expression(parent);
        if (start_expr == 0) return 0;

        Dynamic_Array<Binop_Link> links = dynamic_array_create<Binop_Link>(1);
        SCOPE_EXIT(dynamic_array_destroy(&links));
        while (true)
        {
            Binop_Link link;
            link.binop = Binop::INVALID;
            link.token_index = parser.state.pos;
            if (test_token(Token_Type::OPERATOR))
            {
                switch (get_token(0)->options.op)
                {
                case Operator::ADDITION: link.binop = Binop::ADDITION; break;
                case Operator::SUBTRACTION: link.binop = Binop::SUBTRACTION; break;
                case Operator::MULTIPLY:link.binop = Binop::MULTIPLICATION; break;
                case Operator::DIVISON:link.binop = Binop::DIVISION; break;
                case Operator::MODULO:link.binop = Binop::MODULO; break;
                case Operator::AND:link.binop = Binop::AND; break;
                case Operator::OR:link.binop = Binop::OR; break;
                case Operator::GREATER_THAN:link.binop = Binop::GREATER; break;
                case Operator::GREATER_EQUAL:link.binop = Binop::GREATER_OR_EQUAL; break;
                case Operator::LESS_THAN:link.binop = Binop::LESS; break;
                case Operator::LESS_EQUAL:link.binop = Binop::LESS_OR_EQUAL; break;
                case Operator::EQUALS:link.binop = Binop::EQUAL; break;
                case Operator::NOT_EQUALS:link.binop = Binop::NOT_EQUAL; break;
                case Operator::POINTER_EQUALS:link.binop = Binop::POINTER_EQUAL; break;
                case Operator::POINTER_NOT_EQUALS:link.binop = Binop::POINTER_NOT_EQUAL; break;
                }
                if (link.binop != Binop::INVALID) {
                    advance_token();
                }
            }
            if (link.binop == Binop::INVALID) {
                break;
            }
            link.expr = parse_single_expression_or_error(parent);
            dynamic_array_push_back(&links, link);
        }

        // Now build the overload tree
        if (links.size == 0) PARSE_SUCCESS(start_expr);
        int index = 0;
        Expression* result = parse_priority_level(start_expr, 0, &links, &index);
        result->base.parent = parent;
        return result; // INFO: Don't use PARSE SUCCESS, since this would overwrite the token-ranges set by parse_priority_level
    }

    Expression* parse_expression_or_error_expr(Node* parent)
    {
        auto expr = parse_expression(parent);
        if (expr != 0) {
            return expr;
        }
        log_error_range_offset("Expected Expression", 0);
        expr = allocate_base<AST::Expression>(parent, AST::Node_Type::EXPRESSION);

        expr->type = Expression_Type::ERROR_EXPR;
        PARSE_SUCCESS(expr);
    }

#undef CHECKPOINT_EXIT
#undef CHECKPOINT_SETUP
#undef PARSE_SUCCESS
#undef SET_END_RANGE

    void parse_root()
    {
        auto code = parser.code;

        parser.state.pos = token_index_make(0, 0);
        parser.state.line = source_code_get_line(parser.code, 0);

        // Create root
        auto root = allocate_base<Module>(0, Node_Type::MODULE);
        parser.unit->root = root;
        root->definitions = dynamic_array_create<Definition*>();
        root->import_nodes = dynamic_array_create<Import*>();
        root->context_changes = dynamic_array_create<Context_Change*>();

        // Parse root
        parse_block_of_items(upcast(root), wrapper_parse_module_item, module_add_child, Operator::SEMI_COLON, 0, false);

        // Set range
        Token_Range range = token_range_make(token_index_make(0, 0), token_index_make_line_end(code, code->line_count-1));
        root->base.range = range;
        root->base.bounding_range = range;
    }

    void execute_clean(Compilation_Unit* unit)
    {
        for (int i = 0; i < unit->allocated_nodes.size; i++) {
            AST::base_destroy(unit->allocated_nodes[i]);
        }
        dynamic_array_reset(&unit->allocated_nodes);
        dynamic_array_reset(&unit->parser_errors);
        unit->root = 0;

        parser.code = unit->code;
        parser.unit = unit;
        parser.state.allocated_count = 0;
        parser.state.error_count = 0;
        parser.state.pos = token_index_make(0, 0);
        parser.state.line = source_code_get_line(unit->code, 0);
        parse_root();
    }

    // AST queries based on Token-Indices
    void ast_base_get_section_token_range(Source_Code* code, AST::Node* base, Section section, Dynamic_Array<Token_Range>* ranges)
    {
        auto range = base->range;
        switch (section)
        {
        case Section::NONE: break;
        case Section::WHOLE:
        {
            if (base->type == AST::Node_Type::EXPRESSION && downcast<AST::Expression>(base)->type == AST::Expression_Type::FUNCTION_CALL) {
                dynamic_array_push_back(ranges, base->bounding_range);
                break;
            }
            dynamic_array_push_back(ranges, range);
            break;
        }
        case Section::WHOLE_NO_CHILDREN:
        {
            Token_Range sub_range;
            sub_range.start = range.start;
            int index = 0;
            // Note: This operates under the assumption that base_get_child returns children in the correct order
            //       which e.g. isn't true for things like modules or code-block with multiple child types
            auto child = AST::base_get_child(base, index);
            while (child != 0)
            {
                auto child_range = child->range;
                if (token_index_compare(sub_range.start, child_range.start) == 0) {
                    sub_range.end = child_range.start;
                    // Extra check, as bounding range may differ from normal range (E.g. child starts before parent range)
                    if (token_index_compare(sub_range.start, sub_range.end) == 1) {
                        dynamic_array_push_back(ranges, sub_range);
                    }
                }
                sub_range.start = child_range.end;

                index += 1;
                child = AST::base_get_child(base, index);
            }
            if (token_index_compare(sub_range.start, range.end) != 0) {
                sub_range.end = range.end;
                // Extra check, as bounding range may differ from normal range
                if (token_index_compare(sub_range.start, sub_range.end) == 1) {
                    dynamic_array_push_back(ranges, sub_range);
                }
            }
            if (ranges->size == 0) {
                dynamic_array_push_back(ranges, range);
            }
            break;
        }
        case Section::IDENTIFIER:
        {
            auto result = search_token(code, range.start, [](Token* t, void* _unused) -> bool {return t->type == Token_Type::IDENTIFIER; }, nullptr);
            if (!result.available) {
                break;
            }
            
            dynamic_array_push_back(ranges, token_range_make_offset(result.value, 1));
            break;
        }
        case Section::ENCLOSURE:
        {
            // Find next (), {} or [], and add the tokens to the ranges
            auto result = search_token(code, range.start, [](Token* t, void* type) -> bool {return t->type == Token_Type::PARENTHESIS; }, nullptr);
            if (!result.available) {
                break;
            }
            dynamic_array_push_back(ranges, token_range_make_offset(result.value, 1));

            // Find closing parenthesis
            auto par_type = source_code_get_line(code, result.value.line)->tokens[result.value.token].options.parenthesis.type;
            advance_token();
            auto end_found_fn = [](Token* t, void* type) -> bool { 
                return t->type == Token_Type::PARENTHESIS && 
                    !t->options.parenthesis.is_open && t->options.parenthesis.type == *((Parenthesis_Type*)type); 
            };
            Token_Index start = result.value;
            start.token += 1;
            auto end_token = search_token(code, start, end_found_fn, (void*)(&par_type));
            if (!end_token.available) {
                break;
            }

            dynamic_array_push_back(ranges, token_range_make_offset(end_token.value, 1));
            break;
        }
        case Section::KEYWORD:
        {
            auto result = search_token(code, range.start, [](Token* t, void* type) -> bool {return t->type == Token_Type::KEYWORD; }, 0);
            if (!result.available) {
                break;
            }
            dynamic_array_push_back(ranges, token_range_make_offset(result.value, 1));
            break;
        }
        case Section::FIRST_TOKEN: {
            dynamic_array_push_back(ranges, token_range_make_offset(range.start, 1));
            break;
        }
        case Section::END_TOKEN: {
            Token_Range end_token_range = token_range_make_offset(range.end, -1);
            end_token_range.start.token = math_maximum(0, end_token_range.start.token);
            dynamic_array_push_back(ranges, end_token_range);
            break;
        }
        default: panic("");
        }
    }
}
