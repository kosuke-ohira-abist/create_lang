
#include "ast.hpp"
#include "byte_code.hpp"
#include "expr_parser.hpp"
#include "tokenizer.hpp"

#include <gtest/gtest.h>

#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace
{
    lang::Compiler compile_program(std::string_view source)
    {
        auto lexed = lang::tokenize(source);

        if (const auto *error =
                std::get_if<lang::LexError>(&lexed))
        {
            throw std::runtime_error(error->message);
        }

        auto parsed = lang::statements(
            std::get<std::vector<lang::Token>>(lexed));

        if (const auto *error =
                std::get_if<lang::ParseError>(&parsed))
        {
            throw std::runtime_error(error->message);
        }

        lang::Compiler compiler;

        compiler.compile_program(
            std::get<lang::Statements>(parsed));

        return compiler;
    }

    lang::Compiler compile_source(std::string_view source)
    {
        auto lexed = lang::tokenize(source);

        if (const auto *error = std::get_if<lang::LexError>(&lexed))
        {
            throw std::runtime_error(error->message);
        }

        auto parsed = lang::expr(
            std::get<std::vector<lang::Token>>(lexed));

        if (const auto *error = std::get_if<lang::ParseError>(&parsed))
        {
            throw std::runtime_error(error->message);
        }

        lang::Compiler compiler;
        compiler.compile_expr(std::get<lang::Expression>(parsed));

        return compiler;
    }

    lang::Vm round_trip(const lang::Compiler &compiler)
    {
        std::stringstream buffer(
            std::ios::in | std::ios::out | std::ios::binary);

        if (compiler.funcs.empty())
        {
            // 式単体のテストでは、完成した命令列を
            // テスト用のmain関数として登録する。
            lang::Compiler program;

            program.funcs.emplace(
                "main",
                lang::CompiledFnDef{
                    {},
                    compiler.literals,
                    compiler.instructions,
                });

            program.write_funcs(buffer);
        }
        else
        {
            // 通常のプログラムはすでに関数テーブルを持つ
            compiler.write_funcs(buffer);
        }

        buffer.seekg(0);

        lang::Vm vm;
        vm.read_funcs(buffer);

        return vm;
    }

    double run_program(std::string_view source)
    {
        const auto compiler = compile_program(source);

        // 前回作成したシリアライズ・復元ヘルパ
        const auto vm = round_trip(compiler);

        const auto result = vm.interpret();

        if (!result)
        {
            throw std::runtime_error(
                "Program returned no value");
        }

        return std::get<double>(*result);
    }
}

TEST(CompilerTest, GeneratesCodeForAddition)
{
    const auto compiler = compile_source("10 + 20");

    ASSERT_EQ(compiler.literals.size(), 2u);
    ASSERT_EQ(compiler.instructions.size(), 5u);

    EXPECT_DOUBLE_EQ(std::get<double>(compiler.literals[0]), 10.0);
    EXPECT_DOUBLE_EQ(std::get<double>(compiler.literals[1]), 20.0);

    EXPECT_EQ(compiler.instructions[0].op, lang::OpCode::LoadLiteral);
    EXPECT_EQ(compiler.instructions[0].arg_0, 0);

    EXPECT_EQ(compiler.instructions[1].op, lang::OpCode::LoadLiteral);
    EXPECT_EQ(compiler.instructions[1].arg_0, 1);

    EXPECT_EQ(compiler.instructions[2].op, lang::OpCode::Copy);
    EXPECT_EQ(compiler.instructions[2].arg_0, 1);

    EXPECT_EQ(compiler.instructions[3].op, lang::OpCode::Copy);
    EXPECT_EQ(compiler.instructions[3].arg_0, 1);

    EXPECT_EQ(compiler.instructions[4].op, lang::OpCode::Add);
    EXPECT_EQ(compiler.target_stack.size(), 3u);
}

