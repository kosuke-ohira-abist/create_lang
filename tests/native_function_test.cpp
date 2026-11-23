#include "native_function.hpp"
#include "tokenizer.hpp"
#include "expr_parser.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>

#include <sstream>
#include <string>
#include <variant>
#include <vector>
#include <utility>

TEST(NativeFunctionTest, CallsRegisteredCppFunction)
{
    // 1. C++関数を登録
    lang::NativeFunctionRegistry registry;

    registry.register_function(
        "twice",
        lang::FunctionSignature{
            {lang::TypeDecl::F64},
            lang::TypeDecl::F64},
        [](std::span<const lang::VmValue> args) -> lang::VmValue
        {
            return std::get<double>(args[0]) * 2.0;
        });

    // 2. ソースコード
    const std::string source = "twice(21);";

    auto lexed = lang::tokenize(source);

    ASSERT_TRUE(
        std::holds_alternative<std::vector<lang::Token>>(lexed));

    auto parsed = lang::statements(
        std::get<std::vector<lang::Token>>(lexed));

    ASSERT_TRUE(
        std::holds_alternative<lang::Statements>(parsed));

    const auto &program = std::get<lang::Statements>(parsed);

    // 3. Registryを使った型検査
    lang::TypeCheckContext context;

    const auto error =
        lang::check_statements_type(program, context, &registry);

    ASSERT_FALSE(error.has_value());

    // 4. バイトコードを生成
    lang::Compiler compiler;
    compiler.compile_program(program);

    // 5. 保存・復元
    std::stringstream buffer(
        std::ios::in | std::ios::out | std::ios::binary);

    compiler.write_funcs(buffer);
    buffer.seekg(0);

    lang::Vm vm;
    vm.set_native_functions(registry);
    vm.read_funcs(buffer);

    // 6. ネイティブ関数を実行
    const auto result = vm.interpret();

    ASSERT_TRUE(result.has_value());

    EXPECT_DOUBLE_EQ(
        std::get<double>(*result),
        42.0);
}

TEST(NativeFunctionTest, CallsUserDefinedFunctionWithRegistry)
{
    lang::NativeFunctionRegistry registry;

    const std::string source = R"(
        fn triple(x: f64) -> f64 {
            return x * 3;
        }

        triple(14);
    )";

    auto lexed = lang::tokenize(source);
    ASSERT_TRUE(
        std::holds_alternative<std::vector<lang::Token>>(lexed));

    auto parsed = lang::statements(
        std::get<std::vector<lang::Token>>(lexed));

    ASSERT_TRUE(
        std::holds_alternative<lang::Statements>(parsed));

    const auto &program = std::get<lang::Statements>(parsed);

    lang::TypeCheckContext context;
    ASSERT_FALSE(
        lang::check_statements_type(program, context, &registry)
            .has_value());

    auto compiled = lang::compile_program_checked(program);
    ASSERT_TRUE(
        std::holds_alternative<lang::Compiler>(compiled));

    lang::Vm vm;
    vm.funcs = std::move(
        std::get<lang::Compiler>(compiled).funcs);

    // Registryを設定していても、triple()を実行できること
    vm.set_native_functions(registry);

    const auto result = vm.interpret();

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*result), 42.0);
}

TEST(NativeFunctionTest, AnyArgumentAcceptsAllConcreteVmTypes)
{
    lang::NativeFunctionRegistry registry;
    registry.register_function(
        "type_name",
        lang::FunctionSignature{{lang::AnyArgument{}}, lang::TypeDecl::Str},
        [](std::span<const lang::VmValue> args) -> lang::VmValue
        {
            if (std::holds_alternative<double>(args[0]))
            {
                return std::string{"f"};
            }
            if (std::holds_alternative<std::int64_t>(args[0]))
            {
                return std::string{"i"};
            }
            return std::string{"s"};
        });

    const std::string source = R"(
        var a: str = type_name(1);
        var b: str = type_name(i64(2));
        var c: str = type_name("hello");
        a + b + c;
    )";

    auto lexed = lang::tokenize(source);
    ASSERT_TRUE(std::holds_alternative<std::vector<lang::Token>>(lexed));
    auto parsed = lang::statements(std::get<std::vector<lang::Token>>(lexed));
    ASSERT_TRUE(std::holds_alternative<lang::Statements>(parsed));
    const auto &program = std::get<lang::Statements>(parsed);

    lang::TypeCheckContext context;
    ASSERT_FALSE(lang::check_statements_type(program, context, &registry).has_value());
    auto compiled = lang::compile_program_checked(program);
    ASSERT_TRUE(std::holds_alternative<lang::Compiler>(compiled));

    lang::Vm vm;
    vm.funcs = std::move(std::get<lang::Compiler>(compiled).funcs);
    vm.set_native_functions(registry);

    const auto result = vm.interpret();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<std::string>(*result), "fis");
}

TEST(NativeFunctionTest, RejectsWrongNativeReturnType)
{
    lang::NativeFunctionRegistry registry;
    registry.register_function(
        "wrong_result",
        lang::FunctionSignature{{lang::AnyArgument{}}, lang::TypeDecl::F64},
        [](std::span<const lang::VmValue>) -> lang::VmValue
        {
            return std::string{"unexpected"};
        });

    const std::vector<lang::VmValue> args{2.0};
    EXPECT_THROW(registry.invoke("wrong_result", args), std::runtime_error);
}

TEST(NativeFunctionTest, ConcreteArgumentStillChecksRuntimeType)
{
    lang::NativeFunctionRegistry registry;
    registry.register_function(
        "integer_only",
        lang::FunctionSignature{{lang::TypeDecl::I64}, lang::TypeDecl::I64},
        [](std::span<const lang::VmValue> args) -> lang::VmValue
        {
            return std::get<std::int64_t>(args[0]);
        });

    const std::vector<lang::VmValue> args{std::string{"wrong"}};
    EXPECT_THROW(registry.invoke("integer_only", args), std::runtime_error);
}
