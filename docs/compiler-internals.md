# BF Compiler 技术文档：从源码到可执行文件

本文档深入讲解 `bf-compiler` 子项目的工作原理——如何将 Brainfuck 源码直接编译为 Windows x64 PE 可执行文件，不依赖任何外部汇编器或链接器。

---

## 目录

1. [编译器总览](#1-编译器总览)
2. [第一阶段：前端处理](#2-第一阶段前端处理)
3. [第二阶段：x86-64 机器码生成](#3-第二阶段x86-64-机器码生成)
4. [第三阶段：PE 文件构造](#4-第三阶段pe-文件构造)
5. [完整编译流程示例](#5-完整编译流程示例)

---

## 1. 编译器总览

传统编译器的工作流程是：

```
源码 → 词法分析 → 语法分析 → IR → 优化 → 汇编代码 → 汇编器 → 目标文件 → 链接器 → 可执行文件
```

我们的编译器跳过了汇编器和链接器，直接从 IR 生成可执行文件：

```
BF源码 → Lexer → Parser → IR → Optimizer → 机器码生成 → PE文件构造 → .exe
```

整个过程在一个程序内完成，输入 `.bf` 文件，输出可直接运行的 `.exe`。

### 核心模块

| 模块 | 文件 | 职责 |
|------|------|------|
| 词法分析器 | `common/src/lexer.cpp` | 过滤非BF字符 |
| 解析器 | `common/src/parser.cpp` | 生成IR，匹配括号 |
| 优化器 | `common/src/optimizer.cpp` | 指令合并、清零识别 |
| 机器码生成 | `compiler/src/pe_codegen.h` | IR → x86-64 字节码 |
| PE构造器 | `compiler/src/pe_writer.cpp` | 组装PE文件结构 |

---

## 2. 第一阶段：前端处理

### 2.1 词法分析 (Lexer)

BF 只有 8 个有效字符：`> < + - . , [ ]`，其余全部忽略。

```cpp
// common/src/lexer.cpp
std::vector<char> lex(const std::string& source) {
    std::vector<char> tokens;
    for (char c : source) {
        if (c == '>' || c == '<' || c == '+' || c == '-' ||
            c == '.' || c == ',' || c == '[' || c == ']') {
            tokens.push_back(c);
        }
    }
    return tokens;
}
```

例如输入 `++[>+<-] Hello!`，输出 `['+','+','[','>','+','<','-',']']`。注释和空格被自然过滤。

### 2.2 解析器 (Parser)

解析器将字符序列转换为 **中间表示 (IR)**，同时验证括号匹配：

```cpp
enum class IRType {
    MovePtr,    // > <
    AddVal,     // + -
    Output,     // .
    Input,      // ,
    LoopBegin,  // [
    LoopEnd,    // ]
    SetZero,    // [-] 优化产物
};

struct IRInst {
    IRType type;
    int operand;       // MovePtr/AddVal 的偏移量
    int jump_target;   // 循环配对的指令索引
};
```

关键设计：每条 `LoopBegin` 和 `LoopEnd` 指令都记录了配对指令的索引（`jump_target`），这样后端生成跳转指令时可以直接查找目标地址。

解析器使用栈来匹配括号：

```
遇到 '[' → 压栈当前指令索引
遇到 ']' → 弹栈得到配对的 '[' 索引，互相记录 jump_target
栈非空结束 → 报错 "Unmatched '['"
```

### 2.3 优化器 (Optimizer)

优化器执行三个 pass：

**Pass 1：连续指令合并**

将连续的同类指令合并为一条带计数的指令：

```
原始：  > > > + + + + +
合并后：MovePtr(3)  AddVal(5)
```

这将 N 条指令压缩为 1 条，后端只需生成一条机器指令。

**Pass 2：清零循环识别**

`[-]` 是 BF 中最常见的模式，含义是"将当前单元格置零"。优化器将其识别为单条 `SetZero` 指令：

```
原始：  LoopBegin  AddVal(-1)  LoopEnd
优化后：SetZero
```

未优化时需要循环执行（值为 N 就循环 N 次），优化后只需一条 `mov byte [rbx], 0`。

**Pass 3：死代码消除**

程序开头的循环永远不会执行（因为 tape 初始全为 0），直接移除。

优化完成后，重新计算所有 `jump_target` 索引。

---

## 3. 第二阶段：x86-64 机器码生成

这是编译器最核心的部分。我们需要将 IR 指令翻译为 x86-64 机器码的原始字节。

### 3.1 寄存器分配

BF 虚拟机只有一个"数据指针"，我们将其映射到 CPU 寄存器：

| 用途 | 寄存器 | 说明 |
|------|--------|------|
| 数据指针 | `rbx` | 指向 tape 中当前单元格 |
| stdout 句柄 | `r12` | 缓存 GetStdHandle(-11) 的返回值 |
| stdin 句柄 | `r13` | 缓存 GetStdHandle(-10) 的返回值 |

选择 `rbx`、`r12`、`r13` 是因为它们是 Windows x64 ABI 中的 **被调用者保存寄存器 (callee-saved)**，调用 Windows API 时不会被破坏。

### 3.2 函数序言 (Prologue)

```nasm
push rbx          ; 保存被调用者保存寄存器
push r12
push r13
sub rsp, 48       ; 分配栈空间（shadow space + 对齐）
lea rbx, [tape]   ; rbx = 数据指针，指向 tape 起始
```

**栈对齐**：Windows x64 ABI 要求调用函数时 RSP 必须 16 字节对齐。PE 入口点被调用时 RSP 是 8 字节对齐的（因为 `call` 压入了 8 字节返回地址）。3 次 push（24 字节）+ sub 48 = 总共偏移 80 字节，8 + 80 = 88... 不对，让我重新算：

```
进入时 RSP 对齐情况：
  call 压入返回地址 → RSP ≡ 8 (mod 16)
  push rbx         → RSP ≡ 0 (mod 16)
  push r12         → RSP ≡ 8 (mod 16)
  push r13         → RSP ≡ 0 (mod 16)
  sub rsp, 48      → RSP ≡ 0 (mod 16)  ← 但调用前需要 ≡ 0
```

实际上 `call` 指令本身会再压入返回地址使 RSP 变为 8 mod 16，所以在 `call` 之前 RSP 需要是 16 对齐的。sub rsp,48 后 RSP ≡ 0 (mod 16)，满足要求。48 字节中 32 字节是 shadow space（Windows x64 ABI 要求），剩余 16 字节用于第 5 个参数和对齐。

### 3.3 IR 到机器码的翻译

每条 IR 指令对应一段固定的机器码字节序列：

#### MovePtr(n) — 移动数据指针

```
MovePtr(1)  → 48 FF C3           ; inc rbx
MovePtr(-1) → 48 FF CB           ; dec rbx
MovePtr(n)  → 48 81 C3 [n:32]   ; add rbx, n
MovePtr(-n) → 48 81 EB [n:32]   ; sub rbx, n
```

`48` 是 REX.W 前缀，表示 64 位操作。对于 ±1 的情况使用更短的 `inc`/`dec` 指令。

#### AddVal(n) — 修改当前单元格

```
AddVal(1)   → FE 03              ; inc byte [rbx]
AddVal(-1)  → FE 0B              ; dec byte [rbx]
AddVal(n)   → 80 03 [n:8]       ; add byte [rbx], n
AddVal(-n)  → 80 2B [n:8]       ; sub byte [rbx], n
```

注意这里操作的是 **字节**（byte），因为 BF 的每个单元格是 8 位。

#### SetZero — 清零

```
SetZero → C6 03 00               ; mov byte [rbx], 0
```

一条指令替代了整个 `[-]` 循环。

#### Output — 输出字符

输出需要调用 Windows API `WriteFile`：

```c
// WriteFile(hStdOut, ptr, 1, &written, NULL)
// 参数：rcx=句柄, rdx=缓冲区, r8=长度, r9=&written, [rsp+32]=NULL
```

对应的机器码：

```
4C 89 E1              ; mov rcx, r12        (stdout 句柄)
48 89 DA              ; mov rdx, rbx        (当前单元格地址)
41 B8 01000000        ; mov r8d, 1          (写1字节)
4C 8D 0D [disp:32]   ; lea r9, [rip+written] (输出字节数地址)
48 C7 44 24 20 00...  ; mov qword [rsp+32], 0 (第5个参数=NULL)
FF 15 [disp:32]       ; call [rip+WriteFile]  (通过IAT间接调用)
```

#### LoopBegin / LoopEnd — 循环

```
LoopBegin:
  80 3B 00              ; cmp byte [rbx], 0
  0F 84 [offset:32]     ; jz <LoopEnd之后>    (条件跳转，32位相对偏移)

LoopEnd:
  80 3B 00              ; cmp byte [rbx], 0
  0F 85 [offset:32]     ; jnz <LoopBegin>     (条件跳转，32位相对偏移)
```

### 3.4 跳转地址解析

循环跳转是编译器中最棘手的部分。问题在于：`LoopBegin` 需要跳转到 `LoopEnd` 之后，但生成 `LoopBegin` 时还不知道 `LoopEnd` 在哪里。

解决方案：**前向引用回填 (Forward Patching)**

```
第一遍：顺序生成机器码
  - 遇到 LoopBegin → 记录需要回填的位置，先写入 0 占位
  - 遇到 LoopEnd   → 此时 LoopBegin 的位置已知，直接计算偏移

第二遍：回填所有前向跳转
  - 遍历所有 LoopBegin 的占位位置
  - 计算到对应 LoopEnd 之后的相对偏移
  - 用 patch32() 写入正确的偏移值
```

具体实现：

```cpp
// 记录每条IR指令对应的机器码偏移
std::vector<size_t> inst_offsets(prog.size());

// LoopEnd 的跳转（向后跳）可以直接计算
case IRType::LoopEnd:
    size_t target_off = inst_offsets[inst.jump_target]; // LoopBegin 的偏移
    int32_t rel = target_off - (current_offset + 4);    // 相对偏移
    emit(rel);

// LoopBegin 的跳转（向前跳）需要回填
case IRType::LoopBegin:
    fwd_patches.push_back({current_offset, inst.jump_target});
    emit(0); // 占位

// 所有指令生成完毕后回填
for (auto& p : fwd_patches) {
    size_t target_code = inst_offsets[p.target_inst + 1]; // LoopEnd 之后
    int32_t rel = target_code - (p.patch_off + 4);
    code.patch32(p.patch_off, rel);
}
```

### 3.5 RIP 相对寻址

x86-64 引入了 **RIP 相对寻址**，这是我们访问全局数据（tape、IAT）的方式：

```
指令中的位移 = 目标地址的 RVA - 当前指令结束位置的 RVA
```

例如，`lea rbx, [rip + tape]` 的位移计算：

```cpp
auto rip_rel = [&](uint32_t target_rva) {
    uint32_t next_ip_rva = text_rva + current_code_size + 4; // +4 是位移字段本身的大小
    uint32_t disp = target_rva - next_ip_rva;
    emit32(disp);
};
```

这种寻址方式的好处是生成的代码是 **位置无关的**（PIC），不需要重定位。

---

## 4. 第三阶段：PE 文件构造

生成了机器码之后，我们需要把它包装成 Windows 能识别的 PE (Portable Executable) 格式。

### 4.1 PE 文件结构概览

```
┌──────────────────────────┐  文件偏移 0x0000
│     DOS Header           │  64 字节，以 "MZ" 开头
│     (e_lfanew → PE头)    │
├──────────────────────────┤  文件偏移 0x0040
│     PE Signature         │  "PE\0\0" (4 字节)
│     COFF File Header     │  20 字节
│     Optional Header      │  240 字节 (PE32+)
│       - Entry Point      │
│       - Image Base       │
│       - Import Table Dir │
│       - IAT Dir          │
├──────────────────────────┤
│     Section Headers      │  3 × 40 字节
│       .text              │
│       .idata             │
│       .data              │
├──────────────────────────┤  文件偏移 0x0200 (对齐到 512)
│     .text section        │  机器码
├──────────────────────────┤
│     .idata section       │  导入表
├──────────────────────────┤
│     .data section        │  tape[30000] + written + readcnt
└──────────────────────────┘
```

### 4.2 关键字段解释

**DOS Header**：历史遗留，Windows 仍然要求 PE 文件以 "MZ" 开头。唯一重要的字段是 `e_lfanew`，指向 PE 签名的位置。

**COFF File Header**：
- `Machine = 0x8664` — AMD64 架构
- `NumberOfSections = 3` — .text, .idata, .data
- `Characteristics = 0x0022` — 可执行文件 + 大地址感知

**Optional Header (PE32+)**：
- `Magic = 0x020B` — PE32+（64位）
- `AddressOfEntryPoint = 0x1000` — .text 段的 RVA，程序从这里开始执行
- `ImageBase = 0x140000000` — 首选加载地址
- `SectionAlignment = 0x1000` — 内存中段对齐（4KB 页）
- `FileAlignment = 0x200` — 文件中段对齐（512 字节）
- `Subsystem = 3` — CONSOLE 子系统（控制台程序）

### 4.3 三个段 (Section)

#### .text — 代码段

包含我们生成的 x86-64 机器码。属性：可执行 + 可读。

```cpp
sects[0].Characteristics = 0x60000020; // CODE | EXECUTE | READ
```

#### .idata — 导入表段

这是 PE 文件中最复杂的部分。它告诉 Windows 加载器：我们需要从 `kernel32.dll` 导入哪些函数。

导入表的内部结构：

```
偏移 0-19:   Import Directory Entry (kernel32.dll)
               ├── OriginalFirstThunk → ILT 的 RVA
               ├── Name → "kernel32.dll" 字符串的 RVA
               └── FirstThunk → IAT 的 RVA
偏移 20-39:  Null Entry (终止符)

偏移 40-79:  ILT (Import Lookup Table)
               ├── → Hint/Name "GetStdHandle"
               ├── → Hint/Name "WriteFile"
               ├── → Hint/Name "ReadFile"
               ├── → Hint/Name "ExitProcess"
               └── NULL (终止符)

偏移 80-119: IAT (Import Address Table)
               ├── → Hint/Name "GetStdHandle"  ← 加载后被替换为真实地址
               ├── → Hint/Name "WriteFile"
               ├── → Hint/Name "ReadFile"
               ├── → Hint/Name "ExitProcess"
               └── NULL

偏移 120+:   Hint/Name Table
               ├── [0x0000] "GetStdHandle\0"
               ├── [0x0000] "WriteFile\0"
               ├── [0x0000] "ReadFile\0"
               └── [0x0000] "ExitProcess\0"
             DLL Name: "kernel32.dll\0"
```

**工作原理**：
1. 文件中，ILT 和 IAT 的内容相同，都指向 Hint/Name 条目
2. Windows 加载器读取 ILT，在 `kernel32.dll` 中查找每个函数的地址
3. 加载器将找到的真实地址写入 IAT，覆盖原来的 Hint/Name 指针
4. 我们的代码通过 `call [rip + IAT_entry]` 间接调用，运行时就能跳转到正确的函数

这就是为什么我们的代码使用 `FF 15 [disp]`（间接调用）而不是 `E8 [disp]`（直接调用）——因为函数的真实地址在编译时未知，只有加载时才由 Windows 填入 IAT。

#### .data — 数据段

```
偏移 0-29999:     tape[30000]   BF 的内存带，初始全零
偏移 30000-30007: written[8]    WriteFile 的输出参数
偏移 30008-30015: readcnt[8]    ReadFile 的输出参数
```

### 4.4 两遍生成策略

一个微妙的问题：机器码中的 RIP 相对偏移依赖于各段的 RVA，而段的 RVA 又依赖于代码段的大小。这形成了循环依赖。

解决方案：**两遍生成**

```
第一遍：用估计的 RVA 生成代码，获取代码大小
         ↓
根据代码大小计算真实的段 RVA
         ↓
第二遍：用真实的 RVA 重新生成代码
```

```cpp
// 第一遍：估计 RVA
pe::CodeBuf dummy;
pe::gen_code(program, dummy, text_rva, est_iat_rva, est_data_rva);
uint32_t code_size = dummy.size();

// 计算真实 RVA
idata_rva = text_rva + align_up(code_size, SECT_ALIGN);
data_rva  = idata_rva + align_up(idata_size, SECT_ALIGN);

// 第二遍：用真实 RVA 生成最终代码
pe::CodeBuf code;
pe::gen_code(program, code, text_rva, real_iat_rva, data_rva);
```

### 4.5 文件写出

最后按顺序写出所有部分，每个段对齐到 `FILE_ALIGN`（512 字节）：

```
1. DOS Header + NT Headers + Section Headers → 对齐到 0x200
2. .text 段内容 → 对齐到 0x200
3. .idata 段内容 → 对齐到 0x200
4. .data 段内容 → 对齐到 0x200
```

---

## 5. 完整编译流程示例

以 `++++++++[>++++<-]>.` (输出字符 '@'，ASCII 64) 为例：

### 步骤 1：词法分析

```
输入: "++++++++[>++++<-]>."
输出: ['+','+','+','+','+','+','+','+','[','>','+','+','+','+','<','-',']','>','.']
```

### 步骤 2：解析为 IR

```
AddVal(1), AddVal(1), ..., AddVal(1),   // 8 个
LoopBegin(jump→16),
MovePtr(1), AddVal(1), AddVal(1), AddVal(1), AddVal(1),
MovePtr(-1), AddVal(-1),
LoopEnd(jump→8),
MovePtr(1), Output
```

### 步骤 3：优化

```
AddVal(8),              // 8个+合并
LoopBegin(jump→4),
MovePtr(1),             // 1个>
AddVal(4),              // 4个+合并
MovePtr(-1),            // 1个<
AddVal(-1),             // 1个-
LoopEnd(jump→0),
MovePtr(1),
Output
```

### 步骤 4：生成机器码

```asm
; Prologue
push rbx; push r12; push r13; sub rsp, 48
lea rbx, [rip + tape]
; GetStdHandle(-11) → r12, GetStdHandle(-10) → r13

; AddVal(8)
80 03 08                    ; add byte [rbx], 8

; LoopBegin → jz to after LoopEnd
80 3B 00                    ; cmp byte [rbx], 0
0F 84 [offset to after LoopEnd]

; MovePtr(1)
48 FF C3                    ; inc rbx

; AddVal(4)
80 03 04                    ; add byte [rbx], 4

; MovePtr(-1)
48 FF CB                    ; dec rbx

; AddVal(-1)
FE 0B                       ; dec byte [rbx]

; LoopEnd → jnz to LoopBegin
80 3B 00                    ; cmp byte [rbx], 0
0F 85 [offset to LoopBegin]

; MovePtr(1)
48 FF C3                    ; inc rbx

; Output → call WriteFile(r12, rbx, 1, &written, NULL)
4C 89 E1 48 89 DA 41 B8 01000000 ...
FF 15 [offset to IAT:WriteFile]

; xor ecx, ecx; call [ExitProcess]
33 C9 FF 15 [offset to IAT:ExitProcess]
```

### 步骤 5：包装为 PE

将上述机器码放入 .text 段，构造导入表放入 .idata 段，分配 30KB 的 tape 放入 .data 段，加上 PE 头部信息，写出 `.exe` 文件。

Windows 加载器读取这个文件时：
1. 解析 PE 头，将三个段映射到内存
2. 读取导入表，从 `kernel32.dll` 查找 4 个函数地址，写入 IAT
3. 跳转到 `AddressOfEntryPoint`（.text 段起始）开始执行

程序执行时，`tape[0]` 初始被设为 8，然后执行循环 8 次。每次循环给 `tape[1]` 加 4，并给 `tape[0]` 减 1。循环结束后 `tape[1] = 32`（输出为空格字符）。

编译器忠实地将上述 BF 语义翻译为 x86-64 机器码，其底层的循环控制流和内存访问模式与解释器执行的行为完全一致，但移除了指令分发的开销。

---

## 附录：为什么不用 gcc？

传统方案是生成汇编源码，然后调用 `nasm` 汇编 + `gcc`/`link` 链接。我们的方案直接生成 PE 文件，优势在于：

1. **零依赖**：用户不需要安装任何额外工具
2. **速度快**：省去了写临时文件、启动外部进程的开销
3. **教学价值**：深入理解了 PE 格式、x86-64 指令编码、Windows ABI 等底层知识
4. **完整性**：这才是"真正的编译器"——从源码到可执行文件，一步到位
