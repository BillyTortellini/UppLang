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
        Parsed_Code* parsed_code;
        Block_Parse* block_parse;
        int allocated_count;
        int error_count;
    };

    struct Parser
    {
        Parse_State state;
        Dynamic_Array<AST::Node*> allocated_nodes;
        Dynamic_Array<Error_Message> new_error_messages; // Required in incremental parsing (We need a distinction between old and new errors)
    };


    // Globals
    static Parser parser;


    // Parser Functions
    void parser_rollback(Parse_State checkpoint)
    {
        assert(checkpoint.allocated_count <= parser.allocated_nodes.size, "");
        for (int i = checkpoint.allocated_count; i < parser.allocated_nodes.size; i++) {
            AST::base_destroy(parser.allocated_nodes[i]);
        }
        dynamic_array_rollback_to_size(&parser.allocated_nodes, checkpoint.allocated_count);
        dynamic_array_rollback_to_size(&parser.new_error_messages, checkpoint.error_count);
        parser.state = checkpoint;
    }

    void reset()
    {
        Parse_State state;
        state.allocated_count = 0;
        state.error_count = 0;
        state.parsed_code = 0;
        state.block_parse = 0;
        state.pos = token_index_make(line_index_make(block_index_make(0, 0), 0), 0);
        parser.state.parsed_code = 0;
        parser_rollback(state);
    }

    void initialize()
    {
        parser.allocated_nodes = dynamic_array_create_empty<AST::Node*>(32);
        parser.new_error_messages = dynamic_array_create_empty<Error_Message>(1);
        reset();
    }

    void destroy()
    {
        reset();
        for (int i = 0; i < parser.allocated_nodes.size; i++) {
            AST::base_destroy(parser.allocated_nodes[i]);
        }
        dynamic_array_destroy(&parser.allocated_nodes);
        dynamic_array_destroy(&parser.new_error_messages);
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
        base->range.start = node_position_make_token_index(parser.state.pos);
        base->range.end = base->range.start;

        return result;
    }



    // Error reporting
    void log_error(const char* msg, Node_Range range)
    {
        Error_Message err;
        err.msg = msg;
        err.range = range;
        err.block_parse = parser.state.block_parse;
        err.origin_line_index = parser.state.pos.line_index.line_index;
        assert(index_equal(parser.state.block_parse->index, parser.state.pos.line_index.block_index), "Current line index must be inside current block parse");
        // logg("Logging error %s for block_parse %p\n", msg, err.block_parse);
        dynamic_array_push_back(&parser.new_error_messages, err);
        parser.state.error_count = parser.new_error_messages.size;
    }

    void log_error_to_pos(const char* msg, Token_Index pos) {
        log_error(msg, node_range_make(node_position_make_token_index(parser.state.pos), node_position_make_token_index(pos)));
    }

    void log_error_range_offset_with_start(const char* msg, Token_Index start, int token_offset) {
        log_error(msg, node_range_make(start, token_index_advance(start, token_offset)));
    }

    void log_error_range_offset(const char* msg, int token_offset) {
        log_error_range_offset_with_start(msg, parser.state.pos, token_offset);
    }



    // Positional Stuff
    Token* get_token(int offset) {
        return index_value(token_index_advance(parser.state.pos, offset));
    }

    Token* get_token() {
        return index_value(parser.state.pos);
    }

    Source_Line* get_line() {
        return index_value(parser.state.pos.line_index);
    }

    int remaining_line_tokens() {
        auto& p = parser.state.pos;
        if (!index_valid(p)) return 0; // May happen when we step out of block
        if (index_value(p.line_index)->is_block_reference) return 0; 
        auto tokens = index_value_text(p.line_index)->tokens;
        int remaining = tokens.size - p.token;
        if (tokens.size != 0 && tokens[tokens.size - 1].type == Token_Type::COMMENT) {
            remaining -= 1;
        }
        return remaining;
    }

    bool on_follow_block() {
        auto& pos = parser.state.pos;
        if (!line_index_block_after(pos.line_index).available) return false;
        return remaining_line_tokens() == 0;
    }

    void advance_token() {
        parser.state.pos = token_index_next(parser.state.pos);
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
            if (node_position_compare(bounding_range.start, child_range.start) < 0) {
                bounding_range.start = child_range.start;
            }
            if (node_position_compare(child_range.end, bounding_range.end) < 0) {
                bounding_range.end = child_range.end;
            }

            index += 1;
            child = AST::base_get_child(node, index);
        }
    }

    int counter = 0;
    void node_finalize_range(AST::Node* node) {
        // NOTE: This Step is indeed necessary because Token-Indices can be ambiguous, 
        /*
        DOCU: 
          This function does a Post-Processing on Token-Ranges to correct invalid ranges,
          which are generated the following way:
            1. Parse syntax block steps out of current block (Line-Index becomes invalid (line_index == block_index.lines.size)
                and the parent nodes of the syntax_block therefore get an invalid end_index.
            2. Expressions that go over multiple blocks have their end index at the start of another line_index.
               Instead, the end index should be at the end of the previous line_index
               Maybe I need to think more about this, but this shouldn't be true for code-blocks
            3. It also generates bounding ranges, which are used to efficiently map from tokens to nodes.
               These are different from the normal token ranges, since e.g. 
               in the expression "32 + fib(15)" the binop + has a token range of 1, and the bounding range contains all children
        */

        // Set end of node
        auto& range = node->range;
        {
            auto pos = parser.state.pos;
            auto block = index_value(pos.line_index.block_index);
            assert(pos.line_index.line_index <= block->lines.size, "Cannot be greater, this would mean we already parsed an invalid line");
            if (pos.line_index.line_index == block->lines.size) {
                range.end = node_position_make_end_of_line(line_index_make(pos.line_index.block_index, block->lines.size - 1));
            }
            else if (index_value(pos.line_index)->is_block_reference) {
                panic("I would like to know when this happens");
                range.end = node_position_make_block_end(index_value(pos.line_index)->options.block_index);
            }
            else if (pos.token == 0 && !index_equal(pos, node_position_to_token_index(range.start))) {
                assert(pos.line_index.line_index > 0, "Must be the case");
                range.end = node_position_make_end_of_line(line_index_make(pos.line_index.block_index, pos.line_index.line_index - 1));
            }
            else {
                range.end = node_position_make_token_index(pos);
            }
        }
        
        node_calculate_bounding_range(node);

        // Sanity-Check that start/end is in order
        counter++;
        int order = index_compare(node_position_to_token_index(range.start), node_position_to_token_index(range.end));
        assert(order != -1, "Ranges must be in order");
        if (order == 0) {
            // INFO: There are only 3 AST-Nodes which are allowed to have a 0 sized token range:
            //       Error-Expressions, Code-Blocks and Symbol-Reads (Empty reads)
            if (node->type == AST::Node_Type::EXPRESSION) {
                auto expr = downcast<Expression>(node);
                assert(expr->type == AST::Expression_Type::ERROR_EXPR, "Only error expression may have 0-sized token range");
            }
            else if (node->type == AST::Node_Type::CODE_BLOCK || node->type == AST::Node_Type::SYMBOL_LOOKUP) {
            }
            else if (node->type == AST::Node_Type::STATEMENT && AST::downcast<Statement>(node)->type == AST::Statement_Type::BLOCK) {
            }
            else {
                panic("See comment before");
            }
        }

    }

    // Parsing Helpers
#define CHECKPOINT_SETUP \
        if (!index_valid(parser.state.pos)) {return 0;}\
        if (token_index_is_end_of_line(parser.state.pos) && !on_follow_block()) {return 0;}\
        auto checkpoint = parser.state;\
        bool _error_exit = false;\
        SCOPE_EXIT(if (_error_exit) parser_rollback(checkpoint););

