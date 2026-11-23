#include "expr_parser.hpp"
#include "tokenizer.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{

    using FrontendResult = std::variant<
        lang::Expression,
        lang::LexError,
        lang::ParseError>;

    FrontendResult parse_source(std::string_view input)
    {
        auto lexed = lang::tokenize(input);

        if (const auto *error =
                std::get_if<lang::LexError>(&lexed))
        {
            return *error;
        }

        auto parsed = lang::expr(
            std::get<std::vector<lang::Token>>(lexed));

        if (const auto *error =
                std::get_if<lang::ParseError>(&parsed))
        {
            return *error;
        }

        return std::move(
            std::get<lang::Expression>(parsed));
    }

    std::string describe(const lang::Expression &expr)
    {
        if (const auto *id =
                std::get_if<lang::ExprIdent>(&expr.value))
        {
            return "Ident(" + std::string(id->text) + ")";
        }

        if (const auto *number =
                std::get_if<lang::ExprNumLiteral>(&expr.value))
        {
            std::ostringstream out;
            out << "Num(" << number->value << ")";
            return out.str();
        }

        if (const auto *str =
                std::get_if<lang::ExprStrLiteral>(&expr.value))
        {
            return "Str(\"" + str->value + "\")";
        }

        if (const auto *fn =
                std::get_if<lang::ExprFnInvoke>(&expr.value))
        {
            std::string result =
                "FnInvoke(" + std::string(fn->name) + ",[";

            for (std::size_t i = 0; i < fn->args.size(); ++i)
            {
                if (i != 0)
                {
                    result += ",";
                }

                result += describe(fn->args[i]);
            }

            return result + "])";
        }

        if (const auto *add =
                std::get_if<lang::ExprAdd>(&expr.value))
        {
            return "Add(" + describe(*add->lhs) + "," +
                   describe(*add->rhs) + ")";
        }

        if (const auto *sub =
                std::get_if<lang::ExprSub>(&expr.value))
        {
            return "Sub(" + describe(*sub->lhs) + "," +
                   describe(*sub->rhs) + ")";
        }

        if (const auto *mult =
                std::get_if<lang::ExprMul>(&expr.value))
        {
            return "Mult(" + describe(*mult->lhs) + "," +
                   describe(*mult->rhs) + ")";
        }

        if (const auto *lt =
                std::get_if<lang::ExprLt>(&expr.value))
        {
            return "Lt(" + describe(*lt->lhs) + "," +
                   describe(*lt->rhs) + ")";
        }

        if (const auto *gt =
                std::get_if<lang::ExprGt>(&expr.value))
        {
            return "Gt(" + describe(*gt->lhs) + "," +
                   describe(*gt->rhs) + ")";
        }

        const auto &div = std::get<lang::ExprDiv>(expr.value);

        return "Div(" + describe(*div.lhs) + "," +
               describe(*div.rhs) + ")";
    }

} // namespace

TEST(ExprParserTest, ParsesNumberLiteral)
{
    auto result = parse_source("123.45");

    ASSERT_TRUE(
        std::holds_alternative<lang::Expression>(result));

    const auto &expr =
        std::get<lang::Expression>(result);

    const auto *number =
        std::get_if<lang::ExprNumLiteral>(&expr.value);

    ASSERT_NE(number, nullptr);

    EXPECT_DOUBLE_EQ(number->value, 123.45);
    EXPECT_EQ(expr.span, (lang::SourceSpan{0, 6}));
}

TEST(ExprParserTest, ParsesIdentifier)
{
    auto result = parse_source("Hello");

    ASSERT_TRUE(
        std::holds_alternative<lang::Expression>(result));

    EXPECT_EQ(
        describe(std::get<lang::Expression>(result)),
        "Ident(Hello)");
}

TEST(ExprParserTest, AdditionIsLeftAssociative)
{
    auto result = parse_source("car + cdr + cdr");

    ASSERT_TRUE(
        std::holds_alternative<lang::Expression>(result));

    EXPECT_EQ(
        describe(std::get<lang::Expression>(result)),
        "Add(Add(Ident(car),Ident(cdr)),Ident(cdr))");
}

