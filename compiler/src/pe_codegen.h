#pragma once
#include "pe_defs.h"
#include "bf/ir.h"
#include <vector>
#include <stack>

namespace bf {
namespace pe {

// RIP-relative patch: stores code offset where a 32-bit displacement needs patching
struct RipPatch {
    size_t code_offset; // offset in code buffer of the 4-byte displacement
    uint32_t target_rva; // absolute RVA of target
};

// Generate x86-64 machine code from BF IR
// iat_rva: RVA of IAT (GetStdHandle, WriteFile, ReadFile, ExitProcess - 4 entries, 8 bytes each)
// data_rva: RVA of .data section (tape[30000], written[8], readcnt[8])
// text_rva: RVA of .text section
inline void gen_code(const std::vector<IRInst>& prog, CodeBuf& c,
                     uint32_t text_rva, uint32_t iat_rva, uint32_t data_rva) {
    // IAT layout: [GetStdHandle][WriteFile][ReadFile][ExitProcess] each 8 bytes
    uint32_t iat_GetStdHandle = iat_rva;
    uint32_t iat_WriteFile    = iat_rva + 8;
    uint32_t iat_ReadFile     = iat_rva + 16;
    uint32_t iat_ExitProcess  = iat_rva + 24;

    // Data layout
    uint32_t d_tape    = data_rva;
    uint32_t d_written = data_rva + 30000;
    uint32_t d_readcnt = data_rva + 30008;

    // Helper: emit RIP-relative 32-bit displacement
    // RIP-relative addressing: disp = target_rva - (text_rva + code_offset_after_instr)
    auto rip_rel = [&](uint32_t target) {
        uint32_t next_ip_rva = text_rva + (uint32_t)c.size() + 4;
        uint32_t disp = target - next_ip_rva;
        c.u32(disp);
    };

    // Prologue: push rbx; push r12; push r13; sub rsp, 48
    // Stack alignment: entry RSP is 8-aligned (return addr pushed by call)
    // 3 pushes = 24 bytes, sub 48 => total 8+24+48 = 80 (16-aligned)
    c.u8(0x53);                         // push rbx
    c.u8(0x41); c.u8(0x54);            // push r12
    c.u8(0x41); c.u8(0x55);            // push r13
    c.u8(0x48); c.u8(0x83); c.u8(0xEC); c.u8(0x30); // sub rsp, 48

    // lea rbx, [rip + tape]
    c.u8(0x48); c.u8(0x8D); c.u8(0x1D); rip_rel(d_tape);

    // mov ecx, -11 (STD_OUTPUT_HANDLE)
    c.u8(0xB9); c.u32(0xFFFFFFF5);
    // call [rip + GetStdHandle]
    c.u8(0xFF); c.u8(0x15); rip_rel(iat_GetStdHandle);
    // mov r12, rax
    c.u8(0x49); c.u8(0x89); c.u8(0xC4);

    // mov ecx, -10 (STD_INPUT_HANDLE)
    c.u8(0xB9); c.u32(0xFFFFFFF6);
    // call [rip + GetStdHandle]
    c.u8(0xFF); c.u8(0x15); rip_rel(iat_GetStdHandle);
    // mov r13, rax
    c.u8(0x49); c.u8(0x89); c.u8(0xC5);

    // Track instruction index -> code offset for jump resolution
    std::vector<size_t> inst_offsets(prog.size());
    // Forward jump patches: code_offset of jz displacement, target inst index
    struct FwdPatch { size_t patch_off; size_t target_inst; };
    std::vector<FwdPatch> fwd_patches;

    for (size_t i = 0; i < prog.size(); ++i) {
        inst_offsets[i] = c.size();
        const auto& inst = prog[i];
        switch (inst.type) {
        case IRType::MovePtr:
            if (inst.operand == 1) {
                c.u8(0x48); c.u8(0xFF); c.u8(0xC3); // inc rbx
            } else if (inst.operand == -1) {
                c.u8(0x48); c.u8(0xFF); c.u8(0xCB); // dec rbx
            } else if (inst.operand > 0) {
                c.u8(0x48); c.u8(0x81); c.u8(0xC3); c.u32(inst.operand);
            } else {
                c.u8(0x48); c.u8(0x81); c.u8(0xEB); c.u32(-inst.operand);
            }
            break;
        case IRType::AddVal:
            if (inst.operand == 1) {
                c.u8(0xFE); c.u8(0x03); // inc byte [rbx]
            } else if (inst.operand == -1) {
                c.u8(0xFE); c.u8(0x0B); // dec byte [rbx]
            } else if (inst.operand > 0) {
                c.u8(0x80); c.u8(0x03); c.u8((uint8_t)inst.operand);
            } else {
                c.u8(0x80); c.u8(0x2B); c.u8((uint8_t)(-inst.operand));
            }
            break;
        case IRType::SetZero:
            c.u8(0xC6); c.u8(0x03); c.u8(0x00); // mov byte [rbx], 0
            break;
        case IRType::Output:
            // mov rcx, r12
            c.u8(0x4C); c.u8(0x89); c.u8(0xE1);
            // mov rdx, rbx
            c.u8(0x48); c.u8(0x89); c.u8(0xDA);
            // mov r8d, 1
            c.u8(0x41); c.u8(0xB8); c.u32(1);
            // lea r9, [rip + written]
            c.u8(0x4C); c.u8(0x8D); c.u8(0x0D); rip_rel(d_written);
            // mov qword [rsp+32], 0
            c.u8(0x48); c.u8(0xC7); c.u8(0x44); c.u8(0x24); c.u8(0x20);
            c.u32(0);
            // call [rip + WriteFile]
            c.u8(0xFF); c.u8(0x15); rip_rel(iat_WriteFile);
            break;
        case IRType::Input:
            // mov rcx, r13
            c.u8(0x4C); c.u8(0x89); c.u8(0xE9);
            // mov rdx, rbx
            c.u8(0x48); c.u8(0x89); c.u8(0xDA);
            // mov r8d, 1
            c.u8(0x41); c.u8(0xB8); c.u32(1);
            // lea r9, [rip + readcnt]
            c.u8(0x4C); c.u8(0x8D); c.u8(0x0D); rip_rel(d_readcnt);
            // mov qword [rsp+32], 0
            c.u8(0x48); c.u8(0xC7); c.u8(0x44); c.u8(0x24); c.u8(0x20);
            c.u32(0);
            // call [rip + ReadFile]
            c.u8(0xFF); c.u8(0x15); rip_rel(iat_ReadFile);
            break;
        case IRType::LoopBegin:
            // cmp byte [rbx], 0
            c.u8(0x80); c.u8(0x3B); c.u8(0x00);
            // jz <LoopEnd+1> (6 bytes: 0F 84 xx xx xx xx)
            c.u8(0x0F); c.u8(0x84);
            fwd_patches.push_back({c.size(), (size_t)inst.jump_target});
            c.u32(0); // placeholder
            break;
        case IRType::LoopEnd: {
            // cmp byte [rbx], 0
            c.u8(0x80); c.u8(0x3B); c.u8(0x00);
            // jnz <LoopBegin> (6 bytes: 0F 85 xx xx xx xx)
            c.u8(0x0F); c.u8(0x85);
            size_t target_off = inst_offsets[inst.jump_target];
            int32_t rel = (int32_t)target_off - (int32_t)(c.size() + 4);
            c.u32((uint32_t)rel);
            break;
        }
        }
    }

    // Epilogue: xor ecx,ecx; call [ExitProcess]
    c.u8(0x33); c.u8(0xC9);
    c.u8(0xFF); c.u8(0x15); rip_rel(iat_ExitProcess);

    // Record end offset for forward jump patching
    size_t epilogue_off = c.size();

    // Patch forward jumps (LoopBegin -> after LoopEnd)
    for (auto& p : fwd_patches) {
        size_t target_inst = p.target_inst;
        size_t target_code;
        if (target_inst + 1 < inst_offsets.size())
            target_code = inst_offsets[target_inst + 1];
        else
            target_code = epilogue_off;
        int32_t rel = (int32_t)target_code - (int32_t)(p.patch_off + 4);
        c.patch32(p.patch_off, (uint32_t)rel);
    }
}

} // namespace pe
} // namespace bf
