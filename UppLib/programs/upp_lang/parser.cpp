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
        Syntax_Position pos;
        int allocated_count;
        int error_count;
    };

    struct Parse_Info
    {
        AST::Base* allocation;
        Syntax_Position start_pos;
        Syntax_Position end_pos;
    };

    struct Parser
    {
        Parse_State state;
        Dynamic_Array<Parse_Info> parse_informations;
        Dynamic_Array<Error_Message> error_messages;
        AST::Module* root;
    };

    // Globals
    static Parser parser;

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
        state.pos.block = 0;
        state.pos.line_index = 0;
        state.pos.token_index = 0;
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
        info.start_pos = parser.state.pos;
        info.end_pos = parser.state.pos;
        dynamic_array_push_back(&parser.parse_informations, info);
        parser.state.allocated_count = parser.parse_informations.size;

        return result;
    }

    void log_error(const char* msg, Syntax_Range range)
    {
        Error_Message err;
        err.msg = msg;
        err.range = range;
        dynamic_array_push_back(&parser.error_messages, err);
        parser.state.error_count = parser.error_messages.size;
    }

    void log_error_to_pos(const char* msg, Syntax_Position pos) {
        Syntax_Range range;
        range.start = parser.state.pos;
        range.end = pos;
        log_error(msg, range);
    }

    void log_error_range_offset(const char* msg, int token_offset) {
        Syntax_Range range;
        range.start = parser.state.pos;
        range.end = range.start;
        range.end.token_index += token_offset;
        log_error(msg, range);
    }

    void log_error_range_offset_with_start(const char* msg, Syntax_Position start, int token_offset) {
        Syntax_Range range;
        range.start = start;
        range.end = range.start;
        range.end.token_index += token_offset;
        log_error(msg, range);
    }



    Syntax_Line* get_line() {
        if (!syntax_position_on_line(parser.state.pos)) return 0;
        return syntax_position_get_line(parser.state.pos);
    }

    Syntax_Token* get_token(int offset);
    bool on_follow_block() {
        auto line = get_line();
        if (line == 0) return false;
        if (line->follow_block == 0) return false;
        if (syntax_line_is_empty(line) && !syntax_line_is_multi_line_comment(line)) return true;
        auto& pos = parser.state.pos;
        if (pos.token_index >= line->tokens.size) return true;
        if (get_token(0)->type == Syntax_Token_Type::COMMENT) return true;
        return false;
    }

    // Returns 0 if not on token
    Syntax_Token* get_token(int offset) {
        auto line = get_line();
        if (line == 0) return 0;
        auto& tokens = line->tokens;
        int tok_index = parser.state.pos.token_index + offset;
        if (tok_index >= tokens.size) return 0;
        return &tokens[tok_index];
    }

    void advance_token() {
        parser.state.pos.token_index += 1;
    }

    void advance_line() {
        parser.state.pos.line_index += 1;
        parser.state.pos.token_index = 0;
    }

    // Helpers
    bool test_token_offset(Syntax_Token_Type type, int offset) {
        auto token = get_token(offset);
        if (token == 0) return false;
        return token->type == type;
    }

    bool test_token(Syntax_Token_Type type) {
        return test_token_offset(type, 0);
    }

    bool test_token_2(Syntax_Token_Type t0, Syntax_Token_Type t1) {
        return test_token_offset(t0, 0) && test_token_offset(t1, 1);
    }

    bool test_token_3(Syntax_Token_Type t0, Syntax_Token_Type t1, Syntax_Token_Type t2) {
        return test_token_offset(t0, 0) && test_token_offset(t1, 1) && test_token_offset(t2, 2);
    }

    bool test_token_4(Syntax_Token_Type t0, Syntax_Token_Type t1, Syntax_Token_Type t2, Syntax_Token_Type t3) {
        return test_token_offset(t0, 0) && test_token_offset(t1, 1) && test_token_offset(t2, 2) && test_token_offset(t3, 3);
    }

    bool test_operator_offset(Syntax_Operator op, int offset) {
        if (!test_token_offset(Syntax_Token_Type::OPERATOR, offset))
            return false;
        return get_token(offset)->options.op == op;
    }

    bool test_operator(Syntax_Operator op) {
        return test_operator_offset(op, 0);
    }

    bool test_keyword_offset(Syntax_Keyword keyword, int offset) {
        if (!test_token_offset(Syntax_Token_Type::KEYWORD, offset))
            return false;
        return get_token(offset)->options.keyword == keyword;
    }

    bool test_parenthesis_offset(char c, int offset) {
        Parenthesis p = char_to_parenthesis(c);
        if (!test_token_offset(Syntax_Token_Type::PARENTHESIS, offset))
            return false;
        auto given = get_token(offset)->options.parenthesis;
        return given.is_open == p.is_open && given.type == p.type;
    }

    String* literal_string_handle_escapes(Syntax_Token* token)
    {
        assert(token->type == Syntax_Token_Type::LITERAL_STRING, "");
        auto text = token->options.literal_string.string;

        auto result_str = string_create_empty(text->size);
        SCOPE_EXIT(string_destroy(&result_str));

        // Parse String literal
        bool invalid_escape_found = false;
        bool last_was_escape = false;
        for (int i = 1; i < text->size; i++)
        {
            char c = text->characters[i];
            if (last_was_escape)
            {
                switch (c)
                {
                case 'n':
                    string_append_character(&result_str, '\n');
                    break;
                case 'r':
                    string_append_character(&result_str, '\r');
                    break;
                case 't':
                    string_append_character(&result_str, '\t');
                    break;
                case '\\':
                    string_append_character(&result_str, '\\');
                    break;
                case '\'':
                    string_append_character(&result_str, '\'');
                    break;
                case '\"':
                    string_append_character(&result_str, '\"');
                    break;
                case '\n':
                    break;
                default:
                    invalid_escape_found = true;
                    break;
                }
                last_was_escape = false;
            }
            else
            {
                if (c == '\"') {
                    break;
                }
                last_was_escape = c == '\\';
                if (!last_was_escape) {
                    string_append_character(&result_str, c);
                }
            }
        }
        if (invalid_escape_found) {
            log_error_range_offset("Invalid escape sequence found", 1);
        }

        return identifier_pool_add(&compiler.identifier_pool, result_str);
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
#define SET_END_RANGE(val) parser.parse_informations[val->base.allocation_index].end_pos = parser.state.pos;
#define PARSE_SUCCESS(val) { \
        if (val->base.type == Base_Type::CODE_BLOCK) return val; \
        parser.parse_informations[val->base.allocation_index].end_pos = parser.state.pos; \
        return val; \
    }

    // Parsing Helpers
    typedef bool(*token_predicate_fn)(Syntax_Token* token, void* user_data);
    Optional<Syntax_Position> find_error_recovery_token(token_predicate_fn predicate, void* user_data, bool skip_blocks)
    {
        Dynamic_Array<Parenthesis> parenthesis_stack = dynamic_array_create_empty<Parenthesis>(1);
        SCOPE_EXIT(dynamic_array_destroy(&parenthesis_stack));

        Syntax_Position pos = parser.state.pos;
        auto& lines = pos.block->lines;
        if (pos.line_index >= lines.size) return optional_make_failure<Syntax_Position>();

        Syntax_Line* line = lines[pos.line_index];
        Dynamic_Array<Syntax_Token>* tokens = &line->tokens;
        while (true)
        {
            if (pos.token_index >= tokens->size)
            {
                if (!(skip_blocks || parenthesis_stack.size != 0)) {
                    return optional_make_failure<Syntax_Position>();
                }
                // Parenthesis aren't allowed to reach over blocks if there is no follow_block 
                if (line->follow_block == 0) {
                    return optional_make_failure<Syntax_Position>();
                }
                if (pos.line_index + 1 >= lines.size) {
                    return optional_make_failure<Syntax_Position>();
                }
                pos.line_index = pos.line_index + 1;
                pos.token_index = 0;
                line = lines[pos.line_index];
                tokens = &line->tokens;
            }

            Syntax_Token* token = &(*tokens)[pos.token_index];
            if (parenthesis_stack.size == 0 && predicate(token, user_data)) {
                return optional_make_success(pos);
            }
            if (token->type == Syntax_Token_Type::PARENTHESIS)
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
            pos.token_index += 1;
        }
    }

    template<typename T>
    void parse_syntax_block(Syntax_Block* block, Base* parent, Dynamic_Array<T*>* fill_array, T* (*parse_fn)(Base* parent, bool& add_to_fill))
    {
        // Setup parser position at block start
        auto& pos = parser.state.pos;
        pos.block = block;
        pos.line_index = 0;
        pos.token_index = 0;
        parser.parse_informations[parent->allocation_index].start_pos = pos;

        // Parse block
        auto& lines = pos.block->lines;
        while (pos.line_index < lines.size)
        {
            auto line = lines[pos.line_index];
            auto before_line_index = pos.line_index;
            pos.token_index = 0;

            if ((syntax_line_is_empty(line) && line->follow_block == 0) || syntax_line_is_multi_line_comment(line)) {
                pos.line_index += 1;
                pos.token_index = 0;
                continue;
            }

            bool add_to_fill;
            T* parsed = parse_fn(parent, add_to_fill);
            if (add_to_fill)
            {
                if (parsed != 0) {
                    dynamic_array_push_back(fill_array, parsed);
                }
                else {
                    log_error_to_pos("Couldn't parse line", syntax_line_get_end_pos(line));
                }
            }

            if ((before_line_index == pos.line_index || pos.token_index != 0) && syntax_position_on_line(pos))
            {
                line = lines[pos.line_index];
                if (pos.token_index < line->tokens.size && line->tokens[pos.token_index].type != Syntax_Token_Type::COMMENT) {
                    log_error_to_pos("Unexpected Tokens, Line already parsed", syntax_line_get_end_pos(line));
                }
                if (line->follow_block != 0) {
                    Syntax_Range range;
                    range.start.block = line->follow_block;
                    range.start.token_index = 0;
                    range.start.line_index = 0;
                    range.end.block = line->follow_block;
                    range.end.line_index = line->follow_block->lines.size - 1;
                    range.end.token_index = line->follow_block->lines[range.end.line_index]->tokens.size;
                    log_error_to_pos("Unexpected follow block, Line already parsed", syntax_line_get_end_pos(line));
                }
                pos.line_index += 1;
                pos.token_index = 0;
            }
        }
        parser.parse_informations[parent->allocation_index].end_pos = pos;
    }

    template<typename T>
    void parse_follow_block(Base* parent, Dynamic_Array<T*>* fill_array, T* (*parse_fn)(Base* parent, bool& add_to_fill), bool parse_if_not_on_end)
    {
        auto line = get_line();
        assert(line != 0, "");
        // Check if at end of line
        if (line->follow_block == 0 || (!parse_if_not_on_end && !on_follow_block())) {
            log_error_range_offset("Expected Follow Block", 1);
            return;
        }
        if (!on_follow_block()) {
            log_error_range_offset("Parsing follow block, ignoring rest of line", line->tokens.size - parser.state.pos.token_index);
        }
        auto next_pos = parser.state.pos;
        next_pos.line_index += 1;
        next_pos.token_index = 0;
        parse_syntax_block(line->follow_block, parent, fill_array, parse_fn);
        parser.state.pos = next_pos;
    }

    template<Parenthesis_Type type>
    bool successfull_parenthesis_exit()
    {
        auto parenthesis_pos = find_error_recovery_token(
            [](Syntax_Token* t, void* _unused) -> bool
            { return t->type == Syntax_Token_Type::PARENTHESIS &&
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
                if (test_operator(Syntax_Operator::COMMA)) {
                    advance_token();
                    continue;
                }
                if (test_parenthesis_offset(closing_char, 0)) {
                    continue;
                }
            }

            // Error Recovery
            auto comma_pos = find_error_recovery_token(
                [](Syntax_Token* t, void* _unused) -> bool
                { return t->type == Syntax_Token_Type::OPERATOR && t->options.op == Syntax_Operator::COMMA; },
                0, true
            );
            auto parenthesis_pos = find_error_recovery_token(
                [](Syntax_Token* t, void* _unused) -> bool
                { return t->type == Syntax_Token_Type::PARENTHESIS &&
                !t->options.parenthesis.is_open && t->options.parenthesis.type == Parenthesis_Type::PARENTHESIS; },
                0, true
            );
            enum class Error_Start { COMMA, PARENTHESIS, NOT_FOUND } tactic;
            tactic = Error_Start::NOT_FOUND;
            if (comma_pos.available && parenthesis_pos.available) {
                tactic = syntax_position_in_order(comma_pos.value, parenthesis_pos.value) ? Error_Start::COMMA : Error_Start::PARENTHESIS;
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
                parser.state.pos.token_index = line->tokens.size; // Goto end of line for now
                return;
            }
        }
    }

    Optional<String*> parse_block_label(AST::Expression* related_expression)
    {
        auto result = optional_make_failure<String*>();
        if (test_token(Syntax_Token_Type::IDENTIFIER), test_operator_offset(Syntax_Operator::COLON, 1)) {
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
            result = optional_make_success(related_expression->options.symbol_read->name);
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
        if (test_token(Syntax_Token_Type::IDENTIFIER) && test_operator_offset(Syntax_Operator::ASSIGN, 1)) {
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
        if (test_operator(Syntax_Operator::DOLLAR)) {
            result->is_comptime = true;
            advance_token();
        }

        if (!test_token(Syntax_Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;
        result->name = get_token(0)->options.identifier;
        advance_token();

        if (!test_operator(Syntax_Operator::COLON)) CHECKPOINT_EXIT;
        advance_token();
        result->type = parse_expression_or_error_expr((Base*)result);

        if (test_operator(Syntax_Operator::ASSIGN)) {
            result->type = parse_expression_or_error_expr((Base*)result);
        }
        PARSE_SUCCESS(result);
    }

    Switch_Case* parse_switch_case(Base* parent, bool& add_to_fill)
    {
        add_to_fill = true;
        if (!test_keyword_offset(Syntax_Keyword::CASE, 0) && !test_keyword_offset(Syntax_Keyword::DEFAULT, 0)) {
            return 0;
        }

        auto result = allocate_base<Switch_Case>(parent, Base_Type::SWITCH_CASE);
        bool is_default = test_keyword_offset(Syntax_Keyword::DEFAULT, 0);
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
            if ((syntax_line_is_empty(line) && !syntax_line_is_multi_line_comment(line)) ||
                (test_token(Syntax_Token_Type::IDENTIFIER) && test_operator_offset(Syntax_Operator::COLON, 1) &&
                    (line->tokens.size == 2 || (line->tokens.size == 3 && line->tokens[2].type == Syntax_Token_Type::COMMENT))))
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
                if (test_operator(Syntax_Operator::ASSIGN)) {
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
        if (test_token(Syntax_Token_Type::KEYWORD))
        {
            switch (get_token(0)->options.keyword)
            {
            case Syntax_Keyword::IF:
            {
                advance_token();
                result->type = Statement_Type::IF_STATEMENT;
                auto& if_stat = result->options.if_statement;
                if_stat.condition = parse_expression_or_error_expr(&result->base);
                if_stat.block = parse_code_block(&result->base, if_stat.condition);
                if_stat.else_block.available = false;

                auto last_if_stat = result;
                // Parse else-if chain
                while (test_keyword_offset(Syntax_Keyword::ELSE, 0) && test_keyword_offset(Syntax_Keyword::IF, 1))
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
                if (test_keyword_offset(Syntax_Keyword::ELSE, 0))
                {
                    advance_token();
                    last_if_stat->options.if_statement.else_block = optional_make_success(parse_code_block(&last_if_stat->base, 0));
                }
                PARSE_SUCCESS(result);
            }
            case Syntax_Keyword::WHILE:
            {
                advance_token();
                result->type = Statement_Type::WHILE_STATEMENT;
                auto& loop = result->options.while_statement;
                loop.condition = parse_expression_or_error_expr(&result->base);
                loop.block = parse_code_block(&result->base, loop.condition);
                PARSE_SUCCESS(result);
            }
            case Syntax_Keyword::DEFER:
            {
                advance_token();
                result->type = Statement_Type::DEFER;
                result->options.defer_block = parse_code_block(&result->base, 0);
                PARSE_SUCCESS(result);
            }
            case Syntax_Keyword::SWITCH:
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
            case Syntax_Keyword::DELETE_KEYWORD: {
                advance_token();
                result->type = Statement_Type::DELETE_STATEMENT;
                result->options.delete_expr = parse_expression_or_error_expr(&result->base);
                PARSE_SUCCESS(result);
            }
            case Syntax_Keyword::RETURN: {
                advance_token();
                result->type = Statement_Type::RETURN_STATEMENT;
                auto expr = parse_expression(&result->base);
                result->options.return_value.available = false;
                if (expr != 0) {
                    result->options.return_value = optional_make_success(expr);
                }
                PARSE_SUCCESS(result);
            }
            case Syntax_Keyword::CONTINUE: {
                advance_token();
                result->type = Statement_Type::CONTINUE_STATEMENT;
                if (!test_token(Syntax_Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;
                result->options.continue_name = get_token(0)->options.identifier;
                advance_token();
                PARSE_SUCCESS(result);
            }
            case Syntax_Keyword::BREAK: {
                advance_token();
                result->type = Statement_Type::BREAK_STATEMENT;
                if (!test_token(Syntax_Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;
                result->options.break_name = get_token(0)->options.identifier;
                advance_token();
                PARSE_SUCCESS(result);
            }
            }
            }
            CHECKPOINT_EXIT;
        }

        Enum_Member* parse_enum_member(Base * parent, bool& add_to_fill)
        {
            add_to_fill = true;
            if (!test_token(Syntax_Token_Type::IDENTIFIER)) {
                return 0;
            }

            CHECKPOINT_SETUP;
            auto result = allocate_base<Enum_Member>(parent, Base_Type::ENUM_MEMBER);
            result->name = get_token(0)->options.identifier;
            advance_token();
            if (test_operator(Syntax_Operator::DEFINE_COMPTIME))
            {
                advance_token();
                result->value = optional_make_success(parse_expression_or_error_expr(&result->base));
            }
            PARSE_SUCCESS(result);
        }

        Expression* parse_single_expression_no_postop(Base * parent)
        {
            // Note: Single expression no postop means pre-ops + bases, e.g.
            // --*x   -> is 3 pre-ops + base x
            CHECKPOINT_SETUP;
            auto result = allocate_base<Expression>(parent, Base_Type::EXPRESSION);

            // Unops
            if (test_token(Syntax_Token_Type::OPERATOR))
            {
                Unop unop;
                bool valid = true;
                switch (get_token(0)->options.op)
                {
                case Syntax_Operator::SUBTRACTION: unop = Unop::NEGATE; break;
                case Syntax_Operator::NOT: unop = Unop::NOT; break;
                case Syntax_Operator::AMPERSAND: unop = Unop::DEREFERENCE; break;
                case Syntax_Operator::MULTIPLY: unop = Unop::POINTER; break;
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

            if (test_keyword_offset(Syntax_Keyword::BAKE, 0))
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
                if (test_keyword_offset(Syntax_Keyword::CAST, 0)) {
                    type = Cast_Type::TYPE_TO_TYPE;
                }
                else if (test_keyword_offset(Syntax_Keyword::CAST_PTR, 0)) {
                    type = Cast_Type::RAW_TO_PTR;
                }
                else if (test_keyword_offset(Syntax_Keyword::CAST_RAW, 0)) {
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
            if (test_token(Syntax_Token_Type::IDENTIFIER))
            {
                Symbol_Read* final_read = allocate_base<Symbol_Read>(&result->base, Base_Type::SYMBOL_READ);
                Symbol_Read* read = final_read;
                read->path_child.available = false;
                read->name = get_token(0)->options.identifier;
                while (test_token(Syntax_Token_Type::IDENTIFIER) &&
                    test_operator_offset(Syntax_Operator::TILDE, 1) &&
                    test_token_offset(Syntax_Token_Type::IDENTIFIER, 2))
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

            if (test_operator(Syntax_Operator::DOT))
            {
                advance_token();
                if (test_token(Syntax_Token_Type::IDENTIFIER)) // Member access
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
            if (test_token(Syntax_Token_Type::LITERAL_NUMBER))
            {
                auto str = get_token(0)->options.literal_number;
                int value = 0;
                bool valid = true;
                bool is_float = false;
                int index;
                for (index = 0; index < str->size; index++) {
                    auto c = str->characters[index];
                    if (c == '.') {
                        is_float = true;
                        index += 1;
                        break;
                    }
                    if (!(c >= '0' && c <= '9')) {
                        valid = false;
                        break;
                    }
                    value = value * 10;
                    value += (c - '0');
                }
                float float_val = (float)value;
                if (is_float)
                {
                    float multiplier = 0.1f;
                    while (index < str->size)
                    {
                        auto c = str->characters[index];
                        if (!(c >= '0' && c <= '9')) {
                            valid = false;
                            break;
                        }
                        float_val = float_val + multiplier * (c - '0');
                        multiplier *= 0.1f;
                        index += 1;
                    }
                }

                advance_token();
                if (!valid) {
                    result->type = Expression_Type::ERROR_EXPR;
                    PARSE_SUCCESS(result);
                }

                result->type = Expression_Type::LITERAL_READ;
                if (is_float) {
                    result->options.literal_read.type = Literal_Type::FLOAT_VAL;
                    result->options.literal_read.options.float_val = float_val;
                }
                else {
                    result->options.literal_read.type = Literal_Type::INTEGER;
                    result->options.literal_read.options.int_val = value;
                }
                PARSE_SUCCESS(result);
            }
            if (test_keyword_offset(Syntax_Keyword::NULL_KEYWORD, 0))
            {
                result->type = Expression_Type::LITERAL_READ;
                result->options.literal_read.type = Literal_Type::NULL_VAL;
                advance_token();
                PARSE_SUCCESS(result);
            }
            if (test_token(Syntax_Token_Type::LITERAL_STRING))
            {
                result->type = Expression_Type::LITERAL_READ;
                result->options.literal_read.type = Literal_Type::STRING;
                result->options.literal_read.options.string = literal_string_handle_escapes(get_token(0));
                advance_token();
                PARSE_SUCCESS(result);
            }
            if (test_token(Syntax_Token_Type::LITERAL_BOOL)) {
                result->type = Expression_Type::LITERAL_READ;
                result->options.literal_read.type = Literal_Type::BOOLEAN;
                result->options.literal_read.options.boolean = get_token(0)->options.literal_bool;
                advance_token();
                PARSE_SUCCESS(result);
            }

            // Parse functions + signatures
            if (test_parenthesis_offset('(', 0) && (
                test_parenthesis_offset(')', 1) ||
                (test_token_offset(Syntax_Token_Type::IDENTIFIER, 1) && test_operator_offset(Syntax_Operator::COLON, 2)) ||
                (test_operator_offset(Syntax_Operator::DOLLAR, 1) && test_token_offset(Syntax_Token_Type::IDENTIFIER, 2))
                ))
            {
                result->type = Expression_Type::FUNCTION_SIGNATURE;
                auto& signature = result->options.function_signature;
                signature.parameters = dynamic_array_create_empty<Parameter*>(1);
                signature.return_value.available = false;
                parse_parenthesis_comma_seperated(&result->base, &signature.parameters, parse_parameter, Parenthesis_Type::PARENTHESIS);

                // Parse Return value
                if (test_operator(Syntax_Operator::ARROW)) {
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
            if (test_keyword_offset(Syntax_Keyword::NEW, 0))
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
            if (test_keyword_offset(Syntax_Keyword::STRUCT, 0) ||
                test_keyword_offset(Syntax_Keyword::C_UNION, 0) ||
                test_keyword_offset(Syntax_Keyword::UNION, 0))
            {
                result->type = Expression_Type::STRUCTURE_TYPE;
                result->options.structure.members = dynamic_array_create_empty<Definition*>(1);
                if (test_keyword_offset(Syntax_Keyword::STRUCT, 0)) {
                    result->options.structure.type = AST::Structure_Type::STRUCT;
                }
                else if (test_keyword_offset(Syntax_Keyword::C_UNION, 0)) {
                    result->options.structure.type = AST::Structure_Type::C_UNION;
                }
                else {
                    result->options.structure.type = AST::Structure_Type::UNION;
                }
                advance_token();
                parse_follow_block(&result->base, &result->options.structure.members, parse_definition, false);
                PARSE_SUCCESS(result);
            }
            if (test_keyword_offset(Syntax_Keyword::ENUM, 0)) {
                result->type = Expression_Type::ENUM_TYPE;
                result->options.enum_members = dynamic_array_create_empty<Enum_Member*>(1);
                advance_token();
                parse_follow_block(&result->base, &result->options.enum_members, parse_enum_member, false);
                PARSE_SUCCESS(result);
            }
            if (test_keyword_offset(Syntax_Keyword::MODULE, 0)) {
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

        Expression* parse_post_operator_internal(Expression * child)
        {
            CHECKPOINT_SETUP;
            // Post operators
            auto result = allocate_base<Expression>(child->base.parent, Base_Type::EXPRESSION);
            if (test_operator(Syntax_Operator::DOT))
            {
                advance_token();
                if (test_token(Syntax_Token_Type::IDENTIFIER)) // Member access
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

        Expression* parse_single_expression(Base * parent)
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

        Expression* parse_single_expression_or_error(Base * parent)
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

        Expression* parse_priority_level(Expression * expr, int priority_level, Dynamic_Array<Binop_Link> * links, int* index)
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

        Expression* parse_expression(Base * parent)
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
                if (test_token(Syntax_Token_Type::OPERATOR))
                {
                    switch (get_token(0)->options.op)
                    {
                    case Syntax_Operator::ADDITION: link.binop = Binop::ADDITION; break;
                    case Syntax_Operator::SUBTRACTION: link.binop = Binop::SUBTRACTION; break;
                    case Syntax_Operator::MULTIPLY:link.binop = Binop::MULTIPLICATION; break;
                    case Syntax_Operator::DIVISON:link.binop = Binop::DIVISION; break;
                    case Syntax_Operator::MODULO:link.binop = Binop::MODULO; break;
                    case Syntax_Operator::AND:link.binop = Binop::AND; break;
                    case Syntax_Operator::OR:link.binop = Binop::OR; break;
                    case Syntax_Operator::GREATER_THAN:link.binop = Binop::GREATER; break;
                    case Syntax_Operator::GREATER_EQUAL:link.binop = Binop::GREATER_OR_EQUAL; break;
                    case Syntax_Operator::LESS_THAN:link.binop = Binop::LESS; break;
                    case Syntax_Operator::LESS_EQUAL:link.binop = Binop::LESS_OR_EQUAL; break;
                    case Syntax_Operator::EQUALS:link.binop = Binop::EQUAL; break;
                    case Syntax_Operator::NOT_EQUALS:link.binop = Binop::NOT_EQUAL; break;
                    case Syntax_Operator::POINTER_EQUALS:link.binop = Binop::POINTER_EQUAL; break;
                    case Syntax_Operator::POINTER_NOT_EQUALS:link.binop = Binop::POINTER_NOT_EQUAL; break;
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

        Expression* parse_expression_or_error_expr(Base * parent)
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

        Definition* parse_definition(Base * parent, bool& add_to_fill)
        {
            add_to_fill = true;
            CHECKPOINT_SETUP;
            auto result = allocate_base<Definition>(parent, AST::Base_Type::DEFINITION);
            result->is_comptime = false;

            if (parser.state.pos.token_index != 0) CHECKPOINT_EXIT;
            if (!test_token(Syntax_Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;
            result->name = get_token(0)->options.identifier;
            advance_token();

            int prev_line_index = parser.state.pos.line_index;
            if (test_operator(Syntax_Operator::COLON))
            {
                advance_token();
                result->type = optional_make_success(parse_expression_or_error_expr((Base*)result));

                bool is_assign = test_operator(Syntax_Operator::ASSIGN);
                if (is_assign || test_operator(Syntax_Operator::COLON)) {
                    result->is_comptime = !is_assign;
                    advance_token();
                    result->value = optional_make_success(parse_expression_or_error_expr((Base*)result));
                }
            }
            else if (test_operator(Syntax_Operator::DEFINE_COMPTIME)) {
                advance_token();
                result->is_comptime = true;
                result->value = optional_make_success(parse_expression_or_error_expr((Base*)result));
            }
            else if (test_operator(Syntax_Operator::DEFINE_INFER)) {
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

        Definition* parse_module_item(Base * parent, bool& add_to_fill)
        {
            CHECKPOINT_SETUP;
            if (test_keyword_offset(Syntax_Keyword::IMPORT, 0) && test_token_offset(Syntax_Token_Type::LITERAL_STRING, 1))
            {
                assert(parent->type == Base_Type::MODULE, "");
                auto module = (Module*)parent;
                auto import = allocate_base<Project_Import>(parent, Base_Type::PROJECT_IMPORT);
                import->filename = literal_string_handle_escapes(get_token(1));
                advance_token();
                advance_token();
                SET_END_RANGE(import);
                dynamic_array_push_back(&module->imports, import);

                add_to_fill = false;
                return 0;
            }

            return parse_definition(parent, add_to_fill);
        }

        void base_correct_token_ranges(Base * base)
        {
            int index = 0;
            auto child = AST::base_get_child(base, index);
            if (child == 0) return;
            auto& start = parser.parse_informations[child->allocation_index].start_pos;
            Syntax_Position end;
            while (child != 0) {
                base_correct_token_ranges(child);
                end = parser.parse_informations[child->allocation_index].end_pos;
                index += 1;
                child = AST::base_get_child(base, index);
            }

            auto& info = parser.parse_informations[base->allocation_index];
            start = syntax_position_sanitize(start);
            end = syntax_position_sanitize(end);
            info.start_pos = syntax_position_sanitize(info.start_pos);
            info.end_pos = syntax_position_sanitize(info.end_pos);
            if (syntax_position_in_order(start, info.start_pos)) {
                info.start_pos = start;
            }
            if (syntax_position_in_order(info.end_pos, end)) {
                info.end_pos = end;
            }
        }

        AST::Module* execute(Syntax_Block * root_block)
        {
            parser.root = allocate_base<Module>(0, Base_Type::MODULE);
            parser.parse_informations[0].start_pos.block = root_block;
            parser.root->definitions = dynamic_array_create_empty<Definition*>(1);
            parser.root->imports = dynamic_array_create_empty<Project_Import*>(1);
            parse_syntax_block<Definition>(root_block, &parser.root->base, &parser.root->definitions, parse_module_item);
            SET_END_RANGE(parser.root);
            base_correct_token_ranges(&parser.root->base);
            return parser.root;
        }

#undef CHECKPOINT_EXIT
#undef CHECKPOINT_SETUP
#undef PARSE_SUCCESS
#undef SET_END_RANGE

        Array<Error_Message> get_error_messages() {
            return dynamic_array_as_array(&parser.error_messages);
        }

        void ast_base_get_section_token_range(AST::Base * base, Section section, Dynamic_Array<Syntax_Range> * ranges)
        {
            auto& info = parser.parse_informations[base->allocation_index];
            switch (section)
            {
            case Section::NONE: break;
            case Section::WHOLE:
            {
                Syntax_Range range;
                range.start = info.start_pos;
                range.end = info.end_pos;
                dynamic_array_push_back(ranges, range);
                break;
            }
            case Section::WHOLE_NO_CHILDREN:
            {
                Syntax_Range range;
                range.start = info.start_pos;
                int index = 0;
                auto child = AST::base_get_child(base, index);
                while (child != 0)
                {
                    auto& child_info = parser.parse_informations[child->allocation_index];
                    if (!syntax_position_equal(range.start, child_info.start_pos)) {
                        range.end = child_info.start_pos;
                        dynamic_array_push_back(ranges, range);
                    }
                    range.start = child_info.end_pos;

                    index += 1;
                    child = AST::base_get_child(base, index);
                }
                if (!syntax_position_equal(range.start, info.end_pos)) {
                    range.end = info.end_pos;
                    dynamic_array_push_back(ranges, range);
                }
                break;
            }
            case Section::IDENTIFIER:
            {
                parser.state.pos = info.start_pos;
                auto result = find_error_recovery_token([](Syntax_Token* t, void* _unused) -> bool {return t->type == Syntax_Token_Type::IDENTIFIER; }, 0, false);
                if (!result.available) {
                    break;
                }
                Syntax_Range range;
                range.start = result.value;
                range.end = range.start;
                range.end.token_index += 1;
                dynamic_array_push_back(ranges, range);
                break;
            }
            case Section::ENCLOSURE:
            {
                // Find next (), {} or [], and add the tokens to the ranges
                parser.state.pos = info.start_pos;
                auto result = find_error_recovery_token([](Syntax_Token* t, void* type) -> bool {return t->type == Syntax_Token_Type::PARENTHESIS; }, 0, false);
                if (!result.available) {
                    break;
                }
                Syntax_Range range;
                range.start = result.value;
                range.end = result.value;
                range.end.token_index += 1;
                dynamic_array_push_back(ranges, range);

                // Find closing parenthesis
                parser.state.pos = result.value;
                auto par_type = get_token(0)->options.parenthesis.type;
                advance_token();
                auto end_token = find_error_recovery_token(
                    [](Syntax_Token* t, void* type) -> bool
                    { return t->type == Syntax_Token_Type::PARENTHESIS &&
                    !t->options.parenthesis.is_open && t->options.parenthesis.type == *((Parenthesis_Type*)type); },
                    (void*)(&par_type), true
                );
                if (!end_token.available) {
                    break;
                }
                range.start = end_token.value;
                range.end = end_token.value;
                range.end.token_index += 1;
                dynamic_array_push_back(ranges, range);
                break;
            }
            case Section::KEYWORD:
            {
                auto result = find_error_recovery_token([](Syntax_Token* t, void* type) -> bool {return t->type == Syntax_Token_Type::KEYWORD; }, 0, false);
                if (!result.available) {
                    break;
                }
                Syntax_Range range;
                range.start = result.value;
                range.end = result.value;
                range.end.token_index += 1;
                dynamic_array_push_back(ranges, range);
                break;
            }
            case Section::END_TOKEN: {
                Syntax_Range range;
                range.end = info.end_pos;
                range.start = info.end_pos;
                range.start.token_index -= 1;
                assert(range.start.token_index > 0, "Hey");
                dynamic_array_push_back(ranges, range);
                break;
            }
            default: panic("");
            }
        }
    }
