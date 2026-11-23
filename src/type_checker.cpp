#include "type_checker.hpp"
#include "native_function.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <stdexcept>

namespace lang
{
    namespace
    {
        enum class BinaryOp
        {
            Add,
            Sub,
            Mul,
            Div,
            Lt,
            Gt
        };

        TypeResult binary_result_type(TypeDecl lhs,
                                      TypeDecl rhs,
                                      BinaryOp op,
                                      SourceSpan span)
        {
            // 文字列を含む演算
            const bool lhs_str = lhs == TypeDecl::Str;
            const bool rhs_str = rhs == TypeDecl::Str;

            if (lhs_str || rhs_str)
            {
                if (lhs_str && rhs_str)
                {
                    switch (op)
                    {
                    case BinaryOp::Add:
                        return TypeDecl::Str;

                    case BinaryOp::Lt:
                    case BinaryOp::Gt:
                        return TypeDecl::I64;

                    default:
                        break;
                    }
                }

                return TypeCheckError{span,
                                      "unsupported operands for binary operation"};
            }

            // 数値同士の演算
            const bool both_int = lhs == TypeDecl::I64 && rhs == TypeDecl::I64;

            switch (op)
            {
            case BinaryOp::Add:
            case BinaryOp::Sub:
            case BinaryOp::Mul:
            case BinaryOp::Div:
                return both_int ? TypeDecl::I64 : TypeDecl::F64;

            case BinaryOp::Lt:
            case BinaryOp::Gt:
                return TypeDecl::I64;
            }

            return TypeCheckError{span,
                                  "unknown binary operation"};
        }

        TypeResult binary_type(const Expression &lhs,
                               const Expression &rhs,
                               const TypeCheckContext &context,
                               BinaryOp op)
        {
            auto left_result = infer_expression_type(lhs, context);

            if (const auto *error = std::get_if<TypeCheckError>(&left_result))
            {
                return *error;
            }

            auto right_result = infer_expression_type(rhs, context);

            if (const auto *error = std::get_if<TypeCheckError>(&right_result))
            {
                return *error;
            }

            return binary_result_type(std::get<TypeDecl>(left_result),
                                      std::get<TypeDecl>(right_result),
                                      op,
                                      SourceSpan{lhs.span.begin, rhs.span.end});
        }

        void register_builtins(TypeCheckContext &ctx)
        {
            for (auto name : {"sqrt", "sin", "cos", "tan",
                              "asin", "acos", "atan",
                              "exp", "log10"})
            {
                ctx.functions.try_emplace(name,
                                          FunctionSignature{{TypeDecl::F64},
                                                            TypeDecl::F64});
            }

            for (auto name : {"atan2", "pow", "log"})
            {
                ctx.functions.try_emplace(name,
                                          FunctionSignature{{TypeDecl::F64, TypeDecl::F64},
                                                            TypeDecl::F64});
            }

            ctx.functions.try_emplace("i64",
                                      FunctionSignature{{AnyArgument{}}, TypeDecl::I64});

            ctx.functions.try_emplace("f64",
                                      FunctionSignature{{AnyArgument{}}, TypeDecl::F64});

            ctx.functions.try_emplace("str",
                                      FunctionSignature{{AnyArgument{}}, TypeDecl::Str});

            // print は可変数引数
            ctx.functions.try_emplace("print",
                                      FunctionSignature{{AnyArgument{}}, TypeDecl::F64});
        }

        // 引数制約を検査する。AnyArgument は任意の具体型を受け付ける
        TypeResult check_argument_type(TypeDecl actual,
                                       const ParameterType &expected,
                                       SourceSpan span)
        {
            if (std::holds_alternative<AnyArgument>(expected))
            {
                return actual;
            }

            return coerce_type(actual, std::get<TypeDecl>(expected), span);
        }

        struct StatementsFlow
        {
            // すべての実行経路で return に到達するか
            bool always_returns = false;

            // 通常どおり関数末尾まで到達した場合に、暗黙的な戻り値となる式の型があるか
            std::optional<TypeDecl> final_expression_type;
        };

        using StatementsResult = std::variant<StatementsFlow, TypeCheckError>;

