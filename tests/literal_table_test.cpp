
#include "byte_code.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <stdexcept>
#include <string>

using lang::Compiler;
using lang::OpCode;
using lang::Vm;

namespace
{
    // 旧形式の定数セクションを、引数なし関数の
    // バイナリ形式として読み込むためのテスト用ヘルパ。
    lang::CompiledFnDef read_literal_section(
        const std::string &section,
        bool complete = true)
    {
        // 引数数 = 0
        std::string bytes(4, '\0');

        // 定数数と定数データ
        bytes += section;

        if (complete)
        {
            // 命令数 = 0
            bytes.append(4, '\0');
        }

        std::istringstream reader(bytes, std::ios::binary);

        return lang::CompiledFnDef::deserialize(reader);
    }

    // 命令セクションを、定数と引数のない関数として読み込む
    lang::CompiledFnDef read_instruction_section(
        const std::string &section)
    {
        // 引数数 = 0、定数数 = 0
        std::string bytes(8, '\0');

        // 命令数と命令データ
        bytes += section;

        std::istringstream reader(bytes, std::ios::binary);

        return lang::CompiledFnDef::deserialize(reader);
    }
}

TEST(LiteralTableTest, AddLiteralReturnsTableIndex)
{
    Compiler compiler;

    EXPECT_EQ(compiler.add_literal(512.0), 0);
    EXPECT_EQ(compiler.add_literal(1024.0), 1);

    EXPECT_EQ(compiler.literals.size(), 2u);
    EXPECT_EQ(std::get<double>(compiler.literals[0]), 512);
    EXPECT_EQ(std::get<double>(compiler.literals[1]), 1024);
}

TEST(LiteralTableTest, DeduplicateLiterals)
{
    Compiler compiler;

    EXPECT_EQ(compiler.add_literal(512.0), 0);
    EXPECT_EQ(compiler.add_literal(512.0), 0);

    EXPECT_EQ(compiler.literals.size(), 1u);
}

TEST(LiteralTableTest, RejectsMoreThan256Literals)
{
    Compiler compiler;

    for (int i = 0; i < 256; ++i)
    {
        EXPECT_EQ(compiler.add_literal(i * 1.0), i);
    }

    EXPECT_THROW(compiler.add_literal(256.0), std::runtime_error);
    EXPECT_EQ(compiler.literals.size(), 256u);
}

TEST(LiteralTableTest, WritesExpectedBinaryLayout)
{
    Compiler compiler;

    const auto first = compiler.add_literal(512.0);
    const auto second = compiler.add_literal(1024.0);

    compiler.add_inst(OpCode::LoadLiteral, first);
    compiler.add_inst(OpCode::LoadLiteral, second);
    compiler.add_inst(OpCode::Add, 0);

    std::ostringstream writer;

    compiler.write_literals(writer);
    compiler.write_insts(writer);

    const std::string expected(
        "\x02\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x80\x40"
        "\x00\x00\x00\x00\x00\x00\x00\x90\x40"
        "\x03\x00\x00\x00"
        "\x00\x00\x00\x01\x04\x00",
        32);

    EXPECT_EQ(writer.str(), expected);
}

TEST(LiteralTableTest, ReadsNegativeLiteralFromLittleEndianBytes)
{
    const std::string bytes(
        "\x02\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\xF0\xBF"
        "\x00\x00\x00\x00\x00\x00\x00\x80\x40",
        22);

    const auto fn = read_literal_section(bytes);

    ASSERT_EQ(fn.literals.size(), 2u);
    EXPECT_EQ(std::get<double>(fn.literals[0]), -1);
    EXPECT_EQ(std::get<double>(fn.literals[1]), 512);
}

TEST(LiteralTableTest, ReadsInstructionsUsingDeclaredCount)
{
    const std::string bytes(
        "\x03\x00\x00\x00"
        "\x00\x00\x00\x01\x04\x00",
        10);

    const auto fn = read_instruction_section(bytes);

    ASSERT_EQ(fn.instructions.size(), 3u);
    EXPECT_EQ(fn.instructions[0].op, OpCode::LoadLiteral);
    EXPECT_EQ(fn.instructions[1].arg_0, 1);
    EXPECT_EQ(fn.instructions[2].op, OpCode::Add);
}

TEST(LiteralTableTest, RejectsTruncatedLiteralSection)
{
    const std::string bytes("\x01\x00\x00\x00\x00\x02", 6);

    EXPECT_THROW(read_literal_section(bytes), std::runtime_error);
}

TEST(LiteralTableTest, RejectsTruncatedInstructionSection)
{
    const std::string bytes("\x01\x00\x00\x00\x00", 5);

    EXPECT_THROW(read_instruction_section(bytes), std::runtime_error);
}

TEST(LiteralTableTest, RejectsMissingLiteralCount)
{
    const std::string bytes("\x02\x00", 2);

    EXPECT_THROW(read_literal_section(bytes), std::runtime_error);
}

TEST(LiteralTableTest, SerializesNumbersAndStrings)
{
    Compiler compiler;

    compiler.add_literal(512.0);
    compiler.add_literal(std::string{"pow"});

    std::ostringstream writer;
    compiler.write_literals(writer);

    const std::string expected(
        "\x02\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x80\x40"
        "\x02\x03\x00\x00\x00"
        "pow",
        21);

    EXPECT_EQ(writer.str(), expected);

    const auto fn = read_literal_section(writer.str());

    ASSERT_EQ(fn.literals.size(), 2u);

    EXPECT_DOUBLE_EQ(
        std::get<double>(fn.literals[0]),
        512.0);

    EXPECT_EQ(
        std::get<std::string>(fn.literals[1]),
        "pow");
}

TEST(LiteralTableTest, RejectsUnknownLiteralKind)
{
    const std::string bytes(
        "\x01\x00\x00\x00"
        "\xFF",
        5);

    EXPECT_THROW(read_literal_section(bytes), std::runtime_error);
}

TEST(LiteralTableTest, RejectsTruncatedStringLiteral)
{
    const std::string bytes(
        "\x01\x00\x00\x00"
        "\x01\x03\x00\x00\x00"
        "a",
        10);

    EXPECT_THROW(read_literal_section(bytes), std::runtime_error);
}

TEST(LiteralTableTest, SerializesI64)
{
    Compiler compiler;

    compiler.add_literal(std::int64_t{42});

    std::ostringstream writer;
    compiler.write_literals(writer);

    const std::string expected(
        "\x01\x00\x00\x00"
        "\x01"
        "\x2A\x00\x00\x00"
        "\x00\x00\x00\x00",
        13);

    EXPECT_EQ(writer.str(), expected);
}
