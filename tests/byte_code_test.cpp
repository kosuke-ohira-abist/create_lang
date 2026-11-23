
#include "byte_code.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

using lang::OpCode;
using lang::Vm;

TEST(ByteCodeTest, AddsTwoLiterals)
{
    Vm code;
    auto &main = code.funcs["main"];

    main.literals = {42.0, 36.0};
    main.instructions = {
        {OpCode::LoadLiteral, 0},
        {OpCode::LoadLiteral, 1},
        {OpCode::Add, 0},
    };

    const auto result = code.interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<double>(*result), 78);
}

TEST(ByteCodeTest, ReturnsSingleLiteral)
{
    Vm code;
    auto &main = code.funcs["main"];

    main.literals = {42.0};
    main.instructions = {{OpCode::LoadLiteral, 0}};

    const auto result = code.interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<double>(*result), 42);
}

TEST(ByteCodeTest, HandlesMultipleAdds)
{
    Vm code;
    auto &main = code.funcs["main"];

    main.literals = {10.0, 20.0, 5.0};
    main.instructions = {
        {OpCode::LoadLiteral, 0},
        {OpCode::LoadLiteral, 1},
        {OpCode::Add, 0},
        {OpCode::LoadLiteral, 2},
        {OpCode::Add, 0},
    };

    const auto result = code.interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<double>(*result), 35);
}

TEST(ByteCodeTest, SupportsLargeLiteralValue)
{
    Vm code;
    auto &main = code.funcs["main"];

    main.literals = {1024.0};
    main.instructions = {{OpCode::LoadLiteral, 0}};

    const auto result = code.interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<double>(*result), 1024);
}

TEST(ByteCodeTest, SupportsNegativeLiteralValue)
{
    Vm code;
    auto &main = code.funcs["main"];

    main.literals = {-512.0};
    main.instructions = {{OpCode::LoadLiteral, 0}};

    const auto result = code.interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<double>(*result), -512);
}

TEST(ByteCodeTest, ReturnsNulloptForEmptyProgram)
{
    Vm vm;

    vm.funcs.emplace("main", lang::CompiledFnDef{});

    EXPECT_FALSE(vm.interpret().has_value());
}

TEST(ByteCodeTest, ThrowsWhenStackIsEmpty)
{
    Vm code;
    auto &main = code.funcs["main"];

    main.instructions = {{OpCode::Add, 0}};

    EXPECT_THROW(code.interpret(), std::runtime_error);
}

TEST(ByteCodeTest, ThrowsWhenOnlyOneOperandExists)
{
    Vm code;
    auto &main = code.funcs["main"];

    main.literals = {42.0};
    main.instructions = {
        {OpCode::LoadLiteral, 0},
        {OpCode::Add, 0},
    };

    EXPECT_THROW(code.interpret(), std::runtime_error);
}

TEST(ByteCodeTest, ThrowsWhenLiteralIndexIsInvalid)
{
    Vm code;
    auto &main = code.funcs["main"];

    main.literals = {512.0};
    main.instructions = {{OpCode::LoadLiteral, 1}};

    EXPECT_THROW(code.interpret(), std::runtime_error);
}

TEST(ByteCodeTest, ReturnsTopValueWhenMultipleRemain)
{
    Vm code;
    auto &main = code.funcs["main"];

    main.literals = {10.0, 20.0};
    main.instructions = {
        {OpCode::LoadLiteral, 0},
        {OpCode::LoadLiteral, 1},
    };

    const auto result = code.interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<double>(*result), 20);
}

TEST(ByteCodeTest, CopiesValueFromRelativeDepth)
{
    Vm vm;
    auto &main = vm.funcs["main"];

    main.literals = {10.0, 20.0, 30.0};
    main.instructions = {
        {OpCode::LoadLiteral, 0},
        {OpCode::LoadLiteral, 1},
        {OpCode::LoadLiteral, 2},
        {OpCode::Copy, 2},
    };

    const auto result = vm.interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*result), 10.0);
}

TEST(ByteCodeTest, RejectsInvalidCopyDepth)
{
    Vm vm;
    auto &main = vm.funcs["main"];

    main.literals = {42.0};
    main.instructions = {
        {OpCode::LoadLiteral, 0},
        {OpCode::Copy, 1},
    };

    EXPECT_THROW(vm.interpret(), std::runtime_error);
}

TEST(ByteCodeTest, ExecutesCallInstruction)
{
    Vm vm;
    auto &main = vm.funcs["main"];

    main.literals = {
        std::string{"pow"},
        3.0,
        4.0,
    };

    main.instructions = {
        {OpCode::LoadLiteral, 0},
        {OpCode::LoadLiteral, 1},
        {OpCode::LoadLiteral, 2},
        {OpCode::Call, 2},
    };

    const auto result = vm.interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*result), 81.0);
}

TEST(ByteCodeTest, RejectsCallWithInsufficientStack)
{
    Vm vm;
    auto &main = vm.funcs["main"];

    main.instructions = {{OpCode::Call, 2}};

    EXPECT_THROW(vm.interpret(), std::runtime_error);
}

TEST(ByteCodeTest, RejectsNonStringFunctionName)
{
    Vm vm;
    auto &main = vm.funcs["main"];

    main.literals = {2.0};
    main.instructions = {
        {OpCode::LoadLiteral, 0},
        {OpCode::Call, 0},
    };

    EXPECT_THROW(vm.interpret(), std::runtime_error);
}

TEST(ByteCodeTest, RejectsStringInNumericArithmetic)
{
    Vm vm;
    auto &main = vm.funcs["main"];

    main.literals = {
        std::string{"hello"},
        2.0,
    };

    main.instructions = {
        {OpCode::LoadLiteral, 0},
        {OpCode::LoadLiteral, 1},
        {OpCode::Add, 0},
    };

    EXPECT_THROW(vm.interpret(), std::runtime_error);
}

TEST(ByteCodeTest, ReturnStopsExecution)
{
    Vm vm;
    auto &main = vm.funcs["main"];

    main.literals = {10.0, 20.0};

    main.instructions = {
        {OpCode::LoadLiteral, 0},
        {OpCode::Ret, 0},
        {OpCode::LoadLiteral, 1},
    };

    const auto result = vm.interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*result), 10.0);
}

TEST(ByteCodeTest, RejectsMissingMainFunction)
{
    Vm vm;

    // mainが存在しないVMは実行できない
    EXPECT_THROW(vm.interpret(), std::runtime_error);
}

TEST(ByteCodeTest, JumpToEndOfInstructions)
{
    lang::Vm vm;

    auto &main = vm.funcs["main"];

    main.instructions = {
        {lang::OpCode::Jmp, 2},
        {lang::OpCode::Ret, 0},
    };

    // 命令数2なので、ジャンプ先2は末尾を意味する
    const auto result = vm.interpret();

    EXPECT_FALSE(result.has_value());
}

TEST(ByteCodeTest, RejectsJumpBeyondInstructions)
{
    lang::Vm vm;

    auto &main = vm.funcs["main"];

    main.instructions = {
        {lang::OpCode::Jmp, 2},
    };

    // 命令数1に対してジャンプ先2は範囲外
    EXPECT_THROW(
        vm.interpret(),
        std::runtime_error);
}
