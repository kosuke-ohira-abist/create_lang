#pragma once

#include "token.hpp"

#include <memory>
#include <string_view>
#include <variant>
#include <vector>
#include <optional>
#include <string>

namespace lang
{
    // 扱う型の種類
    enum class TypeDecl
    {
        F64,
        I64,
        Str
    };

    struct Expression;

    struct ExprIdent
    {
        std::string_view text;
    };

    struct ExprNumLiteral
    {
        double value;
    };

    struct ExprStrLiteral
    {
        std::string value;
    };

    struct ExprFnInvoke
    {
        std::string_view name;
        std::vector<Expression> args;
    };

    struct ExprAdd
    {
        std::unique_ptr<Expression> lhs;
        std::unique_ptr<Expression> rhs;
    };

    struct ExprSub
    {
        std::unique_ptr<Expression> lhs;
        std::unique_ptr<Expression> rhs;
    };

    struct ExprMul
    {
        std::unique_ptr<Expression> lhs;
        std::unique_ptr<Expression> rhs;
    };

    struct ExprDiv
    {
        std::unique_ptr<Expression> lhs;
        std::unique_ptr<Expression> rhs;
    };

    struct ExprLt
    {
        std::unique_ptr<Expression> lhs;
        std::unique_ptr<Expression> rhs;
    };

    struct ExprGt
    {
        std::unique_ptr<Expression> lhs;
        std::unique_ptr<Expression> rhs;
    };

    // 式
    struct Expression
    {
        // 式の種類
        std::variant<
            ExprIdent,
            ExprNumLiteral,
            ExprStrLiteral,
            ExprFnInvoke,
            ExprAdd,
            ExprSub,
            ExprMul,
            ExprDiv,
            ExprLt,
            ExprGt>
            value;

        // ソースコード上の範囲
        SourceSpan span;
    };

    struct VarDef
    {
        std::string_view name;

        // 型宣言
        TypeDecl type;

        // 初期化式
        Expression initializer;
    };

    struct VarAssign
    {
        std::string_view name;
        Expression value;
        SourceSpan name_span;
    };

    struct ReturnStatement
    {
        Expression value;
    };
    struct BreakStatement
    {
        SourceSpan span;
    };
    struct ContinueStatement
    {
        SourceSpan span;
    };

    struct IfStatement;
    struct ForStatement;
    struct FunctionDefinition;

    // 文の種類
    using Statement = std::variant<Expression,
                                   VarDef,
                                   VarAssign,
                                   ReturnStatement,
                                   BreakStatement,
                                   ContinueStatement,
                                   std::unique_ptr<IfStatement>,
                                   std::unique_ptr<ForStatement>,
                                   std::unique_ptr<FunctionDefinition>>;

    // 順番に実行される文の列
    using Statements = std::vector<Statement>;

    struct IfStatement
    {
        Expression condition;
        Statements then_branch;
        std::optional<Statements> else_branch;
    };

    struct ForStatement
    {
        std::string_view loop_var;
        SourceSpan loop_var_span;

        Expression start;
        Expression end;

        Statements body;
    };

    struct Parameter
    {
        std::string_view name;
        TypeDecl type;
    };

    struct FunctionDefinition
    {
        std::string_view name;
        SourceSpan name_span;

        std::vector<Parameter> parameters;
        TypeDecl return_type;

        Statements body;
    };
}