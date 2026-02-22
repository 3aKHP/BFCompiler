#include "bf/parser.h"
#include <stack>
#include <stdexcept>

namespace bf {

std::vector<IRInst> parse(const std::vector<char>& tokens) {
    std::vector<IRInst> program;
    std::stack<int> loop_stack;

    for (char c : tokens) {
        IRInst inst{};
        switch (c) {
            case '>': inst.type = IRType::MovePtr;   inst.operand = 1;  break;
            case '<': inst.type = IRType::MovePtr;   inst.operand = -1; break;
            case '+': inst.type = IRType::AddVal;    inst.operand = 1;  break;
            case '-': inst.type = IRType::AddVal;    inst.operand = -1; break;
            case '.': inst.type = IRType::Output;    break;
            case ',': inst.type = IRType::Input;     break;
            case '[': {
                inst.type = IRType::LoopBegin;
                loop_stack.push(static_cast<int>(program.size()));
                break;
            }
            case ']': {
                if (loop_stack.empty()) {
                    throw std::runtime_error("Unmatched ']' found");
                }
                inst.type = IRType::LoopEnd;
                int open = loop_stack.top();
                loop_stack.pop();
                inst.jump_target = open;
                program.push_back(inst);
                program[open].jump_target = static_cast<int>(program.size()) - 1;
                continue; // 已经push了，跳过下面的push
            }
            default: continue;
        }
        program.push_back(inst);
    }

    if (!loop_stack.empty()) {
        throw std::runtime_error("Unmatched '[' found");
    }

    return program;
}

} // namespace bf
