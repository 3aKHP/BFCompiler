#include "codegen.h"
#include <sstream>

namespace bf {

class MasmCodeGen : public CodeGenerator {
public:
    std::string generate(const std::vector<IRInst>& program) override {
        std::ostringstream o;
        int label_id = 0;

        o << "; BF Compiler output - MASM x86-64 for Windows\n";
        o << "extrn GetStdHandle : proc\n";
        o << "extrn WriteFile : proc\n";
        o << "extrn ReadFile : proc\n";
        o << "extrn ExitProcess : proc\n\n";
        o << ".data\n";
        o << "tape    db 30000 dup(0)\n";
        o << "written dq 0\n";
        o << "readcnt dq 0\n\n";
        o << ".code\n";
        o << "main proc\n";
        o << "    push rbx\n";
        o << "    sub rsp, 48\n";
        o << "    lea rbx, tape\n\n";

        // 获取stdout和stdin句柄
        o << "    mov ecx, -11\n";
        o << "    call GetStdHandle\n";
        o << "    mov r12, rax\n";  // r12 = stdout
        o << "    mov ecx, -10\n";
        o << "    call GetStdHandle\n";
        o << "    mov r13, rax\n\n";  // r13 = stdin

        for (size_t i = 0; i < program.size(); ++i) {
            const auto& inst = program[i];
            switch (inst.type) {
                case IRType::MovePtr:
                    if (inst.operand > 0)
                        o << "    add rbx, " << inst.operand << "\n";
                    else
                        o << "    sub rbx, " << -inst.operand << "\n";
                    break;
                case IRType::AddVal:
                    if (inst.operand > 0)
                        o << "    add byte ptr [rbx], " << inst.operand << "\n";
                    else
                        o << "    sub byte ptr [rbx], " << -inst.operand << "\n";
                    break;
                case IRType::SetZero:
                    o << "    mov byte ptr [rbx], 0\n";
                    break;
                case IRType::Output:
                    o << "    ; Output\n";
                    o << "    mov rcx, r12\n";
                    o << "    mov rdx, rbx\n";
                    o << "    mov r8, 1\n";
                    o << "    lea r9, written\n";
                    o << "    push 0\n";
                    o << "    sub rsp, 32\n";
                    o << "    call WriteFile\n";
                    o << "    add rsp, 40\n";
                    break;
                case IRType::Input:
                    o << "    ; Input\n";
                    o << "    mov rcx, r13\n";
                    o << "    mov rdx, rbx\n";
                    o << "    mov r8, 1\n";
                    o << "    lea r9, readcnt\n";
                    o << "    push 0\n";
                    o << "    sub rsp, 32\n";
                    o << "    call ReadFile\n";
                    o << "    add rsp, 40\n";
                    break;
                case IRType::LoopBegin:
                    o << "loop_start_" << label_id << ":\n";
                    o << "    cmp byte ptr [rbx], 0\n";
                    o << "    je loop_end_" << label_id << "\n";
                    ++label_id;
                    break;
                case IRType::LoopEnd: {
                    --label_id;
                    o << "    cmp byte ptr [rbx], 0\n";
                    o << "    jne loop_start_" << label_id << "\n";
                    o << "loop_end_" << label_id << ":\n";
                    break;
                }
            }
        }

        o << "\n    xor ecx, ecx\n";
        o << "    call ExitProcess\n";
        o << "main endp\n";
        o << "end\n";
        return o.str();
    }

    std::string file_extension() override { return ".asm"; }
};

std::unique_ptr<CodeGenerator> create_masm_codegen() {
    return std::make_unique<MasmCodeGen>();
}

} // namespace bf
