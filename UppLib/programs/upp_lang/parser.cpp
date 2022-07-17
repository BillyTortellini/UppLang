#include "parser.hpp"

#include "ast.hpp"
#include "syntax_editor.hpp"
#include "compiler.hpp"
#include "code_history.hpp"

namespace Parser
{
    // Types
    using namespace AST;


    // Parser Helpers
    struct Binop_Link
    {
        Binop binop;
        Token_Index token_index;
        Expression* expr;
    };

    struct Parse_State
    {
        Token_Index pos;
        int allocated_count;
        int error_count;
    };

    struct Parser
    {
        Parse_State state;
        Dynamic_Array<AST::Node*> allocated_nodes;
        Parse_Pass* pass;
    };

    // Globals
    static Parser parser;



    // Parser Functions
    void parser_rollback(Parse_State checkpoint)
    {
        for (int i = checkpoint.allocated_count; i < parser.allocated_nodes.size; i++) {
            AST::base_destroy(parser.allocated_nodes[i]);
        }
        dynamic_array_rollback_to_size(&parser.allocated_nodes, checkpoint.allocated_count);
        if (parser.pass != 0) {
            dynamic_array_rollback_to_size(&parser.pass->error_messages, checkpoint.error_count);
        }
        parser.state = checkpoint;
    }

    void reset()
    {
        parser.pass = 0;

        Parse_State state;
        state.allocated_count = 0;
        state.error_count = 0;
        parser_rollback(state);
    }

    void initialize()
    {
        parser.allocated_nodes = dynamic_array_create_empty<AST::Node*>(32);
        reset();
    }

    void destroy()
    {
        reset();
        dynamic_array_destroy(&parser.allocated_nodes);
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
        base->analysis_item_index = -1;
        base->range.start = parser.state.pos;
        base->range.end = parser.state.pos;

        return result;
    }

    void base_correct_token_ranges(Node* base)
    {
        /*
        DOCU: 
          This function does a Post-Processing on Token-Ranges to correct invalid ranges,
          which are generated the following way:
            1. Parse syntax block steps out of current block (Line-Index becomes invalid (line_index == block.lines.size)
                and the parent items of the syntax_block therefore get an invalid end_index.
            2. Expressions that go over multiple ranges have their end index at the start of another line.
               Instead, the end index should be at the end of the previous line
               Maybe I need to think more about this, but this shouldn't be true for code-blocks
          It also generates bounding ranges, which are used to efficiently find
        FUTURE: Maybe the parser can handle these problems and we don't need a Post-Processing step
        */

        auto& range = base->range;
        auto& bounding_range = base->bounding_range;

        // Correct Invalid ranges
        {
            auto& end = range.end;
            auto block = index_value(end.line.block);
            assert(end.line.line <= block->lines.size, "Cannot be greater");
            if (end.line.line == block->lines.size) {
                end = token_index_make_block_end(end.line.block);
            }
            //else if (end.token == 0 && !index_equal(end, range.start) && base->type != Node_Type::CODE_BLOCK && base->type != Node_Type::MODULE) {
                //end = token_index_make_line_end(line_index_prev(end.line));
            //}
        }
        assert(index_valid(range.start), "Jo");
        assert(index_valid(range.end), "Jo");
        bounding_range = range;

        // Iterate over all children + Calculate bounding ranges
        {
            int index = 0;
            auto child = AST::base_get_child(base, index);
            if (child == 0) return;

            while (child != 0) 
            {
                base_correct_token_ranges(child);

                auto child_range = child->bounding_range;
                if (index_compare(bounding_range.start, child_range.start) < 0) {
                    bounding_range.start = child_range.start;
                }
                if (index_compare(child_range.end, bounding_range.end) < 0) {
                    bounding_range.end = child_range.end;
                }

                index += 1;
                child = AST::base_get_child(base, index);
            }
        }

        assert(index_valid(range.start), "Jo");
        assert(index_valid(range.end), "Jo");
        assert(index_valid(bounding_range.start), "Jo");
        assert(index_valid(bounding_range.end), "Jo");

        // Check that start/end is in order
        int order = index_compare(range.start, range.end);
        assert(order != -1, "Ranges must be in order");
        if (order == 0) {
            // INFO: There are only 3 AST-Nodes which are allowed to have a 0 sized token range:
            //       Error-Expressions, Code-Blocks and Symbol-Reads (Empty reads
            if (base->type == AST::Node_Type::EXPRESSION) {
                auto expr = downcast<Expression>(base);
                assert(expr->type == AST::Expression_Type::ERROR_EXPR, "Only error expression may have 0-sized token range");
            }
            else if (base->type == AST::Node_Type::CODE_BLOCK || base->type == AST::Node_Type::SYMBOL_READ) {
            }
            else {
                panic("See comment before");
            }
        }
    }



    // Error reporting
    void log_error(const char* msg, Token_Range range)
    {
        Error_Message err;
        err.msg = msg;
        err.range = range;
        dynamic_array_push_back(&parser.pass->error_messages, err);
        parser.state.error_count = parser.pass->error_messages.size;
    }

    void log_error_to_pos(const char* msg, Token_Index pos) {
        log_error(msg, token_range_make(parser.state.pos, pos));
    }

    void log_error_range_offset_with_start(const char* msg, Token_Index start, int token_offset) {
        log_error(msg, token_range_make(start, token_index_advance(start, token_offset)));
    }

    void log_error_range_offset(const char* msg, int token_offset) {
        log_error_range_offset_with_start(msg, parser.state.pos, token_offset);
    }



