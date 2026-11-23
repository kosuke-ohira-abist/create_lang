#include "expr_parser.hpp"

#include <memory>
#include <utility>

namespace lang
{
    namespace
    {
        class Parser
        {
        public:
            explicit Parser(std::span<const Token> tokens) : m_tokens(tokens) {}

            ExprResult parse()
            {
                // End で終わらないとだめ
                if (m_tokens.empty() || !std::holds_alternative<End>(m_tokens.back().value))
                {
                    return ParseError{{0, 0}, "token stream must end with End"};
                }

                auto result = expression();

                // 構文エラー
                if (const auto *error = std::get_if<ParseError>(&result))
                {
                    return *error;
                }

                const Token &token = m_tokens[m_position];

                if (!std::holds_alternative<End>(token.value))
                {
                    return ParseError{token.span, "unexpected token after expression"};
                }

                return result;
            }

            StatementsResult parse_program()
            {
                if (m_tokens.empty() || !std::holds_alternative<End>(m_tokens.back().value))
                {
                    return ParseError{{0, 0}, "token stream must end with End"};
                }

                return parse_statement_sequence(false);
            }

        private:
            ExprResult expression()
            {
                return compare();
            }

            StatementResult statement()
            {
                const Token &token = m_tokens[m_position];

                // if
                if (std::holds_alternative<If>(token.value))
                {
                    return if_statement();
                }

                // return
                if (std::holds_alternative<Return>(token.value))
                {
                    return return_statement();
                }

                // break
                if (std::holds_alternative<Break>(token.value))
                {
                    ++m_position;
                    return Statement{BreakStatement{token.span}};
                }

                // continue
                if (std::holds_alternative<Continue>(token.value))
                {
                    ++m_position;
                    return Statement{ContinueStatement{token.span}};
                }

                // fn
                if (std::holds_alternative<Fn>(token.value))
                {
                    return function_definition();
                }

                // for
                if (std::holds_alternative<For>(token.value))
                {
                    return for_statement();
                }

                // var def
                if (std::holds_alternative<Var>(token.value))
                {
                    ++m_position;

                    // 変数名
                    const Token &name_token = m_tokens[m_position];
                    const auto *id = std::get_if<Ident>(&name_token.value);

                    if (!id)
                    {
                        return ParseError{name_token.span, "expected variable name after 'var'"};
                    }

                    const auto name = id->text;
                    ++m_position;

                    // 型宣言
                    if (!std::holds_alternative<Colon>(m_tokens[m_position].value))
                    {
                        return ParseError{m_tokens[m_position].span, "expected ':' after variable name"};
                    }

                    ++m_position;

                    auto type_result = parse_type_decl();

                    if (const auto *error = std::get_if<ParseError>(&type_result))
                    {
                        return *error;
                    }

                    const TypeDecl declared_type = std::get<TypeDecl>(type_result);

                    // =
                    const Token &equal_token = m_tokens[m_position];

                    if (!std::holds_alternative<Equal>(equal_token.value))
                    {
                        return ParseError{equal_token.span, "expected '=' after variable name"};
                    }

                    ++m_position;

                    // 初期化式
                    auto result = expression();

                    if (const auto *error = std::get_if<ParseError>(&result))
                    {
                        return *error;
                    }

                    return Statement{VarDef{
                        name,
                        declared_type,
                        std::get<Expression>(std::move(result))}};
                }

                // var assign
                if (const auto *id = std::get_if<Ident>(&token.value))
                {
                    // 次の token が = か調べる（消費はしない）
                    if (m_position + 1 < m_tokens.size() && std::holds_alternative<Equal>(m_tokens[m_position + 1].value))
                    {
                        // 変数名
                        const auto name = id->text;
                        const auto name_span = token.span;

                        // 変数名 と = を消費
                        m_position += 2;

                        // 代入する式
                        auto result = expression();

                        if (const auto *error = std::get_if<ParseError>(&result))
                        {
                            return *error;
                        }

                        return Statement{VarAssign{name, std::get<Expression>(std::move(result)), name_span}};
                    }
                }

                // expression statement
                auto result = expression();
                if (const auto *error = std::get_if<ParseError>(&result))
                {
                    return *error;
                }
                return Statement{std::get<Expression>(std::move(result))};
            }

