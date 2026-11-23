
#include "diagnostic.hpp"

#include <gtest/gtest.h>

#include <string_view>

TEST(DiagnosticTest, ConvertsOffsetToLineAndColumn)
{
    constexpr std::string_view source =
        "var x: i64 = i64(10);\n"
        "x = \"hello\";\n";

    const auto position = source.find("\"hello\"");

    const auto location =
        lang::locate_source(source, position);

    EXPECT_EQ(location.line, 2u);
    EXPECT_EQ(location.column, 5u);
}

TEST(DiagnosticTest, HandlesEndOfFile)
{
    constexpr std::string_view source = "x\n";

    const auto location =
        lang::locate_source(source, source.size());

    EXPECT_EQ(location.line, 2u);
    EXPECT_EQ(location.column, 1u);
}

TEST(DiagnosticTest, HandlesCRLF)
{
    constexpr std::string_view source = "first\r\nsecond";

    const auto location =
        lang::locate_source(
            source,
            source.find("second"));

    EXPECT_EQ(location.line, 2u);
    EXPECT_EQ(location.column, 1u);
}

TEST(DiagnosticTest, CountsUtf8CodePoints)
{
    constexpr std::string_view source = "\"猫\" x";

    const auto location =
        lang::locate_source(
            source,
            source.find('x'));

    EXPECT_EQ(location.line, 1u);
    EXPECT_EQ(location.column, 5u);
}

TEST(DiagnosticTest, FormatsErrorMessage)
{
    constexpr std::string_view source =
        "var x: i64 = i64(10);\n"
        "x = \"hello\";\n";

    const auto position = source.find("\"hello\"");

    const auto result = lang::format_diagnostic(
        source,
        "Type error",
        {position, position + 7},
        "incompatible types");

    EXPECT_NE(
        result.find(
            "Type error: <stdin>:2:5: incompatible types"),
        std::string::npos);

    EXPECT_NE(
        result.find("2 | x = \"hello\";"),
        std::string::npos);

    EXPECT_NE(
        result.find("  |     ^"),
        std::string::npos);
}
