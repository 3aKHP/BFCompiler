#include "bf/lexer.h"
#include "bf/parser.h"
#include "bf/optimizer.h"
#include "codegen.h"
#include "pe_writer.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void print_usage() {
    std::cerr << "Usage:\n"
              << "  bf-compiler <input.bf> [-o output]           Generate PE executable\n"
              << "  bf-compiler <input.bf> --asm [-o output]     Generate assembly\n"
              << "  bf-compiler <input.bf> --asm --format=nasm   NASM format (default)\n"
              << "  bf-compiler <input.bf> --asm --format=masm   MASM format\n"
              << "  bf-compiler <input.bf> --asm --format=att    AT&T/GAS format\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) { print_usage(); return 1; }

    std::string input_file;
    std::string output_file;
    bool asm_mode = false;
    bf::AsmFormat fmt = bf::AsmFormat::NASM;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--asm") {
            asm_mode = true;
        } else if (arg.rfind("--format=", 0) == 0) {
            std::string f = arg.substr(9);
            if (f == "masm") fmt = bf::AsmFormat::MASM;
            else if (f == "nasm") fmt = bf::AsmFormat::NASM;
            else if (f == "att" || f == "gas") fmt = bf::AsmFormat::ATT;
            else { std::cerr << "Unknown format: " << f << "\n"; return 1; }
        } else if (arg == "-o" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg[0] != '-') {
            input_file = arg;
        }
    }

    if (input_file.empty()) { print_usage(); return 1; }

    std::ifstream file(input_file);
    if (!file) {
        std::cerr << "Error: cannot open '" << input_file << "'\n";
        return 1;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    auto tokens = bf::lex(ss.str());
    auto program = bf::parse(tokens);
    program = bf::optimize(program);

    if (asm_mode) {
        auto gen = bf::create_codegen(fmt);
        std::string code = gen->generate(program);

        if (output_file.empty()) {
            auto dot = input_file.rfind('.');
            output_file = (dot != std::string::npos)
                ? input_file.substr(0, dot) + gen->file_extension()
                : input_file + gen->file_extension();
        }

        std::ofstream out(output_file);
        if (!out) {
            std::cerr << "Error: cannot write '" << output_file << "'\n";
            return 1;
        }
        out << code;
        std::cout << "Assembly written to: " << output_file << "\n";
    } else {
        if (output_file.empty()) {
            auto dot = input_file.rfind('.');
            output_file = (dot != std::string::npos)
                ? input_file.substr(0, dot) + ".exe"
                : input_file + ".exe";
        }

        if (bf::write_pe(program, output_file)) {
            std::cout << "Executable written to: " << output_file << "\n";
        } else {
            std::cerr << "Error: failed to generate executable\n";
            return 1;
        }
    }

    return 0;
}