            using TypeDeclResult = std::variant<TypeDecl, ParseError>;

            TypeDeclResult parse_type_decl()
            {
                const Token &token = m_tokens[m_position];

                const auto *id = std::get_if<Ident>(&token.value);

                if (!id)
                {
                    return ParseError{token.span, "expected a type name"};
                }

                TypeDecl type;

                if (id->text == "f64")
                {
                    type = TypeDecl::F64;
                }
                else if (id->text == "i64")
                {
                    type = TypeDecl::I64;
                }
                else if (id->text == "str")
                {
                    type = TypeDecl::Str;
                }
                else
                {
                    return ParseError{token.span, "unknown type: " + std::string(id->text)};
                }

                ++m_position;

                return type;
            }

            StatementsResult parse_statement_sequence(bool in_block)
            {
                Statements statements;

                while (true)
                {
                    const Token &token = m_tokens[m_position];

                    // トップレベルの End
                    if (std::holds_alternative<End>(token.value))
                    {
                        // block 内で } で終わらないならエラー
                        if (in_block)
                        {
                            return ParseError{token.span, "expected '}' to close block"};
                        }

                        return statements;
                    }

                    // block の終わり
                    if (std::holds_alternative<RBrace>(token.value))
                    {
                        if (in_block)
                        {
                            return statements;
                        }

                        return ParseError{token.span, "unexpected '}'"};
                    }

                    // } で終了する例外の文はここで確認しておく
                    const bool is_for = std::holds_alternative<For>(token.value);
                    const bool is_fn = std::holds_alternative<Fn>(token.value);
                    const bool is_if = std::holds_alternative<If>(token.value);

                    auto result = statement();

                    if (const auto *error = std::get_if<ParseError>(&result))
                    {
                        return *error;
                    }

                    statements.push_back(std::get<Statement>(std::move(result)));

                    // for 文は ; で終わる必要はないのでここで continue
                    const bool is_lbrace_end = is_for || is_fn || is_if;
                    if (is_lbrace_end)
                    {
                        continue;
                    }

                    // ; で終了
                    if (!std::holds_alternative<Semicolon>(m_tokens[m_position].value))
                    {
                        return ParseError{m_tokens[m_position].span, "expected ';' after statement"};
                    }

                    ++m_position;
                }
            }

            StatementResult if_statement()
            {
                // if を消費
                ++m_position;

                // condition
                auto condition_result = expression();

                if (const auto *error = std::get_if<ParseError>(&condition_result))
                {
                    return *error;
                }

                auto condition = std::get<Expression>(std::move(condition_result));

                // {
                if (!std::holds_alternative<LBrace>(m_tokens[m_position].value))
                {
                    return ParseError{m_tokens[m_position].span, "expected '{' after if condition"};
                }
                ++m_position;

                // then branch
                auto then_result = parse_statement_sequence(true);

                if (const auto *error = std::get_if<ParseError>(&then_result))
                {
                    return *error;
                }

                auto then_branch = std::get<Statements>(std::move(then_result));

                // }
                if (!std::holds_alternative<RBrace>(m_tokens[m_position].value))
                {
                    return ParseError{m_tokens[m_position].span, "expected '}' after then branch"};
                }

                ++m_position;

                // else branch (optional)
                std::optional<Statements> else_branch;

                if (std::holds_alternative<Else>(m_tokens[m_position].value))
                {
                    // else を消費
                    ++m_position;

                    // else if
                    if (std::holds_alternative<If>(m_tokens[m_position].value))
                    {
                        auto result = if_statement();

                        if (const auto *error = std::get_if<ParseError>(&result))
                        {
                            return *error;
                        }

                        Statements nested;

                        nested.push_back(std::get<Statement>(std::move(result)));

                        else_branch.emplace(std::move(nested));
                    }

                    // {
                    else if (std::holds_alternative<LBrace>(m_tokens[m_position].value))
                    {

                        ++m_position;

                        auto else_result = parse_statement_sequence(true);

                        if (const auto *error = std::get_if<ParseError>(&else_result))
                        {
                            return *error;
                        }

                        else_branch.emplace(std::get<Statements>(std::move(else_result)));

                        // }
                        if (!std::holds_alternative<RBrace>(m_tokens[m_position].value))
                        {
                            return ParseError{m_tokens[m_position].span, "expected '}' after else branch"};
                        }

                        ++m_position;
                    }
                    else
                    {
                        return ParseError{m_tokens[m_position].span,
                                          "expected 'if' or '{' after else"};
                    }
                }

                return Statement{std::make_unique<IfStatement>(
                    IfStatement{
                        std::move(condition),
                        std::move(then_branch),
                        std::move(else_branch)})};
            }

