#include "tokenizer.hpp"

#include <gtest/gtest.h>

#include <variant>
#include <vector>

using lang::LexError;
using lang::SourceSpan;
using lang::Token;

TEST(TokenizerTest, IdentifierStoresTextAndPosition)
{
    auto result = lang::tokenize("  foo12+3");

    ASSERT_TRUE(
        std::holds_alternative<std::vector<Token>>(result));

    const auto &tokens =
        std::get<std::vector<Token>>(result);

    ASSERT_EQ(tokens.size(), 4u);

    const auto *ident =
        std::get_if<lang::Ident>(&tokens[0].value);

    ASSERT_TRUE(ident != nullptr);
    EXPECT_EQ(ident->text, "foo12");
    EXPECT_EQ(tokens[0].span, (SourceSpan{2, 7}));

    EXPECT_TRUE(
        std::holds_alternative<lang::Plus>(tokens[1].value));
    EXPECT_EQ(tokens[1].span, (SourceSpan{7, 8}));

    const auto *number =
        std::get_if<lang::Number>(&tokens[2].value);

    ASSERT_TRUE(number != nullptr);
    EXPECT_DOUBLE_EQ(number->value, 3.0);
    EXPECT_EQ(tokens[2].span, (SourceSpan{8, 9}));

    EXPECT_TRUE(
        std::holds_alternative<lang::End>(tokens[3].value));
    EXPECT_EQ(tokens[3].span, (SourceSpan{9, 9}));
}

TEST(TokenizerTest, SignsRemainSeparateTokens)
{
    auto result = lang::tokenize("1+-2");

    ASSERT_TRUE(
        std::holds_alternative<std::vector<Token>>(result));

    const auto &tokens =
        std::get<std::vector<Token>>(result);

    ASSERT_EQ(tokens.size(), 5u);

    EXPECT_TRUE(
        std::holds_alternative<lang::Number>(tokens[0].value));
    EXPECT_TRUE(
        std::holds_alternative<lang::Plus>(tokens[1].value));
    EXPECT_TRUE(
        std::holds_alternative<lang::Minus>(tokens[2].value));
    EXPECT_TRUE(
        std::holds_alternative<lang::Number>(tokens[3].value));
    EXPECT_TRUE(
        std::holds_alternative<lang::End>(tokens[4].value));
}

TEST(TokenizerTest, ParenthesesRemainTokens)
{
    auto result = lang::tokenize("(12)");

    ASSERT_TRUE(
        std::holds_alternative<std::vector<Token>>(result));

    const auto &tokens =
        std::get<std::vector<Token>>(result);

    ASSERT_EQ(tokens.size(), 4u);

    EXPECT_TRUE(
        std::holds_alternative<lang::LParen>(tokens[0].value));
    EXPECT_TRUE(
        std::holds_alternative<lang::Number>(tokens[1].value));
    EXPECT_TRUE(
        std::holds_alternative<lang::RParen>(tokens[2].value));
    EXPECT_TRUE(
        std::holds_alternative<lang::End>(tokens[3].value));
}

TEST(TokenizerTest, ConvertsDecimalLiterals)
{
    auto result = lang::tokenize(".5 12. 3.25");

    ASSERT_TRUE(
        std::holds_alternative<std::vector<Token>>(result));

    const auto &tokens =
        std::get<std::vector<Token>>(result);

    ASSERT_EQ(tokens.size(), 4u);

    EXPECT_DOUBLE_EQ(
        std::get<lang::Number>(tokens[0].value).value, 0.5);
    EXPECT_DOUBLE_EQ(
        std::get<lang::Number>(tokens[1].value).value, 12.0);
    EXPECT_DOUBLE_EQ(
        std::get<lang::Number>(tokens[2].value).value, 3.25);
}

TEST(TokenizerTest, InvalidNumberProducesLexError)
{
    auto result = lang::tokenize("12..3");

    ASSERT_TRUE(
        std::holds_alternative<LexError>(result));

    EXPECT_EQ(
        std::get<LexError>(result).span,
        (SourceSpan{3, 4}));
}

TEST(TokenizerTest, UnknownCharacterProducesLexError)
{
    auto result = lang::tokenize("  @abc");

    ASSERT_TRUE(
        std::holds_alternative<LexError>(result));

    EXPECT_EQ(
        std::get<LexError>(result).span,
        (SourceSpan{2, 3}));
}