TEST(ExprParserTest, ParsesNestedParentheses)
{
    auto result =
        parse_source("((1 + 2) + (3 + 4)) + 5 + 6");

    ASSERT_TRUE(
        std::holds_alternative<lang::Expression>(result));

    EXPECT_EQ(
        describe(std::get<lang::Expression>(result)),
        "Add(Add(Add(Add(Num(1),Num(2)),"
        "Add(Num(3),Num(4))),Num(5)),Num(6))");
}

TEST(ExprParserTest, ParenthesesDoNotCreateAstNode)
{
    auto result = parse_source("(123)");

    ASSERT_TRUE(
        std::holds_alternative<lang::Expression>(result));

    EXPECT_EQ(
        describe(std::get<lang::Expression>(result)),
        "Num(123)");

    EXPECT_EQ(
        std::get<lang::Expression>(result).span,
        (lang::SourceSpan{0, 5}));
}

TEST(ExprParserTest, ParsesSignedNumericLiterals)
{
    auto result = parse_source("1+-2");

    ASSERT_TRUE(
        std::holds_alternative<lang::Expression>(result));

    EXPECT_EQ(
        describe(std::get<lang::Expression>(result)),
        "Add(Num(1),Num(-2))");

    auto positive = parse_source("+2.5");

    ASSERT_TRUE(
        std::holds_alternative<lang::Expression>(positive));

    EXPECT_EQ(
        describe(std::get<lang::Expression>(positive)),
        "Num(2.5)");
}

TEST(ExprParserTest, RejectsUnaryMinusOnIdentifier)
{
    EXPECT_TRUE(
        std::holds_alternative<lang::ParseError>(
            parse_source("-foo")));
}

TEST(ExprParserTest, MissingRhsIsSyntaxError)
{
    auto result = parse_source("1 +");

    ASSERT_TRUE(
        std::holds_alternative<lang::ParseError>(result));

    EXPECT_EQ(
        std::get<lang::ParseError>(result).span,
        (lang::SourceSpan{3, 3}));
}

TEST(ExprParserTest, MissingClosingParenIsSyntaxError)
{
    EXPECT_TRUE(
        std::holds_alternative<lang::ParseError>(
            parse_source("(1 + 2")));
}

TEST(ExprParserTest, EmptyParenIsSyntaxError)
{
    EXPECT_TRUE(
        std::holds_alternative<lang::ParseError>(
            parse_source("()")));
}

TEST(ExprParserTest, ExtraTokensAreSyntaxError)
{
    EXPECT_TRUE(
        std::holds_alternative<lang::ParseError>(
            parse_source("1 + 2)")));
}

TEST(ExprParserTest, BadNumberIsLexError)
{
    EXPECT_TRUE(
        std::holds_alternative<lang::LexError>(
            parse_source("12..3")));
}

TEST(ExprParserTest, EmptyInputIsSyntaxError)
{
    EXPECT_TRUE(
        std::holds_alternative<lang::ParseError>(
            parse_source("")));
}

TEST(ExprParserTest, ParsesNewIdentifierAndNumberSyntax)
{
    auto result = parse_source("_foo + 1.2e-3 + foo_2");

    const auto *expression =
        std::get_if<lang::Expression>(&result);

    ASSERT_NE(expression, nullptr);

    EXPECT_EQ(
        describe(*expression),
        "Add(Add(Ident(_foo),Num(0.0012)),Ident(foo_2))");

    EXPECT_EQ(
        expression->span,
        (lang::SourceSpan{0, 21}));
}

TEST(ExprParserTest, ParsesSignedExponentNumber)
{
    auto result = parse_source("1 + -2.5e-2");

    const auto *expression =
        std::get_if<lang::Expression>(&result);

    ASSERT_NE(expression, nullptr);

    EXPECT_EQ(
        describe(*expression),
        "Add(Num(1),Num(-0.025))");
}

TEST(ExprParserTest, MultiplicationHasHigherPrecedence)
{
    auto result = parse_source("1 + 2 * 3");

    const auto *expression =
        std::get_if<lang::Expression>(&result);

    ASSERT_NE(expression, nullptr);

    EXPECT_EQ(
        describe(*expression),
        "Add(Num(1),Mult(Num(2),Num(3)))");
}