            StatementResult return_statement()
            {
                // return を消費
                ++m_position;

                // 返り値
                auto result = expression();

                if (const auto *error = std::get_if<ParseError>(&result))
                {
                    return *error;
                }

                return Statement{ReturnStatement{std::get<Expression>(std::move(result))}};
            }

            StatementResult function_definition()
            {
                // fn を消費
                ++m_position;

                // 関数名
                const Token &name_token = m_tokens[m_position];
                const auto *id = std::get_if<Ident>(&name_token.value);

                if (!id)
                {
                    return ParseError{name_token.span, "expected function name after 'fn'"};
                }

                const auto name = id->text;
                const auto name_span = name_token.span;

                ++m_position;

                // (
                if (!std::holds_alternative<LParen>(m_tokens[m_position].value))
                {
                    return ParseError{m_tokens[m_position].span, "expected '(' after function name"};
                }

                ++m_position;

                // 引数名
                std::vector<Parameter> params;

                if (!std::holds_alternative<RParen>(m_tokens[m_position].value))
                {
                    while (true)
                    {
                        const Token &param_token = m_tokens[m_position];

                        const auto *param = std::get_if<Ident>(&param_token.value);

                        if (!param)
                        {
                            return ParseError{param_token.span, "expected parameter name"};
                        }

                        const auto param_name = param->text;

                        ++m_position;

                        // 型宣言
                        if (!std::holds_alternative<Colon>(m_tokens[m_position].value))
                        {
                            return ParseError{m_tokens[m_position].span,
                                              "expected ':' after parameter name"};
                        }

                        ++m_position;

                        auto type_result = parse_type_decl();

                        if (const auto *error = std::get_if<ParseError>(&type_result))
                        {
                            return *error;
                        }

                        params.push_back(Parameter{param_name,
                                                   std::get<TypeDecl>(type_result)});

                        // )
                        if (std::holds_alternative<RParen>(m_tokens[m_position].value))
                        {
                            break;
                        }

                        // ,
                        if (!std::holds_alternative<Comma>(m_tokens[m_position].value))
                        {
                            return ParseError{m_tokens[m_position].span, "expected ',' or ')' after parameter"};
                        }

                        ++m_position;
                    }
                }

                // ) を消費
                ++m_position;

                // ->
                if (!std::holds_alternative<Arrow>(m_tokens[m_position].value))
                {
                    return ParseError{m_tokens[m_position].span,
                                      "expected '->' after parameters"};
                }

                ++m_position;

                // 戻り値の型宣言
                auto return_type_result = parse_type_decl();

                if (const auto *error = std::get_if<ParseError>(&return_type_result))
                {
                    return *error;
                }

                const TypeDecl return_type = std::get<TypeDecl>(return_type_result);

                // {
                if (!std::holds_alternative<LBrace>(m_tokens[m_position].value))
                {
                    return ParseError{m_tokens[m_position].span, "expected '{' after parameters"};
                }

                ++m_position;

                // body
                auto body_result = parse_statement_sequence(true);

                if (const auto *error = std::get_if<ParseError>(&body_result))
                {
                    return *error;
                }

                auto body = std::get<Statements>(std::move(body_result));

                // }
                ++m_position;

                return Statement{std::make_unique<FunctionDefinition>(
                    FunctionDefinition{name,
                                       name_span,
                                       std::move(params),
                                       return_type,
                                       std::move(body)})};
            }

