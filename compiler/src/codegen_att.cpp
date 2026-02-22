#include "codegen.h"
#include <sstream>
#include <stack>

namespace bf {

class AttCodeGen : public CodeGenerator {
public:
    std::string generate(const std::vector<IRInst>& program) override {
        std::ostringstream o;
        int label_id = 0;
        std::stack<int> label_stack;

        o << "# BF Compiler output - AT&T syntax x86-64 for Windows\n";
        o << ".global main\n\n";
        o << ".extern GetStdHandle\n";
        o << ".extern WriteFile\n";
        o << ".extern ReadFile\n";
        o << ".extern ExitProcess\n\n";
        o << ".bss\n";
        o << "tape:    .space 30000\n";
        o << "written: .space 8\n";
        o << "readcnt: .space 8\n\n";
        o << ".text\n";
        o << "main:\n";
        o << "    pushq %rbx\n";
        o << "    subq $48, %rsp\n";
        o << "    leaq tape(%rip), %rbx\n\n";

        // 获取stdout和stdin句柄
        o << "    movl $-11, %ecx\n";
        o << "    call GetStdHandle\n";
        o << "    movq %rax, %r12\n";
        o << "    movl $-10, %ecx\n";
        o << "    call GetStdHandle\n";
        o << "    movq %rax, %r13\n\n";

        for (size_t i = 0; i < program.size(); ++i) {
            const auto& inst = program[i];
            switch (inst.type) {
                case IRType::MovePtr:
                    if (inst.operand > 0)
                        o << "    addq $" << inst.operand << ", %rbx\n";
                    else
                        o << "    subq $" << -inst.operand << ", %rbx\n";
                    break;
                case IRType::AddVal:
                    if (inst.operand > 0)
                        o << "    addb $" << inst.operand << ", (%rbx)\n";
                    else
                        o << "    subb $" << -inst.operand << ", (%rbx)\n";
                    break;
                case IRType::SetZero:
                    o << "    movb $0, (%rbx)\n";
                    break;
                case IRType::Output:
                    o << "    # Output\n";
                    o << "    movq %r12, %rcx\n";
                    o << "    movq %rbx, %rdx\n";
                    o << "    movq $1, %r8\n";
                    o << "    leaq written(%rip), %r9\n";
                    o << "    pushq $0\n";
                    o << "    subq $32, %rsp\n";
                    o << "    call WriteFile\n";
                    o << "    addq $40, %rsp\n";
                    break;
                case IRType::Input:
                    o << "    # Input\n";
                    o << "    movq %r13, %rcx\n";
                    o << "    movq %rbx, %rdx\n";
                    o << "    movq $1, %r8\n";
                    o << "    leaq readcnt(%rip), %r9\n";
                    o << "    pushq $0\n";
                    o << "    subq $32, %rsp\n";
                    o << "    call ReadFile\n";
                    o << "    addq $40, %rsp\n";
                    break;
                case IRType::LoopBegin: {
                    int id = label_id++;
                    label_stack.push(id);
                    o << ".loop_start_" << id << ":\n";
                    o << "    cmpb $0, (%rbx)\n";
                    o << "    je .loop_end_" << id << "\n";
                    break;
                }
                case IRType::LoopEnd: {
                    int id = label_stack.top();
                    label_stack.pop();
                    o << "    cmpb $0, (%rbx)\n";
                    o << "    jne .loop_start_" << id << "\n";
                    o << ".loop_end_" << id << ":\n";
                    break;
                }
            }
        }

        o << "\n    xorl %ecx, %ecx\n";
        o << "    call ExitProcess\n";
        return o.str();
    }

    std::string file_extension() override { return ".s"; }
};

std::unique_ptr<CodeGenerator> create_att_codegen() {
    return std::make_unique<AttCodeGen>();
}

} // namespace bf