        StatementsResult check_statements_type_impl(const Statements &statements,
                                                    TypeCheckContext &context,
                                                    std::optional<TypeDecl> function_return_type)
        {
            StatementsFlow flow;

            for (const auto &statement : statements)
            {
                // 戻り値が確定していなければ、直前の式文の結果は引き継がない
                if (!flow.always_returns)
                {
                    flow.final_expression_type.reset();
                }

                // var def
                if (const auto *definition = std::get_if<VarDef>(&statement))
                {
                    auto type = infer_expression_type(definition->initializer, context);

                    if (const auto *error = std::get_if<TypeCheckError>(&type))
                    {
                        return *error;
                    }

                    auto checked = coerce_type(std::get<TypeDecl>(type),
                                               definition->type,
                                               definition->initializer.span);

                    if (const auto *error = std::get_if<TypeCheckError>(&checked))
                    {
                        return *error;
                    }

                    context.variables.insert_or_assign(definition->name,
                                                       std::get<TypeDecl>(checked));

                    continue;
                }

                // var assign
                if (const auto *assignment = std::get_if<VarAssign>(&statement))
                {
                    auto current = context.find_variable(assignment->name);

                    if (!current)
                    {
                        return TypeCheckError{assignment->name_span,
                                              "unknown variable: " + std::string(assignment->name)};
                    }

                    auto type = infer_expression_type(assignment->value, context);

                    if (const auto *error = std::get_if<TypeCheckError>(&type))
                    {
                        return *error;
                    }

                    auto checked = coerce_type(std::get<TypeDecl>(type),
                                               *current,
                                               assignment->value.span);

                    if (const auto *error = std::get_if<TypeCheckError>(&checked))
                    {
                        return *error;
                    }

                    continue;
                }

                // function definition
                if (const auto *func_ptr = std::get_if<std::unique_ptr<FunctionDefinition>>(&statement))
                {
                    const auto &func = **func_ptr;

                    FunctionSignature signature{{}, func.return_type};

                    for (const auto &param : func.parameters)
                    {
                        signature.parameters.push_back(param.type);
                    }

                    // 組み込み関数と既存のユーザー定義関数の上書きを禁止する
                    if (context.find_function(func.name))
                    {
                        return TypeCheckError{func.name_span,
                                              "duplicate or reserved function: " + std::string(func.name)};
                    }

                    // 再帰呼び出しの型検査のために、関数本体の検査前に登録する
                    context.functions.insert_or_assign(func.name, std::move(signature));

                    TypeCheckContext local_context;
                    local_context.parent = &context;

                    for (const auto &param : func.parameters)
                    {

                        if (!local_context.variables.try_emplace(param.name, param.type).second)
                        {
                            return TypeCheckError{func.name_span,
                                                  "duplicate parameter: " + std::string(param.name)};
                        }
                    }

                    auto result = check_statements_type_impl(func.body, local_context, func.return_type);

                    if (const auto *error = std::get_if<TypeCheckError>(&result))
                    {
                        return *error;
                    }

                    const auto &body_flow = std::get<StatementsFlow>(result);

                    if (!body_flow.always_returns)
                    {
                        if (!body_flow.final_expression_type)
                        {
                            return TypeCheckError{func.name_span,
                                                  "function may complete without a return value: " + std::string(func.name)};
                        }

                        auto checked = coerce_type(*body_flow.final_expression_type,
                                                   func.return_type,
                                                   func.name_span);

                        if (const auto *error = std::get_if<TypeCheckError>(&checked))
                        {
                            return *error;
                        }
                    }

                    continue;
                }

                // if
                if (const auto *if_ptr = std::get_if<std::unique_ptr<IfStatement>>(&statement))
                {
                    const auto &if_ = **if_ptr;

                    auto cond_type = infer_expression_type(if_.condition, context);

                    if (const auto *error = std::get_if<TypeCheckError>(&cond_type))
                    {
                        return *error;
                    }

                    const auto condition_type = std::get<TypeDecl>(cond_type);

                    if (condition_type != TypeDecl::I64 && condition_type != TypeDecl::F64)
                    {
                        return TypeCheckError{if_.condition.span,
                                              "condition must be numeric"};
                    }

                    // 両方のブランチで別々でチェックするためコピー
                    TypeCheckContext then_context = context;

                    auto then_result = check_statements_type_impl(if_.then_branch,
                                                                  then_context,
                                                                  function_return_type);

                    if (const auto *error = std::get_if<TypeCheckError>(&then_result))
                    {
                        return *error;
                    }

                    bool else_always_returns = false;

                    if (if_.else_branch.has_value())
                    {
                        // then_context と同様にコピー
                        TypeCheckContext else_context = context;

                        auto else_result = check_statements_type_impl(*if_.else_branch,
                                                                      else_context,
                                                                      function_return_type);

                        if (const auto *error = std::get_if<TypeCheckError>(&else_result))
                        {
                            return *error;
                        }

                        else_always_returns = std::get<StatementsFlow>(else_result).always_returns;
                    }

                    // 両方の分岐が必ず return する場合に限り、if 全体も戻り値を保証する
                    if (std::get<StatementsFlow>(then_result).always_returns && else_always_returns)
                    {
                        flow.always_returns = true;
                    }

                    continue;
                }

                // for
                if (const auto *for_loop_ptr = std::get_if<std::unique_ptr<ForStatement>>(&statement))
                {
                    const auto &for_loop = **for_loop_ptr;

                    auto start_type = infer_expression_type(for_loop.start, context);

                    if (const auto *error = std::get_if<TypeCheckError>(&start_type))
                    {
                        return *error;
                    }

                    auto checked_start = coerce_type(std::get<TypeDecl>(start_type),
                                                     TypeDecl::F64,
                                                     for_loop.start.span);
                    if (const auto *error = std::get_if<TypeCheckError>(&checked_start))
                    {
                        return *error;
                    }

                    auto end_type = infer_expression_type(for_loop.end, context);

                    if (const auto *error = std::get_if<TypeCheckError>(&end_type))
                    {
                        return *error;
                    }

                    auto checked_end = coerce_type(std::get<TypeDecl>(end_type),
                                                   TypeDecl::F64,
                                                   for_loop.end.span);

                    if (const auto *error = std::get_if<TypeCheckError>(&checked_end))
                    {
                        return *error;
                    }

                    TypeCheckContext loop_context = context;

                    loop_context.variables.insert_or_assign(for_loop.loop_var, TypeDecl::F64);

                    auto body_type = check_statements_type_impl(for_loop.body,
                                                                loop_context,
                                                                function_return_type);

                    if (const auto *error = std::get_if<TypeCheckError>(&body_type))
                    {
                        return *error;
                    }

                    continue;
                }

                // return
                if (const auto *ret = std::get_if<ReturnStatement>(&statement))
                {
                    auto type = infer_expression_type(ret->value, context);

                    if (const auto *error = std::get_if<TypeCheckError>(&type))
                    {
                        return *error;
                    }

                    // function 内であれば返り値の型検査
                    if (function_return_type)
                    {
                        auto checked = coerce_type(std::get<TypeDecl>(type),
                                                   *function_return_type,
                                                   ret->value.span);

                        if (const auto *error = std::get_if<TypeCheckError>(&checked))
                        {
                            return *error;
                        }
                    }

                    // return が見つかっても後続の処理を検査
                    flow.always_returns = true;
                    continue;
                }

                // break
                if (std::holds_alternative<BreakStatement>(statement))
                {
                    // break のあとも型検査が必要
                    continue;
                }

                // continue
                if (std::holds_alternative<ContinueStatement>(statement))
                {
                    continue;
                }

                // 式文
                const auto &expression = std::get<Expression>(statement);

                auto type = infer_expression_type(expression, context);

                if (const auto *error = std::get_if<TypeCheckError>(&type))
                {
                    return *error;
                }

                if (!flow.always_returns)
                {
                    flow.final_expression_type = std::get<TypeDecl>(type);
                }
            }

            return flow;
        }

    }