TEST(TokenizerTest, WhitespaceProducesEndMarker)
{
    auto result = lang::tokenize("  \n\t");

    ASSERT_TRUE(
        std::holds_alternative<std::vector<Token>>(result));

    const auto &tokens =
        std::get<std::vector<Token>>(result);

    ASSERT_EQ(tokens.size(), 1u);

    EXPECT_TRUE(
        std::holds_alternative<lang::End>(tokens[0].value));
    EXPECT_EQ(tokens[0].span, (SourceSpan{4, 4}));
}

TEST(TokenizerTest, EmptySourceProducesEndMarker)
{
    auto result = lang::tokenize("");

    ASSERT_TRUE(
        std::holds_alternative<std::vector<Token>>(result));

    const auto &tokens =
        std::get<std::vector<Token>>(result);

    ASSERT_EQ(tokens.size(), 1u);

    EXPECT_TRUE(
        std::holds_alternative<lang::End>(tokens[0].value));
    EXPECT_EQ(tokens[0].span, (SourceSpan{0, 0}));
}

TEST(TokenizerTest, RecognizesUnderscoreIdentifiers)
{
    auto result = lang::tokenize("_foo foo_bar foo_42 _");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&result);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 5u);

    const std::string_view expected[] = {
        "_foo", "foo_bar", "foo_42", "_"};

    for (std::size_t i = 0; i < 4; ++i)
    {
        const auto *ident =
            std::get_if<lang::Ident>(&(*tokens)[i].value);

        ASSERT_NE(ident, nullptr);
        EXPECT_EQ(ident->text, expected[i]);
    }

    EXPECT_EQ(
        (*tokens)[0].span,
        (lang::SourceSpan{0, 4}));

    EXPECT_TRUE(
        std::holds_alternative<lang::End>(
            tokens->back().value));
}

TEST(TokenizerTest, RecognizesExponentNumbers)
{
    auto result = lang::tokenize("1e3 2.5E-2 .5e+1");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&result);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 4u);

    const auto *a =
        std::get_if<lang::Number>(&(*tokens)[0].value);
    const auto *b =
        std::get_if<lang::Number>(&(*tokens)[1].value);
    const auto *c =
        std::get_if<lang::Number>(&(*tokens)[2].value);

    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);

    EXPECT_DOUBLE_EQ(a->value, 1000.0);
    EXPECT_DOUBLE_EQ(b->value, 0.025);
    EXPECT_DOUBLE_EQ(c->value, 5.0);
}

TEST(TokenizerTest, RejectsIncompleteExponent)
{
    for (std::string_view input : {
             "1e", "1e+", "1e-"})
    {
        auto result = lang::tokenize(input);

        EXPECT_TRUE(
            std::holds_alternative<lang::LexError>(result))
            << "input: " << input;
    }

    auto result = lang::tokenize("1e+");

    const auto *error =
        std::get_if<lang::LexError>(&result);

    ASSERT_NE(error, nullptr);

    EXPECT_EQ(
        error->span,
        (lang::SourceSpan{1, 3}));
}

TEST(TokenizerTest, RecognizesArithmeticOperators)
{
    auto result = lang::tokenize("1-2*3/4");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&result);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 8u);

    EXPECT_TRUE(
        std::holds_alternative<lang::Number>(
            (*tokens)[0].value));
    EXPECT_TRUE(
        std::holds_alternative<lang::Minus>(
            (*tokens)[1].value));
    EXPECT_TRUE(
        std::holds_alternative<lang::Star>(
            (*tokens)[3].value));
    EXPECT_TRUE(
        std::holds_alternative<lang::Slash>(
            (*tokens)[5].value));
    EXPECT_TRUE(
        std::holds_alternative<lang::End>(
            (*tokens)[7].value));

    EXPECT_EQ(
        (*tokens)[3].span,
        (lang::SourceSpan{3, 4}));
    EXPECT_EQ(
        (*tokens)[5].span,
        (lang::SourceSpan{5, 6}));
}

