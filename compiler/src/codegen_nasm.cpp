#include "codegen.h"
#include <sstream>
#include <stack>

namespace bf {

class NasmCodeGen : public CodeGenerator {
public:
    std::string generate(const std::vector<IRInst>& program) override {
        std::ostringstream o;
        int label_id = 0;
        std::stack<int> label_stack;

        o << "; BF Compiler output - NASM x86-64 for Windows\n";
        o << "default rel\n";
        o << "bits 64\n\n";
        o << "extern GetStdHandle\n";
        o << "extern WriteFile\n";
        o << "extern ReadFile\n";
        o << "extern ExitProcess\n\n";
        o << "section .bss\n";
        o << "tape:    resb 30000\n";
        o << "written: resq 1\n";
        o << "readcnt: resq 1\n\n";
        o << "section .text\n";
        o << "global main\n";
        o << "main:\n";
        o << "    push rbx\n";
        o << "    sub rsp, 48\n";
        o << "    lea rbx, [tape]\n\n";

        // 获取stdout和stdin句柄
        o << "    mov ecx, -11\n";
        o << "    call GetStdHandle\n";
        o << "    mov r12, rax\n";
        o << "    mov ecx, -10\n";
        o << "    call GetStdHandle\n";
        o << "    mov r13, rax\n\n";

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
                        o << "    add byte [rbx], " << inst.operand << "\n";
                    else
                        o << "    sub byte [rbx], " << -inst.operand << "\n";
                    break;
                case IRType::SetZero:
                    o << "    mov byte [rbx], 0\n";
                    break;
                case IRType::Output:
                    o << "    ; Output\n";
                    o << "    mov rcx, r12\n";
                    o << "    mov rdx, rbx\n";
                    o << "    mov r8, 1\n";
                    o << "    lea r9, [written]\n";
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
                    o << "    lea r9, [readcnt]\n";
                    o << "    push 0\n";
                    o << "    sub rsp, 32\n";
                    o << "    call ReadFile\n";
                    o << "    add rsp, 40\n";
                    break;
                case IRType::LoopBegin: {
                    int id = label_id++;
                    label_stack.push(id);
                    o << ".loop_start_" << id << ":\n";
                    o << "    cmp byte [rbx], 0\n";
                    o << "    je .loop_end_" << id << "\n";
                    break;
                }
                case IRType::LoopEnd: {
                    int id = label_stack.top();
                    label_stack.pop();
                    o << "    cmp byte [rbx], 0\n";
                    o << "    jne .loop_start_" << id << "\n";
                    o << ".loop_end_" << id << ":\n";
                    break;
                }
            }
        }

        o << "\n    xor ecx, ecx\n";
        o << "    call ExitProcess\n";
        return o.str();
    }

    std::string file_extension() override { return ".asm"; }
};

std::unique_ptr<CodeGenerator> create_nasm_codegen() {
    return std::make_unique<NasmCodeGen>();
}

} // namespace bf
