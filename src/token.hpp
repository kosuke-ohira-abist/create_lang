#pragma once

#include <cstddef>
#include <string_view>
#include <variant>
#include <string>

namespace lang
{

    // ソース文字列中のバイト範囲
    struct SourceSpan
    {
        std::size_t begin;
        std::size_t end;

        bool operator==(const SourceSpan &) const = default;
    };

    struct Ident
    {
        std::string_view text;
    };

    struct Number
    {
        double value;
    };

    struct StringLiteral
    {
        std::string value;
    };

    struct Var
    {
    };

    struct Fn
    {
    };

    struct Return
    {
    };

    struct Break
    {
    };
    struct Continue
    {
    };

    struct If
    {
    };
    struct Else
    {
    };
    struct For
    {
    };
    struct In
    {
    };
    struct To
    {
    };
    struct Less
    {
    };
    struct Greater
    {
    };
    struct Plus
    {
    };
    struct Minus
    {
    };
    struct Star
    {
    };
    struct Slash
    {
    };
    struct Equal
    {
    };
    struct Colon
    {
    };
    struct Arrow
    {
    };
    struct Comma
    {
    };
    struct Semicolon
    {
    };
    struct LParen
    {
    };
    struct RParen
    {
    };
    struct LBrace
    {
    };
    struct RBrace
    {
    };
    struct End
    {
    };

    using TokenValue = std::variant<Ident,
                                    Number,
                                    StringLiteral,
                                    Var,
                                    Fn,
                                    Return,
                                    Break,
                                    Continue,
                                    If,
                                    Else,
                                    For,
                                    In,
                                    To,
                                    Less,
                                    Greater,
                                    Plus,
                                    Minus,
                                    Star,
                                    Slash,
                                    Equal,
                                    Colon,
                                    Arrow,
                                    Comma,
                                    Semicolon,
                                    LParen,
                                    RParen,
                                    LBrace,
                                    RBrace,
                                    End>;

    // 字句解析で生成する token
    struct Token
    {
        TokenValue value;
        SourceSpan span;
    };

}