
#include "native_function.hpp"

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace lang
{
    namespace
    {
        TypeDecl value_type(const VmValue &value)
        {
            if (std::holds_alternative<double>(value))
            {
                return TypeDecl::F64;
            }

            if (std::holds_alternative<std::int64_t>(value))
            {
                return TypeDecl::I64;
            }

            return TypeDecl::Str;
        }

        // 引数の型制約
        bool matches(const ParameterType &expected, const VmValue &actual)
        {
            // AnyArgument は型を問わず受け付ける
            if (std::holds_alternative<AnyArgument>(expected))
            {
                return true;
            }

            return std::get<TypeDecl>(expected) == value_type(actual);
        }
    }

    void NativeFunctionRegistry::register_function(std::string name,
                                                   FunctionSignature signature,
                                                   Callback callback)
    {
        if (name.empty() || !callback)
        {
            throw std::invalid_argument("Invalid native function registration");
        }

        // Call命令の引数数は8ビット
        if (signature.parameters.size() > std::numeric_limits<std::uint8_t>::max())
        {
            throw std::invalid_argument("Too many native function parameters");
        }

        const auto [it, inserted] = m_functions.try_emplace(std::move(name),
                                                            NativeFunction{std::move(signature),
                                                                           std::move(callback)});

        if (!inserted)
        {
            throw std::invalid_argument("Duplicate native function: " + it->first);
        }
    }

    std::optional<VmValue> NativeFunctionRegistry::invoke(std::string_view name,
                                                          std::span<const VmValue> args) const
    {
        const auto it = m_functions.find(std::string(name));

        if (it == m_functions.end())
        {
            return std::nullopt;
        }

        const auto &fn = it->second;

        if (args.size() != fn.signature.parameters.size())
        {
            throw std::runtime_error("Wrong native argument count: " + it->first);
        }

        // 引数型を検証
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (!matches(fn.signature.parameters[i], args[i]))
            {
                throw std::runtime_error("Wrong native argument type: " + it->first);
            }
        }

        // C++関数を実行
        auto result = fn.callback(args);

        // 戻り値型を検証
        if (value_type(result) != fn.signature.return_type)
        {
            throw std::runtime_error("Wrong native return type: " + it->first);
        }

        return result;
    }
}
