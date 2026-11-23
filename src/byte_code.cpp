#include "byte_code.hpp"
#include "native_function.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <numbers>
#include <fstream>
#include <iostream>
#include <sstream>

namespace lang
{
    // double を IEEE 754 のビット表現として保存する前提を保証するため
    static_assert(std::numeric_limits<double>::is_iec559);

    namespace
    {
        // バイナリに書き込む型情報の値
        enum class ValueKind : std::uint8_t
        {
            F64 = 0,
            I64 = 1,
            Str = 2
        };

        template <class... Fs>
        struct Overloaded : Fs...
        {
            using Fs::operator()...;
        };

        using UnaryFn = double (*)(double);
        using BinaryFn = double (*)(double, double);

        struct BuiltinFunction
        {
            std::string_view name;
            std::variant<UnaryFn, BinaryFn> fn;
        };

        // builtin function
        static const BuiltinFunction BUILTIN_FUNCTIONS[] = {
            {"sqrt", UnaryFn{[](double x)
                             { return std::sqrt(x); }}},
            {"sin", UnaryFn{[](double x)
                            { return std::sin(x); }}},
            {"cos", UnaryFn{[](double x)
                            { return std::cos(x); }}},
            {"tan", UnaryFn{[](double x)
                            { return std::tan(x); }}},
            {"asin", UnaryFn{[](double x)
                             { return std::asin(x); }}},
            {"acos", UnaryFn{[](double x)
                             { return std::acos(x); }}},
            {"atan", UnaryFn{[](double x)
                             { return std::atan(x); }}},
            {"atan2", BinaryFn{[](double y, double x)
                               {
                                   return std::atan2(y, x);
                               }}},
            {"pow", BinaryFn{[](double x, double y)
                             {
                                 return std::pow(x, y);
                             }}},
            {"exp", UnaryFn{[](double x)
                            { return std::exp(x); }}},
            {"log", BinaryFn{[](double x, double base)
                             {
                                 return std::log(x) / std::log(base);
                             }}},
            {"log10", UnaryFn{[](double x)
                              {
                                  return std::log10(x);
                              }}},
        };

        std::uint8_t checked_u8(std::size_t value)
        {
            if (value > std::numeric_limits<std::uint8_t>::max())
            {
                throw std::runtime_error("8-bit operand overflow");
            }

            return static_cast<std::uint8_t>(value);
        }

        double as_number(const VmValue &value)
        {
            if (const auto *number = std::get_if<double>(&value))
            {
                return *number;
            }

            if (const auto *v = std::get_if<std::int64_t>(&value))
            {
                return static_cast<double>(*v);
            }

            throw std::runtime_error("Expected a numeric value");
        }

        std::int64_t as_integer(const VmValue &value)
        {
            if (const auto *v = std::get_if<std::int64_t>(&value))
            {
                return *v;
            }

            if (const auto *v = std::get_if<double>(&value))
            {
                // 範囲外の float から int への変換を防ぐ
                if (!std::isfinite(*v) ||
                    *v < -0x1p63 ||
                    *v >= 0x1p63)
                {
                    throw std::runtime_error("i64 conversion out of range");
                }

                return static_cast<std::int64_t>(*v);
            }

            throw std::runtime_error("Expected a numeric value");
        }

        std::string as_string(const VmValue &value)
        {
            if (const auto *v = std::get_if<std::string>(&value))
            {
                return *v;
            }

            std::ostringstream out;

            if (const auto *v = std::get_if<double>(&value))
            {
                out << *v;
            }
            else if (const auto *v = std::get_if<std::int64_t>(&value))
            {
                out << *v;
            }

            return out.str();
        }

        std::optional<VmValue> call_builtin(std::string_view name,
                                            std::span<const VmValue> args)
        {

            if (name == "i64" ||
                name == "f64" ||
                name == "str")
            {
                if (args.size() != 1)
                {
                    throw std::runtime_error("Wrong number of arguments");
                }

                if (name == "i64")
                {
                    return as_integer(args[0]);
                }

                if (name == "f64")
                {
                    return as_number(args[0]);
                }

                return as_string(args[0]);
            }

            if (name == "print")
            {
                for (const auto &value : args)
                {
                    std::visit(
                        [](const auto &item)
                        {
                            std::cout << item << ' ';
                        },
                        value);
                }

                std::cout << '\n';

                return 0.0;
            }

            for (const auto &builtin : BUILTIN_FUNCTIONS)
            {
                if (builtin.name != name)
                {
                    continue;
                }

                return std::visit(
                    Overloaded{
                        [&args](UnaryFn fn) -> VmValue
                        {
                            if (args.size() != 1)
                            {
                                throw std::runtime_error("Wrong number of function arguments");
                            }

                            return fn(as_number(args[0]));
                        },

                        [&args](BinaryFn fn) -> VmValue
                        {
                            if (args.size() != 2)
                            {
                                throw std::runtime_error("Wrong number of function arguments");
                            }

                            return fn(as_number(args[0]),
                                      as_number(args[1]));
                        },
                    },
                    builtin.fn);
            }

            return std::nullopt;
        }

        void write_bytes(std::ostream &writer,
                         const char *bytes,
                         std::streamsize count)
        {
            writer.write(bytes, count);

            if (!writer)
            {
                throw std::runtime_error("Failed to write bytecode");
            }
        }

        void read_bytes(std::istream &reader,
                        char *bytes,
                        std::streamsize count)
        {
            reader.read(bytes, count);

            if (reader.gcount() != count)
            {
                throw std::runtime_error("Truncated bytecode");
            }
        }

        void serialize_value(std::size_t value, std::ostream &writer)
        {
            if (value > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::runtime_error("Bytecode section is too large");
            }

            const auto v = static_cast<std::uint32_t>(value);

            std::array<char, 4> bytes{};

            // little endian に変換
            for (std::size_t i = 0; i < bytes.size(); ++i)
            {
                // 下位 8 bit
                bytes[i] = static_cast<char>((v >> (8 * i)) & 0xffu);
            }

            write_bytes(writer,
                        bytes.data(),
                        static_cast<std::streamsize>(bytes.size()));
        }