TEST(CompilerTest, GeneratesCorrectCopyDepthForNestedAddition)
{
    const auto compiler = compile_source("1 + (2 + 3)");

    ASSERT_EQ(compiler.instructions.size(), 9u);

    EXPECT_EQ(compiler.instructions[6].op, lang::OpCode::Copy);
    EXPECT_EQ(compiler.instructions[6].arg_0, 3);

    EXPECT_EQ(compiler.instructions[7].op, lang::OpCode::Copy);
    EXPECT_EQ(compiler.instructions[7].arg_0, 1);

    EXPECT_EQ(compiler.target_stack.size(), 5u);

    const auto result = round_trip(compiler).interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*result), 6.0);
}

TEST(CompilerTest, CompilesFloatingPointLiterals)
{
    const auto result = round_trip(
                            compile_source("-1.25e2 + .5"))
                            .interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*result), -124.5);
}

TEST(CompilerTest, CompilesPIConsistentlyWithOldEvaluator)
{
    const auto result = round_trip(
                            compile_source("PI + 1"))
                            .interpret();

    ASSERT_TRUE(result.has_value());

    EXPECT_DOUBLE_EQ(
        std::get<double>(*result),
        std::numbers::pi_v<double> + 1.0);
}

TEST(CompilerTest, ReportsUnknownIdentifierWithSourceSpan)
{
    try
    {
        (void)compile_source("unknown + 1");
        FAIL();
    }
    catch (const lang::CompileError &error)
    {
        EXPECT_EQ(error.span.begin, 0u);
        EXPECT_EQ(error.span.end, 7u);
    }
}

TEST(CompilerTest, RejectsUnencodableCopyDistance)
{
    lang::Compiler compiler;
    compiler.target_stack.resize(257);

    EXPECT_THROW(
        compiler.add_copy_inst(lang::StackIndex{0}),
        std::runtime_error);

    EXPECT_TRUE(compiler.instructions.empty());
}

TEST(CompilerTest, DisplaysDisassembly)
{
    const auto compiler = compile_source("10 + 20");

    std::ostringstream output;
    compiler.disasm(output);

    EXPECT_NE(
        output.str().find("Literals [2]"),
        std::string::npos);

    EXPECT_NE(
        output.str().find("Instructions [5]"),
        std::string::npos);

    EXPECT_NE(
        output.str().find("Copy 1"),
        std::string::npos);
}

TEST(CompilerTest, SerializesDoubleLiteralAsLittleEndian)
{
    const auto compiler = compile_source("512.0");

    std::ostringstream buffer;
    compiler.write_literals(buffer);

    const std::string expected(
        "\x01\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x80\x40",
        13);

    EXPECT_EQ(buffer.str(), expected);
}

TEST(CompilerTest, CompilesArithmeticWithPrecedence)
{
    const auto compiler = compile_source("10 - 3 * 2");

    const auto result = round_trip(compiler).interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*result), 4.0);
}

TEST(CompilerTest, GeneratesCorrectStackPositionsAfterCall)
{
    const auto compiler = compile_source("2 + pow(3, 4)");

    ASSERT_EQ(compiler.instructions.size(), 12u);
    ASSERT_EQ(compiler.literals.size(), 4u);

    EXPECT_EQ(
        std::get<std::string>(compiler.literals[1]),
        "pow");

    EXPECT_EQ(compiler.instructions[6].op, lang::OpCode::Call);
    EXPECT_EQ(compiler.instructions[6].arg_0, 2);

    EXPECT_EQ(compiler.instructions[7].op, lang::OpCode::Store);
    EXPECT_EQ(compiler.instructions[7].arg_0, 2);

    EXPECT_EQ(compiler.instructions[8].op, lang::OpCode::Pop);
    EXPECT_EQ(compiler.instructions[8].arg_0, 1);

    EXPECT_EQ(compiler.instructions[11].op, lang::OpCode::Add);

    EXPECT_EQ(compiler.target_stack.size(), 3u);

    const auto result = round_trip(compiler).interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*result), 83.0);
}