TEST(ExprParserTest, ParenthesesOverridePrecedence)
{
    auto result = parse_source("(1 + 2) * 3");

    const auto *expression =
        std::get_if<lang::Expression>(&result);

    ASSERT_NE(expression, nullptr);

    EXPECT_EQ(
        describe(*expression),
        "Mult(Add(Num(1),Num(2)),Num(3))");

    EXPECT_EQ(
        expression->span,
        (lang::SourceSpan{0, 11}));
}

TEST(ExprParserTest, SubtractionIsLeftAssociative)
{
    auto result = parse_source("10 - 3 - 2");

    const auto *expression =
        std::get_if<lang::Expression>(&result);

    ASSERT_NE(expression, nullptr);

    EXPECT_EQ(
        describe(*expression),
        "Sub(Sub(Num(10),Num(3)),Num(2))");
}

TEST(ExprParserTest, DivisionIsLeftAssociative)
{
    auto result = parse_source("16 / 4 / 2");

    const auto *expression =
        std::get_if<lang::Expression>(&result);

    ASSERT_NE(expression, nullptr);

    EXPECT_EQ(
        describe(*expression),
        "Div(Div(Num(16),Num(4)),Num(2))");
}

TEST(ExprParserTest, ParsesMixedArithmetic)
{
    auto result = parse_source("5 - 2 * 3 + 8 / 4");

    const auto *expression =
        std::get_if<lang::Expression>(&result);

    ASSERT_NE(expression, nullptr);

    EXPECT_EQ(
        describe(*expression),
        "Add(Sub(Num(5),Mult(Num(2),Num(3))),"
        "Div(Num(8),Num(4)))");
}

TEST(ExprParserTest, SignedNumbersWorkWithMultiplication)
{
    auto result = parse_source("2 * -3 + 1");

    const auto *expression =
        std::get_if<lang::Expression>(&result);

    ASSERT_NE(expression, nullptr);

    EXPECT_EQ(
        describe(*expression),
        "Add(Mult(Num(2),Num(-3)),Num(1))");
}

TEST(ExprParserTest, MissingMultiplicationOperandFails)
{
    auto result = parse_source("1 *");

    const auto *error =
        std::get_if<lang::ParseError>(&result);

    ASSERT_NE(error, nullptr);

    EXPECT_EQ(
        error->span,
        (lang::SourceSpan{3, 3}));
}

TEST(ExprParserTest, InvalidOperatorSequenceFails)
{
    auto result = parse_source("1 * * 2");

    EXPECT_TRUE(
        std::holds_alternative<lang::ParseError>(result));
}

TEST(PegConceptTest, OrderedChoiceDependsOnOrder)
{
    // Simplified demonstration of PEG ordered choice.
    const auto match_ab = [](
                              std::string_view input,
                              std::string_view first,
                              std::string_view second)
    {
        auto rest = input;

        if (rest.starts_with(first))
        {
            rest.remove_prefix(first.size());
        }
        else if (rest.starts_with(second))
        {
            rest.remove_prefix(second.size());
        }
        else
        {
            return false;
        }

        // Continue with the sequence: ~ "c"
        if (!rest.starts_with("c"))
        {
            return false;
        }

        rest.remove_prefix(1);
        return rest.empty();
    };

    EXPECT_TRUE(match_ab("abc", "ab", "a"));
    EXPECT_TRUE(match_ab("ac", "ab", "a"));

    // The first choice succeeds, so the parser
    // does not reconsider the second alternative.
    EXPECT_FALSE(match_ab("abc", "a", "ab"));
}

TEST(ExprParserTest, ParsesFunctionCall)
{
    auto result = parse_source("sqrt(1 + 3)");

    const auto *expression =
        std::get_if<lang::Expression>(&result);

    ASSERT_NE(expression, nullptr);

    EXPECT_EQ(
        describe(*expression),
        "FnInvoke(sqrt,[Add(Num(1),Num(3))])");

    EXPECT_EQ(
        expression->span,
        (lang::SourceSpan{0, 11}));
}

TEST(ExprParserTest, ParsesNestedFunctionCall)
{
    auto result = parse_source("pow(2, sqrt(9))");

    const auto *expression =
        std::get_if<lang::Expression>(&result);

    ASSERT_NE(expression, nullptr);

    EXPECT_EQ(
        describe(*expression),
        "FnInvoke(pow,[Num(2),FnInvoke(sqrt,[Num(9)])])");
}