        std::size_t deserialize_value(std::istream &reader)
        {
            std::array<char, 4> bytes{};

            read_bytes(reader,
                       bytes.data(),
                       static_cast<std::streamsize>(bytes.size()));

            std::uint32_t value = 0;

            for (std::size_t i = 0; i < bytes.size(); ++i)
            {
                value |= static_cast<std::uint32_t>(
                             static_cast<unsigned char>(bytes[i]))
                         << (8 * i);
            }

            return static_cast<std::size_t>(value);
        }

        void serialize_literal(const VmValue &literal, std::ostream &writer)
        {
            std::visit(
                Overloaded{
                    [&writer](double value)
                    {
                        // 型情報
                        const char kind = static_cast<char>(ValueKind::F64);
                        write_bytes(writer, &kind, 1);

                        // 値
                        const auto bits = std::bit_cast<std::uint64_t>(value);
                        std::array<char, 8> bytes{};

                        // little endian
                        for (std::size_t i = 0; i < bytes.size(); ++i)
                        {
                            bytes[i] = static_cast<char>((bits >> (8 * i)) & 0xffu);
                        }

                        write_bytes(writer,
                                    bytes.data(),
                                    static_cast<std::streamsize>(bytes.size()));
                    },

                    [&writer](std::int64_t value)
                    {
                        const char kind = static_cast<char>(ValueKind::I64);

                        write_bytes(writer, &kind, 1);

                        const auto bits = std::bit_cast<std::uint64_t>(value);

                        std::array<char, 8> bytes{};

                        for (std::size_t i = 0; i < bytes.size(); ++i)
                        {
                            bytes[i] = static_cast<char>((bits >> (8 * i)) & 0xffu);
                        }

                        write_bytes(writer,
                                    bytes.data(),
                                    static_cast<std::streamsize>(bytes.size()));
                    },

                    [&writer](const std::string &value)
                    {
                        const char kind = static_cast<char>(ValueKind::Str);
                        write_bytes(writer, &kind, 1);

                        // 文字列長を保存する（NULL 終端には依存しない）
                        serialize_value(value.size(), writer);

                        write_bytes(writer,
                                    value.data(),
                                    static_cast<std::streamsize>(value.size()));
                    },
                },
                literal);
        }

        VmValue deserialize_literal(std::istream &reader)
        {
            // 型情報
            char kind{};
            read_bytes(reader, &kind, 1);

            switch (static_cast<std::uint8_t>(kind))
            {
            case static_cast<std::uint8_t>(ValueKind::F64):
            {
                std::array<char, 8> bytes{};

                read_bytes(reader,
                           bytes.data(),
                           static_cast<std::streamsize>(bytes.size()));

                std::uint64_t bits = 0;

                for (std::size_t i = 0; i < bytes.size(); ++i)
                {
                    bits |= static_cast<std::uint64_t>(
                                static_cast<unsigned char>(bytes[i]))
                            << (8 * i);
                }

                // bit 表現を維持してキャスト
                return std::bit_cast<double>(bits);
            }

            case static_cast<std::uint8_t>(ValueKind::I64):
            {
                std::array<char, 8> bytes{};

                read_bytes(reader,
                           bytes.data(),
                           static_cast<std::streamsize>(bytes.size()));

                std::uint64_t bits = 0;

                for (std::size_t i = 0; i < bytes.size(); ++i)
                {
                    bits |= static_cast<std::uint64_t>(
                                static_cast<unsigned char>(bytes[i]))
                            << (8 * i);
                }

                return std::bit_cast<std::int64_t>(bits);
            }

            case static_cast<std::uint8_t>(ValueKind::Str):
            {
                const auto size = deserialize_value(reader);

                std::string value(size, '\0');

                read_bytes(reader,
                           value.data(),
                           static_cast<std::streamsize>(value.size()));

                return value;
            }

            default:
                throw std::runtime_error("Unknown literal kind");
            }
        }

        void print_value(
            std::ostream &writer,
            const VmValue &value)
        {
            std::visit(
                Overloaded{
                    [&writer](double number)
                    {
                        writer << number;
                    },

                    [&writer](std::int64_t number)
                    {
                        writer << number;
                    },

                    [&writer](const std::string &str)
                    {
                        writer << '"' << str << '"';
                    },
                },
                value);
        }

        std::int64_t integer_binary(OpCode op,
                                    std::int64_t lhs,
                                    std::int64_t rhs)
        {
            using Limits = std::numeric_limits<std::int64_t>;

            switch (op)
            {
            case OpCode::Add:
                if ((rhs > 0 && lhs > Limits::max() - rhs) ||
                    (rhs < 0 && lhs < Limits::min() - rhs))
                {
                    throw std::runtime_error("Integer overflow");
                }
                return lhs + rhs;

            case OpCode::Sub:
                if ((rhs < 0 && lhs > Limits::max() + rhs) ||
                    (rhs > 0 && lhs < Limits::min() + rhs))
                {
                    throw std::runtime_error("Integer overflow");
                }
                return lhs - rhs;

            case OpCode::Mul:
                if (lhs == 0 || rhs == 0)
                {
                    return 0;
                }

                if ((lhs == -1 && rhs == Limits::min()) ||
                    (rhs == -1 && lhs == Limits::min()))
                {
                    throw std::runtime_error("Integer overflow");
                }

                if ((lhs > 0 && rhs > 0 && lhs > Limits::max() / rhs) ||
                    (lhs > 0 && rhs < 0 && rhs < Limits::min() / lhs) ||
                    (lhs < 0 && rhs > 0 && lhs < Limits::min() / rhs) ||
                    (lhs < 0 && rhs < 0 && lhs < Limits::max() / rhs))
                {
                    throw std::runtime_error("Integer overflow");
                }

                return lhs * rhs;

            case OpCode::Div:
                if (rhs == 0)
                {
                    throw std::runtime_error("Integer division by zero");
                }

                if (lhs == Limits::min() && rhs == -1)
                {
                    throw std::runtime_error("Integer overflow");
                }

                return lhs / rhs;

            default:
                throw std::runtime_error("Invalid integer operator");
            }
        }