TEST(CompilerTest, CompilesNestedFunctionCallsExpression)
{
    const auto compiler =
        compile_source("pow(2, pow(3, 2))");

    const auto result = round_trip(compiler).interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*result), 512.0);
}

TEST(CompilerTest, CompilesMixedArithmeticAndCalls)
{
    const auto compiler =
        compile_source("2 + pow(3, 4) * sqrt(16)");

    const auto result = round_trip(compiler).interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*result), 326.0);
}

TEST(CompilerTest, PreservesValuesAcrossMultipleCalls)
{
    const auto compiler =
        compile_source("pow(2, 3) + pow(4, 2)");

    const auto result = round_trip(compiler).interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*result), 24.0);
}

TEST(CompilerTest, SupportsBuiltinFunctions)
{
    const std::array<std::pair<std::string_view, double>, 12> cases{{
        {"sqrt(16)", 4.0},
        {"sin(0)", 0.0},
        {"cos(0)", 1.0},
        {"tan(0)", 0.0},
        {"asin(0)", 0.0},
        {"acos(1)", 0.0},
        {"atan(1)", std::numbers::pi_v<double> / 4.0},
        {"atan2(0, -1)", std::numbers::pi_v<double>},
        {"pow(3, 4)", 81.0},
        {"exp(0)", 1.0},
        {"log(8, 2)", 3.0},
        {"log10(100)", 2.0},
    }};

    for (const auto &[source, expected] : cases)
    {
        const auto result =
            round_trip(compile_source(source)).interpret();

        ASSERT_TRUE(result.has_value()) << source;

        EXPECT_NEAR(
            std::get<double>(*result),
            expected,
            1e-12)
            << source;
    }
}

TEST(CompilerTest, RejectsWrongFunctionArity)
{
    EXPECT_THROW(
        round_trip(compile_source("sqrt()")).interpret(),
        std::runtime_error);

    EXPECT_THROW(
        round_trip(compile_source("pow(2)")).interpret(),
        std::runtime_error);

    EXPECT_THROW(
        round_trip(compile_source("sqrt(1, 2)")).interpret(),
        std::runtime_error);
}

TEST(CompilerTest, RejectsUnknownBuiltinFunction)
{
    EXPECT_THROW(
        round_trip(compile_source("unknown(42)")).interpret(),
        std::runtime_error);
}

TEST(CompilerTest, CompilesVariableAssignment)
{
    const auto result = run_program(R"(
        var x: f64 = 10;
        x = x + 5;
        x;
    )");

    EXPECT_DOUBLE_EQ(result, 15.0);
}

TEST(CompilerTest, CopiesExistingLocal)
{
    const auto result = run_program(R"(
        var x: f64 = 10;
        var y: f64 = x;
        x = 20;
        y;
    )");

    EXPECT_DOUBLE_EQ(result, 10.0);
}

TEST(CompilerTest, ExecutesIfStatement)
{
    EXPECT_DOUBLE_EQ(run_program(R"(
        var x: f64 = 0;
        if 1 < 2 {
            x = 10;
        } else {
            x = 20;
        }
        x;
    )"),
                     10.0);

    EXPECT_DOUBLE_EQ(run_program(R"(
        var x: f64 = 0;
        if 2 < 1 {
            x = 10;
        } else {
            x = 20;
        }
        x;
    )"),
                     20.0);
}

TEST(CompilerTest, CompilesForLoop)
{
    const auto result = run_program(R"(
        var total: f64 = 0;

        for i in 0 to 5 {
            total = total + i;
        }

        total;
    )");

    EXPECT_DOUBLE_EQ(result, 10.0);
}

TEST(CompilerTest, SkipsLoopWhenRangeIsEmpty)
{
    const auto result = run_program(R"(
        var total: f64 = 0;

        for i in 0 to 0 {
            total = 100;
        }

        total;
    )");

    EXPECT_DOUBLE_EQ(result, 0.0);
}