    std::optional<TypeDecl> TypeCheckContext::find_variable(std::string_view name) const
    {
        const auto it = variables.find(name);

        if (it != variables.end())
        {
            return it->second;
        }

        return std::nullopt;
    }

    const FunctionSignature *TypeCheckContext::find_function(std::string_view name) const
    {
        for (const TypeCheckContext *current = this; current; current = current->parent)
        {
            const auto it = current->functions.find(name);

            if (it != current->functions.end())
            {
                return &it->second;
            }
        }

        return nullptr;
    }

    TypeResult infer_expression_type(const Expression &expression,
                                     const TypeCheckContext &context)
    {
        // num literal
        if (std::holds_alternative<ExprNumLiteral>(expression.value))
        {
            return TypeDecl::F64;
        }

        // str literal
        if (std::holds_alternative<ExprStrLiteral>(expression.value))
        {
            return TypeDecl::Str;
        }

        // identifier か builtin 定数
        if (const auto *id = std::get_if<ExprIdent>(&expression.value))
        {
            // builtin 定数
            if (id->text == "PI")
            {
                return TypeDecl::F64;
            }

            if (auto type = context.find_variable(id->text))
            {
                return *type;
            }

            return TypeCheckError{expression.span,
                                  "unknown variable: " + std::string(id->text)};
        }

        // function
        if (const auto *func_ptr = std::get_if<ExprFnInvoke>(&expression.value))
        {
            const auto *signature = context.find_function(func_ptr->name);

            if (!signature)
            {
                return TypeCheckError{expression.span,
                                      "unknown function: " + std::string(func_ptr->name)};
            }

            // print は可変数引数のため特例
            if (func_ptr->name == "print")
            {
                for (const auto &arg : func_ptr->args)
                {
                    const auto type = infer_expression_type(arg, context);

                    if (const auto *error = std::get_if<TypeCheckError>(&type))
                    {
                        return *error;
                    }
                }

                return TypeDecl::F64;
            }

            if (func_ptr->args.size() != signature->parameters.size())
            {
                return TypeCheckError{expression.span,
                                      "wrong argument count for function: " + std::string(func_ptr->name)};
            }

            for (std::size_t i = 0; i < func_ptr->args.size(); ++i)
            {
                auto type = infer_expression_type(func_ptr->args[i], context);

                if (const auto *error = std::get_if<TypeCheckError>(&type))
                {
                    return *error;
                }

                const auto argument_type = std::get<TypeDecl>(type);

                if ((func_ptr->name == "i64" || func_ptr->name == "f64") &&
                    argument_type != TypeDecl::I64 && argument_type != TypeDecl::F64)
                {
                    return TypeCheckError{func_ptr->args[i].span,
                                          "numeric argument required for " + std::string(func_ptr->name)};
                }

                auto checked = check_argument_type(argument_type,
                                                   signature->parameters[i],
                                                   func_ptr->args[i].span);

                if (const auto *error = std::get_if<TypeCheckError>(&checked))
                {
                    return *error;
                }
            }

            return signature->return_type;
        }

        // +
        if (const auto *expr_add = std::get_if<ExprAdd>(&expression.value))
        {
            return binary_type(*expr_add->lhs,
                               *expr_add->rhs,
                               context,
                               BinaryOp::Add);
        }

        // -
        if (const auto *expr_sub = std::get_if<ExprSub>(&expression.value))
        {
            return binary_type(*expr_sub->lhs,
                               *expr_sub->rhs,
                               context,
                               BinaryOp::Sub);
        }

        // *
        if (const auto *expr_mul = std::get_if<ExprMul>(&expression.value))
        {
            return binary_type(*expr_mul->lhs,
                               *expr_mul->rhs,
                               context,
                               BinaryOp::Mul);
        }

        // /
        if (const auto *expr_div = std::get_if<ExprDiv>(&expression.value))
        {
            return binary_type(*expr_div->lhs,
                               *expr_div->rhs,
                               context,
                               BinaryOp::Div);
        }

        // <
        if (const auto *lt = std::get_if<ExprLt>(&expression.value))
        {
            return binary_type(*lt->lhs,
                               *lt->rhs,
                               context,
                               BinaryOp::Lt);
        }

        // >
        if (const auto *gt = std::get_if<ExprGt>(&expression.value))
        {
            return binary_type(*gt->lhs,
                               *gt->rhs,
                               context,
                               BinaryOp::Gt);
        }

        return TypeCheckError{expression.span,
                              "type checking for this expression is not implemented yet"};
    }

    TypeResult coerce_type(TypeDecl value,
                           TypeDecl target,
                           SourceSpan span)
    {
        // 暗黙的な型変換は行わないため、型の一致を要求する
        if (value == target)
        {
            return target;
        }

        return TypeCheckError{span, "incompatible types"};
    }

    std::optional<TypeCheckError> check_statements_type(const Statements &statements,
                                                        TypeCheckContext &context,
                                                        const NativeFunctionRegistry *native_functions)
    {
        // 既存の組み込み関数
        register_builtins(context);

        // ホストアプリケーションが登録した関数の型情報
        if (native_functions)
        {
            for (const auto &[name, function] : native_functions->functions())
            {
                if (!context.functions.try_emplace(std::string_view{name},
                                                   function.signature)
                         .second)
                {
                    throw std::invalid_argument("Native function conflicts with an existing function: " + name);
                }
            }
        }

        auto result = check_statements_type_impl(statements,
                                                 context,
                                                 std::nullopt);

        if (const auto *error = std::get_if<TypeCheckError>(&result))
        {
            return *error;
        }

        return std::nullopt;
    }
}