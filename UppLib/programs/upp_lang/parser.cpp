#include "parser.hpp"

#include "ast.hpp"
#include "syntax_editor.hpp"
#include "compiler.hpp"

namespace Parser
{
    // Types
    using namespace AST;

    struct Binop_Link
    {
        Binop binop;
        Expression* expr;
    };

    struct Parse_State
    {
        Token_Position pos;
        int allocated_count;
        int error_count;
    };

    struct Parse_Info
    {
        AST::Base* allocation;
        Token_Range range;
    };

    struct Parser
    {
        Parse_State state;
        Dynamic_Array<Parse_Info> parse_informations;
        Dynamic_Array<Error_Message> error_messages;
        Token_Code* code;
        AST::Module* root;
    };

    // Globals
    static Parser parser;

    Array<Error_Message> get_error_messages() {
        return dynamic_array_as_array(&parser.error_messages);
    }

    // Functions
    void parser_rollback(Parse_State checkpoint)
    {
        for (int i = checkpoint.allocated_count; i < parser.parse_informations.size; i++) {
            AST::base_destroy(parser.parse_informations[i].allocation);
        }
        dynamic_array_rollback_to_size(&parser.parse_informations, checkpoint.allocated_count);
        dynamic_array_rollback_to_size(&parser.error_messages, checkpoint.error_count);
        parser.state = checkpoint;
    }

    void reset()
    {
        parser.root = 0;

        Parse_State state;
        state.pos.line = 0;
        state.pos.token = 0;
        state.allocated_count = 0;
        state.error_count = 0;
        parser_rollback(state);

        dynamic_array_reset(&parser.error_messages);
    }

    void initialize()
    {
        parser.parse_informations = dynamic_array_create_empty<Parse_Info>(32);
        parser.error_messages = dynamic_array_create_empty<Error_Message>(4);
        reset();
    }

    void destroy()
    {
        reset();
        dynamic_array_destroy(&parser.parse_informations);
        dynamic_array_destroy(&parser.error_messages);
    }

    // Allocations
    template<typename T>
    T* allocate_base(Base* parent, Base_Type type)
    {
        auto result = new T;
        memory_zero(result);
        Base* base = &result->base;
        base->parent = parent;
        base->type = type;
        base->allocation_index = parser.parse_informations.size;

        Parse_Info info;
        info.allocation = base;
        info.range.start = parser.state.pos;
        info.range.end = parser.state.pos;
        dynamic_array_push_back(&parser.parse_informations, info);
        parser.state.allocated_count = parser.parse_informations.size;

        return result;
    }

    void log_error(const char* msg, Token_Range range)
    {
        Error_Message err;
        err.msg = msg;
        err.range = range;
        dynamic_array_push_back(&parser.error_messages, err);
        parser.state.error_count = parser.error_messages.size;
    }

    void log_error_to_pos(const char* msg, Token_Position pos) {
        Token_Range range;
        range.start = parser.state.pos;
        range.end = pos;
        log_error(msg, range);
    }

    void log_error_range_offset(const char* msg, int token_offset) {
        Token_Range range;
        range.start = parser.state.pos;
        range.end = range.start;
        range.end.token += token_offset;
        log_error(msg, range);
    }

    void log_error_range_offset_with_start(const char* msg, Token_Position start, int token_offset) {
        Token_Range range;
        range.start = start;
        range.end = range.start;
        range.end.token += token_offset;
        log_error(msg, range);
    }



    // Positional Stuff
    bool on_block() {
        auto& p = parser.state.pos;
        return p.block >= 0 && p.block < parser.code->blocks.size;
    }

    Token_Block* get_block() {
        assert(on_block(), "");
        auto& p = parser.state.pos;
        return &parser.code->blocks[p.block];
    }

    bool on_line() {
        auto& p = parser.state.pos;
        if (!on_block()) return false;
        return p.line >= 0 && p.line < get_block()->lines.size;
    }

    Token_Line* get_line() {
        assert(on_line(), "");
        auto& p = parser.state.pos;
        return &get_block()->lines[p.line];
    }

    bool on_token() {
        auto& p = parser.state.pos;
        if (!on_line()) return false;
        return p.token >= 0 && p.token < get_line()->tokens.size;
    }

