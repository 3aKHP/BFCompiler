#pragma once
#include "ir.h"
#include <vector>

namespace bf {

// 对IR指令序列进行优化
// - 连续指令合并 (MovePtr, AddVal)
// - 清零循环识别 [-] [+]
// - 死代码消除 (开头的循环)
std::vector<IRInst> optimize(const std::vector<IRInst>& program);

} // namespace bf