TEST(ExprParserTest, FunctionCallHasPrimaryPrecedence)
{
    auto result = parse_source("2 * sqrt(9) + 1");

    const auto *expression =
        std::get_if<lang::Expression>(&result);

    ASSERT_NE(expression, nullptr);

    EXPECT_EQ(
        describe(*expression),
        "Add(Mult(Num(2),FnInvoke(sqrt,[Num(9)])),Num(1))");
}

TEST(ExprParserTest, OptionalArgumentComma)
{
    auto result = parse_source("pow(2 3)");

    const auto *expression =
        std::get_if<lang::Expression>(&result);

    ASSERT_NE(expression, nullptr);

    EXPECT_EQ(
        describe(*expression),
        "FnInvoke(pow,[Num(2),Num(3)])");
}

TEST(ExprParserTest, UnclosedFunctionCallFails)
{
    auto result = parse_source("sqrt(1");

    EXPECT_TRUE(
        std::holds_alternative<lang::ParseError>(result));
}

TEST(ExprParserTest, InvalidArgumentListFails)
{
    auto result = parse_source("pow(,2)");

    EXPECT_TRUE(
        std::holds_alternative<lang::ParseError>(result));
}

TEST(ExprParserTest, ParsesMultipleStatements)
{
    auto lexed = lang::tokenize("1 + 2; sqrt(16); PI;");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&lexed);

    ASSERT_NE(tokens, nullptr);

    auto parsed = lang::statements(*tokens);

    const auto *expressions =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(expressions, nullptr);
    ASSERT_EQ(expressions->size(), 3u);

    EXPECT_EQ(
        describe(std::get<lang::Expression>((*expressions)[0])),
        "Add(Num(1),Num(2))");

    EXPECT_EQ(
        describe(std::get<lang::Expression>((*expressions)[1])),
        "FnInvoke(sqrt,[Num(16)])");

    EXPECT_EQ(
        describe(std::get<lang::Expression>((*expressions)[2])),
        "Ident(PI)");
}

TEST(ExprParserTest, AcceptsEmptyStatements)
{
    auto lexed = lang::tokenize("");

    const auto &tokens =
        std::get<std::vector<lang::Token>>(lexed);

    auto parsed = lang::statements(tokens);

    const auto *expressions =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(expressions, nullptr);
    EXPECT_TRUE(expressions->empty());
}

TEST(ExprParserTest, AcceptsTrailingSemicolon)
{
    auto lexed = lang::tokenize("1; 2;");

    const auto &tokens =
        std::get<std::vector<lang::Token>>(lexed);

    auto parsed = lang::statements(tokens);

    const auto *expressions =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(expressions, nullptr);
    EXPECT_EQ(expressions->size(), 2u);
}

TEST(ExprParserTest, RejectsInvalidStatementSeparators)
{
    for (std::string_view input : {
             "1;;2",
             ";1",
             "1 2",
             "1\n2",
             "1; 2 +"})
    {
        auto lexed = lang::tokenize(input);

        const auto *tokens =
            std::get_if<std::vector<lang::Token>>(&lexed);

        ASSERT_NE(tokens, nullptr) << input;

        auto parsed = lang::statements(*tokens);

        EXPECT_TRUE(
            std::holds_alternative<lang::ParseError>(parsed))
            << input;
    }
}

TEST(ExprParserTest, ParsesVariableDefinition)
{
    auto lexed = lang::tokenize("var radius:f64 = 2; radius;");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&lexed);

    ASSERT_NE(tokens, nullptr);

    auto parsed = lang::statements(*tokens);

    const auto *statements =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 2u);

    const auto *definition =
        std::get_if<lang::VarDef>(&(*statements)[0]);

    ASSERT_NE(definition, nullptr);

    EXPECT_EQ(definition->name, "radius");
    EXPECT_EQ(
        describe(definition->initializer),
        "Num(2)");

    const auto *expression =
        std::get_if<lang::Expression>(&(*statements)[1]);

    ASSERT_NE(expression, nullptr);

    EXPECT_EQ(
        describe(*expression),
        "Ident(radius)");
}

