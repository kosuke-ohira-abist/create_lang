#include "expr_parser.hpp"
#include "tokenizer.hpp"
#include "type_checker.hpp"
#include "diagnostic.hpp"

#include <gtest/gtest.h>

#include <string_view>
#include <variant>
#include <vector>

namespace
{
    using CheckResult = std::variant<
        std::monostate,
        lang::LexError,
        lang::ParseError,
        lang::TypeCheckError>;

    CheckResult check_source(std::string_view source)
    {
        auto lexed = lang::tokenize(source);

        if (const auto *error =
                std::get_if<lang::LexError>(&lexed))
        {
            return *error;
        }

        auto parsed = lang::statements(
            std::get<std::vector<lang::Token>>(lexed));

        if (const auto *error =
                std::get_if<lang::ParseError>(&parsed))
        {
            return *error;
        }

        lang::TypeCheckContext context;

        auto error = lang::check_statements_type(
            std::get<lang::Statements>(parsed),
            context);

        if (error)
        {
            return *error;
        }

        return std::monostate{};
    }
}

TEST(TypeCheckerTest, InfersLiteralType)
{
    lang::TypeCheckContext context;

    lang::Expression expression{
        lang::ExprNumLiteral{10.0},
        {0, 2}};

    auto result =
        lang::infer_expression_type(expression, context);

    ASSERT_TRUE(
        std::holds_alternative<lang::TypeDecl>(result));

    EXPECT_EQ(
        std::get<lang::TypeDecl>(result),
        lang::TypeDecl::F64);
}

TEST(TypeCheckerTest, LooksUpVariableType)
{
    lang::TypeCheckContext context;

    context.variables.emplace("x", lang::TypeDecl::I64);

    lang::Expression expression{
        lang::ExprIdent{"x"},
        {0, 1}};

    auto result =
        lang::infer_expression_type(expression, context);

    ASSERT_TRUE(
        std::holds_alternative<lang::TypeDecl>(result));

    EXPECT_EQ(
        std::get<lang::TypeDecl>(result),
        lang::TypeDecl::I64);
}

TEST(TypeCheckerTest, AcceptsValidDeclarations)
{
    auto result = check_source(R"(
        var x: i64 = i64(10);
        var y: i64 = x + i64(5);
        var message: str = "hello";

        x = i64(20);
    )");

    EXPECT_TRUE(
        std::holds_alternative<std::monostate>(result));
}

TEST(TypeCheckerTest, RejectsMismatchedInitializer)
{
    auto result = check_source(
        "var x: i64 = \"hello\";");

    const auto *error =
        std::get_if<lang::TypeCheckError>(&result);

    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->message, "incompatible types");
}

TEST(TypeCheckerTest, RejectsWrongArgumentType)
{
    auto result = check_source(R"(
        fn square(x: f64) -> f64 {
            return x * x;
        }

        var result: f64 = square("hello");
    )");

    EXPECT_TRUE(
        std::holds_alternative<lang::TypeCheckError>(result));
}

TEST(TypeCheckerTest, RejectsWrongArgumentCount)
{
    auto result = check_source(R"(
        fn square(x: f64) -> f64 {
            return x * x;
        }

        var result: f64 = square();
    )");

    EXPECT_TRUE(
        std::holds_alternative<lang::TypeCheckError>(result));
}

TEST(TypeCheckerTest, AcceptsRecursiveFunction)
{
    auto result = check_source(R"(
        fn factorial(n: i64) -> i64 {
            if n < i64(2) {
                return i64(1);
            }

            return n * factorial(n - i64(1));
        }

        var result: i64 = factorial(i64(5));
    )");

    EXPECT_TRUE(
        std::holds_alternative<std::monostate>(result));
}

TEST(TypeCheckerTest, RejectsWrongReturnType)
{
    auto result = check_source(R"(
        fn greeting() -> i64 {
            return "hello";
        }
    )");

    EXPECT_TRUE(
        std::holds_alternative<lang::TypeCheckError>(result));
}