TEST(TokenizerTest, ExponentSignIsPartOfNumber)
{
    auto result = lang::tokenize("1e-3*2");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&result);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 4u);

    const auto *number =
        std::get_if<lang::Number>(&(*tokens)[0].value);

    ASSERT_NE(number, nullptr);

    EXPECT_DOUBLE_EQ(number->value, 0.001);
    EXPECT_EQ(
        (*tokens)[0].span,
        (lang::SourceSpan{0, 4}));

    EXPECT_TRUE(
        std::holds_alternative<lang::Star>(
            (*tokens)[1].value));
}

TEST(TokenizerTest, RecognizesComma)
{
    auto result = lang::tokenize("pow(2,3)");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&result);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 7u);

    EXPECT_TRUE(
        std::holds_alternative<lang::Comma>(
            (*tokens)[3].value));

    EXPECT_EQ(
        (*tokens)[3].span,
        (lang::SourceSpan{5, 6}));
}

TEST(TokenizerTest, RecognizesSemicolon)
{
    auto result = lang::tokenize("1;2;");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&result);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 5u);

    EXPECT_TRUE(std::holds_alternative<lang::Number>((*tokens)[0].value));
    EXPECT_TRUE(std::holds_alternative<lang::Semicolon>((*tokens)[1].value));
    EXPECT_TRUE(std::holds_alternative<lang::Number>((*tokens)[2].value));
    EXPECT_TRUE(std::holds_alternative<lang::Semicolon>((*tokens)[3].value));
    EXPECT_TRUE(std::holds_alternative<lang::End>((*tokens)[4].value));

    EXPECT_EQ(
        (*tokens)[1].span,
        (lang::SourceSpan{1, 2}));
}

TEST(TokenizerTest, RecognizesVariableDefinition)
{
    auto result = lang::tokenize("var x=10; x");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&result);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 7u);

    EXPECT_TRUE(std::holds_alternative<lang::Var>(
        (*tokens)[0].value));

    const auto *name =
        std::get_if<lang::Ident>(&(*tokens)[1].value);

    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->text, "x");

    EXPECT_TRUE(std::holds_alternative<lang::Equal>(
        (*tokens)[2].value));

    EXPECT_EQ(
        (*tokens)[2].span,
        (lang::SourceSpan{5, 6}));

    EXPECT_TRUE(std::holds_alternative<lang::Number>(
        (*tokens)[3].value));

    EXPECT_TRUE(std::holds_alternative<lang::Semicolon>(
        (*tokens)[4].value));

    EXPECT_TRUE(std::holds_alternative<lang::Ident>(
        (*tokens)[5].value));

    EXPECT_TRUE(std::holds_alternative<lang::End>(
        (*tokens)[6].value));
}

TEST(TokenizerTest, VarPrefixIsNotKeyword)
{
    auto result = lang::tokenize("variable var2");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&result);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 3u);

    EXPECT_EQ(
        std::get<lang::Ident>((*tokens)[0].value).text,
        "variable");

    EXPECT_EQ(
        std::get<lang::Ident>((*tokens)[1].value).text,
        "var2");
}

TEST(TokenizerTest, RecognizesIfElseAndBraces)
{
    auto result = lang::tokenize("if x { 1 } else { 2 }");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&result);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 10u);

    EXPECT_TRUE(std::holds_alternative<lang::If>(
        (*tokens)[0].value));

    EXPECT_TRUE(std::holds_alternative<lang::Ident>(
        (*tokens)[1].value));

    EXPECT_TRUE(std::holds_alternative<lang::LBrace>(
        (*tokens)[2].value));

    EXPECT_TRUE(std::holds_alternative<lang::RBrace>(
        (*tokens)[4].value));

    EXPECT_TRUE(std::holds_alternative<lang::Else>(
        (*tokens)[5].value));

    EXPECT_TRUE(std::holds_alternative<lang::LBrace>(
        (*tokens)[6].value));

    EXPECT_TRUE(std::holds_alternative<lang::RBrace>(
        (*tokens)[8].value));

    EXPECT_TRUE(std::holds_alternative<lang::End>(
        (*tokens)[9].value));
}

TEST(TokenizerTest, IfElsePrefixesRemainIdentifiers)
{
    auto result = lang::tokenize("if_value elsewhere");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&result);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 3u);

    EXPECT_EQ(
        std::get<lang::Ident>((*tokens)[0].value).text,
        "if_value");

    EXPECT_EQ(
        std::get<lang::Ident>((*tokens)[1].value).text,
        "elsewhere");
}

