#pragma once

#include "token.hpp"
#include "ast.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <span>
#include <vector>
#include <bit>
#include <limits>
#include <cstddef>
#include <string>
#include <variant>
#include <utility>
#include <unordered_map>
#include <stdexcept>

namespace lang
{
    struct Expression;

    class NativeFunctionRegistry;

    struct CompileError : std::runtime_error
    {
        SourceSpan span;

        CompileError(SourceSpan span, std::string message)
            : std::runtime_error(std::move(message)), span(span) {}
    };

    // byte code の命令
    enum OpCode : std::uint8_t
    {
        LoadLiteral = 0,
        Store = 1,
        Copy = 2,
        Dup = 3,
        Add = 4,
        Sub = 5,
        Mul = 6,
        Div = 7,
        Call = 8,
        Jmp = 9,
        Jf = 10,
        Lt = 11,
        Pop = 12,
        Ret = 13
    };

    // VM が扱う実行時の値
    using VmValue = std::variant<double, std::int64_t, std::string>;

    // 1 命令（2 bytes）
    struct Instruction
    {
        OpCode op;
        std::uint8_t arg_0;

        void serialize(std::ostream &writer) const;

        static std::optional<Instruction> deserialize(std::istream &reader);
    };

    // compile 時に追跡する stack の絶対 index
    struct StackIndex
    {
        std::size_t value;
    };

    // compile 中の命令の index
    struct InstIndex
    {
        std::size_t value;
    };

    // 計算途中の一時値を表す stack スロット
    struct TargetTemp
    {
    };

    // 定数テーブルから読み込んだ値を表す stack スロット
    struct TargetLiteral
    {
        std::size_t index;
    };

    // ローカル変数を表す stack スロット
    struct TargetLocal
    {
        std::string name;
    };

    // compile 時に追跡する stack スロットの種類
    using Target = std::variant<TargetTemp,
                                TargetLiteral,
                                TargetLocal>;

    // loop のジャンプ先を後から確定するための情報
    struct LoopFrame
    {
        // ループ変数が存在する stack 位置
        StackIndex start;

        // break で生成した Jmp 命令の位置
        std::vector<InstIndex> break_insts;

        // continue の Dup 命令の位置と continue 経路の stack サイズ
        std::vector<std::pair<InstIndex, std::size_t>> continue_insts;
    };

    // compile 済みの関数
    struct CompiledFnDef
    {
        // 仮引数名
        std::vector<std::string> params;

        // この関数専用の定数テーブル
        std::vector<VmValue> literals;

        // この関数専用の命令列
        std::vector<Instruction> instructions;

        // 関数定義をバイナリに書き出す
        void serialize(std::ostream &writer) const;

        // 関数定義をバイナリから復元する
        static CompiledFnDef deserialize(std::istream &reader);
    };

    // AST から byte code を生成する
    struct Compiler
    {
        // 定数テーブル
        std::vector<VmValue> literals;

        // 命令列
        std::vector<Instruction> instructions;

        // VM のスタック状態の追跡用
        std::vector<Target> target_stack;

        // loop 情報
        // ネストされた loop に対応する
        std::vector<LoopFrame> loop_stack;

        // compile 済みの関数テーブル
        std::unordered_map<std::string, CompiledFnDef> funcs;

        // ユーザー定義関数本体の compile 中か
        bool in_function = false;

        // stack 頂上の絶対 index を返す
        StackIndex stack_top() const;

        // 定数をテーブルに登録し、テーブル内の index を返す
        std::uint8_t add_literal(VmValue value);

        // 命令を追加し、その命令位置を返す
        InstIndex add_inst(OpCode op, std::uint8_t arg_0);

        // 定数の読み込み命令を生成し、スタック状態を更新する
        InstIndex add_load_literal_inst(std::uint8_t literal_index);

        // bin op 命令を生成し、演算結果のスタック位置を返す
        StackIndex add_bin_op_inst(OpCode op);

        // 指定位置の値を stack 頂上にコピーする命令を生成する
        InstIndex add_copy_inst(StackIndex stack_index);

        // stack 頂上の値を指定位置へ移動する命令を生成する
        InstIndex add_store_inst(StackIndex stack_index);

        // Jf 命令を生成する
        // 生成時、ジャンプ先は未確定
        InstIndex add_jf_inst();

        // 指定位置を残し、それより上の stack 値を取り除く
        std::optional<InstIndex> add_pop_until_inst(StackIndex stack_index);

        // 名前に対応するローカル変数の stack 位置を検索する
        std::optional<StackIndex> find_local(std::string_view name) const;

        // ジャンプ先を現在の命令列の末尾に設定する
        void fixup_jmp(InstIndex inst_index);

        // continue 経路の Dup 数とジャンプ先を確定する
        void fixup_continues();

        // Pop 命令を生成し、compile 時の stack 状態も縮小する
        void discard_to(std::size_t size);

        // Pop 命令だけを生成し、compile 時の stack 状態は維持する
        void emit_pop_to(std::size_t size);

        // 式を compile し、結果の stack 位置を返す
        StackIndex compile_expr(const Expression &expression);

        // 文の列を compile
        // 最後が式文なら結果位置を返す
        std::optional<StackIndex> compile_statements(const Statements &statements);

        // トップレベルコードを compile し、main 関数として登録する
        void compile_program(const Statements &statements);

        // 各文の compile
        void compile_if(const IfStatement &node);
        void compile_for(const ForStatement &node);
        void compile_break(const BreakStatement &node);
        void compile_continue(const ContinueStatement &node);
        void compile_function(const FunctionDefinition &node);

        // 定数テーブルと命令列を読みやすい形式で表示する
        void disasm(std::ostream &writer) const;

        void write_literals(std::ostream &writer) const;
        void write_insts(std::ostream &writer) const;

        // 関数テーブルを書き出す
        void write_funcs(std::ostream &writer) const;
    };

    // compile 結果
    using CompileResult = std::variant<Compiler, CompileError>;

    // プログラムを compile
    // CompileErrorは戻り値として返し、内部エラーの例外は伝播させる
    CompileResult compile_program_checked(const Statements &statements);

    // byte code の実行
    struct Vm
    {
        // 関数名から compile 済み関数を検索するテーブル
        std::unordered_map<std::string, CompiledFnDef> funcs;

        // 関数テーブルを読み込む
        void read_funcs(std::istream &reader);

        // main 関数の実行
        std::optional<VmValue> interpret() const;

        // 指定した関数の実行
        std::optional<VmValue> interpret(std::string_view fn_name,
                                         std::span<const VmValue> args) const;

        // 実行中に参照するネイティブ関数のテーブルを設定する
        void set_native_functions(const NativeFunctionRegistry &registry);

    private:
        // 指定した命令列を、引数を初期 stack として実行する
        // 呼び出しごとに独立した評価 stack を使用する
        std::optional<VmValue> execute(std::span<const VmValue> literal_view,
                                       std::span<const Instruction> code,
                                       std::span<const VmValue> args) const;

        const NativeFunctionRegistry *m_native_functions = nullptr;
    };

    // compile 済みプログラムをファイルに保存する
    void write_program(const std::filesystem::path &path, const Compiler &compiler);

    // ファイルから compile 済みプログラムを復元する
    Vm read_program(const std::filesystem::path &path);
}