        VmValue binary_value(OpCode op,
                             const VmValue &lhs,
                             const VmValue &rhs)
        {
            const auto *lhs_str = std::get_if<std::string>(&lhs);
            const auto *rhs_str = std::get_if<std::string>(&rhs);

            // 文字列同士
            if (lhs_str || rhs_str)
            {
                if (!lhs_str || !rhs_str)
                {
                    throw std::runtime_error("Incompatible operand types");
                }

                if (op == OpCode::Add)
                {
                    return *lhs_str + *rhs_str;
                }

                if (op == OpCode::Lt)
                {
                    return std::int64_t(*lhs_str < *rhs_str);
                }

                throw std::runtime_error("Unsupported string operation");
            }

            // 比較演算は I64 の 0 または 1 を返す
            if (op == OpCode::Lt)
            {
                if (const auto *a = std::get_if<std::int64_t>(&lhs))
                {
                    if (const auto *b = std::get_if<std::int64_t>(&rhs))
                    {
                        return std::int64_t(*a < *b);
                    }
                }

                return std::int64_t(as_number(lhs) < as_number(rhs));
            }

            // I64 同士
            const auto *lhs_int = std::get_if<std::int64_t>(&lhs);
            const auto *rhs_int = std::get_if<std::int64_t>(&rhs);

            if (lhs_int && rhs_int)
            {
                return integer_binary(op, *lhs_int, *rhs_int);
            }

            // F64 同士、または I64 と F64 の組み合わせ
            const double a = as_number(lhs);
            const double b = as_number(rhs);

            switch (op)
            {
            case OpCode::Add:
                return a + b;

            case OpCode::Sub:
                return a - b;

            case OpCode::Mul:
                return a * b;

            case OpCode::Div:
                return a / b;

            default:
                throw std::runtime_error("Unknown binary operator");
            }
        }
    }