TEST(ExprParserTest, VariableInitializerCanBeExpression)
{
    auto lexed = lang::tokenize(
        "var x:f64 = sqrt(9) + 2 * 3;");

    const auto &tokens =
        std::get<std::vector<lang::Token>>(lexed);

    auto parsed = lang::statements(tokens);

    const auto *statements =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1u);

    const auto *definition =
        std::get_if<lang::VarDef>(&(*statements)[0]);

    ASSERT_NE(definition, nullptr);

    EXPECT_EQ(
        describe(definition->initializer),
        "Add(FnInvoke(sqrt,[Num(9)]),Mult(Num(2),Num(3)))");
}

TEST(ExprParserTest, RejectsInvalidVariableDefinitions)
{
    for (std::string_view input : {
             "var = 10",
             "var x 10",
             "var x =",
             "var x = 1; var"})
    {
        auto lexed = lang::tokenize(input);

        const auto *tokens =
            std::get_if<std::vector<lang::Token>>(&lexed);

        ASSERT_NE(tokens, nullptr) << input;

        auto parsed = lang::statements(*tokens);

        EXPECT_TRUE(
            std::holds_alternative<lang::ParseError>(parsed))
            << input;
    }
}

TEST(ExprParserTest, ParsesVariableAssignment)
{
    auto lexed = lang::tokenize(
        "var x:f64 = 1; x = x + 4; x;");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&lexed);

    ASSERT_NE(tokens, nullptr);

    auto parsed = lang::statements(*tokens);

    const auto *statements =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 3u);

    // First statement: variable definition
    EXPECT_TRUE(std::holds_alternative<lang::VarDef>(
        (*statements)[0]));

    // Second statement: variable assignment
    const auto *assignment =
        std::get_if<lang::VarAssign>(&(*statements)[1]);

    ASSERT_NE(assignment, nullptr);

    EXPECT_EQ(assignment->name, "x");

    EXPECT_EQ(
        describe(assignment->value),
        "Add(Ident(x),Num(4))");

    // Third statement: expression
    const auto *expression =
        std::get_if<lang::Expression>(&(*statements)[2]);

    ASSERT_NE(expression, nullptr);
    EXPECT_EQ(describe(*expression), "Ident(x)");
}

TEST(ExprParserTest, DistinguishesAssignmentFromExpression)
{
    auto lexed = lang::tokenize("x + 1; x = 2;");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&lexed);

    ASSERT_NE(tokens, nullptr);

    auto parsed = lang::statements(*tokens);

    const auto *statements =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 2u);

    EXPECT_TRUE(
        std::holds_alternative<lang::Expression>(
            (*statements)[0]));

    EXPECT_TRUE(
        std::holds_alternative<lang::VarAssign>(
            (*statements)[1]));
}

TEST(ExprParserTest, RejectsInvalidAssignments)
{
    for (std::string_view input : {
             "x =",
             "1 = 2",
             "x = 1 = 2",
             "var x = 1; (x) = 2"})
    {
        auto lexed = lang::tokenize(input);

        const auto *tokens =
            std::get_if<std::vector<lang::Token>>(&lexed);

        ASSERT_NE(tokens, nullptr) << input;

        auto parsed = lang::statements(*tokens);

        EXPECT_TRUE(
            std::holds_alternative<lang::ParseError>(parsed))
            << input;
    }
}

TEST(ExprParserTest, ParsesIfStatement)
{
    auto lexed = lang::tokenize(
        "if 1 + 2 * 3 {"
        "    var x:f64 = 10;"
        "} else {"
        "    var x:f64 = 20;"
        "}");

    const auto &tokens =
        std::get<std::vector<lang::Token>>(lexed);

    auto parsed = lang::statements(tokens);

    const auto *statements =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1u);

    const auto *ptr =
        std::get_if<std::unique_ptr<lang::IfStatement>>(
            &(*statements)[0]);

    ASSERT_NE(ptr, nullptr);
    ASSERT_NE(ptr->get(), nullptr);

    const auto &node = **ptr;

    EXPECT_EQ(
        describe(node.condition),
        "Add(Num(1),Mult(Num(2),Num(3)))");

    ASSERT_EQ(node.then_branch.size(), 1u);

    EXPECT_TRUE(std::holds_alternative<lang::VarDef>(
        node.then_branch[0]));

    ASSERT_TRUE(node.else_branch.has_value());
    ASSERT_EQ(node.else_branch->size(), 1u);
}

