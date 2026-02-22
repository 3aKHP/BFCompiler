# 第二篇：前端处理与中间表示 (IR)

在上一篇中，我们提到了**前端 (Frontend)** 这个概念。在编译器的世界里，前端就像是流水线上的质检员和分拣员——它负责把用户写的、乱七八糟的源代码，整理成程序内部好处理的标准格式。

在这个项目中，前端工作分为两步：**词法分析 (Lexer)** 和 **语法分析 (Parser)**。它们最终会吐出一个叫 **IR（中间表示）** 的东西。

## 1. 词法分析 (Lexer)：大浪淘沙

假设你写了这样一段代码：
```bf
加五次
+++++
循环开始 [
  减一次 -
  右移 >
]
```

对于 BF 来说，汉字和空格都是没用的废话。Lexer 的工作就是把这些废话扔掉，只保留那 8 个特殊字符。

在 C++ 中，我们用 `std::vector<char>`（字符数组）来装过滤后的结果。代码非常简单：

```cpp
std::vector<char> lex(const std::string& source) {
    std::vector<char> tokens;
    // 遍历整个字符串
    for (char c : source) {
        // 如果是这 8 个字符之一，就留下来
        if (c == '>' || c == '<' || c == '+' || c == '-' ||
            c == '.' || c == ',' || c == '[' || c == ']') {
            tokens.push_back(c);
        }
    }
    return tokens;
}
```
经过 Lexer 过滤后，上面的代码就变成了干干净净的：`['+', '+', '+', '+', '+', '[', '-', '>', ']']`。

## 2. 为什么要引入 IR (中间表示)？

既然代码已经干净了，我们为什么不直接拿着这个字符数组去执行或者生成机器码呢？

原因有两个：
1. **字符不好携带额外信息**：比如 `>`，它只是一个字符。如果我们要把它优化成"向右移动 3 格"，用字符没法表示。
2. **括号匹配太麻烦**：在执行 `[` 和 `]` 时，我们需要知道对方的位置。如果用字符数组，每次遇到 `[` 都要往后挨个找匹配的 `]`，非常慢。

所以，我们要把"字符"升级成一个"结构体" (Struct)。这个结构体就是 **IR (Intermediate Representation)**。

我们在 `common/include/bf/ir.h` 里这样定义它：

```cpp
// 1. 给每种指令起个好听的名字 (枚举)
enum class IRType {
    MovePtr,    // 代表 > 或 <
    AddVal,     // 代表 + 或 -
    Output,     // 代表 .
    Input,      // 代表 ,
    LoopBegin,  // 代表 [
    LoopEnd,    // 代表 ]
    SetZero,    // 这是我们留给优化的后手，先不管它
};

// 2. 把指令包装成结构体
struct IRInst {
    IRType type;           // 指令的类型
    int operand = 0;       // 操作数（比如移动多少格，加减多少）
    int jump_target = -1;  // 跳跃目标（如果是括号，记录对应括号的编号）
};
```

把 `>` 变成 `IRInst{ type=MovePtr, operand=1 }`，把 `<` 变成 `IRInst{ type=MovePtr, operand=-1 }`。这样是不是看起来专业多了，而且能装下数字了？

## 3. 语法分析 (Parser)：给括号找对象

Parser 的任务就是把 `std::vector<char>` 变成 `std::vector<IRInst>`，并在转换的过程中，把 `jump_target` 给填好。

**难点：怎么匹配括号？**
遇到 `[` 时，我们怎么知道它的 `]` 在哪里？这就需要用到 C++ 里的神器：**栈 (std::stack)**。栈就像一个单开口的桶，后放进去的东西先拿出来（后进先出）。

来看一段简化的 Parser 核心逻辑：
```cpp
std::vector<IRInst> program;
std::stack<int> loop_stack; // 这个桶用来装 '[' 的编号

for (char c : tokens) {
    IRInst inst{};
    
    if (c == '>') { inst.type = IRType::MovePtr; inst.operand = 1; }
    else if (c == '<') { inst.type = IRType::MovePtr; inst.operand = -1; }
    // ... + - . , 的处理类似 ...
    
    else if (c == '[') {
        inst.type = IRType::LoopBegin;
        // 把当前这条指令的编号（也就是将要放进 program 的位置）扔进桶里
        loop_stack.push( program.size() ); 
    }
    
    else if (c == ']') {
        inst.type = IRType::LoopEnd;
        // 1. 从桶里拿出一个最近放进去的 '[' 的编号
        int open_index = loop_stack.top(); 
        loop_stack.pop();
        
        // 2. 互相交换微信号（记录对方的位置）
        inst.jump_target = open_index; // 我指向你
        program[open_index].jump_target = program.size(); // 你也指向我
    }
    
    // 把装配好的指令放进大部队里
    program.push_back(inst);
}
```

### 图解括号匹配过程

假设代码是 `[ + ]`，解析过程如下：

1. 遇到 `[`，生成指令 0：`LoopBegin`。把编号 `0` 压入栈。
   * *栈：[ 0 ]*
2. 遇到 `+`，生成指令 1：`AddVal`。
   * *栈：[ 0 ]*
3. 遇到 `]`，生成指令 2：`LoopEnd`。
   * 从栈里弹出 `0`。
   * 设置指令 2 (`]`) 的 target 为 `0`。
   * 回去修改指令 0 (`[`) 的 target 为 `2`。
   * *栈：空*

最终的 IR 数组长这样：
* `0: LoopBegin (jump_target = 2)`
* `1: AddVal(1)`
* `2: LoopEnd (jump_target = 0)`

完美！以后无论是解释器还是编译器，遇到 `[` 只要看一下 `jump_target`，就知道该往哪里跳，再也不用像无头苍蝇一样去数括号了。

## 4. 总结

前端的工作就像是"备菜"。Lexer 把烂菜叶子挑走，Parser 把菜切好、装进名为 IR 的盘子里，还贴心地把有关联的菜（括号）用红绳绑在了一起。

现在，我们的菜已经切好了。在下一篇《让程序跑得更快——基础优化》中，我们将学习如何对这些装进盘子的 IR 动些手脚，让它变得更精简、更高效！