    // Positional Stuff
    bool on_line() {
        auto& p = parser.state.pos;
        return index_valid(parser.state.pos.line);
    }

    Token* get_token(int offset) {
        return index_value(token_index_advance(parser.state.pos, offset));
    }

    Token* get_token() {
        return index_value(parser.state.pos);
    }

    Source_Line* get_line() {
        return index_value(parser.state.pos.line);
    }

    int remaining_line_tokens() {
        auto& p = parser.state.pos;
        if (!index_valid(p)) return 0;
        auto tokens = index_value(p.line)->tokens;
        int remaining = tokens.size - p.token;
        if (tokens.size != 0 && tokens[tokens.size - 1].type == Token_Type::COMMENT) {
            remaining -= 1;
        }
        return remaining;
    }

    bool on_follow_block() {
        auto& pos = parser.state.pos;
        if (!line_index_block_after(pos.line).available) return false;
        return remaining_line_tokens() == 0;
    }

    void advance_token() {
        parser.state.pos = token_index_next(parser.state.pos);
    }

    void advance_line() {
        parser.state.pos.line.line += 1;
        parser.state.pos.token = 0;
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



    // Parsing Helpers
#define CHECKPOINT_SETUP \
        if (!index_valid(parser.state.pos)) {return 0;}\
        if (token_index_is_last_in_line(parser.state.pos) && !on_follow_block()) {return 0;}\
        auto checkpoint = parser.state;\
        bool _error_exit = false;\
        SCOPE_EXIT(if (_error_exit) parser_rollback(checkpoint););

#define CHECKPOINT_EXIT {_error_exit = true; return 0;}
#define SET_END_RANGE(val) val->base.range.end = parser.state.pos;
#define PARSE_SUCCESS(val) { \
        if (val->base.type == Node_Type::CODE_BLOCK) return val; \
        val->base.range.end = parser.state.pos; \
        return val; \
    }

    typedef bool(*token_predicate_fn)(Token* token, void* user_data);
    Optional<Token_Index> search_token(Token_Index start, token_predicate_fn predicate, void* user_data, bool starting_inside_parenthesis)
    {
        // IDEA: Maybe I can move this whole function to source_code, since it could be used outside of parsing
        // PERF: The parenthesis stack can be held in the parser, and just be resetted here
        Dynamic_Array<Parenthesis> parenthesis_stack = dynamic_array_create_empty<Parenthesis>(1);
        SCOPE_EXIT(dynamic_array_destroy(&parenthesis_stack));

        Token_Index pos = start;
        while (true)
        {
            // End of line handling
            if (source_line_is_end_of_block(pos.line)) {
                return optional_make_failure<Token_Index>();
            }
            if (token_index_is_last_in_line(pos))
            {
                if (!(starting_inside_parenthesis || parenthesis_stack.size != 0)) {
                    return optional_make_failure<Token_Index>();
                }
                // Parenthesis aren't allowed to reach over blocks if there is no follow_block
                if (!line_index_block_after(pos.line).available) {
                    return optional_make_failure<Token_Index>();
                }
                if (line_index_is_last_in_block(pos.line)) {
                    return optional_make_failure<Token_Index>();
                }
                pos.line.line += 1;
                pos.token = 0;
                continue;
            }

            auto token = index_value(pos);
            if (parenthesis_stack.size != 0)
            {
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
            }
            else if (predicate(token, user_data)) {
                return optional_make_success(pos);
            }
            pos = token_index_next(pos);
        }
    }

    template<typename Container_Node_Type>
    void parse_source_block(Block_Index block_index, Container_Node_Type* parent, 
        void (*item_parse_fn)(Container_Node_Type* parent), void (*block_parse_fn)(Container_Node_Type* parent, Block_Index index))
    {
        // DOCU: Parse syntax block does not advance the parser position. All calling functions must manually set the cursor after this block
        //       after calling this function
        //       Both item_parse_fn and block_parse_fn do not need to advance the parser position/line index

        // Setup parser position at block start
        auto& pos = parser.state.pos;
        pos = token_index_make(line_index_make(block_index, 0), 0);
        parent->base.range.end = token_index_make_block_end(block_index);

        auto block = index_value(pos.line.block);
        // Check for special cases
        if (block->lines.size == 0) {
            assert(block->children.size == 1, "This is the only case where block->lines can be 0");
        }
        if (block->children.size != 0) {
            auto first_child_block = index_value(block->children[0]);
            if (first_child_block->line_index == 0) {
                auto rewind_pos = pos;
                block_parse_fn(parent, block->children[0]);
                pos = rewind_pos;
            }
        }
        // Parse all lines
        while (pos.line.line < block->lines.size)
        {
            auto line = index_value(pos.line);
            auto before_line_index = pos.line;
            pos.token = 0;

            // Skip empty lines/Lines that are only comments
            {
                auto block_after = line_index_block_after(pos.line);
                if (line->tokens.size == 0 || source_line_is_comment(pos.line)) {
                    if (block_after.available && !source_line_is_multi_line_comment_start(pos.line)) {
                        auto rewind_pos = pos;
                        block_parse_fn(parent, block_after.value);
                        pos = rewind_pos;
                    }
                    advance_line();
                    continue;
                }
            }

            // Parse line
            item_parse_fn(parent);

            if (pos.line.line >= block->lines.size) break;
            // Check if we moved/skipped tokens
            bool changed_lines = before_line_index.line != pos.line.line;
            auto block_after = line_index_block_after(pos.line);
            auto& tokens = index_value(pos.line)->tokens;
            if (!changed_lines && pos.token == 0) {
                // Log error if we didn't move
                log_error_to_pos("Could't parse line item", token_index_make_line_end(pos.line));
                if (block_after.available) {
                    log_error("Couldn't parse block header", token_range_make_block(block_after.value));
                }
                advance_line();
                continue;
            }
            else if (token_index_is_last_in_line(pos)) {
                if (block_after.available) {
                    log_error("Follow block wasn't required by block header", token_range_make_block(block_after.value));
                }
                advance_line();
                continue;
            }
            else if (changed_lines && pos.token == 0) {
                continue;
            }
            else if (pos.token != 0 && tokens[pos.token].type == Token_Type::COMMENT) {
                if (block_after.available) {
                    log_error("Block header contains errors", token_range_make_block(block_after.value));
                }
                advance_line();
                continue;
            }
            else {
                log_error_to_pos("Unexpected Tokens, Line already parsed", token_index_make_line_end(pos.line));
                if (block_after.available) {
                    log_error("Block header contains errors", token_range_make_block(block_after.value));
                }
                advance_line();
                continue;
            }
        }
    }

