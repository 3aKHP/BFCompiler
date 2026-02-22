#include "codegen.h"

namespace bf {

// 前向声明各后端的创建函数
std::unique_ptr<CodeGenerator> create_masm_codegen();
std::unique_ptr<CodeGenerator> create_nasm_codegen();
std::unique_ptr<CodeGenerator> create_att_codegen();

std::unique_ptr<CodeGenerator> create_codegen(AsmFormat fmt) {
    switch (fmt) {
        case AsmFormat::MASM: return create_masm_codegen();
        case AsmFormat::NASM: return create_nasm_codegen();
        case AsmFormat::ATT:  return create_att_codegen();
    }
    return create_nasm_codegen(); // 默认NASM
}

} // namespace bf
