
#pragma once

#include "byte_code.hpp"
#include "type_checker.hpp"

#include <optional>
#include <span>
#include <unordered_map>
#include <functional>

namespace lang
{
    // ホストアプリケーションが提供する関数
    struct NativeFunction
    {
        FunctionSignature signature;

        // 実際に呼び出すC++関数
        std::function<VmValue(std::span<const VmValue>)> callback;
    };

    // ネイティブ関数の型情報と実装を管理する
    class NativeFunctionRegistry
    {
    public:
        using Callback = std::function<VmValue(std::span<const VmValue>)>;

        // ネイティブ関数を登録する
        void register_function(std::string name,
                               FunctionSignature signature,
                               Callback callback);

        const std::unordered_map<std::string, NativeFunction> &functions() const
        {
            return m_functions;
        }

        // 登録されたC++関数を呼び出す
        std::optional<VmValue> invoke(std::string_view name,
                                      std::span<const VmValue> args) const;

    private:
        std::unordered_map<std::string, NativeFunction> m_functions;
    };
}