TEST(ExprParserTest, ParsesIfWithoutElse)
{
    auto lexed = lang::tokenize(
        "if 1 { 10; }");

    const auto &tokens =
        std::get<std::vector<lang::Token>>(lexed);

    auto parsed = lang::statements(tokens);

    const auto *statements =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1u);

    const auto *ptr =
        std::get_if<std::unique_ptr<lang::IfStatement>>(
            &(*statements)[0]);

    ASSERT_NE(ptr, nullptr);
    ASSERT_NE(ptr->get(), nullptr);

    const auto &node = **ptr;

    EXPECT_EQ(
        describe(node.condition),
        "Num(1)");

    ASSERT_EQ(node.then_branch.size(), 1u);

    EXPECT_TRUE(std::holds_alternative<lang::Expression>(
        node.then_branch[0]));

    ASSERT_FALSE(node.else_branch.has_value());
}

TEST(ExprParserTest, ParsesNestedIf)
{
    auto lexed = lang::tokenize(
        "if 0 { 1; } else { if 1 { 2; } else { 3; } }");

    const auto &tokens =
        std::get<std::vector<lang::Token>>(lexed);

    auto parsed = lang::statements(tokens);

    const auto *statements =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1u);

    const auto *ptr =
        std::get_if<std::unique_ptr<lang::IfStatement>>(
            &(*statements)[0]);

    ASSERT_NE(ptr, nullptr);
    ASSERT_NE(ptr->get(), nullptr);

    const auto &node = **ptr;

    EXPECT_EQ(
        describe(node.condition),
        "Num(0)");

    ASSERT_EQ(node.then_branch.size(), 1u);

    EXPECT_TRUE(std::holds_alternative<lang::Expression>(
        node.then_branch[0]));

    ASSERT_TRUE(node.else_branch.has_value());
    ASSERT_EQ(node.else_branch->size(), 1u);

    const auto *ptr_nested_if =
        std::get_if<std::unique_ptr<lang::IfStatement>>(
            &(*node.else_branch)[0]);

    ASSERT_NE(ptr_nested_if, nullptr);
    ASSERT_NE(ptr_nested_if->get(), nullptr);

    const auto &node_nested_if = **ptr_nested_if;

    EXPECT_EQ(
        describe(node_nested_if.condition),
        "Num(1)");
}

TEST(ExprParserTest, ParsesForStatement)
{
    auto lexed = lang::tokenize(
        "for i in 0 to 3 { var x:f64 = i; x; }");

    const auto &tokens =
        std::get<std::vector<lang::Token>>(lexed);

    auto parsed = lang::statements(tokens);

    const auto *statements =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1u);

    const auto *loop_ptr =
        std::get_if<std::unique_ptr<lang::ForStatement>>(
            &(*statements)[0]);

    ASSERT_NE(loop_ptr, nullptr);
    ASSERT_NE(loop_ptr->get(), nullptr);

    const auto &loop = **loop_ptr;

    EXPECT_EQ(loop.loop_var, "i");
    EXPECT_EQ(describe(loop.start), "Num(0)");
    EXPECT_EQ(describe(loop.end), "Num(3)");

    ASSERT_EQ(loop.body.size(), 2u);

    EXPECT_TRUE(
        std::holds_alternative<lang::VarDef>(
            loop.body[0]));

    EXPECT_TRUE(
        std::holds_alternative<lang::Expression>(
            loop.body[1]));
}

TEST(ExprParserTest, ParsesNestedForStatements)
{
    auto lexed = lang::tokenize(
        "for i in 0 to 2 {"
        "    for j in 0 to 3 {"
        "        i + j;"
        "    }"
        "}");

    const auto &tokens =
        std::get<std::vector<lang::Token>>(lexed);

    auto parsed = lang::statements(tokens);

    const auto *statements =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1u);

    const auto &outer =
        *std::get<std::unique_ptr<lang::ForStatement>>(
            (*statements)[0]);

    ASSERT_EQ(outer.body.size(), 1u);

    const auto &inner =
        *std::get<std::unique_ptr<lang::ForStatement>>(
            outer.body[0]);

    EXPECT_EQ(outer.loop_var, "i");
    EXPECT_EQ(inner.loop_var, "j");

    ASSERT_EQ(inner.body.size(), 1u);

    EXPECT_TRUE(
        std::holds_alternative<lang::Expression>(
            inner.body[0]));
}

