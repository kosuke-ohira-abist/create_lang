#pragma once

#include "ast.hpp"
#include "token.hpp"

#include <span>
#include <variant>

namespace lang
{
    // 構文解析で発生したエラーと、そのソース位置
    struct ParseError
    {
        SourceSpan span;
        std::string message;
    };

    // 式の解析結果
    using ExprResult = std::variant<Expression, ParseError>;

    // token 列全体を１つの式として解析する
    ExprResult expr(std::span<const Token> tokens);

    // １つの文の解析結果
    using StatementResult = std::variant<Statement, ParseError>;

    // 文の列の解析結果
    using StatementsResult = std::variant<Statements, ParseError>;

    // token 列全体をプログラムの文の列として解析する
    StatementsResult statements(std::span<const Token> tokens);
}