            StatementResult for_statement()
            {
                // for を消費
                ++m_position;

                // loop 変数
                const Token &name_token = m_tokens[m_position];

                const auto *id = std::get_if<Ident>(&name_token.value);

                if (!id)
                {
                    return ParseError{name_token.span, "expected loop variable after 'for'"};
                }

                const auto name = id->text;
                const auto name_span = name_token.span;

                ++m_position;

                // in
                if (!std::holds_alternative<In>(m_tokens[m_position].value))
                {
                    return ParseError{m_tokens[m_position].span, "expected 'in' after loop variable"};
                }

                ++m_position;

                // 最初の expression
                auto start_result = expression();

                if (const auto *error = std::get_if<ParseError>(&start_result))
                {
                    return *error;
                }

                auto start = std::get<Expression>(std::move(start_result));

                // to
                if (!std::holds_alternative<To>(m_tokens[m_position].value))
                {
                    return ParseError{m_tokens[m_position].span, "expected 'to' after start expression"};
                }

                ++m_position;

                // 最後の expression
                auto end_result = expression();

                if (const auto *error = std::get_if<ParseError>(&end_result))
                {
                    return *error;
                }

                auto end = std::get<Expression>(std::move(end_result));

                // {
                if (!std::holds_alternative<LBrace>(m_tokens[m_position].value))
                {
                    return ParseError{m_tokens[m_position].span, "expected '{' after for range"};
                }

                ++m_position;

                // } まで statements を解釈
                auto body_result = parse_statement_sequence(true);

                if (const auto *error = std::get_if<ParseError>(&body_result))
                {
                    return *error;
                }

                auto body = std::get<Statements>(std::move(body_result));

                // }
                ++m_position;

                return Statement{std::make_unique<ForStatement>(ForStatement{name,
                                                                             name_span,
                                                                             std::move(start),
                                                                             std::move(end),
                                                                             std::move(body)})};
            }

            ExprResult function_call(std::string_view name, std::size_t begin)
            {
                // lparen を消費
                ++m_position;

                std::vector<Expression> args;

                while (!std::holds_alternative<RParen>(m_tokens[m_position].value))
                {
                    // rparen がない場合
                    if (std::holds_alternative<End>(m_tokens[m_position].value))
                    {
                        return ParseError{m_tokens[m_position].span, "expected ')' after function arguments"};
                    }

                    auto result = expression();

                    if (const auto *error = std::get_if<ParseError>(&result))
                    {
                        return *error;
                    }

                    args.push_back(std::get<Expression>(std::move(result)));

                    // カンマは省略可能
                    if (std::holds_alternative<Comma>(m_tokens[m_position].value))
                    {
                        ++m_position;
                    }
                }

                const auto end = m_tokens[m_position].span.end;

                // rparen を消費
                ++m_position;

                return Expression{ExprFnInvoke{name, std::move(args)}, {begin, end}};
            }

            ExprResult primary()
            {
                const Token &token = m_tokens[m_position];

                // identifier か function call
                if (const auto *id = std::get_if<Ident>(&token.value))
                {
                    const auto name = id->text;

                    ++m_position;

                    // lparen があれば function call
                    if (std::holds_alternative<LParen>(m_tokens[m_position].value))
                    {
                        return function_call(name, token.span.begin);
                    }

                    return Expression{ExprIdent{name}, token.span};
                }

                // number
                if (const auto *number = std::get_if<Number>(&token.value))
                {
                    ++m_position;

                    return Expression{ExprNumLiteral{number->value}, token.span};
                }

                // string literal
                if (const auto *str = std::get_if<StringLiteral>(&token.value))
                {
                    ++m_position;

                    return Expression{ExprStrLiteral{str->value}, token.span};
                }

                // 符号付き numeric literal
                const bool positive = std::holds_alternative<Plus>(token.value);
                const bool negative = std::holds_alternative<Minus>(token.value);

                if (positive || negative)
                {
                    const auto begin = token.span.begin;

                    ++m_position;

                    const Token &next = m_tokens[m_position];
                    const auto *number = std::get_if<Number>(&next.value);

                    if (!number)
                    {
                        return ParseError{next.span, "expected a number after sign"};
                    }

                    const double value = negative ? -number->value : number->value;

                    const auto end = next.span.end;
                    ++m_position;

                    return Expression{ExprNumLiteral{value}, {begin, end}};
                }

                // parenthesized expression
                if (std::holds_alternative<LParen>(token.value))
                {
                    const auto begin = token.span.begin;
                    ++m_position;

                    auto result = expression();

                    if (const auto *error = std::get_if<ParseError>(&result))
                    {
                        return *error;
                    }

                    if (!std::holds_alternative<RParen>(m_tokens[m_position].value))
                    {
                        return ParseError{m_tokens[m_position].span, "expected ')'"};
                    }

                    auto inside = std::get<Expression>(std::move(result));

                    inside.span = {begin, m_tokens[m_position].span.end};

                    ++m_position;

                    return inside;
                }

                return ParseError{token.span, "expected an expression"};
            }