TEST(CompilerTest, CompilesBreak)
{
    const auto result = run_program(R"(
        var total: f64 = 0;

        for i in 0 to 10 {
            if i > 3 {
                break;
            }

            total = total + i;
        }

        total;
    )");

    EXPECT_DOUBLE_EQ(result, 6.0);
}

TEST(CompilerTest, RejectsBreakOutsideLoop)
{
    EXPECT_THROW(
        compile_program("break;"),
        lang::CompileError);
}

TEST(CompilerTest, CompilesContinue)
{
    const auto result = run_program(R"(
        var total: f64 = 0;

        for i in 0 to 5 {
            if i < 2 {
                continue;
            }

            total = total + i;
        }

        total;
    )");

    EXPECT_DOUBLE_EQ(result, 9.0);
}

TEST(CompilerTest, PatchesContinueDup)
{
    const auto compiler = compile_program(R"(
        var total: f64 = 0;

        for i in 0 to 5 {
            if i < 2 {
                continue;
            }

            total = total + i;
        }

        total;
    )");

    const auto &instructions =
        compiler.funcs.at("main").instructions;

    const auto it = std::find_if(
        instructions.begin(),
        instructions.end(),
        [](const lang::Instruction &inst)
        {
            return inst.op == lang::OpCode::Dup;
        });

    ASSERT_NE(it, instructions.end());

    EXPECT_EQ(it->arg_0, 1);
}

TEST(CompilerTest, CompilesNestedLoops)
{
    const auto result = run_program(R"(
        var total: f64 = 0;

        for i in 0 to 3 {
            for j in 0 to 2 {
                total = total + 1;
            }
        }

        total;
    )");

    EXPECT_DOUBLE_EQ(result, 6.0);
}

TEST(CompilerTest, ContinueInInnerLoop)
{
    const auto result = run_program(R"(
        var total: f64 = 0;

        for i in 0 to 3 {
            for j in 0 to 3 {
                if j < 2 {
                    continue;
                }

                total = total + 1;
            }
        }

        total;
    )");

    EXPECT_DOUBLE_EQ(result, 3.0);
}

TEST(CompilerTest, RejectsContinueOutsideLoop)
{
    EXPECT_THROW(
        compile_program("continue;"),
        lang::CompileError);
}

TEST(CompilerTest, CallsBuiltinWithLocalVariable)
{
    EXPECT_DOUBLE_EQ(
        run_program(R"(
            var x: f64 = 16;
            sqrt(x);
        )"),
        4.0);
}

TEST(CompilerTest, CallsBuiltinWithTwoLocalVariables)
{
    EXPECT_DOUBLE_EQ(
        run_program(R"(
            var a: f64 = 2;
            var b: f64 = 3;
            pow(a, b);
        )"),
        8.0);
}

TEST(CompilerTest, IfWithoutElse)
{
    EXPECT_DOUBLE_EQ(run_program(R"(
        var x: f64 = 10;
        if 0 {
            x = 20;
        }
        x;
    )"),
                     10.0);
}

TEST(CompilerTest, IfCleansUpTemporaryValues)
{
    EXPECT_DOUBLE_EQ(run_program(R"(
        var x: f64 = 0;
        if 1 < 2 {
            x = 10 + 20;
        } else {
            x = 40;
        }
        x;
    )"),
                     30.0);
}

TEST(CompilerTest, StatementOnlyProgramReturnsNone)
{
    const auto compiler = compile_program(R"(
        var x: f64 = 10;
        x = 20;
    )");

    const auto result = round_trip(compiler).interpret();

    EXPECT_FALSE(result.has_value());
}

TEST(CompilerTest, CompilesUserDefinedFunction)
{
    const auto compiler = compile_program(R"(
        fn add(a: f64, b: f64) -> f64 {
            return a + b;
        }

        add(10, 20);
    )");

    ASSERT_TRUE(compiler.funcs.contains("add"));
    ASSERT_TRUE(compiler.funcs.contains("main"));

    const auto &fn = compiler.funcs.at("add");

    ASSERT_EQ(fn.params.size(), 2u);
    EXPECT_EQ(fn.params[0], "a");
    EXPECT_EQ(fn.params[1], "b");

    const auto result = round_trip(compiler).interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*result), 30.0);
}