    template<typename Container_Node_Type>
    void parse_follow_block(bool parse_next_block_if_not_on_line_end, Container_Node_Type* parent,
        void (*item_parse_fn)(Container_Node_Type* parent), void (*block_parse_fn)(Container_Node_Type* parent, Block_Index index))
    {
        auto& pos = parser.state.pos;

        auto follow_block_opt = line_index_block_after(pos.line);
        if (!follow_block_opt.available) {
            log_error_range_offset("Expected Follow Block", 0);
            return;
        }
        if (!parse_next_block_if_not_on_line_end && !on_follow_block()) {
            log_error_to_pos("Parsing follow block, ignoring rest of line", token_index_make_line_end(pos.line));
        }

        advance_line();
        auto backup_pos = parser.state.pos;
        parse_source_block(follow_block_opt.value, parent, item_parse_fn, block_parse_fn);
        parser.state.pos = backup_pos;
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
            parser.state.pos,
            [](Token* t, void* _unused) -> bool
            { return t->type == Token_Type::PARENTHESIS &&
            !t->options.parenthesis.is_open && t->options.parenthesis.type == type; },
            0, true
        );
        if (!parenthesis_pos.available) {
            return false;
        }
        log_error_to_pos("Parenthesis context was already finished, unexpected tokens afterwards", parenthesis_pos.value);
        parser.state.pos = parenthesis_pos.value;
        advance_token();
        return true;
    }

    // DOCU: Parser position must be on Open Parenthesis for this to work
    template<typename T>
    void parse_parenthesis_comma_seperated(Node* parent, Dynamic_Array<T*>* fill_array, T* (*parse_fn)(Node* parent), Parenthesis_Type type)
    {
        // Check for open parenthesis
        char closing_char;
        auto open_parenthesis_pos = parser.state.pos;
        {
            Parenthesis p;
            p.type = type;
            p.is_open = true;
            if (!test_parenthesis_offset(parenthesis_to_char(p), 0)) return;
            advance_token();
            p.is_open = false;
            closing_char = parenthesis_to_char(p);
        }

        // Parse Items
        while (true)
        {
            if (test_parenthesis_offset(closing_char, 0)) {
                advance_token();
                break;
            }
            auto item = parse_fn(parent);
            if (item != 0)
            {
                dynamic_array_push_back(fill_array, item);
                if (test_operator(Operator::COMMA)) {
                    advance_token();
                    continue;
                }
                if (test_parenthesis_offset(closing_char, 0)) {
                    continue;
                }
            }

            // ERROR RECOVERY
            // Find possible Recovery points
            auto comma_pos = search_token(
                parser.state.pos,
                [](Token* t, void* _unused) -> bool
                { return t->type == Token_Type::OPERATOR && t->options.op == Operator::COMMA; },
                0, true
            );
            auto parenthesis_pos = search_token(
                parser.state.pos,
                [](Token* t, void* _unused) -> bool
                { return t->type == Token_Type::PARENTHESIS &&
                !t->options.parenthesis.is_open && t->options.parenthesis.type == Parenthesis_Type::PARENTHESIS; },
                0, true
            );

            // Select Recovery point
            Optional<Token_Index> recovery_point = optional_make_failure<Token_Index>();
            if (comma_pos.available && parenthesis_pos.available) {
                // Select earlier recovery point
                if (index_compare(comma_pos.value, parenthesis_pos.value)) {
                    recovery_point = optional_make_success(comma_pos.value);
                }
                else {
                    recovery_point = optional_make_success(parenthesis_pos.value);
                }
            }
            else if (comma_pos.available) {
                recovery_point = optional_make_success(comma_pos.value);
            }
            else if (parenthesis_pos.available) {
                recovery_point = optional_make_success(parenthesis_pos.value);
            }

            if (recovery_point.available) {
                log_error_to_pos("Couldn't parse list item", recovery_point.value);
                parser.state.pos = recovery_point.value;
                advance_token();
            }
            else
            {
                // INFO: In this case there is no end to the parenthesis we are in,
                //       so we log an error to the end of the line and stop parsing
                auto line_end = token_index_make_line_end(parser.state.pos.line);
                log_error_to_pos("Couldn't find closing_parenthesis", line_end);
                parser.state.pos = line_end;
                return;
            }
        }
    }



    // Prototypes
    Project_Import* parse_project_import(Node* parent);
    Definition* parse_definition(Node* parent);
    Statement* parse_statement(Node* parent);
    Enum_Member* parse_enum_member(Node* parent);
    Project_Import* parse_project_import(Node* parent);
    Switch_Case* parse_switch_case(Node* parent);

    Expression* parse_expression(Node* parent);
    Expression* parse_expression_or_error_expr(Node* parent);
    Expression* parse_single_expression(Node* parent);
    Expression* parse_single_expression_or_error(Node* parent);

    void parse_item_fn_statement(Code_Block* parent);
    void parse_item_fn_module_item(Module* parent);

    // Parse Item/Block Functions
    void parse_block_fn_anonymous_block_in_code_block(Code_Block* parent, Block_Index block_index)
    {
        auto code_block = allocate_base<Code_Block>(&parent->base, Node_Type::CODE_BLOCK);
        code_block->base.range = token_range_make_block(block_index);
        code_block->statements = dynamic_array_create_empty<Statement*>(1);
        code_block->block_id = optional_make_failure<String*>();

        auto statement = allocate_base<Statement>(&parent->base, Node_Type::STATEMENT);
        statement->type = Statement_Type::BLOCK;
        statement->options.block = code_block;

        parse_source_block(block_index, code_block, parse_item_fn_statement, parse_block_fn_anonymous_block_in_code_block);
        SET_END_RANGE(code_block);
        SET_END_RANGE(statement);
        dynamic_array_push_back(&parent->statements, statement);
    }

    void parse_block_fn_anonymous_module_block(Module* parent, Block_Index block_index) {
        parse_source_block(block_index, parent, parse_item_fn_module_item, parse_block_fn_anonymous_module_block);
    }

    template<typename Parent_Type>
    void parse_block_fn_error_on_block(Parent_Type* _unused, Block_Index block_index) {
        log_error("Anonymous blocks aren't allowed in this context", token_range_make_block(block_index));
    }

    void parse_item_fn_statement(Code_Block* parent)
    {
        auto statement = parse_statement(&parent->base);
        if (statement != 0) {
            dynamic_array_push_back(&parent->statements, statement);
        }
    }

    void parse_item_fn_module_item(Module* parent)
    {
        auto definition = parse_definition(&parent->base);
        if (definition != 0) {
            dynamic_array_push_back(&parent->definitions, definition);
            return;
        }
        auto project_import = parse_project_import(&parent->base);
        if (project_import != 0) {
            dynamic_array_push_back(&parent->imports, project_import);
            return;
        }
    }

    void parse_item_fn_enum_member(Expression* parent)
    {
        assert(parent->type == Expression_Type::ENUM_TYPE, "");
        auto member = parse_enum_member(&parent->base);
        if (member != 0) {
            dynamic_array_push_back(&parent->options.enum_members, member);
        }
    }

    void parse_item_fn_struct_member(Expression* parent)
    {
        assert(parent->type == Expression_Type::STRUCTURE_TYPE, "");
        auto member = parse_definition(&parent->base);
        if (member != 0) {
            dynamic_array_push_back(&parent->options.structure.members, member);
        }
    }

    void parse_item_fn_switch_case(Statement* parent)
    {
        assert(parent->type == Statement_Type::SWITCH_STATEMENT, "");
        auto sw_case = parse_switch_case(&parent->base);
        if (sw_case != 0) {
            dynamic_array_push_back(&parent->options.switch_statement.cases, sw_case);
        }
    }



    // Parsing
    Optional<String*> parse_block_label(AST::Expression* related_expression)
    {
        Optional<String*> result = optional_make_failure<String*>();
        if (test_token(Token_Type::IDENTIFIER), test_operator_offset(Operator::COLON, 1)) {
            result = optional_make_success(get_token(0)->options.identifier);
            advance_token();
            advance_token();
        }
        else if (related_expression != 0 && related_expression->type == Expression_Type::SYMBOL_READ && !related_expression->options.symbol_read->path_child.available) {
            // This is an experimental feature: give the block the name of the condition if possible,
            // e.g. switch color
            //      case .RED
            //          break color
            // In the future I also want to use this for loop variables, e.g.   loop a in array {break a}
            result = optional_make_success<String*>(related_expression->options.symbol_read->name);
        }
        return result;
    }

    Code_Block* parse_code_block(Node* parent, AST::Expression* related_expression)
    {
        auto result = allocate_base<Code_Block>(parent, Node_Type::CODE_BLOCK);
        auto follow_block_opt = line_index_block_after(parser.state.pos.line);
        if (follow_block_opt.available) {
            result->base.range = token_range_make_block(follow_block_opt.value);
        }
        result->statements = dynamic_array_create_empty<Statement*>(1);
        result->block_id = parse_block_label(related_expression);
        parse_follow_block(true, result, parse_item_fn_statement, parse_block_fn_anonymous_block_in_code_block);
        PARSE_SUCCESS(result);
    }

    Argument* parse_argument(Node* parent)
    {
        CHECKPOINT_SETUP;
        auto result = allocate_base<Argument>(parent, Node_Type::ARGUMENT);
        if (test_token(Token_Type::IDENTIFIER) && test_operator_offset(Operator::ASSIGN, 1)) {
            result->name = optional_make_success(get_token(0)->options.identifier);
            advance_token();
            advance_token();
            result->value = parse_expression_or_error_expr(&result->base);
            PARSE_SUCCESS(result);
        }
        result->value = parse_expression(&result->base);
        if (result->value == 0) CHECKPOINT_EXIT;
        PARSE_SUCCESS(result);
    }

    Parameter* parse_parameter(Node* parent)
    {
        CHECKPOINT_SETUP;
        auto result = allocate_base<Parameter>(parent, Node_Type::PARAMETER);
        result->is_comptime = false;
        if (test_operator(Operator::DOLLAR)) {
            result->is_comptime = true;
            advance_token();
        }

        if (!test_token(Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;
        result->name = get_token(0)->options.identifier;
        advance_token();

        if (!test_operator(Operator::COLON)) CHECKPOINT_EXIT;
        advance_token();
        result->type = parse_expression_or_error_expr((Node*)result);

        if (test_operator(Operator::ASSIGN)) {
            result->type = parse_expression_or_error_expr((Node*)result);
        }
        PARSE_SUCCESS(result);
    }

    Switch_Case* parse_switch_case(Node* parent)
    {
        if (!test_keyword_offset(Keyword::CASE, 0) && !test_keyword_offset(Keyword::DEFAULT, 0)) {
            return 0;
        }

        auto result = allocate_base<Switch_Case>(parent, Node_Type::SWITCH_CASE);
        bool is_default = test_keyword_offset(Keyword::DEFAULT, 0);
        advance_token();
        result->value.available = false;
        if (!is_default) {
            result->value = optional_make_success(parse_expression_or_error_expr(&result->base));
        }
        result->block = parse_code_block(&result->base, 0);

        // Set block label (Switch cases need special treatment because they 'Inherit' the label from the switch
        assert(parent->type == Node_Type::STATEMENT && ((Statement*)parent)->type == Statement_Type::SWITCH_STATEMENT, "");
        result->block->block_id = ((Statement*)parent)->options.switch_statement.label;
        PARSE_SUCCESS(result);
    }

    Statement* parse_statement(Node* parent)
    {
        CHECKPOINT_SETUP;
        auto result = allocate_base<Statement>(parent, Node_Type::STATEMENT);

        {
            // Check for Anonymous Blocks
            // INFO: This needs to be done before definition
            auto line = get_line();
            if ((line->tokens.size == 1 && test_token(Token_Type::COMMENT)) ||
                (line->tokens.size == 2 && test_token(Token_Type::IDENTIFIER) && test_operator_offset(Operator::COLON, 1)) ||
                (line->tokens.size == 3 && test_token(Token_Type::IDENTIFIER) && test_operator_offset(Operator::COLON, 1) && test_token_offset(Token_Type::COMMENT, 3)))
            {
                if (line_index_block_after(parser.state.pos.line).available)
                {
                    auto block_id = optional_make_failure<String*>();
                    if (test_token(Token_Type::IDENTIFIER)) {
                        block_id = optional_make_success(get_token()->options.identifier);
                    }
                    result->type = Statement_Type::BLOCK;
                    result->options.block = parse_code_block(&result->base, 0);
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
        {
            auto expr = parse_expression(&result->base);
            if (expr != 0)
            {
                if (test_operator(Operator::ASSIGN)) {
                    result->type = Statement_Type::ASSIGNMENT;
                    result->options.assignment.left_side = expr;
                    advance_token();
                    result->options.assignment.right_side = parse_expression_or_error_expr(&result->base);
                    PARSE_SUCCESS(result);
                }
                result->type = Statement_Type::EXPRESSION_STATEMENT;
                result->options.expression = expr;
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
                if_stat.block = parse_code_block(&result->base, if_stat.condition);
                if_stat.else_block.available = false;

                auto last_if_stat = result;
                // Parse else-if chain
                while (test_keyword_offset(Keyword::ELSE, 0) && test_keyword_offset(Keyword::IF, 1))
                {
                    auto implicit_else_block = allocate_base<AST::Code_Block>(&last_if_stat->base, Node_Type::CODE_BLOCK);
                    implicit_else_block->statements = dynamic_array_create_empty<Statement*>(1);
                    implicit_else_block->block_id = optional_make_failure<String*>();

                    auto new_if_stat = allocate_base<AST::Statement>(&last_if_stat->base, Node_Type::STATEMENT);
                    advance_token();
                    advance_token();
                    new_if_stat->type = Statement_Type::IF_STATEMENT;
                    auto& new_if = new_if_stat->options.if_statement;
                    new_if.condition = parse_expression_or_error_expr(&new_if_stat->base);
                    new_if.block = parse_code_block(&last_if_stat->base, new_if.condition);
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
                loop.block = parse_code_block(&result->base, loop.condition);
                PARSE_SUCCESS(result);
            }
            case Keyword::DEFER:
            {
                advance_token();
                result->type = Statement_Type::DEFER;
                result->options.defer_block = parse_code_block(&result->base, 0);
                PARSE_SUCCESS(result);
            }
            case Keyword::SWITCH:
            {
                advance_token();
                result->type = Statement_Type::SWITCH_STATEMENT;
                auto& switch_stat = result->options.switch_statement;
                switch_stat.condition = parse_expression_or_error_expr(&result->base);
                switch_stat.cases = dynamic_array_create_empty<Switch_Case*>(1);
                switch_stat.label = parse_block_label(switch_stat.condition);
                parse_follow_block(true, result, parse_item_fn_switch_case, parse_block_fn_error_on_block<Statement>);
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
        CHECKPOINT_EXIT;
    }

    Enum_Member* parse_enum_member(Node* parent)
    {
        if (!test_token(Token_Type::IDENTIFIER)) {
            return 0;
        }

        CHECKPOINT_SETUP;
        auto result = allocate_base<Enum_Member>(parent, Node_Type::ENUM_MEMBER);
        result->name = get_token(0)->options.identifier;
        advance_token();
        if (test_operator(Operator::DEFINE_COMPTIME))
        {
            advance_token();
            result->value = optional_make_success(parse_expression_or_error_expr(&result->base));
        }
        PARSE_SUCCESS(result);
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

        // Casts
        {
            bool is_cast = true;
            Cast_Type type;
            if (test_keyword_offset(Keyword::CAST, 0)) {
                type = Cast_Type::TYPE_TO_TYPE;
            }
            else if (test_keyword_offset(Keyword::CAST_PTR, 0)) {
                type = Cast_Type::RAW_TO_PTR;
            }
            else if (test_keyword_offset(Keyword::CAST_RAW, 0)) {
                type = Cast_Type::PTR_TO_RAW;
            }
            else {
                is_cast = false;
            }
            if (is_cast)
            {
                advance_token();
                result->type = Expression_Type::CAST;
                auto& cast = result->options.cast;
                cast.type = type;
                if (test_parenthesis_offset('{', 0))
                {
                    advance_token();
                    cast.to_type = optional_make_success(parse_single_expression_or_error(&result->base));
                    if (!finish_parenthesis<Parenthesis_Type::BRACES>()) CHECKPOINT_EXIT;
                }

                cast.operand = parse_single_expression_or_error(&result->base);
                PARSE_SUCCESS(result);
            }
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

        // Bases
        if (test_token(Token_Type::IDENTIFIER))
        {
            Symbol_Read* final_read = allocate_base<Symbol_Read>(&result->base, Node_Type::SYMBOL_READ);
            Symbol_Read* read = final_read;
            read->path_child.available = false;
            read->name = get_token(0)->options.identifier;
            while (test_token(Token_Type::IDENTIFIER) &&
                test_operator_offset(Operator::TILDE, 1))
            {
                advance_token();
                advance_token();
                read->path_child = optional_make_success(allocate_base<Symbol_Read>(&read->base, Node_Type::SYMBOL_READ));
                if (test_token(Token_Type::IDENTIFIER)) {
                    read->path_child.value->name = get_token(0)->options.identifier;
                }
                else {
                    auto& pos = parser.state.pos;
                    log_error("Expected identifier", token_range_make(token_index_advance(pos, -1), pos));
                    read->path_child.value->name = compiler.id_empty_string;
                }

                read = read->path_child.value;
                SET_END_RANGE(read);
            }

            result->type = Expression_Type::SYMBOL_READ;
            result->options.symbol_read = final_read;
            if (test_token(Token_Type::IDENTIFIER)) {
                advance_token();
            }
            SET_END_RANGE(final_read);
            PARSE_SUCCESS(result);
        }

        if (test_operator(Operator::DOT))
        {
            advance_token();
            if (test_parenthesis_offset('{', 0)) // Struct Initializer
            {
                result->type = Expression_Type::STRUCT_INITIALIZER;
                auto& init = result->options.struct_initializer;
                init.type_expr = optional_make_failure<Expression*>();
                init.arguments = dynamic_array_create_empty<Argument*>(1);
                parse_parenthesis_comma_seperated(&result->base, &init.arguments, parse_argument, Parenthesis_Type::BRACES);
                PARSE_SUCCESS(result);
            }
            else if (test_parenthesis_offset('[', 0)) // Array Initializer
            {
                result->type = Expression_Type::ARRAY_INITIALIZER;
                auto& init = result->options.array_initializer;
                init.type_expr = optional_make_failure<Expression*>();
                init.values = dynamic_array_create_empty<Expression*>(1);
                parse_parenthesis_comma_seperated(&result->base, &init.values, parse_expression, Parenthesis_Type::BRACKETS);
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
                    auto& pos = parser.state.pos;
                    log_error("Missing member name", token_range_make(token_index_advance(pos, -1), pos));
                    result->options.auto_enum = compiler.id_empty_string;
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
            (test_operator_offset(Operator::DOLLAR, 1) && test_token_offset(Token_Type::IDENTIFIER, 2))
            ))
        {
            result->type = Expression_Type::FUNCTION_SIGNATURE;
            auto& signature = result->options.function_signature;
            signature.parameters = dynamic_array_create_empty<Parameter*>(1);
            signature.return_value.available = false;
            parse_parenthesis_comma_seperated(&result->base, &signature.parameters, parse_parameter, Parenthesis_Type::PARENTHESIS);

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
            if (test_parenthesis_offset('[', 0)) {
                advance_token();
                result->options.new_expr.count_expr = optional_make_success(parse_expression_or_error_expr(&result->base));
                if (!finish_parenthesis<Parenthesis_Type::BRACKETS>()) CHECKPOINT_EXIT;
            }
            result->options.new_expr.type_expr = parse_expression_or_error_expr(&result->base);
            PARSE_SUCCESS(result);
        }
        if (test_keyword_offset(Keyword::STRUCT, 0) ||
            test_keyword_offset(Keyword::C_UNION, 0) ||
            test_keyword_offset(Keyword::UNION, 0))
        {
            result->type = Expression_Type::STRUCTURE_TYPE;
            result->options.structure.members = dynamic_array_create_empty<Definition*>(1);
            if (test_keyword_offset(Keyword::STRUCT, 0)) {
                result->options.structure.type = AST::Structure_Type::STRUCT;
            }
            else if (test_keyword_offset(Keyword::C_UNION, 0)) {
                result->options.structure.type = AST::Structure_Type::C_UNION;
            }
            else {
                result->options.structure.type = AST::Structure_Type::UNION;
            }
            advance_token();
            parse_follow_block(false, result, parse_item_fn_struct_member, parse_block_fn_error_on_block<Expression>);
            PARSE_SUCCESS(result);
        }
        if (test_keyword_offset(Keyword::ENUM, 0)) {
            result->type = Expression_Type::ENUM_TYPE;
            result->options.enum_members = dynamic_array_create_empty<Enum_Member*>(1);
            advance_token();
            parse_follow_block(false, result, parse_item_fn_enum_member, parse_block_fn_error_on_block);
            PARSE_SUCCESS(result);
        }
        if (test_keyword_offset(Keyword::MODULE, 0)) {
            auto module = allocate_base<Module>(&result->base, Node_Type::MODULE);
            module->definitions = dynamic_array_create_empty<Definition*>(1);
            module->imports = dynamic_array_create_empty<Project_Import*>(1);
            advance_token();
            parse_follow_block(false, module, parse_item_fn_module_item, parse_block_fn_anonymous_module_block);

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
                init.arguments = dynamic_array_create_empty<Argument*>(1);
                parse_parenthesis_comma_seperated(&result->base, &init.arguments, parse_argument, Parenthesis_Type::BRACES);
                PARSE_SUCCESS(result);
            }
            else if (test_parenthesis_offset('[', 0)) // Array Initializer
            {
                result->type = Expression_Type::ARRAY_INITIALIZER;
                auto& init = result->options.array_initializer;
                init.type_expr = optional_make_success(child);
                init.values = dynamic_array_create_empty<Expression*>(1);
                parse_parenthesis_comma_seperated(&result->base, &init.values, parse_expression, Parenthesis_Type::BRACKETS);
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
                    auto& pos = parser.state.pos;
                    log_error("Missing member name", token_range_make(token_index_advance(pos, -1), pos));
                    result->options.member_access.name = compiler.id_empty_string;
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
        else if (test_parenthesis_offset('(', 0)) // Function call
        {
            result->type = Expression_Type::FUNCTION_CALL;
            auto& call = result->options.call;
            call.expr = child;
            call.arguments = dynamic_array_create_empty<Argument*>(1);
            parse_parenthesis_comma_seperated<Argument>(&result->base, &call.arguments, parse_argument, Parenthesis_Type::PARENTHESIS);
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
        return expr;
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
                range.end = token_index_next(link.token_index);
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

        // PERF: The arrays could be stored in the parser, and just get reseted here
        Dynamic_Array<Binop_Link> links = dynamic_array_create_empty<Binop_Link>(1);
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

        // Now build the binop tree
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

    Project_Import* parse_project_import(Node* parent)
    {
        CHECKPOINT_SETUP;
        if (test_keyword_offset(Keyword::IMPORT, 0) && test_token_offset(Token_Type::LITERAL, 1))
        {
            if (get_token(1)->options.literal_value.type != Literal_Type::STRING) {
                CHECKPOINT_EXIT;
            }

            assert(parent->type == Node_Type::MODULE, "");
            auto module = (Module*)parent;
            auto import = allocate_base<Project_Import>(parent, Node_Type::PROJECT_IMPORT);
            import->filename = get_token(1)->options.literal_value.options.string;
            advance_token();
            advance_token();
            PARSE_SUCCESS(import);
        }
        CHECKPOINT_EXIT;
    }

    Definition* parse_definition(Node* parent)
    {
        // INFO: Definitions cannot start in the middle of a line
        CHECKPOINT_SETUP;
        auto result = allocate_base<Definition>(parent, AST::Node_Type::DEFINITION);
        result->is_comptime = false;

        if (parser.state.pos.token != 0) CHECKPOINT_EXIT;
        if (!test_token(Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;
        result->name = get_token(0)->options.identifier;
        advance_token();

        if (test_operator(Operator::COLON))
        {
            advance_token();
            result->type = optional_make_success(parse_expression_or_error_expr((Node*)result));

            bool is_assign = test_operator(Operator::ASSIGN);
            if (is_assign || test_operator(Operator::COLON)) {
                result->is_comptime = !is_assign;
                advance_token();
                result->value = optional_make_success(parse_expression_or_error_expr((Node*)result));
            }
        }
        else if (test_operator(Operator::DEFINE_COMPTIME)) {
            advance_token();
            result->is_comptime = true;
            result->value = optional_make_success(parse_expression_or_error_expr((Node*)result));
        }
        else if (test_operator(Operator::DEFINE_INFER)) {
            advance_token();
            result->is_comptime = false;
            result->value = optional_make_success(parse_expression_or_error_expr((Node*)result));
        }
        else {
            CHECKPOINT_EXIT;
        }

        PARSE_SUCCESS(result);
    }

#undef CHECKPOINT_EXIT
#undef CHECKPOINT_SETUP
#undef PARSE_SUCCESS
#undef SET_END_RANGE



    Parse_Pass* execute_clean(Source_Code* code)
    {
        Parse_Pass* pass = new Parse_Pass;
        pass->block_parses = dynamic_array_create_empty<Block_Parse>(1);
        pass->code = code;
        pass->error_messages = dynamic_array_create_empty<Error_Message>(1);
        pass->timestamp.node_index = 0;
        parser.pass = pass;

        // Create/Parse root module
        pass->root = allocate_base<Module>(0, Node_Type::MODULE);
        pass->root->definitions = dynamic_array_create_empty<Definition*>(1);
        pass->root->imports = dynamic_array_create_empty<Project_Import*>(1);
        parse_source_block(block_index_make_root(code), pass->root, parse_item_fn_module_item, parse_block_fn_anonymous_module_block);
        pass->root->base.range = token_range_make_block(block_index_make_root(code));
        pass->root->base.bounding_range = pass->root->base.range;

        // Correct token ranges 
        // NOTE: This Step is indeed necessary because Token-Indices can be ambiguous, 
        // but it probably could be done during parsing (e.g. on PARSER_SUCCESS) and not as an post processing step 
        base_correct_token_ranges(AST::upcast(pass->root));
        return pass;
    }

    void block_parse_destroy(Block_Parse* parse) {
        dynamic_array_destroy(&parse->nodes);
    }

    void parse_pass_destroy(Parse_Pass* pass)
    {
        dynamic_array_destroy(&pass->error_messages);
        for (int i = 0; i < pass->block_parses.size; i++) {
            block_parse_destroy(&pass->block_parses[i]);
        }
        dynamic_array_destroy(&pass->block_parses);
    }

    void parse_pass_reset(Parse_Pass* pass)
    {
        for (int i = 0; i < pass->block_parses.size; i++) {
            block_parse_destroy(&pass->block_parses[i]);
        }
        dynamic_array_reset(&pass->block_parses);
        dynamic_array_reset(&pass->error_messages);
        pass->root = 0;
    }

    void execute_incremental(Parse_Pass* pass, Code_History* history)
    {
        parse_pass_reset(pass);

        auto code = history->code;
        parser.pass = pass;
        parser.state.error_count = 0;

        // TODO: Change this
        // Create/Parse root module
        pass->root = allocate_base<Module>(0, Node_Type::MODULE);
        pass->root->definitions = dynamic_array_create_empty<Definition*>(1);
        pass->root->imports = dynamic_array_create_empty<Project_Import*>(1);
        parse_source_block(block_index_make_root(code), pass->root, parse_item_fn_module_item, parse_block_fn_anonymous_module_block);
        pass->root->base.range = token_range_make_block(block_index_make_root(code));
        pass->root->base.bounding_range = pass->root->base.range;

        // Correct token ranges 
        // NOTE: This Step is indeed necessary because Token-Indices can be ambiguous, 
        // but it probably could be done during parsing (e.g. on PARSER_SUCCESS) and not as an post processing step 
        base_correct_token_ranges(AST::upcast(pass->root));
    }



    void ast_base_get_section_token_range(AST::Node* base, Section section, Dynamic_Array<Token_Range>* ranges)
    {
        switch (section)
        {
        case Section::NONE: break;
        case Section::WHOLE:
        {
            dynamic_array_push_back(ranges, base->range);
            break;
        }
        case Section::WHOLE_NO_CHILDREN:
        {
            Token_Range range;
            range.start = base->range.start;
            int index = 0;
            auto child = AST::base_get_child(base, index);
            while (child != 0)
            {
                if (!index_equal(range.start, child->range.start)) {
                    range.end = child->range.start;
                    dynamic_array_push_back(ranges, range);
                }
                range.start = child->range.end;

                index += 1;
                child = AST::base_get_child(base, index);
            }
            if (!index_equal(range.start, base->range.end)) {
                range.end = base->range.end;
                dynamic_array_push_back(ranges, range);
            }
            break;
        }
        case Section::IDENTIFIER:
        {
            auto result = search_token(base->range.start, [](Token* t, void* _unused) -> bool {return t->type == Token_Type::IDENTIFIER; }, 0, false);
            if (!result.available) {
                break;
            }
            dynamic_array_push_back(ranges, token_range_make_offset(result.value, 1));
            break;
        }
        case Section::ENCLOSURE:
        {
            // Find next (), {} or [], and add the tokens to the ranges
            auto result = search_token(base->range.start, [](Token* t, void* type) -> bool {return t->type == Token_Type::PARENTHESIS; }, 0, false);
            if (!result.available) {
                break;
            }
            dynamic_array_push_back(ranges, token_range_make_offset(result.value, 1));

            // Find closing parenthesis
            auto par_type = index_value(result.value)->options.parenthesis.type;
            advance_token();
            auto end_token = search_token(
                result.value,
                [](Token* t, void* type) -> bool
                { return t->type == Token_Type::PARENTHESIS &&
                !t->options.parenthesis.is_open && t->options.parenthesis.type == *((Parenthesis_Type*)type); },
                (void*)(&par_type), true
            );
            if (!end_token.available) {
                break;
            }
            dynamic_array_push_back(ranges, token_range_make_offset(end_token.value, 1));
            break;
        }
        case Section::KEYWORD:
        {
            auto result = search_token(base->range.start, [](Token* t, void* type) -> bool {return t->type == Token_Type::KEYWORD; }, 0, false);
            if (!result.available) {
                break;
            }
            dynamic_array_push_back(ranges, token_range_make_offset(result.value, 1));
            break;
        }
        case Section::END_TOKEN: {
            Token_Range range = token_range_make(token_index_advance(base->range.end, -1), base->range.end);
            assert(range.start.token > 0, "Hey");
            dynamic_array_push_back(ranges, range);
            break;
        }
        default: panic("");
        }
    }

    AST::Node* find_smallest_enclosing_node(AST::Node* base, Token_Index index)
    {
        int child_index = 0;
        AST::Node* child = AST::base_get_child(base, child_index);
        if (child == 0) {
            return base;
        }
        while (child != 0) {
            auto& child_range = child->bounding_range;
            if (token_range_contains(child_range, index)) {
                return find_smallest_enclosing_node(child, index);
            }
            child_index += 1;
            child = AST::base_get_child(base, child_index);
        }
        return base;
    }
}