TEST(ExprParserTest, RejectsInvalidForStatements)
{
    for (std::string_view input : {
             "for in 0 to 3 { 1; }",
             "for i 0 to 3 { 1; }",
             "for i in 0 3 { 1; }",
             "for i in 0 to 3 1;",
             "for i in 0 to 3 { 1;",
             "for i in 0 to 3 { 1;; }",
             "for i in 0 to 3 { 1; }}",
             "for i in 0 to 3 { var = 1; }"})
    {
        auto lexed = lang::tokenize(input);

        const auto *tokens =
            std::get_if<std::vector<lang::Token>>(&lexed);

        ASSERT_NE(tokens, nullptr) << input;

        auto parsed = lang::statements(*tokens);

        EXPECT_TRUE(
            std::holds_alternative<lang::ParseError>(parsed))
            << input;
    }
}

TEST(ExprParserTest, PreservesForStatementAndBody)
{
    auto lexed = lang::tokenize(
        "var sum:f64 = 0;"
        "for i in 0 to 3 {"
        "    sum = sum + i;"
        "}");

    const auto &tokens =
        std::get<std::vector<lang::Token>>(lexed);

    auto parsed = lang::statements(tokens);

    const auto *stmts =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(stmts, nullptr);
    ASSERT_EQ(stmts->size(), 2u);

    const auto *loop =
        std::get_if<std::unique_ptr<lang::ForStatement>>(
            &(*stmts)[1]);

    ASSERT_NE(loop, nullptr);
    ASSERT_NE(loop->get(), nullptr);

    EXPECT_EQ((*loop)->loop_var, "i");
    EXPECT_EQ((*loop)->body.size(), 1u);
}

TEST(ExprParserTest, ParsesFunctionDefinition)
{
    auto lexed = lang::tokenize(
        "fn add(a:f64, b:f64)->f64 { a + b; }");

    const auto &tokens =
        std::get<std::vector<lang::Token>>(lexed);

    auto parsed = lang::statements(tokens);

    const auto *statements =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1u);

    const auto *ptr =
        std::get_if<std::unique_ptr<lang::FunctionDefinition>>(
            &(*statements)[0]);

    ASSERT_NE(ptr, nullptr);
    ASSERT_NE(ptr->get(), nullptr);

    const auto &definition = **ptr;

    EXPECT_EQ(definition.name, "add");

    ASSERT_EQ(definition.parameters.size(), 2u);
    EXPECT_EQ(definition.parameters[0].name, "a");
    EXPECT_EQ(definition.parameters[1].name, "b");

    ASSERT_EQ(definition.body.size(), 1u);

    const auto *expression =
        std::get_if<lang::Expression>(&definition.body[0]);

    ASSERT_NE(expression, nullptr);

    EXPECT_TRUE(
        std::holds_alternative<lang::ExprAdd>(
            expression->value));
}

TEST(ExprParserTest, RejectsInvalidFunctionDefinitions)
{
    for (std::string_view source : {
             "fn { 1; }",
             "fn f { 1; }",
             "fn f(a b) { a; }",
             "fn f(a,) { a; }",
             "fn f(a) a;",
             "fn f(a) { a }",
             "fn f(a) { a;",
             "fn f() {};"})
    {
        auto lexed = lang::tokenize(source);

        const auto *tokens =
            std::get_if<std::vector<lang::Token>>(&lexed);

        ASSERT_NE(tokens, nullptr) << source;

        auto parsed = lang::statements(*tokens);

        EXPECT_TRUE(
            std::holds_alternative<lang::ParseError>(parsed))
            << source;
    }
}

TEST(ExprParserTest, ParsesBreakAndContinue)
{
    auto lexed = lang::tokenize(
        "for i in 0 to 5 {"
        "    if i > 2 {"
        "        break;"
        "    }"
        "    continue;"
        "}");

    const auto &tokens =
        std::get<std::vector<lang::Token>>(lexed);

    auto parsed = lang::statements(tokens);

    const auto *statements =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1u);

    const auto *loop_ptr =
        std::get_if<std::unique_ptr<lang::ForStatement>>(
            &(*statements)[0]);

    ASSERT_NE(loop_ptr, nullptr);
    ASSERT_NE(loop_ptr->get(), nullptr);

    const auto &body = (*loop_ptr)->body;

    ASSERT_EQ(body.size(), 2u);

    const auto *if_ptr =
        std::get_if<std::unique_ptr<lang::IfStatement>>(
            &body[0]);

    ASSERT_NE(if_ptr, nullptr);
    ASSERT_NE(if_ptr->get(), nullptr);

    ASSERT_EQ((*if_ptr)->then_branch.size(), 1u);

    EXPECT_TRUE(
        std::holds_alternative<lang::BreakStatement>(
            (*if_ptr)->then_branch[0]));

    EXPECT_TRUE(
        std::holds_alternative<lang::ContinueStatement>(
            body[1]));
}