    Token* get_token() {
        assert(on_token(), "");
        auto& p = parser.state.pos;
        return &get_line()->tokens[p.token];
    }

    int remaining_line_tokens() {
        if (!on_line()) return 0;
        auto& p = parser.state.pos;
        return get_line()->tokens.size - p.token;
    }

    bool on_follow_block() {
        if (!on_line()) return false;
        auto line = get_line();
        if (!line->follow_block.available) return false;
        return remaining_line_tokens() == 0;
    }

    // Returns 0 if not on token
    Token* get_token(int offset) 
    {
        if (offset >= remaining_line_tokens()) return 0;
        return &get_line()->tokens[parser.state.pos.token + offset];
    }

    void advance_token() {
        parser.state.pos.token += 1;
    }

    void advance_line() {
        parser.state.pos.line += 1;
        parser.state.pos.token = 0;
    }



    // Helpers
    bool test_token_offset(Token_Type type, int offset) {
        if (offset >= remaining_line_tokens()) return false;
        auto token = get_token(offset);
        if (token == 0) return false;
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

    bool test_parenthesis_offset(char c, int offset) {
        Parenthesis p = char_to_parenthesis(c);
        if (!test_token_offset(Token_Type::PARENTHESIS, offset))
            return false;
        auto given = get_token(offset)->options.parenthesis;
        return given.is_open == p.is_open && given.type == p.type;
    }



    // Prototypes
    Definition* parse_definition(Base* parent, bool& add_to_fill);
    Statement* parse_statement(Base* parent, bool& add_to_fill);
    Definition* parse_module_item(Base* parent, bool& add_to_fill);
    Expression* parse_expression(Base* parent);
    Expression* parse_expression_or_error_expr(Base* parent);
    Expression* parse_single_expression(Base* parent);
    Expression* parse_single_expression_or_error(Base* parent);

#define CHECKPOINT_SETUP \
        if (get_token(0) == 0) {return 0;}\
        auto checkpoint = parser.state;\
        bool _error_exit = false;\
        SCOPE_EXIT(if (_error_exit) parser_rollback(checkpoint););

#define CHECKPOINT_EXIT {_error_exit = true; return 0;}
#define SET_END_RANGE(val) parser.parse_informations[val->base.allocation_index].range.end = parser.state.pos;
#define PARSE_SUCCESS(val) { \
        if (val->base.type == Base_Type::CODE_BLOCK) return val; \
        parser.parse_informations[val->base.allocation_index].range.end = parser.state.pos; \
        return val; \
    }

    // Parsing Helpers
    typedef bool(*token_predicate_fn)(Token* token, void* user_data);
    Optional<Token_Position> find_error_recovery_token(token_predicate_fn predicate, void* user_data, bool skip_blocks)
    {
        Dynamic_Array<Parenthesis> parenthesis_stack = dynamic_array_create_empty<Parenthesis>(1);
        SCOPE_EXIT(dynamic_array_destroy(&parenthesis_stack));

        Token_Position pos = parser.state.pos;
        auto block = token_position_get_block(pos, parser.code);
        if (block == 0) return optional_make_failure<Token_Position>();
        auto& lines = block->lines;

        auto line = &lines[pos.line];
        Array<Token> tokens = line->tokens;
        while (true)
        {
            if (pos.token >= tokens.size)
            {
                if (!(skip_blocks || parenthesis_stack.size != 0)) {
                    return optional_make_failure<Token_Position>();
                }
                // Parenthesis aren't allowed to reach over blocks if there is no follow_block
                if (!line->follow_block.available) {
                    return optional_make_failure<Token_Position>();
                }
                if (pos.line + 1 >= lines.size) {
                    return optional_make_failure<Token_Position>();
                }
                pos.line = pos.line + 1;
                pos.token = 0;
                line = &lines[pos.line];
                tokens = line->tokens;
            }

            Token* token = &tokens[pos.token];
            if (parenthesis_stack.size == 0 && predicate(token, user_data)) {
                return optional_make_success(pos);
            }
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
        }
    }