TEST(TypeCheckerTest, ChecksReturnInsideIf)
{
    auto result = check_source(R"(
        fn f(x: f64) -> i64 {
            if x < 0 {
                return "invalid";
            }

            return i64(1);
        }
    )");

    EXPECT_TRUE(
        std::holds_alternative<lang::TypeCheckError>(result));
}

TEST(TypeCheckerTest, ChecksBothIfBranches)
{
    auto result = check_source(R"(
        var x: i64 = i64(10);

        if i64(0) {
            x = "invalid";
        } else {
            x = i64(20);
        }
    )");

    EXPECT_TRUE(
        std::holds_alternative<lang::TypeCheckError>(result));
}

TEST(TypeCheckerTest, RejectsStringLoopBound)
{
    const auto result = check_source(R"(
        for i in "hello" to 5 {
            print(i);
        }
    )");

    const auto *error =
        std::get_if<lang::TypeCheckError>(&result);

    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->message, "incompatible types");
}

TEST(TypeCheckerTest, RecognizesBuiltinFunctions)
{
    auto result = check_source(R"(
        var x: f64 = sqrt(16);
        var text: str = str(i64(42));

        print(text);
    )");

    EXPECT_TRUE(
        std::holds_alternative<std::monostate>(result));
}

TEST(TypeCheckerTest, ReportsErrorLocation)
{
    const std::string source =
        "var x: i64 = i64(10);\n"
        "x = \"hello\";\n";

    auto lexed = lang::tokenize(source);

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&lexed);

    ASSERT_NE(tokens, nullptr);

    auto parsed = lang::statements(*tokens);

    const auto *program =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(program, nullptr);

    lang::TypeCheckContext context;

    auto error = lang::check_statements_type(*program, context);

    ASSERT_TRUE(error.has_value());

    const auto location = lang::locate_source(
        source,
        error->span.begin);

    EXPECT_EQ(location.line, 2u);
    EXPECT_EQ(location.column, 5u);
}

TEST(TypeCheckerTest, RejectsImplicitNumericConversion)
{
    for (const auto *source : {
             "var x: i64 = 10;",
             "var x: f64 = i64(10);",
             "var x: i64 = i64(10); x = 5;",
             "var x: f64 = 10; x = i64(5);"})
    {
        const auto result = check_source(source);

        EXPECT_TRUE(
            std::holds_alternative<lang::TypeCheckError>(result))
            << source;
    }
}

TEST(TypeCheckerTest, AcceptsExplicitNumericConversion)
{
    const auto result = check_source(R"(
        var x: i64 = i64(10);
        var y: f64 = f64(x);
        y = y + 5;
    )");

    EXPECT_TRUE(
        std::holds_alternative<std::monostate>(result));
}

TEST(TypeCheckerTest, AcceptsNumericIfConditions)
{
    const auto result = check_source(R"(
        if 1 {
            print("f64");
        }

        if i64(1) {
            print("i64");
        }
    )");

    EXPECT_TRUE(
        std::holds_alternative<std::monostate>(result));
}

TEST(TypeCheckerTest, AcceptsFloatingPointLoop)
{
    const auto result = check_source(R"(
        var total: f64 = 0;

        for i in 0 to 5 {
            total = total + i;
        }
    )");

    EXPECT_TRUE(
        std::holds_alternative<std::monostate>(result));
}

TEST(TypeCheckerTest, RejectsIntegerLoopForNow)
{
    const auto result = check_source(R"(
        for i in i64(0) to i64(5) {
            print(i);
        }
    )");

    EXPECT_TRUE(
        std::holds_alternative<lang::TypeCheckError>(result));
}

TEST(TypeCheckerTest, AcceptsReturnsFromBothBranches)
{
    const auto result = check_source(R"(
        fn choose(x: f64) -> f64 {
            if x < 2 {
                return 10;
            } else {
                return 20;
            }
        }
    )");

    EXPECT_TRUE(
        std::holds_alternative<std::monostate>(result));
}

TEST(TypeCheckerTest, AcceptsImplicitReturnValue)
{
    const auto result = check_source(R"(
        fn example() -> i64 {
            var x: i64 = i64(10);
            x;
        }
    )");

    EXPECT_TRUE(
        std::holds_alternative<std::monostate>(result));
}

