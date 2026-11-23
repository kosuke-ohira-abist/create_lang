#include "byte_code.hpp"
#include "diagnostic.hpp"
#include "expr_parser.hpp"
#include "native_function.hpp"
#include "tokenizer.hpp"
#include "type_checker.hpp"

#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#ifdef ENABLE_HOST_API
namespace lang
{
    // ホストアプリケーションが提供する関数を登録する
    void register_host_functions(NativeFunctionRegistry &registry);
}
#endif

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: lang \"program\"\n";
        return 1;
    }

    const std::string source = argv[1];

    const auto print_result = [](const std::optional<lang::VmValue> &result)
    {
        std::cout << "result: ";

        if (!result)
        {
            std::cout << "None\n";
            return;
        }

        std::cout << "Some(";
        std::visit(
            [](const auto &value)
            {
                std::cout << value;
            },
            *result);
        std::cout << ")\n";
    };

    try
    {
        lang::NativeFunctionRegistry registry;

#ifdef ENABLE_HOST_API
        lang::register_host_functions(registry);
#endif

        // Lexer
        auto lexed = lang::tokenize(source);
        if (const auto *error = std::get_if<lang::LexError>(&lexed))
        {
            std::cerr << lang::format_diagnostic(
                source, "Lex error", error->span, error->message);
            return 1;
        }

        const auto &tokens = std::get<std::vector<lang::Token>>(lexed);

        // Parser
        auto parsed = lang::statements(tokens);
        if (const auto *error = std::get_if<lang::ParseError>(&parsed))
        {
            std::cerr << lang::format_diagnostic(
                source, "Parse error", error->span, error->message);
            return 1;
        }

        // Type Checker
        const auto &program = std::get<lang::Statements>(parsed);
        lang::TypeCheckContext context;
        if (const auto error = lang::check_statements_type(program, context, &registry))
        {
            std::cerr << lang::format_diagnostic(
                source, "Type error", error->span, error->message);
            return 1;
        }

        // Compiler
        auto compiled = lang::compile_program_checked(program);
        if (const auto *error = std::get_if<lang::CompileError>(&compiled))
        {
            std::cerr << lang::format_diagnostic(
                source, "Compile error", error->span, error->what());
            return 1;
        }

        lang::Compiler compiler = std::get<lang::Compiler>(std::move(compiled));

        // バイトコードのメモリ上での保存・復元
        std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);
        compiler.write_funcs(buffer);
        buffer.seekg(0);

        // VM
        lang::Vm vm;
        vm.read_funcs(buffer);
        vm.set_native_functions(registry);
        print_result(vm.interpret());

        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