TEST(TokenizerTest, RecognizesForKeywords)
{
    auto result = lang::tokenize(
        "for i in 0 to 3 { i; }");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&result);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 11u);

    EXPECT_TRUE(std::holds_alternative<lang::For>(
        (*tokens)[0].value));

    EXPECT_EQ(
        std::get<lang::Ident>((*tokens)[1].value).text,
        "i");

    EXPECT_TRUE(std::holds_alternative<lang::In>(
        (*tokens)[2].value));

    EXPECT_TRUE(std::holds_alternative<lang::To>(
        (*tokens)[4].value));

    EXPECT_TRUE(std::holds_alternative<lang::LBrace>(
        (*tokens)[6].value));

    EXPECT_TRUE(std::holds_alternative<lang::RBrace>(
        (*tokens)[9].value));

    EXPECT_TRUE(std::holds_alternative<lang::End>(
        (*tokens)[10].value));
}

TEST(TokenizerTest, ForKeywordPrefixesAreIdentifiers)
{
    auto result = lang::tokenize(
        "format inside total");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&result);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 4u);

    EXPECT_EQ(
        std::get<lang::Ident>((*tokens)[0].value).text,
        "format");

    EXPECT_EQ(
        std::get<lang::Ident>((*tokens)[1].value).text,
        "inside");

    EXPECT_EQ(
        std::get<lang::Ident>((*tokens)[2].value).text,
        "total");
}

TEST(TokenizerTest, RecognizesFunctionAndComparisonTokens)
{
    auto lexed = lang::tokenize("fn f(x) { x < 10; }");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&lexed);

    ASSERT_NE(tokens, nullptr);

    EXPECT_TRUE(std::holds_alternative<lang::Fn>(
        (*tokens)[0].value));

    EXPECT_TRUE(std::holds_alternative<lang::Less>(
        (*tokens)[7].value));
}

TEST(TokenizerTest, FunctionKeywordPrefixIsIdentifier)
{
    auto lexed = lang::tokenize("function fn_value");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&lexed);

    ASSERT_NE(tokens, nullptr);

    EXPECT_EQ(
        std::get<lang::Ident>((*tokens)[0].value).text,
        "function");

    EXPECT_EQ(
        std::get<lang::Ident>((*tokens)[1].value).text,
        "fn_value");
}

TEST(TokenizerTest, RecognizesBreakAndContinue)
{
    auto result = lang::tokenize(
        "break; continue;");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&result);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 5u);

    EXPECT_TRUE(std::holds_alternative<lang::Break>(
        (*tokens)[0].value));

    EXPECT_TRUE(std::holds_alternative<lang::Semicolon>(
        (*tokens)[1].value));

    EXPECT_TRUE(std::holds_alternative<lang::Continue>(
        (*tokens)[2].value));

    EXPECT_TRUE(std::holds_alternative<lang::Semicolon>(
        (*tokens)[3].value));

    EXPECT_TRUE(std::holds_alternative<lang::End>(
        (*tokens)[4].value));
}

TEST(TokenizerTest, RecognizesStringLiteral)
{
    auto lexed = lang::tokenize(R"("hello\nworld")");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&lexed);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 2u);

    const auto *str =
        std::get_if<lang::StringLiteral>(
            &(*tokens)[0].value);

    ASSERT_NE(str, nullptr);

    EXPECT_EQ(str->value, "hello\nworld");
}

TEST(TokenizerTest, RejectsUnterminatedString)
{
    auto lexed = lang::tokenize("\"hello");

    EXPECT_TRUE(
        std::holds_alternative<lang::LexError>(lexed));
}

TEST(TokenizerTest, RecognizesTypeDeclarationPunctuation)
{
    auto lexed = lang::tokenize("x: f64 -> str");

    const auto *tokens =
        std::get_if<std::vector<lang::Token>>(&lexed);

    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(tokens->size(), 6u);

    EXPECT_TRUE(std::holds_alternative<lang::Colon>(
        (*tokens)[1].value));

    EXPECT_TRUE(std::holds_alternative<lang::Arrow>(
        (*tokens)[3].value));

    EXPECT_EQ(
        std::get<lang::Ident>((*tokens)[2].value).text,
        "f64");

    EXPECT_EQ(
        std::get<lang::Ident>((*tokens)[4].value).text,
        "str");
}