#define CHECKPOINT_EXIT {_error_exit = true; return 0;}
#define SET_END_RANGE(val) {node_finalize_range(&val->base);}
#define PARSE_SUCCESS(val) { \
        node_finalize_range(&val->base); \
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
            // End of line_index handling
            if (line_index_is_end_of_block(pos.line_index)) {
                return optional_make_failure<Token_Index>();
            }
            if (token_index_is_end_of_line(pos))
            {
                if (!(starting_inside_parenthesis || parenthesis_stack.size != 0)) {
                    return optional_make_failure<Token_Index>();
                }
                // Parenthesis aren't allowed to reach over blocks if there is no follow_block
                if (!line_index_block_after(pos.line_index).available) {
                    return optional_make_failure<Token_Index>();
                }
                if (line_index_is_end_of_block(pos.line_index)) {
                    return optional_make_failure<Token_Index>();
                }
                pos.line_index.line_index += 1;
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

    typedef bool (*error_recovery_stop_function)(Token& token);

    template<typename T>
    void parse_comma_seperated_items(Node* parent, Dynamic_Array<T*>* fill_array, T* (*parse_fn)(Node* parent), error_recovery_stop_function stop_fn)
    {
        // Parse Items
        while (true)
        {
            auto item = parse_fn(parent);
            if (item != 0)
            {
                dynamic_array_push_back(fill_array, item);
                if (test_operator(Operator::COMMA)) {
                    advance_token();
                    continue;
                }
                // Check if we reached end of line/block
                if (parser.state.pos.line_index.line_index >= index_value(parser.state.pos.line_index.block_index)->lines.size) {
                    return;
                }
                if (token_index_is_end_of_line(parser.state.pos)) {
                    return;
                }
                if (stop_fn != 0) {
                    if (stop_fn(*get_token())) {
                        return;
                    }
                }
            }
            
            // Otherwise try to find next comma
            auto search_fn = [](Token* token, void* userdata) -> bool {
                auto stop_function = (error_recovery_stop_function)userdata;
                if (stop_function != 0) {
                    if (stop_function(*token)) {
                        return true;
                    }
                }
                return token->type == Token_Type::OPERATOR && token->options.op == Operator::COMMA;
            };
            auto recovery_point_opt = search_token(parser.state.pos, search_fn, stop_fn, false);

            // Check if we reached stop, found comma or none of both
            if (!recovery_point_opt.available) {
                return;
            }
            auto token = index_value(recovery_point_opt.value);
            if (token->type == Token_Type::OPERATOR && token->options.op == Operator::COMMA) {
                log_error_to_pos("Couldn't parse item", recovery_point_opt.value);
                parser.state.pos = recovery_point_opt.value;
                advance_token();
                continue;
            }
            // Otherwise we found the stop point
            log_error_to_pos("Couldn't parse item", recovery_point_opt.value);
            parser.state.pos = recovery_point_opt.value;
            return;
        }
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
                //       so we log an error to the end of the line_index and stop parsing
                auto line_end = token_index_make_line_end(parser.state.pos.line_index);
                log_error_to_pos("Couldn't find closing_parenthesis", line_end);
                parser.state.pos = line_end;
                return;
            }
        }
    }


    // Prototypes
    Optional<String*> parse_block_label(AST::Expression* related_expression);
    void parse_follow_block(AST::Node* parent, Block_Context context);
    Code_Block* parse_code_block(Node* parent, AST::Expression* related_expression);
    Block_Parse* parse_source_block(AST::Node* parent, Block_Index block_index, Block_Context context);
    Path_Lookup* parse_path_lookup(Node* parent);
    Expression* parse_expression(Node* parent);
    Expression* parse_expression_or_error_expr(Node* parent);
    Expression* parse_single_expression(Node* parent);
    Expression* parse_single_expression_or_error(Node* parent);
    void block_parse_remove_items_from_ast(Block_Parse* block_parse);
    void block_parse_add_items_to_ast(Block_Parse* block_parse);

    // Block Parsing
    void ast_node_destroy_recursively(AST::Node* node) {
        int child_index = 0;
        auto child = AST::base_get_child(node, child_index);
        while (child != 0) {
            ast_node_destroy_recursively(child);
            child_index += 1;
            child = AST::base_get_child(node, child_index);
        }
        AST::base_destroy(node);
    }

    void block_parse_destroy(Block_Parse* block_parse)
    {
        auto& parsed_code = parser.state.parsed_code;
        // logg("Removing block_parse of block %d\n", block_parse->index.block_index);
        // Remove error messages created in this block
        for (int j = 0; j < parsed_code->error_messages.size; j++) {
            auto& error = parsed_code->error_messages[j];
            if (error.block_parse == block_parse) {
                dynamic_array_swap_remove(&parsed_code->error_messages, j);
                // logg("Removing error message: %s\n", error.msg);
                j -= 1;
                continue;
            }
        }

        // Remove all child blocks (In reverse order since children remove themselves from this array)
        for (int i = block_parse->child_block_parses.size - 1; i >= 0; i--) {
            block_parse_destroy(block_parse->child_block_parses[i]);
        }

        // Remove nodes from parent node
        block_parse_remove_items_from_ast(block_parse);
        hashtable_remove_element(&parsed_code->block_parses, block_parse->index);
        // Destroy all child-items
        for (int i = 0; i < block_parse->items.size; i++) {
            ast_node_destroy_recursively(block_parse->items[i].node);
        }
        // Destroy block parse
        dynamic_array_destroy(&block_parse->items);
        dynamic_array_destroy(&block_parse->child_block_parses);
        // Remove from parent
        if (block_parse->parent_parse != 0) {
            bool found = false;
            for (int i = 0; i < block_parse->parent_parse->child_block_parses.size; i++) {
                auto child = block_parse->parent_parse->child_block_parses[i];
                if (block_parse == child) {
                    found = true;
                    dynamic_array_swap_remove(&block_parse->parent_parse->child_block_parses, i);
                    break;
                }
            }
            assert(found, "Must be contained in parent!");
        }
        delete block_parse;
    }

    void block_index_remove_block_parse(Block_Index index)
    {
        auto& parsed_code = parser.state.parsed_code; 
        // Check if the block_index was parsed
        auto block_parse = hashtable_find_element(&parsed_code->block_parses, index);
        if (block_parse == 0) { // block was not parsed
            return;
        }
        block_parse_destroy(*block_parse);
    }

    namespace Block_Items
    {
        // Anonymous block functions
        AST::Node* parse_anonymous_block(AST::Node* parent, Block_Index block_index)
        {
            auto statement = allocate_base<Statement>(parent, Node_Type::STATEMENT);
            statement->type = Statement_Type::BLOCK;
            statement->base.range = node_range_make_block(block_index);
            statement->base.bounding_range = node_range_make_block(block_index);

            auto& code_block = statement->options.block;
            code_block = allocate_base<Code_Block>(AST::upcast(statement), Node_Type::CODE_BLOCK);
            code_block->base.range = node_range_make_block(block_index);
            code_block->base.bounding_range = node_range_make_block(block_index);
            code_block->statements = dynamic_array_create_empty<Statement*>(1);
            code_block->block_id = optional_make_failure<String*>();
            parse_source_block(AST::upcast(code_block), block_index, Block_Context::STATEMENTS);
            return AST::upcast(statement);
        }

        AST::Node* parse_block_as_error(AST::Node* parent, Block_Index block_index) {
            log_error("Anonymous blocks aren't allowed in this context", node_range_make_block(block_index));
            block_index_remove_block_parse(block_index);
            return 0;
        }

        Definition_Symbol* parse_definition_symbol(Node* parent) {
            if (!test_token(Token_Type::IDENTIFIER)) {
                return 0;
            }
            auto node = allocate_base<Definition_Symbol>(parent, Node_Type::DEFINITION_SYMBOL);
            node->name = get_token()->options.identifier;
            advance_token();
            PARSE_SUCCESS(node);
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

        Definition* parse_definition(Node* parent)
        {
            // INFO: Definitions, like all line items, cannot start in the middle of a line
            CHECKPOINT_SETUP;
            if (parser.state.pos.token != 0) CHECKPOINT_EXIT;
            if (!test_token(Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;

            auto result = allocate_base<Definition>(parent, AST::Node_Type::DEFINITION);
            result->is_comptime = false;
            result->symbols = dynamic_array_create_empty<Definition_Symbol*>(1);
            result->types = dynamic_array_create_empty<AST::Expression*>(1);
            result->values = dynamic_array_create_empty<AST::Expression*>(1);

            // Parse comma seperated list of identifiers
            auto found_definition_operator = [](Token& token) -> bool {
                return token.type == Token_Type::OPERATOR &&
                    (token.options.op == Operator::COLON ||
                     token.options.op == Operator::DEFINE_COMPTIME ||
                     token.options.op == Operator::DEFINE_INFER);
            };
            parse_comma_seperated_items(upcast(result), &result->symbols, parse_definition_symbol, found_definition_operator);

            // Check if there is a colon :, or a := or an ::
            if (test_operator(Operator::COLON))
            {
                advance_token();
                // Note: Parse types (At least one value is guaranteed by parse_expression or error)
                auto found_value_start = [](Token& token) -> bool {
                    return token.type == Token_Type::OPERATOR &&
                        (token.options.op == Operator::COLON ||
                         token.options.op == Operator::ASSIGN);
                };
                parse_comma_seperated_items(upcast(result), &result->types, parse_expression_or_error_expr, found_value_start);

                if (test_operator(Operator::ASSIGN)) {
                    result->is_comptime = false;
                    advance_token();
                }
                else if (test_operator(Operator::COLON)) {
                    result->is_comptime = true;
                    advance_token();
                }
                else {
                    PARSE_SUCCESS(result);
                }
            }
            else if (test_operator(Operator::DEFINE_COMPTIME)) {
                advance_token();
                result->is_comptime = true;
            }
            else if (test_operator(Operator::DEFINE_INFER)) {
                advance_token();
                result->is_comptime = false;
            }
            else {
                CHECKPOINT_EXIT;
            }
            
            // Parse values (Or add at least one error expression)
            parse_comma_seperated_items(upcast(result), &result->values, parse_expression_or_error_expr, 0);

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
            return nullptr;
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
                // Check for Anonymous Blocks
                // INFO: This needs to be done before definition
                // This is still required for blocks that have a identifier
                auto& tokens = get_line()->options.text.tokens;
                if ((tokens.size == 1 && test_token(Token_Type::COMMENT)) ||
                    (tokens.size == 2 && test_token(Token_Type::IDENTIFIER) && test_operator_offset(Operator::COLON, 1)) ||
                    (tokens.size == 3 && test_token(Token_Type::IDENTIFIER) && test_operator_offset(Operator::COLON, 1) && test_token_offset(Token_Type::COMMENT, 3)))
                {
                    if (line_index_block_after(parser.state.pos.line_index).available)
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
                    if (test_operator(Operator::COMMA)) 
                    {
                        // Assume that it's an assignment
                        result->type = Statement_Type::ASSIGNMENT;
                        result->options.assignment.left_side = dynamic_array_create_empty<Expression*>(1);
                        result->options.assignment.right_side = dynamic_array_create_empty<Expression*>(1);
                        dynamic_array_push_back(&result->options.assignment.left_side, expr);
                        advance_token();
                        
                        // Parse remaining left_side expressions
                        auto is_assign = [](Token& token) -> bool {return token.type == Token_Type::OPERATOR && token.options.op == Operator::ASSIGN; };
                        parse_comma_seperated_items(upcast(result), &result->options.assignment.left_side, parse_expression_or_error_expr, is_assign);

                        // Check if assignment found, otherwise error
                        if (!test_operator(Operator::ASSIGN)) {
                            CHECKPOINT_EXIT;
                        }
                        advance_token();

                        // Parse right side
                        parse_comma_seperated_items(upcast(result), &result->options.assignment.right_side, parse_expression_or_error_expr, 0);
                        PARSE_SUCCESS(result);
                    }
                    else if (test_operator(Operator::ASSIGN)) 
                    {
                        result->type = Statement_Type::ASSIGNMENT;
                        result->options.assignment.left_side = dynamic_array_create_empty<Expression*>(1);
                        result->options.assignment.right_side = dynamic_array_create_empty<Expression*>(1);
                        dynamic_array_push_back(&result->options.assignment.left_side, expr);
                        advance_token();

                        // Parse right side
                        parse_comma_seperated_items(upcast(result), &result->options.assignment.right_side, parse_expression_or_error_expr, 0);
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
                    parse_follow_block(AST::upcast(result), Block_Context::SWITCH);
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

    };

    // If returned line_item.node == 0, this means that the line was either empty or an error occured,
    // but result.line_count is always valid.
    Line_Item parse_line_item(Line_Index line_index, Block_Context context, AST::Node* parent)
    {
        Line_Item result;
        result.line_start = line_index.line_index;
        result.line_count = 1;
        result.node = 0;

        auto& pos = parser.state.pos;
        pos.token = 0;
        pos.line_index = line_index;
        auto line = index_value(line_index);

        // Parse anonymous blocks
        if (line->is_block_reference) {
            // Check if previous line is empty or we are at the start of the block
            auto block_index = line->options.block_index;
            bool prev_is_multiline_comment = false;
            bool prev_is_empty = true;
            if (line_index.line_index != 0) {
                auto prev_index = line_index_make(line_index.block_index, line_index.line_index - 1);
                prev_is_multiline_comment = source_line_is_multi_line_comment_start(prev_index);
                prev_is_empty = index_value_text(prev_index)->tokens.size == 0 || index_value_text(prev_index)->tokens[0].type == Token_Type::COMMENT;
            }

            // Check if we need to reparse
            if (!prev_is_multiline_comment && prev_is_empty) {
                if (context == Block_Context::STATEMENTS) {
                    result.node = Block_Items::parse_anonymous_block(parent, block_index);
                }
                else {
                    result.node = Block_Items::parse_block_as_error(parent, block_index);
                }
            }
            else if (prev_is_multiline_comment) {
                block_index_remove_block_parse(block_index);
            }
            else {
                log_error("Invalid block, previous line wasn't empty and didn't require a follow block!", node_range_make_block(index_value(pos.line_index)->options.block_index));
                block_index_remove_block_parse(block_index);
            }
            return result;
        }

        // Skip empty lines + comments
        auto& text = line->options.text;
        if (source_line_is_comment(line_index) || text.tokens.size == 0) {
            return result;
        }

        // Parse line item
        switch (context)
        {
        case Block_Context::ENUM:
            result.node = AST::upcast(Block_Items::parse_enum_member(parent));
            break;
        case Block_Context::MODULE:
            result.node = Block_Items::parse_module_item(parent);
            break;
        case Block_Context::STATEMENTS:
            result.node = AST::upcast(Block_Items::parse_statement(parent));
            break;
        case Block_Context::STRUCT:
            result.node = AST::upcast(Block_Items::parse_definition(parent));
            break;
        case Block_Context::SWITCH:
            result.node = AST::upcast(Block_Items::parse_switch_case(parent));
            break;
        default: panic("");
        }

        if (result.node == 0) {
            // Line nodes don't report errors themselves if no parsing was possible
            result.line_count = 1;
            log_error_to_pos("Could't parse line item", token_index_make_line_end(pos.line_index));
            return result;
        }
        assert(line_index.line_index != pos.line_index.line_index || pos.token != 0, "If an item was parsed/not null, the position _must_ have chagned");

        if (pos.token == 0) {
            result.line_count = math_maximum(1, pos.line_index.line_index - line_index.line_index);
            return result;
        }

        result.line_count = pos.line_index.line_index - line_index.line_index + 1;
        if (remaining_line_tokens() > 0) {
            log_error_to_pos("Unexpected Tokens, Line already parsed", token_index_make_line_end(pos.line_index));
        }

        return result;
    }

    Block_Parse* parse_source_block(AST::Node* parent, Block_Index block_index, Block_Context context)
    {
        // DOCU: This function does not handle stepping out of the block at the end of parsing.
        //       All calling functions must manually set the cursor after this block after calling this function
        //       Both item_parse_fn and block_parse_fn don't have to advance the parser position/line_index index

        // Check if block is already parsed
        {
            auto block_parse_opt = hashtable_find_element(&parser.state.parsed_code->block_parses, block_index);
            if (block_parse_opt != 0) {
                auto block_parse = *block_parse_opt;
                block_parse_remove_items_from_ast(block_parse);
                block_parse->parent = parent;
                if (block_parse->context == context) {
                    block_parse->parent = parent;
                    block_parse->parent_parse = parser.state.block_parse;
                    return block_parse;
                }
                // Remove block_parse
                block_index_remove_block_parse(block_index);
            }
        }

        auto block_parse = new Block_Parse;
        // logg("Create new block parse for: %d %p\n", block_index.block_index, block_parse);
        block_parse->context = context;
        block_parse->index = block_index;
        block_parse->items = dynamic_array_create_empty<Line_Item>(1);
        block_parse->child_block_parses = dynamic_array_create_empty<Block_Parse*>(1);
        block_parse->parent_parse = parser.state.block_parse;
        block_parse->line_count = index_value(block_index)->lines.size;
        block_parse->parent = parent;
        hashtable_insert_element(&parser.state.parsed_code->block_parses, block_index, block_parse);

        // Insert into parent block parse
        if (parser.state.block_parse != 0) {
            dynamic_array_push_back(&parser.state.block_parse->child_block_parses, block_parse);
        }

        auto rewind_block = parser.state.block_parse;
        parser.state.block_parse = block_parse;
        SCOPE_EXIT(parser.state.block_parse = rewind_block);

        // Set end range (TODO: Check if this is even necessary)
        parent->range.end = node_position_make_block_end(block_index);

        // Parse all lines
        auto block = index_value(block_index);
        Line_Index line_index = line_index_make(block_index, 0);
        while (line_index.line_index < block->lines.size)
        {
            auto& pos = parser.state.pos;
            pos.token = 0;
            pos.line_index = line_index;

            auto line_item = parse_line_item(line_index, context, parent);
            if (line_item.node != 0) {
                dynamic_array_push_back(&block_parse->items, line_item);
            }
            assert(line_item.line_count > 0, "");
            line_index.line_index += line_item.line_count;
        }

        return block_parse;
    }

    void parse_follow_block(AST::Node* parent, Block_Context context)
    {
        auto& pos = parser.state.pos;

        auto block = index_value(pos.line_index.block_index);
        if (pos.line_index.line_index + 1 >= block->lines.size) {
            log_error_range_offset("Expected Follow Block, but found end of block", 0);
            return;
        }
        auto& next_line_item = block->lines[pos.line_index.line_index + 1];
        if (!next_line_item.is_block_reference) {
            log_error_range_offset("Expected Follow block", 0);
            return;
        }

        auto line = index_value_text(pos.line_index);
        if (remaining_line_tokens() > 0) {
            log_error_to_pos("Unexpected Tokens, ignoring tokens and parseing follow block", token_index_make_line_end(pos.line_index));
        }

        auto next_pos = token_index_make(line_index_make(pos.line_index.block_index, pos.line_index.line_index + 2), 0);
        parse_source_block(parent, next_line_item.options.block_index, context);
        parser.state.pos = next_pos;
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
        else if (related_expression != 0 && related_expression->type == Expression_Type::PATH_LOOKUP && related_expression->options.path_lookup->parts.size == 1) {
            // This is an experimental feature: give the block the name of the condition if possible,
            // e.g. switch color
            //      case .RED
            //          break color
            // In the future I also want to use this for loop variables, e.g.   loop a in array {break a}
            result = optional_make_success<String*>(related_expression->options.path_lookup->last->name);
        }
        return result;
    }

    Code_Block* parse_code_block(Node* parent, AST::Expression* related_expression)
    {
        auto result = allocate_base<Code_Block>(parent, Node_Type::CODE_BLOCK);
        auto follow_block_opt = line_index_block_after(parser.state.pos.line_index);
        if (follow_block_opt.available) {
            result->base.range = node_range_make_block(follow_block_opt.value);
        }
        result->statements = dynamic_array_create_empty<Statement*>(1);
        result->block_id = parse_block_label(related_expression);
        parse_follow_block(AST::upcast(result), Block_Context::STATEMENTS);
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
        result->default_value.available = false;
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
        path->parts = dynamic_array_create_empty<Symbol_Lookup*>(1);

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
                auto& pos = parser.state.pos;
                log_error("Expected identifier", node_range_make(token_index_advance(pos, -1), pos)); // Put error on the ~
                lookup->name = compiler.id_empty_string;
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
                    log_error("Missing member name", node_range_make(token_index_advance(pos, -1), pos));
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
            result->options.structure.parameters = dynamic_array_create_empty<Parameter*>(1);
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

            // Parse struct parameters
            if (test_parenthesis('(')) {
                parse_parenthesis_comma_seperated(&result->base, &result->options.structure.parameters, parse_parameter, Parenthesis_Type::PARENTHESIS);
            }

            parse_follow_block(AST::upcast(result), Block_Context::STRUCT);
            PARSE_SUCCESS(result);
        }
        if (test_keyword_offset(Keyword::ENUM, 0)) {
            result->type = Expression_Type::ENUM_TYPE;
            result->options.enum_members = dynamic_array_create_empty<Enum_Member*>(1);
            advance_token();
            parse_follow_block(AST::upcast(result), Block_Context::ENUM);
            PARSE_SUCCESS(result);
        }
        if (test_keyword_offset(Keyword::MODULE, 0)) {
            auto module = allocate_base<Module>(&result->base, Node_Type::MODULE);
            module->definitions = dynamic_array_create_empty<Definition*>(1);
            module->import_nodes = dynamic_array_create_empty<Import*>(1);
            advance_token();
            parse_follow_block(AST::upcast(module), Block_Context::MODULE);
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
                    log_error("Missing member name", node_range_make(token_index_advance(pos, -1), pos));
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
                range.start = node_position_make_token_index(link.token_index);
                range.end = node_position_make_token_index(token_index_next(link.token_index));
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

#undef CHECKPOINT_EXIT
#undef CHECKPOINT_SETUP
#undef PARSE_SUCCESS
#undef SET_END_RANGE

    void source_parse_destroy(Parsed_Code* parsed_code)
    {
        parser.state.parsed_code = parsed_code;
        // We need to destroy the root block parse because it recursively destory all others
        block_index_remove_block_parse(block_index_make(parsed_code->code, 0));
        hashtable_destroy(&parsed_code->block_parses);
        dynamic_array_destroy(&parsed_code->error_messages);
    }

    void source_parse_reset(Parsed_Code* parsed_code)
    {
        parser.state.parsed_code = parsed_code;
        // We need to destroy the root block parse because it recursively destory all others
        block_index_remove_block_parse(block_index_make(parsed_code->code, 0));
        hashtable_reset(&parsed_code->block_parses);
        dynamic_array_reset(&parsed_code->error_messages);
        parsed_code->root = 0;
    }

    void parse_root()
    {
        // Create root
        auto& root = parser.state.parsed_code->root;
        auto& code = parser.state.parsed_code->code;
        parser.state.pos = token_index_make_root(code);
        root = allocate_base<Module>(0, Node_Type::MODULE);
        root->definitions = dynamic_array_create_empty<Definition*>(1);
        root->import_nodes = dynamic_array_create_empty<Import*>(1);

        // Parse root
        parse_source_block(AST::upcast(root), block_index_make_root(code), Block_Context::MODULE);
        root->base.range = node_range_make_block(block_index_make_root(code));
        root->base.bounding_range = root->base.range;
    }

    void block_parse_remove_items_from_ast(Block_Parse* block_parse) {
        if (block_parse->parent == 0) {
            return;
        }
        switch (block_parse->context)
        {
        case Block_Context::MODULE: {
            if (block_parse->parent->type != AST::Node_Type::MODULE) {
                break;
            }
            auto module = AST::downcast<AST::Module>(block_parse->parent);
            dynamic_array_reset(&module->definitions);
            dynamic_array_reset(&module->import_nodes);
            break;
        }
        case Block_Context::ENUM: {
            if (block_parse->parent->type != AST::Node_Type::EXPRESSION) {
                break;
            }
            auto expression = AST::downcast<AST::Expression>(block_parse->parent);
            if (expression->type != AST::Expression_Type::ENUM_TYPE) {
                break;
            }
            dynamic_array_reset(&expression->options.enum_members);
            break;
        }
        case Block_Context::STATEMENTS: {
            if (block_parse->parent->type != AST::Node_Type::CODE_BLOCK) {
                break;
            }
            auto block = AST::downcast<AST::Code_Block>(block_parse->parent);
            dynamic_array_reset(&block->statements);
            break;
        }
        case Block_Context::STRUCT: {
            if (block_parse->parent->type != AST::Node_Type::EXPRESSION) {
                break;
            }
            auto expression = AST::downcast<AST::Expression>(block_parse->parent);
            if (expression->type != AST::Expression_Type::STRUCTURE_TYPE) {
                break;
            }
            dynamic_array_reset(&expression->options.structure.members);
            break;
        }
        case Block_Context::SWITCH: {
            if (block_parse->parent->type != AST::Node_Type::STATEMENT) {
                break;
            }
            auto statement = AST::downcast<AST::Statement>(block_parse->parent);
            if (statement->type != AST::Statement_Type::SWITCH_STATEMENT) {
                break;
            }
            dynamic_array_reset(&statement->options.switch_statement.cases);
            break;
        }
        default: panic("");
        }
    }

    void block_parse_add_items_to_ast(Block_Parse* block_parse) {
        assert(block_parse->parent != 0, "Cannot happen anymore");
        for (int i = 0; i < block_parse->items.size; i++) {
            block_parse->items[i].node->parent = block_parse->parent;
        }
        switch (block_parse->context)
        {
        case Block_Context::MODULE: {
            if (block_parse->parent->type != AST::Node_Type::MODULE) {
                break;
            }
            auto module = AST::downcast<AST::Module>(block_parse->parent);
            for (int i = 0; i < block_parse->items.size; i++) {
                auto& item = block_parse->items[i].node;
                if (item->type == AST::Node_Type::IMPORT) {
                    dynamic_array_push_back(&module->import_nodes, AST::downcast<AST::Import>(item));
                }
                else if (item->type == AST::Node_Type::DEFINITION) {
                    dynamic_array_push_back(&module->definitions, AST::downcast<AST::Definition>(item));
                }
                else {
                    panic("HEY");
                }
            }
            break;
        }
        case Block_Context::ENUM: {
            if (block_parse->parent->type != AST::Node_Type::EXPRESSION) {
                break;
            }
            auto expression = AST::downcast<AST::Expression>(block_parse->parent);
            if (expression->type != AST::Expression_Type::ENUM_TYPE) {
                break;
            }
            for (int i = 0; i < block_parse->items.size; i++) {
                auto& item = block_parse->items[i].node;
                dynamic_array_push_back(&expression->options.enum_members, AST::downcast<AST::Enum_Member>(item));
            }
            break;
        }
        case Block_Context::STATEMENTS: {
            if (block_parse->parent->type != AST::Node_Type::CODE_BLOCK) {
                break;
            }
            auto block = AST::downcast<AST::Code_Block>(block_parse->parent);
            for (int i = 0; i < block_parse->items.size; i++) {
                auto& item = block_parse->items[i].node;
                dynamic_array_push_back(&block->statements, AST::downcast<AST::Statement>(item));
            }
            break;
        }
        case Block_Context::STRUCT: {
            if (block_parse->parent->type != AST::Node_Type::EXPRESSION) {
                break;
            }
            auto expression = AST::downcast<AST::Expression>(block_parse->parent);
            if (expression->type != AST::Expression_Type::STRUCTURE_TYPE) {
                break;
            }
            for (int i = 0; i < block_parse->items.size; i++) {
                auto& item = block_parse->items[i].node;
                dynamic_array_push_back(&expression->options.structure.members, AST::downcast<AST::Definition>(item));
            }
            break;
        }
        case Block_Context::SWITCH: {
            if (block_parse->parent->type != AST::Node_Type::STATEMENT) {
                break;
            }
            auto statement = AST::downcast<AST::Statement>(block_parse->parent);
            if (statement->type != AST::Statement_Type::SWITCH_STATEMENT) {
                break;
            }
            for (int i = 0; i < block_parse->items.size; i++) {
                auto& item = block_parse->items[i].node;
                dynamic_array_push_back(&statement->options.switch_statement.cases, AST::downcast<AST::Switch_Case>(item));
            }
            break;
        }
        default: panic("");
        }
    }

    bool block_index_equal(Block_Index* a, Block_Index* b) {
        return index_equal(*a, *b);
    }
    u64 block_index_hash(Block_Index* a) {
        return hash_combine(hash_pointer(a->code), hash_i32(&a->block_index));
    }

    void parser_prepare_parsing(Parsed_Code* parsed_code)
    {
        parser.state.parsed_code = parsed_code;
        parser.state.error_count = 0;
        parser.state.block_parse = 0;
        dynamic_array_reset(&parser.new_error_messages);
        dynamic_array_reset(&parser.allocated_nodes);
    }

    Parsed_Code* execute_clean(Source_Code* code)
    {
        Parsed_Code* parsed_code = new Parsed_Code;
        parsed_code->block_parses = hashtable_create_empty<Block_Index, Block_Parse*>(1, block_index_hash, block_index_equal);
        parsed_code->code = code;
        parsed_code->error_messages = dynamic_array_create_empty<Error_Message>(1);
        parsed_code->timestamp.node_index = 0;

        parser_prepare_parsing(parsed_code);
        parse_root();

        // Add block parse items to things...
        {
            auto iter = hashtable_iterator_create(&parsed_code->block_parses);
            while (hashtable_iterator_has_next(&iter)) {
                auto block_parse = *iter.value;
                block_parse_remove_items_from_ast(block_parse);
                block_parse_add_items_to_ast(block_parse);
                hashtable_iterator_next(&iter);
            }
        }

        // Add created error messages to all error messages
        dynamic_array_append_other(&parsed_code->error_messages, &parser.new_error_messages);
        dynamic_array_reset(&parser.new_error_messages);

        return parsed_code;
    }


    // Incremental Block-Parsing
    struct Line_Status
    {
        bool is_original;
        int original_id;
        bool was_changed;

        bool needs_reparse;
        bool was_reparsed;
        bool is_original_item_line;
        int original_item_index;
        int original_item_line;
    };

    struct Block_Difference {
        int block_index;
        Dynamic_Array<Line_Status> line_states;
    };

    struct Source_Difference {
        Parsed_Code* parsed_code;
        Dynamic_Array<Block_Index> removed_blocks;
        Dynamic_Array<Block_Difference> block_differences;
    };

    Block_Difference* source_difference_get_or_create_block_difference(Source_Difference* differences, Block_Index block_index)
    {
        Block_Difference* block_difference = 0;
        for (int i = 0; i < differences->block_differences.size; i++) {
            if (differences->block_differences[i].block_index == block_index.block_index) {
                return &differences->block_differences[i];
            }
        }
        for (int i = 0; i < differences->removed_blocks.size; i++) {
            if (differences->removed_blocks[i].block_index == block_index.block_index) {
                return 0;
            }
        }
        auto block_parse = hashtable_find_element(&differences->parsed_code->block_parses, block_index);
        if (block_parse == 0) {
            return 0; // We don't need differences if no block parse exists
        }

        Block_Difference new_diff;
        new_diff.block_index = block_index.block_index;
        new_diff.line_states = dynamic_array_create_empty<Line_Status>((*block_parse)->line_count);
        for (int i = 0; i < (*block_parse)->line_count; i++) {
            Line_Status line_state;
            line_state.is_original = true;
            line_state.original_id = i;
            line_state.was_changed = false;
            line_state.is_original_item_line = false;
            line_state.original_item_index = -1;
            line_state.original_item_line = -1;
            line_state.needs_reparse = false;
            line_state.was_reparsed = false;
            dynamic_array_push_back(&new_diff.line_states, line_state);
        }
        for (int i = 0; i < (*block_parse)->items.size; i++) {
            auto& item = (*block_parse)->items[i];
            for (int j = 0; j < item.line_count; j++) {
                auto& state = new_diff.line_states[item.line_start + j];
                state.is_original_item_line = true;
                state.original_item_index = i;
                state.original_item_line = j;
            }
        }
        dynamic_array_push_back(&differences->block_differences, new_diff);
        return &differences->block_differences[differences->block_differences.size - 1];
    }

    void source_difference_insert_new_line(Source_Difference* source_difference, Line_Index line_index)
    {
        auto block_difference = source_difference_get_or_create_block_difference(source_difference, line_index.block_index);
        if (block_difference == 0) {
            return;
        }
        Line_Status status;
        status.is_original = false;
        status.original_id = -1;
        status.was_changed = false;
        status.is_original_item_line = false;
        status.original_item_line = -1;
        status.original_item_index = -1;
        dynamic_array_insert_ordered(&block_difference->line_states, status, line_index.line_index);
    }

    void source_difference_remove_line(Source_Difference* source_difference, Line_Index line_index) {
        auto block_difference = source_difference_get_or_create_block_difference(source_difference, line_index.block_index);
        if (block_difference == 0) {
            return;
        }
        dynamic_array_remove_ordered(&block_difference->line_states, line_index.line_index);
    }

    void source_difference_mark_line_as_changed(Source_Difference* source_difference, Line_Index line_index) {
        auto block_difference = source_difference_get_or_create_block_difference(source_difference, line_index.block_index);
        if (block_difference == 0) {
            return;
        }
        block_difference->line_states[line_index.line_index].was_changed = true;
    }

    void source_differences_remove_block(Source_Difference* differences, Block_Index block_index, Line_Index block_line_index)
    {
        assert(block_index.block_index != 0, "Root block cannot be removed");
        // Add to removed-blocks set if its not in there
        {
            int index = -1;
            auto& removed = differences->removed_blocks;
            for (int i = 0; i < removed.size; i++) {
                if (removed[i].block_index == block_index.block_index) {
                    index = i;
                    break;
                }
            }
            if (index == -1) {
                dynamic_array_push_back(&removed, block_index);
            }
        }

        // Remove block-difference if existing
        {
            int index = -1;
            auto& block_diffs = differences->block_differences;
            for (int i = 0; i < block_diffs.size; i++) {
                if (block_diffs[i].block_index == block_index.block_index) {
                    index = i;
                    break;
                }
            }
            if (index != -1) {
                dynamic_array_destroy(&block_diffs[index].line_states);
                dynamic_array_remove_ordered(&block_diffs, index);
            }
        }

        // Delete reference line
        source_difference_remove_line(differences, block_line_index);
    }

    void ast_node_update_node_range_in_block(AST::Node* node, int line_difference, Block_Index block_index)
    {
        auto update_token_index = [&](AST::Node_Position& pos, bool& any_inside) {
            if (pos.type != AST::Node_Position_Type::TOKEN_INDEX) {
                return;
            }
            if (pos.options.token_index.line_index.block_index.block_index == block_index.block_index) {
                any_inside = true;
                pos.options.token_index.line_index.line_index += line_difference;
            }
        };
        bool any_inside = false;
        update_token_index(node->range.start, any_inside);
        update_token_index(node->range.end, any_inside);
        update_token_index(node->bounding_range.start, any_inside);
        update_token_index(node->bounding_range.end, any_inside);
        if (!any_inside) {
            return;
        }

        int child_index = 0;
        auto child = base_get_child(node, child_index);
        while (child != 0) {
            ast_node_update_node_range_in_block(child, line_difference, block_index);
            child_index += 1;
            child = base_get_child(node, child_index);
        }
    }

    void execute_incremental(Parsed_Code* parsed_code, Code_History* history)
    {
        // Get changes since last sync
        Dynamic_Array<Code_Change> changes = dynamic_array_create_empty<Code_Change>(1);
        SCOPE_EXIT(dynamic_array_destroy(&changes));
        auto now = history_get_timestamp(history);
        history_get_changes_between(history, parsed_code->timestamp, now, &changes);
        parsed_code->timestamp = now;
        if (changes.size == 0) {
            return;
        }

        // Prepare for re-parsing
        parser_prepare_parsing(parsed_code);

        Source_Difference differences;
        differences.parsed_code = parsed_code;
        differences.removed_blocks = dynamic_array_create_empty<Block_Index>(1);
        differences.block_differences = dynamic_array_create_empty<Block_Difference>(1);
        SCOPE_EXIT(
            for (int i = 0; i < differences.block_differences.size; i++) {
                dynamic_array_destroy(&differences.block_differences[i].line_states);
            }
        dynamic_array_destroy(&differences.block_differences);
        dynamic_array_destroy(&differences.removed_blocks);
        );

        // Fill source-differences
        for (int i = 0; i < changes.size; i++)
        {
            auto& change = changes[i];
            switch (change.type)
            {
            case Code_Change_Type::BLOCK_INSERT:
            {
                auto& insert = change.options.block_insert;
                if (change.apply_forwards) {
                    source_difference_insert_new_line(&differences, insert.line_index);
                }
                else {
                    source_differences_remove_block(&differences, insert.new_block_index, insert.line_index);
                }
                break;
            }
            case Code_Change_Type::BLOCK_MERGE:
            {
                auto& merge = change.options.block_merge;
                if (change.apply_forwards) {
                    auto into_line_index = line_index_make(merge.from_line_index.block_index, merge.from_line_index.line_index - 1);
                    source_differences_remove_block(&differences, merge.from_block_index, merge.from_line_index);
                    source_differences_remove_block(&differences, merge.into_block_index, into_line_index);
                    source_difference_insert_new_line(&differences, into_line_index);
                }
                else {
                    // Remove lines after split
                    auto into_diff = source_difference_get_or_create_block_difference(&differences, merge.into_block_index);
                    dynamic_array_rollback_to_size(&into_diff->line_states, merge.into_line_count);
                    // Add split block as new block
                    source_difference_insert_new_line(&differences, merge.from_line_index);
                }
                break;
            }
            case Code_Change_Type::LINE_INSERT: {
                auto& insert = change.options.line_insert;
                if (change.apply_forwards) {
                    source_difference_insert_new_line(&differences, insert);
                }
                else {
                    source_difference_remove_line(&differences, insert);
                }
                break;
            }
            case Code_Change_Type::TEXT_INSERT: {
                source_difference_mark_line_as_changed(&differences, change.options.text_insert.index.line_index);
                break;
            }
            default: panic("");
            }
        }

        // Print source differences
        {
            String tmp = string_create_empty(512);
            SCOPE_EXIT(string_destroy(&tmp));
            string_append_formated(&tmp, "Removed blocks: ");
            for (int i = 0; i < differences.removed_blocks.size; i++) {
                string_append_formated(&tmp, "%d ", differences.removed_blocks[i].block_index);
            }
            string_append_formated(&tmp, "\n");
            for (int i = 0; i < differences.block_differences.size; i++) {
                auto& block_diff = differences.block_differences[i];
                string_append_formated(&tmp, "Block_Difference of block #%d:\n", block_diff.block_index);
                for (int j = 0; j < block_diff.line_states.size; j++) {
                    auto& line_state = block_diff.line_states[j];
                    string_append_formated(&tmp, "    #%d: ", j);
                    if (line_state.is_original) {
                        string_append_formated(&tmp, "ORIGIAL(%2d) %s\n", line_state.original_id, (line_state.was_changed ? "was changed" : ""));
                    }
                    else {
                        string_append_formated(&tmp, "ADDED\n");
                    }
                }
            }
            //logg("Source Differences: \n%s\n\n", tmp.characters);
        }

        // Remove removed blocks (Deletes all ast-nodes)
        for (int i = 0; i < differences.removed_blocks.size; i++) {
            block_index_remove_block_parse(differences.removed_blocks[i]);
        }

        // Reparse changed blocks
        for (int i = 0; i < differences.block_differences.size; i++)
        {
            auto& block_difference = differences.block_differences[i];
            // Ignore new blocks
            Block_Parse* block_parse;
            {
                auto block_parse_opt = hashtable_find_element(&parsed_code->block_parses, block_index_make(parsed_code->code, block_difference.block_index));
                if (block_parse_opt == 0) {
                    continue;
                }
                block_parse = *block_parse_opt;
            }
            assert(block_difference.line_states.size == index_value(block_parse->index)->lines.size, "");

            // Mark lines/items for reparsing
            struct Original_Item_Change {
                bool needs_reparse; // If any lines have changed or were deleted
                bool was_deleted; // If the head was deleted or reparsing parsed over the head
                bool was_reparsed;
                Line_Item reparse_item;
                int new_line_start;
            };
            Array<Original_Item_Change> original_item_changes = array_create_empty<Original_Item_Change>(block_parse->items.size);
            SCOPE_EXIT(array_destroy(&original_item_changes));
            for (int i = 0; i < block_parse->items.size; i++) {
                original_item_changes[i].needs_reparse = true;
                original_item_changes[i].was_deleted = true;
                original_item_changes[i].was_reparsed = false;
                original_item_changes[i].new_line_start = block_parse->items[i].line_start;
            }
            {
                int expected_original_line = 0;
                for (int line_index = 0; line_index < block_difference.line_states.size; line_index++)
                {
                    auto& line_status = block_difference.line_states[line_index];
                    // Check if we are on the start of a original item
                    if (line_status.is_original_item_line && line_status.original_item_line == 0)
                    {
                        auto& item = block_parse->items[line_status.original_item_index];
                        auto& item_change = original_item_changes[line_status.original_item_index];
                        item_change.was_deleted = false;
                        item_change.new_line_start = line_index;
                        // Check if all following lines are also originals, otherwise we need to mark the item as reparse
                        bool all_check = true;
                        for (int i = 1; i < item.line_count; i++) {
                            if (line_index + i >= block_difference.line_states.size) { // Some lines were deleted
                                all_check = false;
                                break;
                            }
                            const auto& next_state = block_difference.line_states[line_index + i];
                            if (next_state.was_changed || !(next_state.is_original && next_state.original_id == line_status.original_id + i)) {
                                all_check = false;
                                break;
                            }
                        }
                        if (all_check) {
                            item_change.needs_reparse = false;
                        }
                    }

                    bool mark_previous_potential_multiline_items_as_reparse = false;
                    if (line_status.is_original) {
                        if (line_status.original_id == expected_original_line) {
                            expected_original_line += 1;
                        }
                        else {
                            // Note that this lines doesn't need a reparse, since only previous lines were deleted
                            mark_previous_potential_multiline_items_as_reparse = true;
                            expected_original_line = line_status.original_id + 1;
                        }
                    }
                    if (!line_status.is_original) {
                        auto watch_line_index = line_index_make(block_parse->index, line_index);
                        auto line = index_value(watch_line_index);
                        if (!(source_line_is_comment(watch_line_index) || (!line->is_block_reference && line->options.text.tokens.size == 0))) {
                            mark_previous_potential_multiline_items_as_reparse = true;
                            line_status.needs_reparse = true;
                        }
                    }
                    else if (line_status.was_changed) {
                        mark_previous_potential_multiline_items_as_reparse = true;
                        line_status.needs_reparse = true;
                    }

                    if (mark_previous_potential_multiline_items_as_reparse) {
                        int k = line_index;
                        if (index_value(line_index_make(block_parse->index, line_index))->is_block_reference) {
                            k += 1;
                        }
                        while (k - 2 >= 0) {
                            if (index_value(line_index_make(block_parse->index, k - 1))->is_block_reference &&
                                !index_value(line_index_make(block_parse->index, k - 2))->is_block_reference) {
                                block_difference.line_states[k - 1].needs_reparse = true;
                                block_difference.line_states[k - 2].needs_reparse = true;
                                k -= 2;
                            }
                            else {
                                break;
                            }
                        }
                    }
                }
            }

            // Do the block-reparse :D
            parser.state.parsed_code = parsed_code;
            parser.state.block_parse = block_parse;
            int line_index = 0;
            auto block = index_value(block_parse->index);
            Dynamic_Array<Line_Item> new_line_items = dynamic_array_create_empty<Line_Item>(1);
            SCOPE_EXIT(dynamic_array_destroy(&new_line_items));
            while (line_index < block->lines.size)
            {
                auto& line = block->lines[line_index];
                auto& reparse_info = block_difference.line_states[line_index];

                // Skip lines that don't need a reparse
                if (!(reparse_info.needs_reparse || (reparse_info.is_original_item_line && original_item_changes[reparse_info.original_item_index].needs_reparse))) {
                    line_index += 1;
                    continue;
                }

                //logg("Reparsing block : %d, line: %d\n", block_parse->index.block_index, line_index);
                auto line_item = parse_line_item(line_index_make(block_parse->index, line_index), block_parse->context, block_parse->parent);
                if (reparse_info.is_original_item_line && reparse_info.original_item_line == 0) {
                    if (line_item.node != 0) {
                        original_item_changes[reparse_info.original_item_index].was_reparsed = true;
                        original_item_changes[reparse_info.original_item_index].reparse_item = line_item;
                    }
                    else {
                        original_item_changes[reparse_info.original_item_index].was_deleted = true;
                        original_item_changes[reparse_info.original_item_index].needs_reparse = true;
                    }
                }
                else if (line_item.node != 0) {
                    dynamic_array_push_back(&new_line_items, line_item);
                }
                // Check all lines that were skipped
                reparse_info.was_reparsed = true;
                for (int i = line_index + 1; i < line_index + line_item.line_count; i++) {
                    auto& info = block_difference.line_states[i];
                    info.was_reparsed = true;
                    if (info.is_original_item_line) {
                        original_item_changes[info.original_item_index].needs_reparse = true;
                        if (info.original_item_line == 0) {
                            original_item_changes[info.original_item_index].was_deleted = true;
                        }
                    }
                }
                line_index += line_item.line_count;
                // If we land on a block-reference, this will always need reparse, so that block-parse parents are updated correctly
                if (line_index < block->lines.size && index_value(line_index_make(block_parse->index, line_index))->is_block_reference) {
                    block_difference.line_states[line_index].needs_reparse = true;
                }
            }

            // Remove errors in reparsed/deleted/changed lines
            {
                struct Original_Line_Change
                {
                    bool was_deleted;
                    bool was_changed;
                };
                Array<Original_Line_Change> original_line_changes = array_create_empty<Original_Line_Change>(block_parse->line_count);
                SCOPE_EXIT(array_destroy(&original_line_changes));
                for (int i = 0; i < original_line_changes.size; i++) {
                    original_line_changes[i].was_deleted = true;
                    original_line_changes[i].was_changed = false;
                }
                for (int i = 0; i < block->lines.size; i++) {
                    auto& line_state = block_difference.line_states[i];
                    if (line_state.is_original) {
                        original_line_changes[line_state.original_id].was_deleted = false;
                        if (line_state.was_changed || line_state.was_reparsed) {
                            original_line_changes[line_state.original_id].was_changed = true;
                        }
                    }
                }
                for (int i = 0; i < parsed_code->error_messages.size; i++) {
                    auto& error = parsed_code->error_messages[i];
                    if (error.block_parse == block_parse) {
                        auto& line_change = original_line_changes[error.origin_line_index];
                        if (line_change.was_changed || line_change.was_deleted) {
                            //logg("Removing error message: %s\n", error.msg);
                            dynamic_array_swap_remove(&parsed_code->error_messages, i);
                            i -= 1;
                            continue;
                        }
                    }
                }
            }
            block_parse->line_count = block->lines.size;

            // Update Line_Items: remove deleted items, update node-ranges, update errors from this item
            block_parse_remove_items_from_ast(block_parse);
            for (int i = original_item_changes.size - 1; i >= 0; i--) {
                auto& change = original_item_changes[i];
                auto& item = block_parse->items[i];
                // TODO: Cleanup/free old item
                if (change.was_deleted) {
                    ast_node_destroy_recursively(item.node);
                    dynamic_array_remove_ordered(&block_parse->items, i);
                }
                else if (change.was_reparsed) {
                    ast_node_destroy_recursively(item.node);
                    item = change.reparse_item;
                }
                else if (change.new_line_start != block_parse->items[i].line_start) {
                    auto index_update_helper = [&](int& line, Block_Index block_index) {
                        if (line >= item.line_start && line < item.line_start + item.line_count &&
                            index_equal(block_index, block_parse->index))
                        {
                            line = change.new_line_start + (line - item.line_start);
                        }
                    };
                    auto& item = block_parse->items[i];
                    for (int j = 0; j < parsed_code->error_messages.size; j++) {
                        auto& error = parsed_code->error_messages[j];
                        index_update_helper(error.origin_line_index, block_parse->index);
                        if (error.range.start.type == AST::Node_Position_Type::TOKEN_INDEX) {
                            index_update_helper(error.range.start.options.token_index.line_index.line_index, error.range.start.options.token_index.line_index.block_index);
                        }
                        if (error.range.end.type == AST::Node_Position_Type::TOKEN_INDEX) {
                            index_update_helper(error.range.end.options.token_index.line_index.line_index, error.range.end.options.token_index.line_index.block_index);
                        }
                    }
                    ast_node_update_node_range_in_block(item.node, change.new_line_start - item.line_start, block_parse->index);
                    item.line_start = change.new_line_start;
                }
            }

            // Insert added line-items
            for (int i = 0; i < new_line_items.size; i++) {
                auto new_item = new_line_items[i];
                int insertion_index = 0;
                for (; insertion_index < block_parse->items.size; insertion_index++) {
                    auto& item = block_parse->items[insertion_index];
                    assert(new_item.node != 0, "");
                    assert(item.line_start != new_item.line_start, "");
                    if (item.line_start >= new_item.line_start) {
                        break;
                    }
                }
                dynamic_array_insert_ordered(&block_parse->items, new_item, insertion_index);
            }
        }

        // Add generated errors
        dynamic_array_append_other(&parsed_code->error_messages, &parser.new_error_messages);
        dynamic_array_reset(&parser.new_error_messages);

        // Add block parse items to things...
        {
            auto iter = hashtable_iterator_create(&parsed_code->block_parses);
            while (hashtable_iterator_has_next(&iter)) {
                auto block_parse = *iter.value;
                block_parse_remove_items_from_ast(block_parse);
                block_parse_add_items_to_ast(block_parse);
                hashtable_iterator_next(&iter);
            }
        }

        //logg("INCREMENTAL REPARSE RESULT: \n----------------------\n");
        //AST::base_print(AST::upcast(parsed_code->root));
    }



    // AST queries based on Token-Indices
    void ast_base_get_section_token_range(AST::Node* base, Section section, Dynamic_Array<Token_Range>* ranges)
    {
        auto range = node_range_to_token_range(base->range);
        switch (section)
        {
        case Section::NONE: break;
        case Section::WHOLE:
        {
            dynamic_array_push_back(ranges, range);
            break;
        }
        case Section::WHOLE_NO_CHILDREN:
        {
            Token_Range sub_range;
            sub_range.start = range.start;
            int index = 0;
            auto child = AST::base_get_child(base, index);
            while (child != 0)
            {
                auto child_range = node_range_to_token_range(child->range);
                if (!index_equal(sub_range.start, child_range.start)) {
                    sub_range.end = child_range.start;
                    dynamic_array_push_back(ranges, sub_range);
                }
                sub_range.start = child_range.end;

                index += 1;
                child = AST::base_get_child(base, index);
            }
            if (!index_equal(sub_range.start, range.end)) {
                sub_range.end = range.end;
                dynamic_array_push_back(ranges, sub_range);
            }
            break;
        }
        case Section::IDENTIFIER:
        {
            auto result = search_token(range.start, [](Token* t, void* _unused) -> bool {return t->type == Token_Type::IDENTIFIER; }, 0, false);
            if (!result.available) {
                break;
            }
            dynamic_array_push_back(ranges, token_range_make_offset(result.value, 1));
            break;
        }
        case Section::ENCLOSURE:
        {
            // Find next (), {} or [], and add the tokens to the ranges
            auto result = search_token(range.start, [](Token* t, void* type) -> bool {return t->type == Token_Type::PARENTHESIS; }, 0, false);
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
            auto result = search_token(range.start, [](Token* t, void* type) -> bool {return t->type == Token_Type::KEYWORD; }, 0, false);
            if (!result.available) {
                break;
            }
            dynamic_array_push_back(ranges, token_range_make_offset(result.value, 1));
            break;
        }
        case Section::FIRST_TOKEN: {
            Token_Index next = token_index_advance(range.start, 1);
            if (!index_valid(next)) {
                next = range.start;
            }
            dynamic_array_push_back(ranges, token_range_make(range.start, next));
            break;
        }
        case Section::END_TOKEN: {
            Token_Range end_token_range = token_range_make(token_index_advance(range.end, -1), range.end);
            end_token_range.start.token = math_maximum(0, end_token_range.start.token);
            dynamic_array_push_back(ranges, end_token_range);
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
            auto child_range = node_range_to_token_range(child->bounding_range);
            if (token_range_contains(child_range, index)) {
                return find_smallest_enclosing_node(child, index);
            }
            child_index += 1;
            child = AST::base_get_child(base, child_index);
        }
        return base;
    }
}
