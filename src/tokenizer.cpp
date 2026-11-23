#include "tokenizer.hpp"

#include <charconv>
#include <system_error>
#include <cctype>

namespace lang
{
    LexResult tokenize(std::string_view input)
    {
        std::vector<Token> tokens;
        std::size_t pos = 0;

        while (pos < input.size())
        {
            const char c = input[pos];

            // 空白を進める
            if (std::isspace(c))
            {
                ++pos;
                continue;
            }

            const auto begin = pos;

            // string literal
            if (c == '"')
            {
                // " を消費
                ++pos;

                std::string value;

                while (pos < input.size() && input[pos] != '"')
                {
                    // エスケープ
                    if (input[pos] == '\\' && pos + 1 < input.size())
                    {
                        const char next = input[pos + 1];

                        if (next == 'n')
                        {
                            value.push_back('\n');
                            pos += 2;
                            continue;
                        }

                        if (next == 't')
                        {
                            value.push_back('\t');
                            pos += 2;
                            continue;
                        }

                        if (next == '"')
                        {
                            value.push_back('\"');
                            pos += 2;
                            continue;
                        }

                        if (next == '\\')
                        {
                            value.push_back('\\');
                            pos += 2;
                            continue;
                        }
                    }

                    value.push_back(input[pos]);

                    ++pos;
                }

                if (pos == input.size())
                {
                    return LexError{{begin, pos},
                                    "unterminated string literal"};
                }

                // " を消費
                ++pos;

                tokens.push_back({StringLiteral{std::move(value)}, {begin, pos}});

                continue;
            }

            // Identifier か Keyword
            if (std::isalpha(c) || c == '_')
            {
                ++pos;

                while (pos < input.size() && (std::isalpha(input[pos]) || std::isdigit(input[pos]) || input[pos] == '_'))
                {
                    ++pos;
                }

                const auto word = input.substr(begin, pos - begin);

                if (word == "var")
                {
                    tokens.push_back({Var{}, {begin, pos}});
                }
                else if (word == "fn")
                {
                    tokens.push_back({Fn{}, {begin, pos}});
                }
                else if (word == "return")
                {
                    tokens.push_back({Return{}, {begin, pos}});
                }
                else if (word == "break")
                {
                    tokens.push_back({Break{}, {begin, pos}});
                }
                else if (word == "continue")
                {
                    tokens.push_back({Continue{}, {begin, pos}});
                }
                else if (word == "if")
                {
                    tokens.push_back({If{}, {begin, pos}});
                }
                else if (word == "else")
                {
                    tokens.push_back({Else{}, {begin, pos}});
                }
                else if (word == "for")
                {
                    tokens.push_back({For{}, {begin, pos}});
                }
                else if (word == "in")
                {
                    tokens.push_back({In{}, {begin, pos}});
                }
                else if (word == "to")
                {
                    tokens.push_back({To{}, {begin, pos}});
                }
                else
                {
                    tokens.push_back({Ident{word}, {begin, pos}});
                }

                continue;
            }

            // Unsigned numeric literal
            if (std::isdigit(c) ||
                // 小数点は . で開始してもいい
                (c == '.' && pos + 1 < input.size() && std::isdigit(input[pos + 1])))
            {
                // 小数の仮数部分
                while (pos < input.size() && std::isdigit(input[pos]))
                {
                    ++pos;
                }
                if (pos < input.size() && input[pos] == '.')
                {
                    ++pos;

                    while (pos < input.size() && std::isdigit(input[pos]))
                    {
                        ++pos;
                    }
                }
                if (pos < input.size() && input[pos] == '.')
                {
                    return LexError{{pos, pos + 1}, "multiple decimal points in a number"};
                }

                // 小数の指数部分
                if (pos < input.size() && (input[pos] == 'e' || input[pos] == 'E'))
                {
                    const auto exponent_begin = pos;
                    ++pos;

                    if (pos < input.size() && (input[pos] == '+' || input[pos] == '-'))
                    {
                        ++pos;
                    }

                    const auto digits_begin = pos;
                    while (pos < input.size() && std::isdigit(input[pos]))
                    {
                        ++pos;
                    }

                    if (pos == digits_begin)
                    {
                        return LexError{{exponent_begin, pos}, "expected digits after exponent"};
                    }
                }

                const auto lexeme = input.substr(begin, pos - begin);

                double value;

                const auto [end, ec] = std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), value);

                if (ec != std::errc{} || end != lexeme.data() + lexeme.size())
                {
                    return LexError{{begin, pos}, "invalid or out-of-range number"};
                }

                tokens.push_back({Number{value}, {begin, pos}});

                continue;
            }

            // Operators and delimiters
            switch (c)
            {
            case '<':
                tokens.push_back({Less{}, {begin, begin + 1}});
                break;

            case '>':
                tokens.push_back({Greater{}, {begin, begin + 1}});
                break;

            case '+':
                tokens.push_back({Plus{}, {begin, begin + 1}});
                break;

            case '-':
                if (pos + 1 < input.size() && input[pos + 1] == '>')
                {
                    tokens.push_back({Arrow{}, {begin, begin + 2}});

                    ++pos;
                }
                else
                {
                    tokens.push_back({Minus{}, {begin, begin + 1}});
                }

                break;

            case '*':
                tokens.push_back({Star{}, {begin, begin + 1}});
                break;

            case '/':
                tokens.push_back({Slash{}, {begin, begin + 1}});
                break;

            case '=':
                tokens.push_back({Equal{}, {begin, begin + 1}});
                break;

            case ':':
                tokens.push_back({Colon{}, {begin, begin + 1}});
                break;

            case ',':
                tokens.push_back({Comma{}, {begin, begin + 1}});
                break;

            case ';':
                tokens.push_back({Semicolon{}, {begin, begin + 1}});
                break;

            case '(':
                tokens.push_back({LParen{}, {begin, begin + 1}});
                break;

            case ')':
                tokens.push_back({RParen{}, {begin, begin + 1}});
                break;

            case '{':
                tokens.push_back({LBrace{}, {begin, begin + 1}});
                break;

            case '}':
                tokens.push_back({RBrace{}, {begin, begin + 1}});
                break;

            default:
                return LexError{{begin, begin + 1}, "unrecognized character"};
            }

            ++pos;
        }

        tokens.push_back({End{}, {input.size(), input.size()}});

        return tokens;
    }
}