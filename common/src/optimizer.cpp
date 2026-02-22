#include "bf/optimizer.h"
#include <stack>

namespace bf {

// 第一遍：合并连续的 MovePtr 和 AddVal
static std::vector<IRInst> merge_consecutive(const std::vector<IRInst>& program) {
    std::vector<IRInst> result;
    for (const auto& inst : program) {
        if (!result.empty() && result.back().type == inst.type &&
            (inst.type == IRType::MovePtr || inst.type == IRType::AddVal)) {
            result.back().operand += inst.operand;
            // 如果合并后为0，移除
            if (result.back().operand == 0) {
                result.pop_back();
            }
        } else {
            result.push_back(inst);
        }
    }
    return result;
}

// 第二遍：识别清零循环 [-] 和 [+]
static std::vector<IRInst> detect_set_zero(const std::vector<IRInst>& program) {
    std::vector<IRInst> result;
    for (size_t i = 0; i < program.size(); ++i) {
        if (i + 2 < program.size() &&
            program[i].type == IRType::LoopBegin &&
            program[i + 1].type == IRType::AddVal &&
            (program[i + 1].operand == 1 || program[i + 1].operand == -1) &&
            program[i + 2].type == IRType::LoopEnd) {
            IRInst sz{};
            sz.type = IRType::SetZero;
            result.push_back(sz);
            i += 2; // 跳过 AddVal 和 LoopEnd
        } else {
            result.push_back(program[i]);
        }
    }
    return result;
}

// 第三遍：死代码消除（开头的循环不会执行）
static std::vector<IRInst> eliminate_dead_code(const std::vector<IRInst>& program) {
    std::vector<IRInst> result;
    size_t i = 0;
    // 跳过开头的循环（初始值为0，不会进入）
    while (i < program.size() && program[i].type == IRType::LoopBegin) {
        int depth = 1;
        ++i;
        while (i < program.size() && depth > 0) {
            if (program[i].type == IRType::LoopBegin) ++depth;
            if (program[i].type == IRType::LoopEnd) --depth;
            ++i;
        }
    }
    // 复制剩余指令
    for (; i < program.size(); ++i) {
        result.push_back(program[i]);
    }
    return result;
}

// 重新计算 jump_target
static void recompute_jumps(std::vector<IRInst>& program) {
    std::stack<int> loop_stack;
    for (int i = 0; i < static_cast<int>(program.size()); ++i) {
        if (program[i].type == IRType::LoopBegin) {
            loop_stack.push(i);
        } else if (program[i].type == IRType::LoopEnd) {
            int open = loop_stack.top();
            loop_stack.pop();
            program[i].jump_target = open;
            program[open].jump_target = i;
        }
    }
}

std::vector<IRInst> optimize(const std::vector<IRInst>& program) {
    auto result = merge_consecutive(program);
    result = detect_set_zero(result);
    result = eliminate_dead_code(result);
    recompute_jumps(result);
    return result;
}

} // namespace bf
