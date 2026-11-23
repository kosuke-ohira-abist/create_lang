#pragma once

#include "token.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace lang
{
    struct SourceLocation
    {
        // ソースコード中の行と列
        std::size_t line;
        std::size_t column;

        // 該当ソースの行の先頭バイト・終了バイト
        std::size_t line_start;
        std::size_t line_end;

        bool operator==(const SourceLocation &) const = default;
    };

    // バイト位置を行・列番号に変換する
    SourceLocation locate_source(std::string_view source, std::size_t byte_offset);

    // 読みやすい診断メッセージを構築する
    std::string format_diagnostic(std::string_view source,
                                  std::string_view category,
                                  SourceSpan span,
                                  std::string_view message,
                                  std::string_view filename = "<stdin>");
}