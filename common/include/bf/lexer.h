#pragma once
#include <string>
#include <vector>

namespace bf {

// 过滤BF源码，只保留有效的BF指令字符
std::vector<char> lex(const std::string& source);

} // namespace bf