TEST(TypeCheckerTest, RejectsDuplicateParameters)
{
    const auto result = check_source(R"(
        fn example(x: f64, x: i64) -> f64 {
            return 10;
        }
    )");

    EXPECT_TRUE(
        std::holds_alternative<lang::TypeCheckError>(result));
}

TEST(TypeCheckerTest, PrintReturnsF64)
{
    lang::TypeCheckContext context;
    const lang::Statements statements;

    ASSERT_FALSE(
        lang::check_statements_type(statements, context)
            .has_value());

    const auto *signature =
        context.find_function("print");

    ASSERT_NE(signature, nullptr);

    EXPECT_EQ(
        signature->return_type,
        lang::TypeDecl::F64);

    // printは可変長引数関数
    const auto result = check_source(
        "var x: f64 = print(1, \"hello\");");

    EXPECT_TRUE(
        std::holds_alternative<std::monostate>(result));
}

TEST(TypeCheckerTest, RejectsStringToNumericConversion)
{
    for (const auto *source : {
             "i64(\"123\");",
             "f64(\"3.14\");"})
    {
        const auto result = check_source(source);

        const auto *error =
            std::get_if<lang::TypeCheckError>(&result);

        ASSERT_NE(error, nullptr) << source;

        EXPECT_NE(
            error->message.find("numeric argument"),
            std::string::npos);
    }
}

TEST(TypeCheckerTest, AcceptsNumericConversions)
{
    const auto result = check_source(R"(
        var a: i64 = i64(10);
        var b: f64 = f64(a);
        var c: str = str(a);
        var d: f64 = sqrt(b);
    )");

    EXPECT_TRUE(
        std::holds_alternative<std::monostate>(result));
}

TEST(TypeCheckerTest, RejectsBuiltinRedefinition)
{
    const auto result = check_source(R"(
        fn sqrt(x: f64) -> f64 {
            return x + 1;
        }
    )");

    EXPECT_TRUE(
        std::holds_alternative<lang::TypeCheckError>(result));
}

TEST(TypeCheckerTest, NumericAndControlFlowContracts)
{
    struct Case
    {
        std::string_view source;
        bool should_succeed;
    };

    constexpr Case cases[] = {
        // 数値の暗黙的な型変換は禁止
        {"var x: i64 = 10;", false},
        {"var x: i64 = i64(10);", true},
        {"var x: f64 = i64(10);", false},
        {"var x: f64 = f64(i64(10));", true},

        // 代入時も宣言型との一致が必要
        {"var x: i64 = i64(10); x = 5;", false},
        {"var x: i64 = i64(10); x = i64(5);", true},

        // ifの条件は数値型
        {"if 1 { print(1); }", true},
        {"if i64(1) { print(1); }", true},
        {"if \"hello\" { print(1); }", false},

        // 現在のforはF64の範囲を使用する
        {"for i in 0 to 5 { print(i); }", true},
        {"for i in i64(0) to i64(5) { print(i); }", false},
        {"for i in \"hello\" to 5 { print(i); }", false},
    };

    for (const auto &[source, should_succeed] : cases)
    {
        const auto result = check_source(source);

        if (should_succeed)
        {
            EXPECT_TRUE(
                std::holds_alternative<std::monostate>(result))
                << source;
        }
        else
        {
            EXPECT_TRUE(
                std::holds_alternative<lang::TypeCheckError>(result))
                << source;
        }
    }
}