    void Instruction::serialize(std::ostream &writer) const
    {
        const auto op_code = static_cast<std::uint8_t>(op);

        if (op_code > static_cast<std::uint8_t>(OpCode::Ret))
        {
            throw std::runtime_error("Unknown opcode");
        }

        const std::array<char, 2> bytes{static_cast<char>(op_code),
                                        static_cast<char>(arg_0)};

        // byte code を書き出す
        writer.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));

        if (!writer)
        {
            throw std::runtime_error("Failed to write instruction");
        }
    }

    std::optional<Instruction> Instruction::deserialize(std::istream &reader)
    {
        std::array<char, 2> bytes{};

        // 1 命令読み込む
        reader.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));

        if (reader.gcount() == 0 && reader.eof())
        {
            return std::nullopt;
        }

        if (reader.gcount() != static_cast<std::streamsize>(bytes.size()))
        {
            throw std::runtime_error("Incomplete or unreadable instruction");
        }

        const auto op_code = static_cast<std::uint8_t>(bytes[0]);

        OpCode op;
        switch (op_code)
        {
        case static_cast<std::uint8_t>(OpCode::LoadLiteral):
            op = OpCode::LoadLiteral;
            break;

        case static_cast<std::uint8_t>(OpCode::Store):
            op = OpCode::Store;
            break;

        case static_cast<std::uint8_t>(OpCode::Copy):
            op = OpCode::Copy;
            break;

        case static_cast<std::uint8_t>(OpCode::Dup):
            op = OpCode::Dup;
            break;

        case static_cast<std::uint8_t>(OpCode::Add):
            op = OpCode::Add;
            break;

        case static_cast<std::uint8_t>(OpCode::Sub):
            op = OpCode::Sub;
            break;

        case static_cast<std::uint8_t>(OpCode::Mul):
            op = OpCode::Mul;
            break;

        case static_cast<std::uint8_t>(OpCode::Div):
            op = OpCode::Div;
            break;

        case static_cast<std::uint8_t>(OpCode::Call):
            op = OpCode::Call;
            break;

        case static_cast<std::uint8_t>(OpCode::Jmp):
            op = OpCode::Jmp;
            break;

        case static_cast<std::uint8_t>(OpCode::Jf):
            op = OpCode::Jf;
            break;

        case static_cast<std::uint8_t>(OpCode::Lt):
            op = OpCode::Lt;
            break;

        case static_cast<std::uint8_t>(OpCode::Pop):
            op = OpCode::Pop;
            break;

        case static_cast<std::uint8_t>(OpCode::Ret):
            op = OpCode::Ret;
            break;

        default:
            throw std::runtime_error("Unknown opcode");
        }

        const auto arg_0 = static_cast<std::uint8_t>(bytes[1]);

        return Instruction{op, arg_0};
    }

    void CompiledFnDef::serialize(std::ostream &writer) const
    {
        constexpr auto max_operand = static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max());

        if (params.size() > max_operand)
        {
            throw std::runtime_error("Too many function parameters");
        }

        if (literals.size() > max_operand + 1)
        {
            throw std::runtime_error("Too many function literals");
        }

        // 引数
        serialize_value(params.size(), writer);

        for (const auto &param : params)
        {
            serialize_value(param.size(), writer);

            write_bytes(writer,
                        param.data(),
                        static_cast<std::streamsize>(param.size()));
        }

        // 定数テーブル
        serialize_value(literals.size(), writer);

        for (const auto &literal : literals)
        {
            serialize_literal(literal, writer);
        }

        // 命令列
        serialize_value(instructions.size(), writer);

        for (const auto &instruction : instructions)
        {
            instruction.serialize(writer);
        }
    }

    CompiledFnDef CompiledFnDef::deserialize(std::istream &reader)
    {
        CompiledFnDef fn;

        // 引数
        const auto param_count = deserialize_value(reader);

        if (param_count > std::numeric_limits<std::uint8_t>::max())
        {
            throw std::runtime_error("Too many function parameters");
        }

        for (std::size_t i = 0; i < param_count; ++i)
        {
            const auto len = deserialize_value(reader);

            std::string param(len, '\0');

            read_bytes(reader,
                       param.data(),
                       static_cast<std::streamsize>(len));

            fn.params.push_back(std::move(param));
        }

        // 定数テーブル
        const auto literal_count = deserialize_value(reader);

        constexpr auto max_literal_count = static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()) + 1;

        if (literal_count > max_literal_count)
        {
            throw std::runtime_error("Too many function literals");
        }

        for (std::size_t i = 0; i < literal_count; ++i)
        {
            fn.literals.push_back(deserialize_literal(reader));
        }

        // 命令列
        const auto instruction_count = deserialize_value(reader);

        for (std::size_t i = 0; i < instruction_count; ++i)
        {
            const auto instruction = Instruction::deserialize(reader);

            if (!instruction)
            {
                throw std::runtime_error("Truncated function instructions");
            }

            fn.instructions.push_back(*instruction);
        }

        return fn;
    }

    StackIndex Compiler::stack_top() const
    {
        if (target_stack.empty())
        {
            throw std::runtime_error("Empty target stack");
        }

        return StackIndex{target_stack.size() - 1};
    }

    std::uint8_t Compiler::add_literal(VmValue value)
    {
        for (std::size_t i = 0; i < literals.size(); ++i)
        {
            if (literals[i] == value)
            {
                return static_cast<std::uint8_t>(i);
            }
        }

        // 定数テーブルがいっぱいか
        if (literals.size() > std::numeric_limits<std::uint8_t>::max())
        {
            throw std::runtime_error("Too many literals for an 8-bit index");
        }

        const auto index = static_cast<std::uint8_t>(literals.size());

        // 定数テーブルに追加
        literals.push_back(std::move(value));

        return index;
    }

    InstIndex Compiler::add_inst(OpCode op, std::uint8_t arg_0)
    {
        const auto index = instructions.size();

        instructions.push_back(Instruction{op, arg_0});

        return InstIndex{index};
    }

    InstIndex Compiler::add_load_literal_inst(std::uint8_t literal_index)
    {
        const auto inst = add_inst(OpCode::LoadLiteral, literal_index);

        target_stack.push_back(TargetLiteral{literal_index});

        return inst;
    }

    StackIndex Compiler::add_bin_op_inst(OpCode op)
    {
        if (target_stack.size() < 2)
        {
            throw std::runtime_error("Binary operation stack underflow");
        }

        add_inst(op, 0);

        // bin_op により stack の要素は１つ減る
        target_stack.pop_back();

        // stack top は演算結果
        target_stack.back() = TargetTemp{};

        return stack_top();
    }

    InstIndex Compiler::add_copy_inst(StackIndex stack_index)
    {
        if (stack_index.value >= target_stack.size())
        {
            throw std::runtime_error("Invalid stack position");
        }

        // copy 対象の位置
        const auto depth = target_stack.size() - stack_index.value - 1;

        if (depth > std::numeric_limits<std::uint8_t>::max())
        {
            throw std::runtime_error("Copy depth exceeds 8-bit operand");
        }

        const auto index = add_inst(OpCode::Copy,
                                    static_cast<std::uint8_t>(depth));

        // copy によって stack の深さが１つ増える
        target_stack.push_back(TargetTemp{});

        return index;
    }

    InstIndex Compiler::add_store_inst(StackIndex stack_index)
    {
        if (target_stack.size() <= 1 ||
            stack_index.value >= target_stack.size() - 1)
        {
            throw std::runtime_error("Store must target below top of stack");
        }

        const auto depth = target_stack.size() - stack_index.value - 1;

        if (depth > std::numeric_limits<std::uint8_t>::max())
        {
            throw std::runtime_error("Store depth exceeds 8-bit operand");
        }

        const auto result = add_inst(OpCode::Store,
                                     static_cast<std::uint8_t>(depth));

        // store 後 top を pop
        target_stack.pop_back();

        return result;
    }

    InstIndex Compiler::add_jf_inst()
    {
        if (target_stack.empty())
        {
            throw std::runtime_error("Jf requires condition");
        }

        // 一旦ジャンプ先を 0 に設定し、then-branch のコンパイル後に fixup_jmp  で更新
        const auto result = add_inst(OpCode::Jf, 0);

        // condition 結果を pop
        target_stack.pop_back();

        return result;
    }

    std::optional<InstIndex> Compiler::add_pop_until_inst(StackIndex stack_index)
    {
        if (target_stack.size() <= stack_index.value)
        {
            return std::nullopt;
        }

        const auto count = target_stack.size() - stack_index.value - 1;

        if (count > std::numeric_limits<std::uint8_t>::max())
        {
            throw std::runtime_error("Pop count exceeds 8-bit operand");
        }

        const auto result = add_inst(OpCode::Pop, static_cast<std::uint8_t>(count));

        target_stack.resize(stack_index.value + 1, TargetTemp{});

        return result;
    }

    std::optional<StackIndex> Compiler::find_local(std::string_view name) const
    {
        // 新しい要素から探索
        for (std::size_t i = target_stack.size(); i-- > 0;)
        {
            const auto *local = std::get_if<TargetLocal>(&target_stack[i]);

            if (local && local->name == name)
            {
                return StackIndex{i};
            }
        }

        return std::nullopt;
    }

    void Compiler::fixup_jmp(InstIndex inst_index)
    {
        if (inst_index.value >= instructions.size())
        {
            throw std::runtime_error("Invalid jump fixup position");
        }

        if (instructions[inst_index.value].op != OpCode::Jmp &&
            instructions[inst_index.value].op != OpCode::Jf)
        {
            throw std::runtime_error("Fixup target is not a jump");
        }

        if (instructions.size() > std::numeric_limits<std::uint8_t>::max())
        {
            throw std::runtime_error("Jump address exceeds 8-bit operand");
        }

        instructions[inst_index.value].arg_0 = static_cast<std::uint8_t>(instructions.size());
    }

    void Compiler::fixup_continues()
    {
        if (loop_stack.empty())
        {
            throw std::runtime_error("continue outside loop");
        }

        // 通常ループの終了時の stack サイズ
        const auto normal_size = target_stack.size();

        for (const auto &[dup_inst, continue_size] : loop_stack.back().continue_insts)
        {
            if (normal_size < continue_size)
            {
                throw std::runtime_error("Invalid continue stack state");
            }

            // continue 経路で不足する要素数
            const auto missing_size = normal_size - continue_size;

            // dup の値を更新
            if (instructions[dup_inst.value].op != OpCode::Dup)
            {
                throw std::runtime_error("Invalid Dup fixup position");
            }

            instructions[dup_inst.value].arg_0 = checked_u8(missing_size);

            // jmp (dup の後は jmp)
            fixup_jmp(InstIndex{dup_inst.value + 1});
        }
    }

    void Compiler::discard_to(std::size_t size)
    {
        if (size > target_stack.size())
        {
            throw std::runtime_error("Invalid stack cleanup");
        }

        const auto count = target_stack.size() - size;

        if (count > 0)
        {
            add_inst(OpCode::Pop, checked_u8(count));

            target_stack.resize(size);
        }
    }

    void Compiler::emit_pop_to(std::size_t size)
    {
        if (size > target_stack.size())
        {
            throw std::runtime_error("Invalid jump cleanup");
        }

        const auto count = target_stack.size() - size;

        if (count > 0)
        {
            add_inst(OpCode::Pop, checked_u8(count));
        }
    }

    StackIndex Compiler::compile_expr(const Expression &expression)
    {
        auto bin_op = [this](OpCode op,
                             const Expression &lhs,
                             const Expression &rhs)
        {
            const auto lhs_pos = compile_expr(lhs);
            const auto rhs_pos = compile_expr(rhs);

            // 2 要素取り出す
            add_copy_inst(lhs_pos);
            add_copy_inst(rhs_pos);

            add_bin_op_inst(op);

            return stack_top();
        };

        return std::visit(Overloaded{
                              [this](const ExprNumLiteral &node) -> StackIndex
                              {
                                  const auto literal_index = add_literal(node.value);

                                  add_load_literal_inst(literal_index);

                                  return stack_top();
                              },

                              [this](const ExprStrLiteral &node) -> StackIndex
                              {
                                  const auto literal_index = add_literal(node.value);

                                  add_load_literal_inst(literal_index);

                                  return stack_top();
                              },

                              [this, &expression](const ExprIdent &node) -> StackIndex
                              {
                                  // 規定の定数
                                  if (node.text == "PI")
                                  {
                                      const auto id = add_literal(std::numbers::pi_v<double>);

                                      add_load_literal_inst(id);

                                      return stack_top();
                                  }

                                  //  ローカル変数
                                  if (const auto local = find_local(node.text))
                                  {
                                      return *local;
                                  }

                                  throw CompileError{expression.span,
                                                     "Unknown identifier: " + std::string(node.text)};
                              },

                              [bin_op](const ExprAdd &node) -> StackIndex
                              {
                                  return bin_op(OpCode::Add, *node.lhs, *node.rhs);
                              },

                              [bin_op](const ExprSub &node) -> StackIndex
                              {
                                  return bin_op(OpCode::Sub, *node.lhs, *node.rhs);
                              },

                              [bin_op](const ExprMul &node) -> StackIndex
                              {
                                  return bin_op(OpCode::Mul, *node.lhs, *node.rhs);
                              },

                              [bin_op](const ExprDiv &node) -> StackIndex
                              {
                                  return bin_op(OpCode::Div, *node.lhs, *node.rhs);
                              },

                              [bin_op](const ExprLt &node) -> StackIndex
                              {
                                  return bin_op(OpCode::Lt, *node.lhs, *node.rhs);
                              },

                              [bin_op](const ExprGt &node) -> StackIndex
                              {
                                  return bin_op(OpCode::Lt, *node.rhs, *node.lhs);
                              },

                              [this, &expression](const ExprFnInvoke &node) -> StackIndex
                              {
                                  if (node.args.size() > std::numeric_limits<std::uint8_t>::max())
                                  {
                                      throw CompileError{expression.span,
                                                         "Too many function arguments"};
                                  }

                                  // 引数評価開始前の stack サイズ
                                  const auto stack_before_args = target_stack.size();

                                  // 関数名
                                  const auto name = add_literal(std::string(node.name));

                                  // 引数
                                  std::vector<StackIndex> positions;
                                  positions.reserve(node.args.size());

                                  for (const auto &arg : node.args)
                                  {
                                      positions.push_back(compile_expr(arg));
                                  }

                                  const auto stack_before_call = target_stack.size();

                                  // 引数の式をコンパイル後に stack に積む
                                  add_load_literal_inst(name);

                                  for (const auto pos : positions)
                                  {
                                      add_copy_inst(pos);
                                  }

                                  add_inst(OpCode::Call, checked_u8(positions.size()));

                                  // 実行後は関数名と引数を取り除き、戻り値を１つ積む
                                  target_stack.resize(stack_before_call + 1, TargetTemp{});

                                  // 引数評価時にできた一時値が存在する時のみ、
                                  // 戻り値を引数評価前の stack 位置に追加
                                  if (stack_before_call > stack_before_args)
                                  {
                                      add_store_inst(StackIndex{stack_before_args});

                                      // 引数評価時にできた一時値を削除
                                      add_pop_until_inst(StackIndex{stack_before_args});
                                  }
                                  return stack_top();
                              },

                              [&expression](const auto &) -> StackIndex
                              { throw CompileError{expression.span,
                                                   "Expression kind is not supported by "
                                                   "the bytecode compiler yet"}; },
                          },
                          expression.value);
    }

    std::optional<StackIndex> Compiler::compile_statements(const Statements &statements)
    {
        std::optional<StackIndex> last_result;

        for (const auto &statement : statements)
        {
            std::visit(
                Overloaded{
                    // 式文
                    [this, &last_result](const Expression &node)
                    {
                        last_result = compile_expr(node);
                    },

                    // var def
                    [this, &last_result](const VarDef &node)
                    {
                        last_result.reset();

                        auto pos = compile_expr(node.initializer);

                        if (std::holds_alternative<TargetLocal>(target_stack[pos.value]))
                        {
                            add_copy_inst(pos);
                            pos = stack_top();
                        }

                        target_stack[pos.value] = TargetLocal{std::string(node.name)};
                    },

                    // var assignment
                    [this, &last_result](const VarAssign &node)
                    {
                        last_result.reset();

                        const auto value_pos = compile_expr(node.value);
                        const auto local_pos = find_local(node.name);

                        if (!local_pos)
                        {
                            throw CompileError{node.name_span,
                                               "Undefined variable: " + std::string(node.name)};
                        }

                        add_copy_inst(value_pos);
                        add_store_inst(*local_pos);
                    },

                    // if
                    [this, &last_result](const std::unique_ptr<IfStatement> &node)
                    {
                        last_result.reset();

                        compile_if(*node);
                    },

                    // for
                    [this, &last_result](
                        const std::unique_ptr<ForStatement> &node)
                    {
                        last_result.reset();

                        compile_for(*node);
                    },

                    // break
                    [this, &last_result](const BreakStatement &node)
                    {
                        last_result.reset();

                        compile_break(node);
                    },

                    // continue
                    [this, &last_result](const ContinueStatement &node)
                    {
                        last_result.reset();

                        compile_continue(node);
                    },

                    // return
                    [this, &last_result](const ReturnStatement &node)
                    {
                        last_result.reset();

                        if (!in_function)
                        {
                            throw CompileError{node.value.span,
                                               "return outside a function"};
                        }

                        const auto result = compile_expr(node.value);

                        add_copy_inst(result);

                        add_inst(OpCode::Ret, 0);
                    },

                    // fn def
                    [this, &last_result](const std::unique_ptr<FunctionDefinition> &node)
                    {
                        last_result.reset();

                        compile_function(*node);
                    },

                    [](const auto &)
                    {
                        throw std::runtime_error(
                            "Statement is not supported yet");
                    },
                },
                statement);
        }

        return last_result;
    }

    void Compiler::compile_program(const Statements &statements)
    {
        const auto result = compile_statements(statements);

        if (result)
        {
            // 最後の式の結果をスタック頂上に配置する
            if (result->value != stack_top().value)
            {
                add_copy_inst(*result);
            }
        }
        else
        {
            // 最終結果がないのでスタックを空にする
            discard_to(0);
        }

        if (funcs.contains("main"))
        {
            throw std::runtime_error("main is reserved");
        }

        // トップレベルの命令列も関数として登録する
        funcs.emplace("main",
                      CompiledFnDef{
                          {},
                          std::move(literals),
                          std::move(instructions),
                      });
    }

    void Compiler::compile_if(const IfStatement &node)
    {
        // if 前の stack 状態を保存
        const auto stack_size_before = target_stack.size();

        // condition
        const auto condition = compile_expr(node.condition);

        // Jf
        // condition を消費するため積む
        add_copy_inst(condition);
        const auto jf = add_jf_inst();

        // condition 後の stack を保存
        const auto after_condition = target_stack;

        // then
        // condition で発生した一時値を削除
        discard_to(stack_size_before);

        compile_statements(node.then_branch);

        discard_to(stack_size_before);

        // else を飛ばす Jmp
        const auto jmp = add_inst(OpCode::Jmp, 0);

        // else
        // Jf のジャンプ先を更新
        fixup_jmp(jf);

        // else も condition 後の stack から開始
        target_stack = after_condition;

        // condition で発生した一時値を削除
        discard_to(stack_size_before);

        if (node.else_branch)
        {
            compile_statements(*node.else_branch);
        }

        discard_to(stack_size_before);

        // Jmp のジャンプ先を更新
        fixup_jmp(jmp);
    }

    void Compiler::compile_for(const ForStatement &node)
    {
        // for 前の stack 状態を保存
        const auto stack_size_before = target_stack.size();

        // 開始値と終了値
        const auto start = compile_expr(node.start);
        const auto end = compile_expr(node.end);

        // ループ変数
        add_copy_inst(start);
        const auto loop_var = stack_top();

        target_stack[loop_var.value] = TargetLocal{std::string(node.loop_var)};

        // ループ条件判定命令の位置
        const auto check_inst = instructions.size();

        // condition (i < end)
        add_copy_inst(loop_var);
        add_copy_inst(end);
        add_bin_op_inst(OpCode::Lt);

        // Jf
        const auto jf = add_jf_inst();

        loop_stack.push_back(LoopFrame{loop_var, {}, {}});

        // ループ本体
        compile_statements(node.body);

        // continue の合流先を確定
        fixup_continues();

        // i = i + 1
        const auto one = add_literal(1.0);
        add_copy_inst(loop_var);
        add_load_literal_inst(one);
        add_bin_op_inst(OpCode::Add);
        add_store_inst(loop_var);

        // １ループ終了後の stack の一時値を削除
        discard_to(loop_var.value + 1);

        // 次のループへ
        add_inst(OpCode::Jmp, checked_u8(check_inst));

        // Jf をループ終了位置に更新
        fixup_jmp(jf);

        // break のジャンプ先も同じ位置に更新
        for (const auto insts : loop_stack.back().break_insts)
        {
            fixup_jmp(insts);
        }

        loop_stack.pop_back();

        discard_to(stack_size_before);
    }

    void Compiler::compile_break(const BreakStatement &node)
    {
        if (loop_stack.empty())
        {
            throw CompileError{node.span,
                               "break outside loop"};
        }

        // ループ変数の位置まで残す
        // ：次のループの時とおなじスタック状態にするため
        const auto keep_pos = loop_stack.back().start.value + 1;

        emit_pop_to(keep_pos);

        // Jmp
        const auto jmp = add_inst(OpCode::Jmp, 0);

        loop_stack.back().break_insts.push_back(jmp);
    }

    void Compiler::compile_continue(const ContinueStatement &node)
    {
        if (loop_stack.empty())
        {
            throw CompileError{node.span,
                               "continue outside loop"};
        }

        // ループ変数の位置まで残す
        // ：次のループの時とおなじスタック状態にするため
        const auto keep_pos = loop_stack.back().start.value + 1;

        // dup の開始は keep_pos からなので pop
        emit_pop_to(keep_pos);

        // continue の場合、通常ループより stack サイズが小さくなるため dup が必要
        const auto dup = add_inst(OpCode::Dup, 0);

        add_inst(OpCode::Jmp, 0);

        loop_stack.back().continue_insts.push_back({dup, keep_pos});
    }

    void Compiler::compile_function(const FunctionDefinition &node)
    {
        if (in_function)
        {
            throw CompileError{node.name_span,
                               "Nested function definitions are not supported yet"};
        }

        const std::string name{node.name};

        if (name == "main" || funcs.contains(name))
        {
            throw CompileError{node.name_span,
                               "Duplicate or reserved function name: " + name};
        }

        if (node.parameters.size() > std::numeric_limits<std::uint8_t>::max())
        {
            throw CompileError{node.name_span,
                               "Too many parameters"};
        }

        // 関数専用のコンパイル環境
        Compiler function_compiler;
        function_compiler.in_function = true;

        CompiledFnDef fn;

        // 引数を stack に積む
        for (const auto &param : node.parameters)
        {
            const std::string param_name{param.name};

            // 名前の重複をチェック
            for (const auto &existing_params : fn.params)
            {
                if (existing_params == param_name)
                {
                    throw CompileError{node.name_span,
                                       "Duplicate parameter: " + param_name};
                }
            }

            fn.params.push_back(param_name);

            function_compiler.target_stack.push_back(TargetLocal{param_name});
        }

        // 関数本体
        const auto result = function_compiler.compile_statements(node.body);

        if (result)
        {
            // 最後が式文なら、その値を戻り値にする
            if (result->value != function_compiler.stack_top().value)
            {
                function_compiler.add_copy_inst(*result);
            }
        }
        else
        {
            // 値を返さないなら stack を空にする
            function_compiler.discard_to(0);
        }

        // 完成した関数の byte code を保存
        fn.literals = std::move(function_compiler.literals);
        fn.instructions = std::move(function_compiler.instructions);

        funcs.emplace(name, std::move(fn));
    }

    void Compiler::disasm(std::ostream &writer) const
    {
        if (!funcs.empty())
        {
            for (const auto &[name, fn] : funcs)
            {
                writer << "Function \"" << name << "\":\n";

                Compiler debug;
                debug.literals = fn.literals;
                debug.instructions = fn.instructions;

                debug.disasm(writer);
            }

            return;
        }

        writer << "Literals [" << literals.size() << "]\n";

        for (std::size_t i = 0; i < literals.size(); ++i)
        {
            writer << "  [" << i << "] ";
            print_value(writer, literals[i]);
            writer << '\n';
        }

        writer << "Instructions [" << instructions.size() << "]\n";

        for (std::size_t i = 0; i < instructions.size(); ++i)
        {
            writer << "  [" << i << "] ";

            const auto &inst = instructions[i];

            switch (inst.op)
            {
            case OpCode::LoadLiteral:
                writer << "LoadLiteral "
                       << static_cast<unsigned>(inst.arg_0)
                       << " (";

                print_value(writer, literals.at(inst.arg_0));
                writer << ")\n";
                break;

            case OpCode::Store:
                writer << "Store "
                       << static_cast<unsigned>(inst.arg_0) << '\n';
                break;

            case OpCode::Copy:
                writer << "Copy "
                       << static_cast<unsigned>(inst.arg_0)
                       << '\n';
                break;

            case OpCode::Dup:
                writer << "Dup "
                       << static_cast<unsigned>(inst.arg_0) << '\n';
                break;

            case OpCode::Call:
                writer << "Call "
                       << static_cast<unsigned>(inst.arg_0)
                       << '\n';
                break;

            case OpCode::Add:
                writer << "Add\n";
                break;

            case OpCode::Sub:
                writer << "Sub\n";
                break;

            case OpCode::Mul:
                writer << "Mul\n";
                break;

            case OpCode::Div:
                writer << "Div\n";
                break;

            case OpCode::Jmp:
                writer << "Jmp "
                       << static_cast<unsigned>(inst.arg_0) << '\n';
                break;

            case OpCode::Jf:
                writer << "Jf "
                       << static_cast<unsigned>(inst.arg_0) << '\n';
                break;

            case OpCode::Lt:
                writer << "Lt\n";
                break;

            case OpCode::Pop:
                writer << "Pop "
                       << static_cast<unsigned>(inst.arg_0) << '\n';
                break;

            case OpCode::Ret:
                writer << "Ret\n";
                break;

            default:
                throw std::runtime_error("Unknown opcode");
            }
        }
    }

    void Compiler::write_literals(std::ostream &writer) const
    {
        // 定数テーブルサイズの書き込み
        serialize_value(literals.size(), writer);

        // 定数の書き込み
        for (const auto &value : literals)
        {
            serialize_literal(value, writer);
        }
    }

    void Compiler::write_insts(std::ostream &writer) const
    {
        // 命令列のサイズの書き込み
        serialize_value(instructions.size(), writer);

        // 命令の書き込み
        for (const auto &instruction : instructions)
        {
            instruction.serialize(writer);
        }
    }

    void Compiler::write_funcs(std::ostream &writer) const
    {
        serialize_value(funcs.size(), writer);

        for (const auto &[name, fn] : funcs)
        {
            serialize_value(name.size(), writer);

            write_bytes(writer,
                        name.data(),
                        static_cast<std::streamsize>(name.size()));

            fn.serialize(writer);
        }
    }

    CompileResult compile_program_checked(const Statements &statements)
    {
        Compiler compiler;

        try
        {
            compiler.compile_program(statements);
        }
        // CompileError のみ捕捉
        catch (const CompileError &error)
        {
            return error;
        }

        return compiler;
    }

    void Vm::read_funcs(std::istream &reader)
    {
        const auto count = deserialize_value(reader);

        std::unordered_map<std::string, CompiledFnDef> loaded;

        for (std::size_t i = 0; i < count; ++i)
        {
            const auto len = deserialize_value(reader);

            std::string name(len, '\0');

            read_bytes(reader,
                       name.data(),
                       static_cast<std::streamsize>(len));

            auto fn = CompiledFnDef::deserialize(reader);

            // 関数の多重定義チェック
            if (!loaded.emplace(std::move(name),
                                std::move(fn))
                     .second)
            {
                throw std::runtime_error(
                    "Duplicate function name in bytecode");
            }
        }

        funcs = std::move(loaded);
    }

    std::optional<VmValue> Vm::interpret() const
    {
        return interpret("main", {});
    }

    std::optional<VmValue> Vm::interpret(std::string_view fn_name,
                                         std::span<const VmValue> args) const
    {
        const auto it = funcs.find(std::string(fn_name));

        if (it == funcs.end())
        {
            throw std::runtime_error("Unknown function: " + std::string(fn_name));
        }

        const auto &fn = it->second;

        if (args.size() != fn.params.size())
        {
            throw std::runtime_error("Wrong number of arguments for: " +
                                     std::string(fn_name));
        }

        return execute(fn.literals, fn.instructions, args);
    }

    void Vm::set_native_functions(const NativeFunctionRegistry &registry)
    {
        m_native_functions = &registry;
    }

    std::optional<VmValue> Vm::execute(std::span<const VmValue> literal_view,
                                       std::span<const Instruction> code,
                                       std::span<const VmValue> args) const
    {
        // 渡された実引数を関数の初期 stack にする
        std::vector<VmValue> stack(args.begin(), args.end());

        const auto pop = [&stack]() -> VmValue
        {
            if (stack.empty())
            {
                throw std::runtime_error("Stack underflow");
            }

            const auto value = stack.back();
            stack.pop_back();

            return value;
        };

        const auto bin = [&stack, &pop](OpCode op)
        {
            const auto rhs = pop();
            const auto lhs = pop();

            stack.push_back(binary_value(op, lhs, rhs));
        };

        std::size_t ip = 0;

        while (ip < code.size())
        {
            const auto &instruction = code[ip];

            switch (instruction.op)
            {
            case OpCode::LoadLiteral:
                if (instruction.arg_0 >= literal_view.size())
                {
                    throw std::runtime_error("Literal index out of range");
                }

                stack.push_back(literal_view[instruction.arg_0]);

                break;

            case OpCode::Store:
            {
                const auto depth = static_cast<std::size_t>(instruction.arg_0);

                if (depth == 0 || depth >= stack.size())
                {
                    throw std::runtime_error("Invalid Store depth");
                }

                const auto index = stack.size() - depth - 1;

                // 頂上の値を取り出し移動
                auto value = pop();
                stack[index] = std::move(value);

                break;
            }

            case OpCode::Copy:
            {
                const auto depth = static_cast<std::size_t>(instruction.arg_0);

                if (depth >= stack.size())
                {
                    throw std::runtime_error("Copy index out of range");
                }

                const auto value = stack[stack.size() - depth - 1];

                stack.push_back(value);

                break;
            }

            case OpCode::Dup:
            {
                if (stack.empty())
                {
                    throw std::runtime_error("Dup requires a stack value");
                }

                const auto value = stack.back();

                for (std::size_t i = 0; i < instruction.arg_0; ++i)
                {
                    stack.push_back(value);
                }

                break;
            }

            case OpCode::Add:
                bin(OpCode::Add);
                break;

            case OpCode::Sub:
                bin(OpCode::Sub);
                break;

            case OpCode::Mul:
                bin(OpCode::Mul);
                break;

            case OpCode::Div:
                bin(OpCode::Div);
                break;

            case OpCode::Lt:
                bin(OpCode::Lt);
                break;

            case OpCode::Call:
            {
                const auto arg_count = static_cast<std::size_t>(instruction.arg_0);

                if (stack.size() < arg_count + 1)
                {
                    throw std::runtime_error("Stack underflow in Call");
                }

                const auto name_index = stack.size() - arg_count - 1;

                const auto *name = std::get_if<std::string>(&stack[name_index]);

                if (!name)
                {
                    throw std::runtime_error("Function name is not a string");
                }

                // 引数の span
                // ：関数名の直後から引数が並んでいる
                const auto call_args = std::span<const VmValue>(stack.data() + name_index + 1,
                                                                arg_count);

                // 組み込み関数
                auto result = call_builtin(*name, call_args);

                // ネイティブ関数
                if (!result && m_native_functions)
                {
                    result = m_native_functions->invoke(*name, call_args);
                }

                // ユーザー定義関数
                if (!result)
                {
                    const auto returned = interpret(*name, call_args);

                    if (!returned)
                    {
                        throw std::runtime_error("Function did not return a value: " + *name);
                    }

                    result = *returned;
                }

                // 関数名、引数を pop
                stack.resize(name_index);

                // 戻り値
                stack.push_back(std::move(*result));

                break;
            }

            case OpCode::Jmp:
            {
                const auto target = static_cast<std::size_t>(instruction.arg_0);

                if (target > code.size())
                {
                    throw std::runtime_error("Invalid jump target");
                }

                ip = target;
                continue;
            }

            case OpCode::Jf:
            {
                const auto condition = as_number(pop());

                // false の時ジャンプ
                if (condition == 0.0)
                {
                    const auto target = static_cast<std::size_t>(instruction.arg_0);

                    if (target > code.size())
                    {
                        throw std::runtime_error("Invalid jump target");
                    }

                    ip = target;
                    continue;
                }

                break;
            }

            case OpCode::Pop:
            {
                const auto count = static_cast<std::size_t>(instruction.arg_0);

                if (count > stack.size())
                {
                    throw std::runtime_error("Stack underflow in Pop");
                }

                stack.resize(stack.size() - count);

                break;
            }

            case OpCode::Ret:
            {
                if (stack.empty())
                {
                    throw std::runtime_error("Return stack underflow");
                }

                return pop();
            }

            default:
                throw std::runtime_error("Unknown opcode");
            }

            ++ip;
        }

        if (stack.empty())
        {
            return std::nullopt;
        }

        return pop();
    }

    void write_program(const std::filesystem::path &path, const Compiler &compiler)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);

        if (!file)
        {
            throw std::runtime_error("Cannot open output file: " + path.string());
        }

        compiler.write_funcs(file);

        file.close();

        if (!file)
        {
            throw std::runtime_error("Failed to close output file: " + path.string());
        }
    }

    Vm read_program(const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);

        if (!file)
        {
            throw std::runtime_error("Cannot open input file: " + path.string());
        }

        Vm vm;

        vm.read_funcs(file);

        return vm;
    }
}