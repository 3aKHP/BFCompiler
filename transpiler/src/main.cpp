#include "bf/lexer.h"
#include "bf/parser.h"
#include "bf/optimizer.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string transpile_to_c(const std::vector<bf::IRInst>& program) {
    std::ostringstream out;
    int indent = 1;

    out << "#include <stdio.h>\n";
    out << "#include <string.h>\n\n";
    out << "int main(void) {\n";
    out << "    unsigned char tape[30000];\n";
    out << "    memset(tape, 0, sizeof(tape));\n";
    out << "    unsigned char *ptr = tape;\n\n";

    auto emit_indent = [&]() {
        for (int i = 0; i < indent; ++i) out << "    ";
    };

    for (const auto& inst : program) {
        switch (inst.type) {
            case bf::IRType::MovePtr:
                emit_indent();
                if (inst.operand > 0)
                    out << "ptr += " << inst.operand << ";\n";
                else
                    out << "ptr -= " << -inst.operand << ";\n";
                break;
            case bf::IRType::AddVal:
                emit_indent();
                if (inst.operand > 0)
                    out << "*ptr += " << inst.operand << ";\n";
                else
                    out << "*ptr -= " << -inst.operand << ";\n";
                break;
            case bf::IRType::Output:
                emit_indent();
                out << "putchar(*ptr);\n";
                break;
            case bf::IRType::Input:
                emit_indent();
                out << "*ptr = (unsigned char)getchar();\n";
                break;
            case bf::IRType::LoopBegin:
                emit_indent();
                out << "while (*ptr) {\n";
                ++indent;
                break;
            case bf::IRType::LoopEnd:
                --indent;
                emit_indent();
                out << "}\n";
                break;
            case bf::IRType::SetZero:
                emit_indent();
                out << "*ptr = 0;\n";
                break;
        }
    }

    out << "\n    return 0;\n";
    out << "}\n";
    return out.str();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: bf-transpiler <input.bf> [-o output.c]\n";
        return 1;
    }

    std::string input_file = argv[1];
    std::string output_file;

    // 解析 -o 参数
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "-o" && i + 1 < argc) {
            output_file = argv[++i];
        }
    }

    if (output_file.empty()) {
        // 默认输出文件名：替换扩展名为 .c
        output_file = input_file;
        auto dot = output_file.rfind('.');
        if (dot != std::string::npos)
            output_file = output_file.substr(0, dot) + ".c";
        else
            output_file += ".c";
    }

    std::ifstream file(input_file);
    if (!file) {
        std::cerr << "Error: cannot open file '" << input_file << "'\n";
        return 1;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    auto tokens = bf::lex(source);
    auto program = bf::parse(tokens);
    program = bf::optimize(program);

    std::string c_code = transpile_to_c(program);

    std::ofstream out(output_file);
    if (!out) {
        std::cerr << "Error: cannot write to '" << output_file << "'\n";
        return 1;
    }
    out << c_code;
    std::cout << "Transpiled to: " << output_file << "\n";

    return 0;
}
