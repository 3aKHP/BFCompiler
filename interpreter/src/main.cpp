#include "bf/lexer.h"
#include "bf/parser.h"
#include "bf/optimizer.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <cstdint>

static constexpr int TAPE_SIZE = 30000;

static void interpret(const std::vector<bf::IRInst>& program) {
    std::vector<uint8_t> tape(TAPE_SIZE, 0);
    int ptr = 0;
    int ip = 0;
    int size = static_cast<int>(program.size());

    while (ip < size) {
        const auto& inst = program[ip];
        switch (inst.type) {
            case bf::IRType::MovePtr:
                ptr += inst.operand;
                break;
            case bf::IRType::AddVal:
                tape[ptr] += static_cast<uint8_t>(inst.operand);
                break;
            case bf::IRType::Output:
                std::putchar(tape[ptr]);
                break;
            case bf::IRType::Input:
                tape[ptr] = static_cast<uint8_t>(std::getchar());
                break;
            case bf::IRType::LoopBegin:
                if (tape[ptr] == 0) {
                    ip = inst.jump_target;
                }
                break;
            case bf::IRType::LoopEnd:
                if (tape[ptr] != 0) {
                    ip = inst.jump_target;
                }
                break;
            case bf::IRType::SetZero:
                tape[ptr] = 0;
                break;
        }
        ++ip;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: bf-interpreter <input.bf>\n";
        return 1;
    }

    std::ifstream file(argv[1]);
    if (!file) {
        std::cerr << "Error: cannot open file '" << argv[1] << "'\n";
        return 1;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    auto tokens = bf::lex(source);
    auto program = bf::parse(tokens);
    program = bf::optimize(program);
    interpret(program);

    return 0;
}