    Token_Position position_make_line_end(int block, int line_index) {
        Token_Position pos;
        pos.block = block;
        pos.line = line_index;
        auto line = token_position_get_line(pos, parser.code);
        if (line == 0) {
            pos.token = 0;
            return pos;
        }
        pos.token = line->tokens.size;
        return pos;
    }

    template<typename T>
    void parse_syntax_block(int block_index, Base* parent, Dynamic_Array<T*>* fill_array, T* (*parse_fn)(Base* parent, bool& add_to_fill))
    {
        // Setup parser position at block start
        Token_Position& pos = parser.state.pos;
        pos.block = block_index;
        pos.line = 0;
        pos.token = 0;
        parser.parse_informations[parent->allocation_index].range.start = pos;

        // Parse block
        while (true)
        {
            auto line = token_position_get_line(pos, parser.code);
            if (line == 0) break;

            auto before_line_index = pos.line;
            pos.token = 0;
            // Skip empty lines
            if (line->tokens.size == 0 && !line->follow_block.available) {
                pos.line += 1;
                continue;
            }

            // Parse line
            bool add_to_array;
            T* line_item = parse_fn(parent, add_to_array);
            if (add_to_array)
            {
                if (line_item != 0) {
                    dynamic_array_push_back(fill_array, line_item);
                }
                else {
                    log_error_to_pos("Couldn't parse line", position_make_line_end(pos.block, pos.line));
                }
            }

            // Check if whole line was parsed
            if (before_line_index == pos.line || pos.token != 0)
            {
                line = token_position_get_line(pos, parser.code); // Requery line because parse_fn could have changed line index
                if (line == 0) {
                    break;
                }
                if (pos.token < line->tokens.size) {
                    log_error_to_pos("Unexpected Tokens, Line already parsed", position_make_line_end(pos.block, pos.line));
                }
                if (line->follow_block.available) {
                    log_error_to_pos("Unexpected follow block, Line already parsed", position_make_line_end(pos.block, pos.line));
                }
                pos.line += 1;
                pos.token = 0;
            }
        }
        parser.parse_informations[parent->allocation_index].range.end = pos;
    }

    template<typename T>
    void parse_follow_block(Base* parent, Dynamic_Array<T*>* fill_array, T* (*parse_fn)(Base* parent, bool& add_to_fill), bool parse_if_not_on_end)
    {
        auto line = get_line();
        assert(line != 0, "");
        // Check if at end of line
        if (!line->follow_block.available || (!parse_if_not_on_end && !on_follow_block())) {
            log_error_range_offset("Expected Follow Block", 1);
            return;
        }
        if (!on_follow_block()) {
            log_error_range_offset("Parsing follow block, ignoring rest of line", line->tokens.size - parser.state.pos.token);
        }
        auto next_pos = parser.state.pos;
        next_pos.line += 1;
        next_pos.token = 0;
        parse_syntax_block(line->follow_block.value, parent, fill_array, parse_fn);
        parser.state.pos = next_pos;
    }

    template<Parenthesis_Type type>
    bool successfull_parenthesis_exit()
    {
        auto parenthesis_pos = find_error_recovery_token(
            [](Token* t, void* _unused) -> bool
            { return t->type == Token_Type::PARENTHESIS &&
            !t->options.parenthesis.is_open && t->options.parenthesis.type == type; },
            0, true
        );
        if (!parenthesis_pos.available) {
            return false;
        }
        parser.state.pos = parenthesis_pos.value;
        advance_token();
        return true;
    }

