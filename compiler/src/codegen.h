#pragma once
#include "bf/ir.h"
#include <string>
#include <vector>
#include <memory>

namespace bf {

class CodeGenerator {
public:
    virtual ~CodeGenerator() = default;
    virtual std::string generate(const std::vector<IRInst>& program) = 0;
    virtual std::string file_extension() = 0;
};

enum class AsmFormat { MASM, NASM, ATT };

std::unique_ptr<CodeGenerator> create_codegen(AsmFormat fmt);

} // namespace bf