TEST(CompilerTest, ReturnsLocalVariable)
{
    EXPECT_DOUBLE_EQ(run_program(R"(
        fn identity(x: f64) -> f64 {
            return x;
        }

        identity(42);
    )"),
                     42.0);
}

TEST(CompilerTest, CompilesNestedFunctionCalls)
{
    EXPECT_DOUBLE_EQ(run_program(R"(
        fn add(a: f64, b: f64) -> f64 {
            return a + b;
        }

        add(2, add(3, 4));
    )"),
                     9.0);
}

TEST(CompilerTest, CompilesRecursiveFunction)
{
    EXPECT_DOUBLE_EQ(run_program(R"(
        fn factorial(n: f64) -> f64 {
            if n < 2 {
                return 1;
            }

            return n * factorial(n - 1);
        }

        factorial(5);
    )"),
                     120.0);
}

TEST(CompilerTest, RejectsWrongArgumentCount)
{
    EXPECT_THROW(
        run_program(R"(
            fn add(a: f64, b: f64) -> f64 {
                return a + b;
            }

            add(10);
        )"),
        std::runtime_error);
}

TEST(CompilerTest, RejectsUnknownFunction)
{
    EXPECT_THROW(
        run_program("unknown(42);"),
        std::runtime_error);
}

TEST(CompilerTest, RejectsReturnOutsideFunction)
{
    EXPECT_THROW(
        compile_program("return 42;"),
        lang::CompileError);
}

TEST(CompilerTest, RejectsDuplicateFunctionName)
{
    EXPECT_THROW(
        compile_program(R"(
            fn f(x: f64) -> f64 {
                return x;
            }

            fn f(y: f64) -> f64 {
                return y;
            }
        )"),
        lang::CompileError);
}

TEST(CompilerTest, ReturnsFromConditionalBranches)
{
    EXPECT_DOUBLE_EQ(run_program(R"(
        fn choose(x: f64) -> f64 {
            if x < 2 {
                return 10;
            } else {
                return 20;
            }
        }

        choose(3);
    )"),
                     20.0);
}

TEST(CompilerTest, FunctionWithoutReturnValueThrows)
{
    EXPECT_THROW(
        run_program(R"(
            fn example() -> f64 {
                var x: f64 = 10;
            }

            example();
        )"),
        std::runtime_error);
}

TEST(CompilerTest, ImplicitlyReturnsLastExpression)
{
    EXPECT_DOUBLE_EQ(run_program(R"(
        fn example() -> f64 {
            var x: f64 = 10;
            x + 5;
        }

        example();
    )"),
                     15.0);
}

TEST(CompilerTest, RejectsMissingReturnTypeAnnotation)
{
    EXPECT_THROW(
        compile_program(R"(
            fn example() {
                return 42;
            }
        )"),
        std::runtime_error);
}

TEST(CompilerTest, ExecutesIntegerArithmetic)
{
    const auto compiler = compile_program(R"(
        var a: i64 = i64(10);
        var b: i64 = i64(20);

        a + b;
    )");

    const auto result = round_trip(compiler).interpret();

    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(
        std::get<std::int64_t>(*result),
        30);
}

TEST(CompilerTest, ExecutesStringConcatenation)
{
    const auto compiler = compile_program(R"(
        var first: str = "Hello";
        var second: str = " World";

        first + second;
    )");

    const auto result = round_trip(compiler).interpret();

    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(
        std::get<std::string>(*result),
        "Hello World");
}

TEST(CompilerTest, ExecutesIntegerFunction)
{
    const auto compiler = compile_program(R"(
        fn add(a: i64, b: i64) -> i64 {
            return a + b;
        }

        add(i64(10), i64(20));
    )");

    const auto result = round_trip(compiler).interpret();

    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(
        std::get<std::int64_t>(*result),
        30);
}

