#pragma once
#include "bf/ir.h"
#include <string>
#include <vector>

namespace bf {

// 直接将BF IR编译为Windows x64 PE可执行文件
// 不依赖任何外部汇编器或链接器
bool write_pe(const std::vector<IRInst>& program, const std::string& output_path);

} // namespace bf
