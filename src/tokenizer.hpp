#pragma once

#include "token.hpp"

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lang
{
    // 字句解析で発生したエラーとそのソース位置
    struct LexError
    {
        SourceSpan span;
        std::string message;
    };

    // 字句解析の結果
    using LexResult = std::variant<std::vector<Token>, LexError>;

    // ソースコードを token 列へ変換する
    LexResult tokenize(std::string_view input);
}