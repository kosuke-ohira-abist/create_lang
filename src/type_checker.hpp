#pragma once

#include "ast.hpp"

#include <optional>
#include <unordered_map>
#include <variant>

namespace lang
{
    class NativeFunctionRegistry;

    // 型検査で発生したエラーと、そのソース位置
    struct TypeCheckError
    {
        SourceSpan span;
        std::string message;
    };

    // 任意の具体型の値を受け付ける引数制約
    struct AnyArgument
    {
    };

    // 引数に要求する型
    using ParameterType = std::variant<TypeDecl, AnyArgument>;

    // 関数シグネチャ
    struct FunctionSignature
    {
        std::vector<ParameterType> parameters;
        TypeDecl return_type;
    };

    // 型検査中の変数・関数の型情報を保持する環境
    struct TypeCheckContext
    {
        // 変数名と型
        std::unordered_map<std::string_view, TypeDecl> variables;

        // 関数名とシグネチャ
        std::unordered_map<std::string_view, FunctionSignature> functions;

        // 親環境
        // 関数シグネチャの検索に使用
        const TypeCheckContext *parent = nullptr;

        // 現在の環境から変数の型を検索する
        // 現状の compiler は関数外の変数を参照する機能を実装していないため
        // 現在の環境のみを対象とした検索を行う
        std::optional<TypeDecl> find_variable(std::string_view name) const;

        // 現在の環境と親環境から関数を検索する
        const FunctionSignature *find_function(std::string_view name) const;
    };

    // 型推論の結果
    using TypeResult = std::variant<TypeDecl, TypeCheckError>;

    // 式の型を推論する
    TypeResult infer_expression_type(const Expression &expression,
                                     const TypeCheckContext &context);

    // 型の適合性を検査する
    TypeResult coerce_type(TypeDecl value,
                           TypeDecl target,
                           SourceSpan span);

    // 文の列を型検査する
    std::optional<TypeCheckError> check_statements_type(const Statements &statements,
                                                        TypeCheckContext &context,
                                                        const NativeFunctionRegistry *native_functions = nullptr);
}