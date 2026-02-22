#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bf {

enum class IRType {
    MovePtr,    // > < 合并
    AddVal,     // + - 合并
    Output,     // .
    Input,      // ,
    LoopBegin,  // [
    LoopEnd,    // ]
    SetZero,    // [-] 或 [+]
};

struct IRInst {
    IRType type;
    int operand = 0;       // MovePtr/AddVal 的偏移量
    int jump_target = -1;  // LoopBegin/LoopEnd 的配对索引
};

std::string ir_type_name(IRType type);

} // namespace bf