TEST(CompilerTest, CompileResultSuccess)
{
    const std::string source = "var x: f64 = 10; x + 20;";

    const auto lexed = lang::tokenize(source);
    ASSERT_TRUE(std::holds_alternative<std::vector<lang::Token>>(lexed));

    auto parsed = lang::statements(
        std::get<std::vector<lang::Token>>(lexed));

    ASSERT_TRUE(std::holds_alternative<lang::Statements>(parsed));

    const auto result = lang::compile_program_checked(
        std::get<lang::Statements>(parsed));

    ASSERT_TRUE(std::holds_alternative<lang::Compiler>(result));

    const auto &compiler = std::get<lang::Compiler>(result);

    EXPECT_TRUE(compiler.funcs.contains("main"));
}

TEST(CompilerTest, ReturnsStringFromUserFunction)
{
    const auto compiler = compile_program(R"(
        fn greet(name: str) -> str {
            return "Hello, " + name;
        }

        greet("Lang");
    )");

    const auto result = round_trip(compiler).interpret();

    ASSERT_TRUE(result.has_value());

    // 値だけでなく、実際のVM型も確認する
    ASSERT_TRUE(
        std::holds_alternative<std::string>(*result));

    EXPECT_EQ(
        std::get<std::string>(*result),
        "Hello, Lang");
}

TEST(CompilerTest, CopyDepthBoundary)
{
    lang::Compiler compiler;

    // Index 0〜255
    compiler.target_stack.resize(256);

    // 頂上はIndex 255なので、Index 0までの距離は255
    const auto inst =
        compiler.add_copy_inst(lang::StackIndex{0});

    EXPECT_EQ(compiler.instructions[inst.value].op,
              lang::OpCode::Copy);
    EXPECT_EQ(compiler.instructions[inst.value].arg_0, 255);

    // Copyによってスタックが257要素になった
    EXPECT_EQ(compiler.target_stack.size(), 257u);

    // 次のIndex 0への距離は256なので表現できない
    EXPECT_THROW(
        compiler.add_copy_inst(lang::StackIndex{0}),
        std::runtime_error);

    // 失敗した命令は追加されていない
    EXPECT_EQ(compiler.instructions.size(), 1u);
}

TEST(CompilerTest, StoreDepthBoundary)
{
    lang::Compiler compiler;

    compiler.target_stack.resize(256);

    // 頂上からIndex 0への距離は255
    const auto inst =
        compiler.add_store_inst(lang::StackIndex{0});

    EXPECT_EQ(compiler.instructions[inst.value].op,
              lang::OpCode::Store);
    EXPECT_EQ(compiler.instructions[inst.value].arg_0, 255);

    // Storeは頂上を取り除く
    EXPECT_EQ(compiler.target_stack.size(), 255u);

    // 距離256は表現できない
    compiler.target_stack.resize(257);

    EXPECT_THROW(
        compiler.add_store_inst(lang::StackIndex{0}),
        std::runtime_error);

    EXPECT_EQ(compiler.instructions.size(), 1u);
}

TEST(CompilerTest, JumpAddressBoundary)
{
    lang::Compiler compiler;

    const auto jump =
        compiler.add_inst(lang::OpCode::Jmp, 0);

    // 命令数が255になるまで埋める
    while (compiler.instructions.size() < 255)
    {
        compiler.add_inst(lang::OpCode::Ret, 0);
    }

    // ジャンプ先255は許可
    compiler.fixup_jmp(jump);

    EXPECT_EQ(
        compiler.instructions[jump.value].arg_0,
        255);

    // 命令数を256に増やす
    compiler.add_inst(lang::OpCode::Ret, 0);

    // ジャンプ先256は8ビットでは表現できない
    EXPECT_THROW(
        compiler.fixup_jmp(jump),
        std::runtime_error);

    // 失敗しても以前のジャンプ先は維持される
    EXPECT_EQ(
        compiler.instructions[jump.value].arg_0,
        255);
}