    // Parser position must be on Open Parenthesis for this to work
    template<typename T>
    void parse_parenthesis_comma_seperated(Base* parent, Dynamic_Array<T*>* fill_array, T* (*parse_fn)(Base* parent), Parenthesis_Type type)
    {
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

            // Error Recovery
            auto comma_pos = find_error_recovery_token(
                [](Token* t, void* _unused) -> bool
                { return t->type == Token_Type::OPERATOR && t->options.op == Operator::COMMA; },
                0, true
            );
            auto parenthesis_pos = find_error_recovery_token(
                [](Token* t, void* _unused) -> bool
                { return t->type == Token_Type::PARENTHESIS &&
                !t->options.parenthesis.is_open && t->options.parenthesis.type == Parenthesis_Type::PARENTHESIS; },
                0, true
            );
            enum class Error_Start { COMMA, PARENTHESIS, NOT_FOUND } tactic;
            tactic = Error_Start::NOT_FOUND;
            if (comma_pos.available && parenthesis_pos.available) {
                tactic = token_position_in_order(comma_pos.value, parenthesis_pos.value, parser.code) ? Error_Start::COMMA : Error_Start::PARENTHESIS;
            }
            else if (comma_pos.available) {
                tactic = Error_Start::COMMA;
            }
            else if (parenthesis_pos.available) {
                tactic = Error_Start::PARENTHESIS;
            }

            if (tactic == Error_Start::COMMA) {
                log_error_to_pos("Couldn't parse list item", comma_pos.value);
                parser.state.pos = comma_pos.value;
                advance_token();
            }
            else if (tactic == Error_Start::PARENTHESIS) {
                log_error_to_pos("Couldn't parse list item", parenthesis_pos.value);
                parser.state.pos = parenthesis_pos.value;
            }
            else {
                log_error_range_offset_with_start("Couldn't find closing_parenthesis", open_parenthesis_pos, 1);
                // TODO: Think/Test this case
                auto line = get_line();
                if (line == 0) return;
                parser.state.pos.token = line->tokens.size; // Goto end of line for now
                return;
            }
        }
    }

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

    // Parsing
    Code_Block* parse_code_block(Base* parent, AST::Expression* related_expression)
    {
        auto result = allocate_base<Code_Block>(parent, Base_Type::CODE_BLOCK);
        result->statements = dynamic_array_create_empty<Statement*>(1);
        result->block_id = parse_block_label(related_expression);
        parse_follow_block(parent, &result->statements, parse_statement, true);
        PARSE_SUCCESS(result);
    }

    Argument* parse_argument(Base* parent)
    {
        CHECKPOINT_SETUP;
        auto result = allocate_base<Argument>(parent, Base_Type::ARGUMENT);
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

    Parameter* parse_parameter(Base* parent)
    {
        CHECKPOINT_SETUP;
        auto result = allocate_base<Parameter>(parent, Base_Type::PARAMETER);
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
        result->type = parse_expression_or_error_expr((Base*)result);

        if (test_operator(Operator::ASSIGN)) {
            result->type = parse_expression_or_error_expr((Base*)result);
        }
        PARSE_SUCCESS(result);
    }

    Switch_Case* parse_switch_case(Base* parent, bool& add_to_fill)
    {
        add_to_fill = true;
        if (!test_keyword_offset(Keyword::CASE, 0) && !test_keyword_offset(Keyword::DEFAULT, 0)) {
            return 0;
        }

        auto result = allocate_base<Switch_Case>(parent, Base_Type::SWITCH_CASE);
        bool is_default = test_keyword_offset(Keyword::DEFAULT, 0);
        advance_token();
        result->value.available = false;
        if (!is_default) {
            result->value = optional_make_success(parse_expression_or_error_expr(&result->base));
        }
        result->block = parse_code_block(&result->base, 0);

        // Set block label (Switch cases need special treatment because they 'Inherit' the label from the switch
        assert(parent->type == Base_Type::STATEMENT && ((Statement*)parent)->type == Statement_Type::SWITCH_STATEMENT, "");
        result->block->block_id = ((Statement*)parent)->options.switch_statement.label;
        return result;
    }

    Statement* parse_statement(Base* parent, bool& add_to_fill)
    {
        CHECKPOINT_SETUP;

        auto result = allocate_base<Statement>(parent, Base_Type::STATEMENT);
        {
            // This needs to be done before definition
            auto line = get_line();
            if ((line->tokens.size == 0) ||
                (line->tokens.size == 2 && test_token(Token_Type::IDENTIFIER) && test_operator_offset(Operator::COLON, 1)))
            {
                result->type = Statement_Type::BLOCK;
                result->options.block = parse_code_block(&result->base, 0);
                PARSE_SUCCESS(result);
            }
        }

        {
            bool _unused;
            auto definition = parse_definition(&result->base, _unused);
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
                    advance_token();
                    auto else_block = allocate_base<AST::Code_Block>(&result->base, Base_Type::CODE_BLOCK);
                    else_block->statements = dynamic_array_create_empty<Statement*>(1);
                    else_block->block_id = optional_make_failure<String*>();
                    auto new_if_stat = allocate_base<AST::Statement>(&result->base, Base_Type::STATEMENT);
                    new_if_stat->type = Statement_Type::IF_STATEMENT;
                    dynamic_array_push_back(&else_block->statements, new_if_stat);
                    last_if_stat->options.if_statement.else_block = optional_make_success(else_block);
                    last_if_stat = new_if_stat;

                    advance_token();
                    auto& new_if = new_if_stat->options.if_statement;
                    new_if.condition = parse_expression_or_error_expr(&new_if_stat->base);
                    new_if.block = parse_code_block(&result->base, new_if.condition);
                    new_if.else_block.available = false;
                }
                if (test_keyword_offset(Keyword::ELSE, 0))
                {
                    advance_token();
                    last_if_stat->options.if_statement.else_block = optional_make_success(parse_code_block(&last_if_stat->base, 0));
                }
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
                switch_stat.label.available = false;
                switch_stat.label = parse_block_label(switch_stat.condition);
                parse_follow_block(&result->base, &switch_stat.cases, parse_switch_case, true);
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

    Enum_Member* parse_enum_member(Base* parent, bool& add_to_fill)
    {
        add_to_fill = true;
        if (!test_token(Token_Type::IDENTIFIER)) {
            return 0;
        }

        CHECKPOINT_SETUP;
        auto result = allocate_base<Enum_Member>(parent, Base_Type::ENUM_MEMBER);
        result->name = get_token(0)->options.identifier;
        advance_token();
        if (test_operator(Operator::DEFINE_COMPTIME))
        {
            advance_token();
            result->value = optional_make_success(parse_expression_or_error_expr(&result->base));
        }
        PARSE_SUCCESS(result);
    }

    Expression* parse_single_expression_no_postop(Base* parent)
    {
        // Note: Single expression no postop means pre-ops + bases, e.g.
        // --*x   -> is 3 pre-ops + base x
        CHECKPOINT_SETUP;
        auto result = allocate_base<Expression>(parent, Base_Type::EXPRESSION);

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
                    if (!successfull_parenthesis_exit<Parenthesis_Type::BRACES>()) CHECKPOINT_EXIT;
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
            if (!successfull_parenthesis_exit<Parenthesis_Type::BRACKETS>()) CHECKPOINT_EXIT;
            result->options.array_type.type_expr = parse_single_expression_or_error(&result->base);
            PARSE_SUCCESS(result);
        }

        // Bases
        if (test_token(Token_Type::IDENTIFIER))
        {
            Symbol_Read* final_read = allocate_base<Symbol_Read>(&result->base, Base_Type::SYMBOL_READ);
            Symbol_Read* read = final_read;
            read->path_child.available = false;
            read->name = get_token(0)->options.identifier;
            while (test_token(Token_Type::IDENTIFIER) &&
                test_operator_offset(Operator::TILDE, 1) &&
                test_token_offset(Token_Type::IDENTIFIER, 2))
            {
                advance_token();
                advance_token();
                read->path_child = optional_make_success(allocate_base<Symbol_Read>(&read->base, Base_Type::SYMBOL_READ));
                read->path_child.value->name = get_token(0)->options.identifier;
                read = read->path_child.value;
                SET_END_RANGE(read);
            }

            result->type = Expression_Type::SYMBOL_READ;
            result->options.symbol_read = final_read;
            advance_token();
            SET_END_RANGE(final_read);
            PARSE_SUCCESS(result);
        }

        if (test_operator(Operator::DOT))
        {
            advance_token();
            if (test_token(Token_Type::IDENTIFIER)) // Member access
            {
                result->type = Expression_Type::AUTO_ENUM;
                result->options.auto_enum = get_token(0)->options.identifier;
                advance_token();
                PARSE_SUCCESS(result);
            }
            else if (test_parenthesis_offset('{', 0)) // Struct Initializer
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
                signature.return_value = optional_make_success(parse_expression_or_error_expr((Base*)result));
            }

            if (!on_follow_block()) {
                PARSE_SUCCESS(result);
            }

            // Check if its a function or just a function signature
            auto signature_expr = result;
            result = allocate_base<Expression>(parent, Base_Type::EXPRESSION);
            result->type = Expression_Type::FUNCTION;
            auto& function = result->options.function;
            function.body = parse_code_block(&result->base, 0);
            function.signature = signature_expr;
            signature_expr->base.parent = (Base*)result;
            PARSE_SUCCESS(result);
        }

        if (test_parenthesis_offset('(', 0))
        {
            parser_rollback(checkpoint); // This is pretty stupid, but needed to reset result
            advance_token();
            result = parse_expression_or_error_expr(parent);
            if (!successfull_parenthesis_exit<Parenthesis_Type::PARENTHESIS>()) CHECKPOINT_EXIT;
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
                if (!successfull_parenthesis_exit<Parenthesis_Type::BRACKETS>()) CHECKPOINT_EXIT;
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
            parse_follow_block(&result->base, &result->options.structure.members, parse_definition, false);
            PARSE_SUCCESS(result);
        }
        if (test_keyword_offset(Keyword::ENUM, 0)) {
            result->type = Expression_Type::ENUM_TYPE;
            result->options.enum_members = dynamic_array_create_empty<Enum_Member*>(1);
            advance_token();
            parse_follow_block(&result->base, &result->options.enum_members, parse_enum_member, false);
            PARSE_SUCCESS(result);
        }
        if (test_keyword_offset(Keyword::MODULE, 0)) {
            auto module = allocate_base<Module>(&result->base, Base_Type::MODULE);
            module->definitions = dynamic_array_create_empty<Definition*>(1);
            module->imports = dynamic_array_create_empty<Project_Import*>(1);
            advance_token();
            parse_follow_block(&module->base, &module->definitions, parse_module_item, false);

            result->type = Expression_Type::MODULE;
            result->options.module = module;
            PARSE_SUCCESS(result);
        }

        CHECKPOINT_EXIT;
    }

    Expression* parse_post_operator_internal(Expression* child)
    {
        CHECKPOINT_SETUP;
        // Post operators
        auto result = allocate_base<Expression>(child->base.parent, Base_Type::EXPRESSION);
        if (test_operator(Operator::DOT))
        {
            advance_token();
            if (test_token(Token_Type::IDENTIFIER)) // Member access
            {
                result->type = Expression_Type::MEMBER_ACCESS;
                result->options.member_access.name = get_token(0)->options.identifier;
                result->options.member_access.expr = child;
                advance_token();
                PARSE_SUCCESS(result);
            }
            else if (test_parenthesis_offset('{', 0)) // Struct Initializer
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
            CHECKPOINT_EXIT;
        }
        else if (test_parenthesis_offset('[', 0)) // Array access
        {
            advance_token();
            result->type = Expression_Type::ARRAY_ACCESS;
            result->options.array_access.array_expr = child;
            result->options.array_access.index_expr = parse_expression_or_error_expr(&result->base);
            if (!successfull_parenthesis_exit<Parenthesis_Type::BRACKETS>()) CHECKPOINT_EXIT;
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

    Expression* parse_single_expression(Base* parent)
    {
        // This function only does post-op parsing
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

    Expression* parse_single_expression_or_error(Base* parent)
    {
        auto expr = parse_single_expression(parent);
        if (expr != 0) {
            return expr;
        }
        log_error_range_offset("Expected Single Expression", 1);
        expr = allocate_base<AST::Expression>(parent, AST::Base_Type::EXPRESSION);
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
                Expression* result = allocate_base<Expression>(0, Base_Type::EXPRESSION);
                result->type = Expression_Type::BINARY_OPERATION;
                result->options.binop.type = link.binop;
                result->options.binop.left = expr;
                result->options.binop.right = parse_priority_level(link.expr, priority_level + 1, links, index);
                result->options.binop.left->base.parent = &result->base;
                result->options.binop.right->base.parent = &result->base;
                expr = result;
            }
            else {
                break;
            }
        }
        return expr;
    }

    Expression* parse_expression(Base* parent)
    {
        CHECKPOINT_SETUP;
        Expression* start_expr = parse_single_expression(parent);
        if (start_expr == 0) return 0;

        Dynamic_Array<Binop_Link> links = dynamic_array_create_empty<Binop_Link>(1);
        SCOPE_EXIT(dynamic_array_destroy(&links));
        while (true)
        {
            Binop_Link link;
            link.binop = Binop::INVALID;
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
        PARSE_SUCCESS(result);
    }

    Expression* parse_expression_or_error_expr(Base* parent)
    {
        auto expr = parse_expression(parent);
        if (expr != 0) {
            return expr;
        }
        log_error_range_offset("Expected Expression", 1);
        expr = allocate_base<AST::Expression>(parent, AST::Base_Type::EXPRESSION);

        expr->type = Expression_Type::ERROR_EXPR;
        PARSE_SUCCESS(expr);
    }

    Definition* parse_definition(Base* parent, bool& add_to_fill)
    {
        add_to_fill = true;
        CHECKPOINT_SETUP;
        auto result = allocate_base<Definition>(parent, AST::Base_Type::DEFINITION);
        result->is_comptime = false;

        if (parser.state.pos.token != 0) CHECKPOINT_EXIT;
        if (!test_token(Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;
        result->name = get_token(0)->options.identifier;
        advance_token();

        int prev_line_index = parser.state.pos.line;
        if (test_operator(Operator::COLON))
        {
            advance_token();
            result->type = optional_make_success(parse_expression_or_error_expr((Base*)result));

            bool is_assign = test_operator(Operator::ASSIGN);
            if (is_assign || test_operator(Operator::COLON)) {
                result->is_comptime = !is_assign;
                advance_token();
                result->value = optional_make_success(parse_expression_or_error_expr((Base*)result));
            }
        }
        else if (test_operator(Operator::DEFINE_COMPTIME)) {
            advance_token();
            result->is_comptime = true;
            result->value = optional_make_success(parse_expression_or_error_expr((Base*)result));
        }
        else if (test_operator(Operator::DEFINE_INFER)) {
            advance_token();
            result->is_comptime = false;
            result->value = optional_make_success(parse_expression_or_error_expr((Base*)result));
        }
        else {
            CHECKPOINT_EXIT;
        }

        // Definitions are one line long, so everything afterwards here is an error
        // But this should be handled by block parser if i'm not mistaken

        PARSE_SUCCESS(result);
    }

    Definition* parse_module_item(Base* parent, bool& add_to_fill)
    {
        CHECKPOINT_SETUP;
        if (test_keyword_offset(Keyword::IMPORT, 0) && test_token_offset(Token_Type::LITERAL, 1))
        {
            if (get_token(1)->options.literal_value.type != Literal_Type::STRING) {
                CHECKPOINT_EXIT;
            }

            assert(parent->type == Base_Type::MODULE, "");
            auto module = (Module*)parent;
            auto import = allocate_base<Project_Import>(parent, Base_Type::PROJECT_IMPORT);
            import->filename = get_token(1)->options.literal_value.options.string;
            advance_token();
            advance_token();
            SET_END_RANGE(import);
            dynamic_array_push_back(&module->imports, import);

            add_to_fill = false;
            return 0;
        }

        return parse_definition(parent, add_to_fill);
    }

    void base_correct_token_ranges(Base* base)
    {
        int index = 0;
        auto child = AST::base_get_child(base, index);
        if (child == 0) return;
        Token_Position child_start = parser.parse_informations[child->allocation_index].range.start;
        Token_Position child_end;
        while (child != 0) {
            base_correct_token_ranges(child);
            child_end = parser.parse_informations[child->allocation_index].range.end;
            index += 1;
            child = AST::base_get_child(base, index);
        }

        auto& info = parser.parse_informations[base->allocation_index];
        token_position_sanitize(&child_start, parser.code);
        token_position_sanitize(&child_end, parser.code);
        token_position_sanitize(&info.range.start, parser.code);
        token_position_sanitize(&info.range.end, parser.code);
        if (token_position_in_order(child_start, info.range.start, parser.code)) {
            info.range.start = child_start;
        }
        if (token_position_in_order(info.range.end, child_end, parser.code)) {
            info.range.end = child_end;
        }
    }

    AST::Module* execute(Token_Code* tokens)
    {
        assert(tokens->blocks.size != 0, "");
        parser.code = tokens;
        parser.root = allocate_base<Module>(0, Base_Type::MODULE);
        parser.parse_informations[0].range.start.block = 0;
        parser.root->definitions = dynamic_array_create_empty<Definition*>(1);
        parser.root->imports = dynamic_array_create_empty<Project_Import*>(1);
        parse_syntax_block<Definition>(0, &parser.root->base, &parser.root->definitions, parse_module_item);
        SET_END_RANGE(parser.root);
        base_correct_token_ranges(&parser.root->base);
        return parser.root;
    }

#undef CHECKPOINT_EXIT
#undef CHECKPOINT_SETUP
#undef PARSE_SUCCESS
#undef SET_END_RANGE

    void ast_base_get_section_token_range(AST::Base* base, Section section, Dynamic_Array<Token_Range>* ranges)
    {
        auto& info = parser.parse_informations[base->allocation_index];
        switch (section)
        {
        case Section::NONE: break;
        case Section::WHOLE:
        {
            dynamic_array_push_back(ranges, info.range);
            break;
        }
        case Section::WHOLE_NO_CHILDREN:
        {
            Token_Range range;
            range.start = info.range.start;
            int index = 0;
            auto child = AST::base_get_child(base, index);
            while (child != 0)
            {
                auto& child_info = parser.parse_informations[child->allocation_index];
                if (!token_position_are_equal(range.start, child_info.range.start)) {
                    range.end = child_info.range.start;
                    dynamic_array_push_back(ranges, range);
                }
                range.start = child_info.range.end;

                index += 1;
                child = AST::base_get_child(base, index);
            }
            if (!token_position_are_equal(range.start, info.range.end)) {
                range.end = info.range.end;
                dynamic_array_push_back(ranges, range);
            }
            break;
        }
        case Section::IDENTIFIER:
        {
            parser.state.pos = info.range.start;
            auto result = find_error_recovery_token([](Token* t, void* _unused) -> bool {return t->type == Token_Type::IDENTIFIER; }, 0, false);
            if (!result.available) {
                break;
            }
            Token_Range range;
            range.start = result.value;
            range.end = range.start;
            range.end.token += 1;
            dynamic_array_push_back(ranges, range);
            break;
        }
        case Section::ENCLOSURE:
        {
            // Find next (), {} or [], and add the tokens to the ranges
            parser.state.pos = info.range.start;
            auto result = find_error_recovery_token([](Token* t, void* type) -> bool {return t->type == Token_Type::PARENTHESIS; }, 0, false);
            if (!result.available) {
                break;
            }
            Token_Range range;
            range.start = result.value;
            range.end = result.value;
            range.end.token += 1;
            dynamic_array_push_back(ranges, range);

            // Find closing parenthesis
            parser.state.pos = result.value;
            auto par_type = get_token(0)->options.parenthesis.type;
            advance_token();
            auto end_token = find_error_recovery_token(
                [](Token* t, void* type) -> bool
                { return t->type == Token_Type::PARENTHESIS &&
                !t->options.parenthesis.is_open && t->options.parenthesis.type == *((Parenthesis_Type*)type); },
                (void*)(&par_type), true
            );
            if (!end_token.available) {
                break;
            }
            range.start = end_token.value;
            range.end = end_token.value;
            range.end.token += 1;
            dynamic_array_push_back(ranges, range);
            break;
        }
        case Section::KEYWORD:
        {
            auto result = find_error_recovery_token([](Token* t, void* type) -> bool {return t->type == Token_Type::KEYWORD; }, 0, false);
            if (!result.available) {
                break;
            }
            Token_Range range;
            range.start = result.value;
            range.end = result.value;
            range.end.token += 1;
            dynamic_array_push_back(ranges, range);
            break;
        }
        case Section::END_TOKEN: {
            Token_Range range;
            range.end = info.range.end;
            range.start = info.range.end;
            range.start.token -= 1;
            assert(range.start.token > 0, "Hey");
            dynamic_array_push_back(ranges, range);
            break;
        }
        default: panic("");
        }
    }
}