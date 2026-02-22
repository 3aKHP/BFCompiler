#pragma once
#include "ir.h"
#include <vector>
#include <string>

namespace bf {

// 将过滤后的BF字符序列解析为IR指令
// 验证括号匹配，失败时抛出 std::runtime_error
std::vector<IRInst> parse(const std::vector<char>& tokens);

} // namespace bf