            ExprResult compare()
            {
                auto left_result = add();

                if (const auto *error = std::get_if<ParseError>(&left_result))
                {
                    return *error;
                }

                auto left = std::get<Expression>(std::move(left_result));

                while (std::holds_alternative<Greater>(m_tokens[m_position].value) ||
                       std::holds_alternative<Less>(m_tokens[m_position].value))
                {

                    const bool greater = std::holds_alternative<Greater>(m_tokens[m_position].value);

                    ++m_position;

                    auto right_result = add();

                    if (const auto *error = std::get_if<ParseError>(&right_result))
                    {
                        return *error;
                    }

                    auto right = std::get<Expression>(std::move(right_result));

                    const SourceSpan span{left.span.begin, right.span.end};

                    if (greater)
                    {
                        left = Expression{
                            ExprGt{
                                std::make_unique<Expression>(std::move(left)),
                                std::make_unique<Expression>(std::move(right))},
                            span};
                    }
                    else
                    {
                        left = Expression{
                            ExprLt{
                                std::make_unique<Expression>(std::move(left)),
                                std::make_unique<Expression>(std::move(right))},
                            span};
                    }
                }

                return left;
            }

            ExprResult add()
            {
                auto left_result = product();

                if (const auto *error = std::get_if<ParseError>(&left_result))
                {
                    return *error;
                }

                auto left = std::get<Expression>(std::move(left_result));

                while (std::holds_alternative<Plus>(m_tokens[m_position].value) ||
                       std::holds_alternative<Minus>(m_tokens[m_position].value))
                {

                    const bool add = std::holds_alternative<Plus>(m_tokens[m_position].value);

                    ++m_position;

                    auto right_result = product();

                    if (const auto *error = std::get_if<ParseError>(&right_result))
                    {
                        return *error;
                    }

                    auto right = std::get<Expression>(std::move(right_result));

                    const SourceSpan span{left.span.begin, right.span.end};

                    if (add)
                    {
                        left = Expression{
                            ExprAdd{
                                std::make_unique<Expression>(std::move(left)),
                                std::make_unique<Expression>(std::move(right))},
                            span};
                    }
                    else
                    {
                        left = Expression{
                            ExprSub{
                                std::make_unique<Expression>(std::move(left)),
                                std::make_unique<Expression>(std::move(right))},
                            span};
                    }
                }

                return left;
            }

            ExprResult product()
            {
                auto left_result = primary();

                if (std::holds_alternative<ParseError>(left_result))
                {
                    return left_result;
                }

                auto left = std::get<Expression>(std::move(left_result));

                while (std::holds_alternative<Star>(m_tokens[m_position].value) ||
                       std::holds_alternative<Slash>(m_tokens[m_position].value))
                {

                    const bool multiply = std::holds_alternative<Star>(m_tokens[m_position].value);

                    ++m_position;

                    auto right_result = primary();

                    if (std::holds_alternative<ParseError>(right_result))
                    {
                        return right_result;
                    }

                    auto right = std::get<Expression>(std::move(right_result));

                    const SourceSpan span{
                        left.span.begin,
                        right.span.end};

                    if (multiply)
                    {
                        left = Expression{
                            ExprMul{std::make_unique<Expression>(std::move(left)),
                                    std::make_unique<Expression>(std::move(right))},
                            span};
                    }
                    else
                    {
                        left = Expression{
                            ExprDiv{std::make_unique<Expression>(std::move(left)),
                                    std::make_unique<Expression>(std::move(right))},
                            span};
                    }
                }

                return left;
            }

            std::span<const Token> m_tokens;
            std::size_t m_position = 0;
        };

    }

    ExprResult expr(std::span<const Token> tokens)
    {
        return Parser{tokens}.parse();
    }

    StatementsResult statements(std::span<const Token> tokens)
    {
        return Parser{tokens}.parse_program();
    }
}
