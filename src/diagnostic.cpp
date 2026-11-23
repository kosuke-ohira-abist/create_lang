#include "diagnostic.hpp"

#include <algorithm>
#include <sstream>

namespace lang
{
    namespace
    {
        // utf-8 かどうか
        bool is_utf8_continuation(char ch)
        {
            const auto byte = static_cast<unsigned char>(ch);

            // 複数バイトの場合、２バイト目以降の先頭２ビットが10になる
            return (byte & 0xC0u) == 0x80u;
        }
    }

    SourceLocation locate_source(std::string_view source, std::size_t byte_offset)
    {
        const auto offset = std::min(byte_offset, source.size());

        // 行数
        std::size_t line = 1;

        std::size_t line_start = 0;

        for (std::size_t i = 0; i < offset; ++i)
        {
            if (source[i] == '\n')
            {
                ++line;
                line_start = i + 1;
            }
        }

        auto line_end = source.find('\n', offset);

        if (line_end == std::string_view::npos)
        {
            line_end = source.size();
        }

        // windows では改行文字が \r\n のため調整
        if (line_start < line_end && source[line_end - 1] == '\r')
        {
            --line_end;
        }

        // 列数
        std::size_t column = 1;

        for (std::size_t i = line_start; i < offset; ++i)
        {
            // UTF-8 は 1 文字が複数バイトになる可能性があるため、
            // バイト数をそのまま列番号にするとずれてしまうための判定
            if (!is_utf8_continuation(source[i]))
            {
                ++column;
            }
        }

        return {line, column, line_start, line_end};
    }

    // 読みやすい診断メッセージを構築する
    std::string format_diagnostic(std::string_view source,
                                  std::string_view category,
                                  SourceSpan span,
                                  std::string_view message,
                                  std::string_view filename)
    {
        const auto offset = std::min(span.begin, source.size());

        const auto location = locate_source(source, offset);

        const auto line_text = source.substr(location.line_start,
                                             location.line_end - location.line_start);

        const auto line_number = std::to_string(location.line);

        std::ostringstream oss;

        oss << category << ": " << filename << ':'
            << location.line << ':' << location.column
            << ": " << message << '\n';

        oss << line_number << " | "
            << line_text << '\n';

        oss << std::string(line_number.size(), ' ')
            << " | "
            << std::string(location.column - 1, ' ')
            << "^\n";

        return oss.str();
    }
}