TEST(ExprParserTest, RejectsInvalidLoopControlSyntax)
{
    for (std::string_view source : {
             "for i in 0 to 3 { break }",
             "for i in 0 to 3 { continue }",
             "for i in 0 to 3 { break 10; }",
             "var x = break;"})
    {
        auto lexed = lang::tokenize(source);

        const auto *tokens =
            std::get_if<std::vector<lang::Token>>(&lexed);

        ASSERT_NE(tokens, nullptr) << source;

        auto parsed = lang::statements(*tokens);

        EXPECT_TRUE(
            std::holds_alternative<lang::ParseError>(parsed))
            << source;
    }
}

TEST(ExprParserTest, ParsesStringLiteral)
{
    auto result = parse_source(R"("hello")");

    const auto *expression =
        std::get_if<lang::Expression>(&result);

    ASSERT_NE(expression, nullptr);

    const auto *str =
        std::get_if<lang::ExprStrLiteral>(
            &expression->value);

    ASSERT_NE(str, nullptr);

    EXPECT_EQ(str->value, "hello");
    EXPECT_EQ(
        expression->span,
        (lang::SourceSpan{0, 7}));
}

TEST(ExprParserTest, ParsesVariableTypeDeclaration)
{
    auto lexed = lang::tokenize(
        "var x: i64 = i64(10);");

    const auto &tokens =
        std::get<std::vector<lang::Token>>(lexed);

    auto parsed = lang::statements(tokens);

    const auto *statements =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1u);

    const auto *definition =
        std::get_if<lang::VarDef>(&(*statements)[0]);

    ASSERT_NE(definition, nullptr);

    EXPECT_EQ(definition->name, "x");
    EXPECT_EQ(
        definition->type,
        lang::TypeDecl::I64);
}

TEST(ExprParserTest, ParsesFunctionTypeDeclaration)
{
    auto lexed = lang::tokenize(
        "fn add(a: f64, b: i64) -> f64 {"
        "    return a + b;"
        "}");

    const auto &tokens =
        std::get<std::vector<lang::Token>>(lexed);

    auto parsed = lang::statements(tokens);

    const auto *statements =
        std::get_if<lang::Statements>(&parsed);

    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1u);

    const auto *ptr =
        std::get_if<
            std::unique_ptr<lang::FunctionDefinition>>(&(*statements)[0]);

    ASSERT_NE(ptr, nullptr);
    ASSERT_NE(ptr->get(), nullptr);

    const auto &definition = **ptr;

    EXPECT_EQ(definition.name, "add");
    EXPECT_EQ(definition.return_type, lang::TypeDecl::F64);

    ASSERT_EQ(definition.parameters.size(), 2u);

    EXPECT_EQ(definition.parameters[0].name, "a");
    EXPECT_EQ(
        definition.parameters[0].type,
        lang::TypeDecl::F64);

    EXPECT_EQ(definition.parameters[1].name, "b");
    EXPECT_EQ(
        definition.parameters[1].type,
        lang::TypeDecl::I64);
}

TEST(ExprParserTest, RejectsInvalidTypeDeclarations)
{
    for (std::string_view source : {
             "var x = 10;",
             "var x: unknown = 10;",
             "var x: = 10;",
             "fn f(x) -> f64 { return x; }",
             "fn f(x: f64) { return x; }",
             "fn f() -> unknown { return 1; }"})
    {
        auto lexed = lang::tokenize(source);

        const auto *tokens =
            std::get_if<std::vector<lang::Token>>(&lexed);

        ASSERT_NE(tokens, nullptr) << source;

        auto parsed = lang::statements(*tokens);

        EXPECT_TRUE(
            std::holds_alternative<lang::ParseError>(parsed))
            << source;
    }
}