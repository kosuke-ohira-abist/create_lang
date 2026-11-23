
#include "byte_code.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

using lang::Compiler;
using lang::Instruction;
using lang::OpCode;
using lang::read_program;
using lang::write_program;

TEST(SerializationTest, SerializesExpectedBytes)
{
    const std::array<Instruction, 3> instructions{
        Instruction{OpCode::LoadLiteral, 42},
        Instruction{OpCode::LoadLiteral, 36},
        Instruction{OpCode::Add, 0},
    };

    std::ostringstream writer;

    for (const auto &instruction : instructions)
    {
        instruction.serialize(writer);
    }

    const std::string expected("\x00\x2A\x00\x24\x04\x00", 6);

    EXPECT_EQ(writer.str(), expected);
}

TEST(SerializationTest, DeserializesExpectedBytes)
{
    const std::string bytes("\x00\x2A\x00\x24\x04\x00", 6);
    std::istringstream reader(bytes);

    const auto first = Instruction::deserialize(reader);
    const auto second = Instruction::deserialize(reader);
    const auto third = Instruction::deserialize(reader);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(third.has_value());

    EXPECT_EQ(first->op, OpCode::LoadLiteral);
    EXPECT_EQ(first->arg_0, 42);

    EXPECT_EQ(second->op, OpCode::LoadLiteral);
    EXPECT_EQ(second->arg_0, 36);

    EXPECT_EQ(third->op, OpCode::Add);
    EXPECT_EQ(third->arg_0, 0);
}

TEST(SerializationTest, ReturnsNulloptAtEOF)
{
    std::istringstream reader;

    EXPECT_FALSE(Instruction::deserialize(reader).has_value());
}

TEST(SerializationTest, RejectsTruncatedInstruction)
{
    const std::string bytes("\x00", 1);
    std::istringstream reader(bytes);

    EXPECT_THROW(
        Instruction::deserialize(reader),
        std::runtime_error);
}

TEST(SerializationTest, RejectsUnknownOpcode)
{
    const std::string bytes("\xFF\x00", 2);
    std::istringstream reader(bytes);

    EXPECT_THROW(
        Instruction::deserialize(reader),
        std::runtime_error);
}

TEST(SerializationTest, RoundTripsMaxByteValue)
{
    const Instruction original{OpCode::LoadLiteral, 255};

    std::ostringstream writer;
    original.serialize(writer);

    std::istringstream reader(writer.str());
    const auto restored = Instruction::deserialize(reader);

    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->op, original.op);
    EXPECT_EQ(restored->arg_0, original.arg_0);
}

TEST(SerializationTest, FileRoundTrip)
{
    Compiler compiler;

    const auto first = compiler.add_literal(512.0);
    compiler.add_inst(OpCode::LoadLiteral, first);

    const auto second = compiler.add_literal(1024.0);
    compiler.add_inst(OpCode::LoadLiteral, second);

    compiler.add_inst(OpCode::Add, 0);

    // 完成した命令列をmain関数として登録する
    compiler.funcs.emplace(
        "main",
        lang::CompiledFnDef{
            {},
            std::move(compiler.literals),
            std::move(compiler.instructions),
        });

    const auto path = std::filesystem::temp_directory_path() /
                      ("lang-bytecode-" +
                       std::to_string(
                           std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count()) +
                       ".bin");

    write_program(path, compiler);

    const auto restored = read_program(path);
    std::filesystem::remove(path);

    // main関数が復元されているか
    ASSERT_TRUE(restored.funcs.contains("main"));

    const auto &original = compiler.funcs.at("main");
    const auto &actual = restored.funcs.at("main");

    EXPECT_EQ(actual.params, original.params);
    EXPECT_EQ(actual.literals, original.literals);

    ASSERT_EQ(actual.instructions.size(),
              original.instructions.size());

    for (std::size_t i = 0; i < original.instructions.size(); ++i)
    {
        EXPECT_EQ(actual.instructions[i].op,
                  original.instructions[i].op);

        EXPECT_EQ(actual.instructions[i].arg_0,
                  original.instructions[i].arg_0);
    }

    const auto result = restored.interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*result), 1536.0);
}

TEST(SerializationTest, ReportsMissingFile)
{
    const auto path = std::filesystem::temp_directory_path() /
                      ("lang-missing-" +
                       std::to_string(
                           std::chrono::steady_clock::now().time_since_epoch().count()) +
                       ".bin");

    EXPECT_THROW(read_program(path), std::runtime_error);
}

TEST(SerializationTest, RejectsTooManyFunctionParameters)
{
    lang::CompiledFnDef fn;

    // Call命令が表現できる最大数は255
    fn.params.resize(256, "x");

    std::ostringstream writer;

    EXPECT_THROW(
        fn.serialize(writer),
        std::runtime_error);

    // 検証失敗時に不完全なデータを書き出さない
    EXPECT_TRUE(writer.str().empty());
}

TEST(SerializationTest, RejectsTooManyFunctionLiterals)
{
    lang::CompiledFnDef fn;

    // 定数インデックス0〜255を超える257個
    fn.literals.resize(257, 1.0);

    std::ostringstream writer;

    EXPECT_THROW(
        fn.serialize(writer),
        std::runtime_error);

    EXPECT_TRUE(writer.str().empty());
}

TEST(SerializationTest, RejectsExcessiveLiteralCountOnRead)
{
    // 仮引数数 = 0
    // 定数数   = 257
    const std::string bytes(
        "\x00\x00\x00\x00"
        "\x01\x01\x00\x00",
        8);

    std::istringstream reader(bytes, std::ios::binary);

    try
    {
        (void)lang::CompiledFnDef::deserialize(reader);
        FAIL() << "Expected an exception";
    }
    catch (const std::runtime_error &error)
    {
        EXPECT_STREQ(
            error.what(),
            "Too many function literals");
    }
}

TEST(SerializationTest, Supports256FunctionLiterals)
{
    lang::CompiledFnDef fn;

    // インデックス255まで利用する
    fn.literals.resize(256, 42.0);

    fn.instructions = {
        {lang::OpCode::LoadLiteral, 255},
    };

    std::ostringstream writer(std::ios::binary);
    fn.serialize(writer);

    std::istringstream reader(
        writer.str(),
        std::ios::binary);

    auto restored =
        lang::CompiledFnDef::deserialize(reader);

    ASSERT_EQ(restored.literals.size(), 256u);
    ASSERT_EQ(restored.instructions.size(), 1u);

    lang::Vm vm;
    vm.funcs.emplace("main", std::move(restored));

    const auto result = vm.interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(
        std::get<double>(*result),
        42.0);
}