TEST(TypeCheckerTest, BuiltinFunctionContracts)
{
    struct Case
    {
        std::string_view source;
        bool should_succeed;
    };

    constexpr Case cases[] = {
        // printは可変長引数を受け取り、F64を返す
        {"var x: f64 = print(1, \"hello\", i64(2));", true},

        // 数値変換関数は文字列を受け付けない
        {"i64(\"12\");", false},
        {"f64(\"12\");", false},

        // 数値変換関数を明示的に使えば数学関数へ渡せる
        {"var x: f64 = sqrt(i64(4));", false},
        {"var x: f64 = sqrt(f64(i64(4)));", true},

        // 組み込み関数の再定義は禁止
        {"fn sqrt(x: f64) -> f64 { return x; }", false},
    };

    for (const auto &[source, should_succeed] : cases)
    {
        const auto result = check_source(source);

        if (should_succeed)
        {
            EXPECT_TRUE(
                std::holds_alternative<std::monostate>(result))
                << source;
        }
        else
        {
            EXPECT_TRUE(
                std::holds_alternative<lang::TypeCheckError>(result))
                << source;
        }
    }
}

TEST(TypeCheckerTest, RejectsMissingReturnValueOnSomePaths)
{
    for (const auto *source : {
             R"(fn f() -> f64 { 9; var x: f64 = 2; })",
             R"(fn f() -> i64 { if 0 { return i64(1); } })",
             R"(fn f() -> f64 { for i in 0 to 2 { return i; } })",
             R"(fn f() -> f64 { var x: f64 = 1; })",
             R"(fn f() -> f64 {})",
         })

    {
        const auto result = check_source(source);
        const auto *error = std::get_if<lang::TypeCheckError>(&result);
        ASSERT_NE(error, nullptr) << source;
        EXPECT_NE(error->message.find("without a return value"), std::string::npos) << source;
    }
}

TEST(TypeCheckerTest, AcceptsAllPathsReturningValue)
{
    for (const auto *source : {
             R"(fn f() -> f64 { return 10; })",
             R"(fn f() -> f64 { var x: f64 = 10; x; })",
             R"(fn f(x: f64) -> f64 { if x < 0 { return 10; } else { return 20; } })",
             R"(fn f(x: f64) -> f64 { if x < 0 { return 10; } 20; })",
             R"(fn f(x: f64) -> f64 { if x < 0 { if x < -1 { return 1; } else { return 2; } } else { return 3; } })",
         })
    {
        const auto result = check_source(source);
        EXPECT_TRUE(std::holds_alternative<std::monostate>(result)) << source;
    }
}

TEST(TypeCheckerTest, ChecksStatementsAfterReturn)
{
    const auto result = check_source(R"(
        fn f() -> f64 {
            return 1;
            unknown_var;
        }
    )");
    EXPECT_TRUE(std::holds_alternative<lang::TypeCheckError>(result));
}

TEST(TypeCheckerTest, BuiltinAnyArgumentAcceptsConcreteTypes)
{
    const auto result = check_source(R"(
        var a: str = str(1);
        var b: str = str(i64(2));
        var c: str = str("hello");
        var d: f64 = print(1, i64(2), "hello");
        var e: f64 = f64(i64(3));
        var f: i64 = i64(4);
    )");

    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(TypeCheckerTest, NumericConversionsStillRejectStringArguments)
{
    for (const auto *source : {
             R"(var x: i64 = i64("42");)",
             R"(var x: f64 = f64("42");)",
             R"(var x: f64 = str(42);)",
         })
    {
        const auto result = check_source(source);
        EXPECT_TRUE(std::holds_alternative<lang::TypeCheckError>(result)) << source;
    }
}

TEST(TypeCheckerTest, AnyArgumentIsNotAValueOrReturnType)
{
    lang::TypeCheckContext context;
    lang::Statements empty;
    ASSERT_FALSE(lang::check_statements_type(empty, context).has_value());

    const auto *signature = context.find_function("str");
    ASSERT_NE(signature, nullptr);
    ASSERT_EQ(signature->parameters.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<lang::AnyArgument>(signature->parameters[0]));
    EXPECT_EQ(signature->return_type, lang::TypeDecl::Str);

    // lang の型宣言として any は使用できない
    auto lexed = lang::tokenize("var x: any = 1;");
    ASSERT_TRUE(std::holds_alternative<std::vector<lang::Token>>(lexed));

    auto parsed = lang::statements(std::get<std::vector<lang::Token>>(lexed));
    EXPECT_TRUE(std::holds_alternative<lang::ParseError>(parsed));
}
