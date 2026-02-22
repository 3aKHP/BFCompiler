# 第三篇：让程序跑得更快——基础优化 (Optimization)

上一篇我们把 BF 代码变成了结构化的 IR 数组。现在，程序已经可以运行了。但是，BF 程序员写出来的代码往往非常繁琐。

举个例子，要让一个格子的数字加 10，在 C 语言里你只需要写 `*ptr += 10;`，但在 BF 里你得手敲 10 个加号：`++++++++++`。

如果解释器每次遇到一个 `+` 就执行一次加 1，10 个加号就要执行 10 次。这就像你去菜市场买 10 根葱，老板每递给你一根，你就得掏一次钱，太慢了！为什么不一次性付 10 根的钱呢？

这就是**优化器 (Optimizer)** 要做的事情：找出代码里"笨拙"的写法，替换成"聪明"的写法。在这个项目中，我们实现了三种最基础也是最有效的优化。

---

## 1. 连续指令合并 (Run-Length Encoding)

这是最容易理解的优化。既然 `+` 和 `-` 是对数字的加减，`>` 和 `<` 是对指针的移动，我们完全可以把相邻的同类指令合并。

*   `>>>` 合并成 `MovePtr(3)`
*   `<<<<` 合并成 `MovePtr(-4)`
*   `++++` 合并成 `AddVal(4)`
*   `+-+` 合并成什么？加一减一再加一，结果就是 `AddVal(1)`！

### C++ 实现思路

我们新建一个空的 `result` 数组。然后从头到尾遍历原来的 IR 数组。
每次拿到一条指令，先看看它和 `result` 数组最后一条指令是不是"同类"。
如果是同类（比如都是加减，或者都是移动），就把它们的 `operand`（操作数）加起来；如果不是，就直接放进 `result` 里。

```cpp
std::vector<IRInst> merge_consecutive(const std::vector<IRInst>& program) {
    std::vector<IRInst> result;
    
    for (const auto& inst : program) {
        // 如果 result 不为空，且当前指令和最后一条指令是同类（都是移动或都是加减）
        if (!result.empty() && 
            result.back().type == inst.type &&
            (inst.type == IRType::MovePtr || inst.type == IRType::AddVal)) {
            
            // 直接把数字加起来！
            result.back().operand += inst.operand;
            
            // 如果加完之后变成了 0（比如 +-），这条指令就没用了，扔掉
            if (result.back().operand == 0) {
                result.pop_back();
            }
        } else {
            // 不同类，或者 result 是空的，直接放进去
            result.push_back(inst);
        }
    }
    return result;
}
```

经过这一步，原本上万条指令的 BF 代码，可能瞬间就缩减到了几千条。

---

## 2. 清零循环识别 (Zeroing Loop)

在 BF 中，如果你想把当前格子的数字清零，你会怎么写？
由于不知道当前格子里的数是多少，BF 程序员的唯一解法是：写一个循环，每次减 1，直到变成 0 为止。
代码就是：`[-]` 或 `[+]`。

如果我们不优化它，当格子里是 255 时，程序要执行 255 次判断、255 次减法、255 次跳转。
但从上帝视角来看，这不就是一句 `*ptr = 0;` 吗？

### 模式识别 (Pattern Matching)

我们要做的，就是在 IR 数组里"找规律"。如果我们发现连续的三条指令正好是：
1.  `LoopBegin` `[`
2.  `AddVal(-1)` 或者 `AddVal(1)` `+` 或 `-`
3.  `LoopEnd` `]`

我们就把这三条指令删掉，换成一条全新的指令：`SetZero`。

```cpp
std::vector<IRInst> detect_set_zero(const std::vector<IRInst>& program) {
    std::vector<IRInst> result;
    
    for (size_t i = 0; i < program.size(); ++i) {
        // 确保后面至少还有两条指令
        if (i + 2 < program.size() &&
            program[i].type == IRType::LoopBegin &&
            program[i + 1].type == IRType::AddVal &&
            (program[i + 1].operand == 1 || program[i + 1].operand == -1) &&
            program[i + 2].type == IRType::LoopEnd) {
            
            // 发现了清零循环！换成 SetZero
            IRInst sz{};
            sz.type = IRType::SetZero;
            result.push_back(sz);
            
            // 跳过已经被处理的循环体和右括号
            i += 2; 
        } else {
            // 没发现规律，原样保留
            result.push_back(program[i]);
        }
    }
    return result;
}
```

引入 `SetZero` 指令后，后端（无论是解释器还是编译器）只要看到它，直接把当前格子赋值为 0，一步到位！

---

## 3. 死代码消除 (Dead Code Elimination)

最后一种优化非常有趣。有些 BF 程序为了写注释，会把注释放在代码最开头的一个死循环里，比如：
```bf
[ 这里是注释，反正是死循环不会执行的 ]
+++++
```

为什么不会执行？因为 BF 规定，程序刚启动时，指针指向的第 0 个格子初始值必然是 0。
而 `[` 的规则是：如果当前是 0，就跳过循环。
所以，**整个程序开头的第一个循环，绝对不可能被执行！** 这种永远不会执行的代码，在编译原理中被称为**死代码 (Dead Code)**。

既然不会执行，我们留着它干嘛？不仅占空间，还浪费编译时间。直接删掉！

```cpp
std::vector<IRInst> eliminate_dead_code(const std::vector<IRInst>& program) {
    std::vector<IRInst> result;
    size_t i = 0;
    
    // 如果一开始就遇到了 '['，直接跳过整个大括号
    while (i < program.size() && program[i].type == IRType::LoopBegin) {
        int depth = 1;
        ++i;
        // 往后找，直到把匹配的 ']' 找到为止
        while (i < program.size() && depth > 0) {
            if (program[i].type == IRType::LoopBegin) ++depth;
            if (program[i].type == IRType::LoopEnd) --depth;
            ++i;
        }
    }
    
    // 开头的废话都跳过了，剩下的才是真正的代码，原样复制
    for (; i < program.size(); ++i) {
        result.push_back(program[i]);
    }
    return result;
}
```

---

## 4. 别忘了修补"红线"

在第一和第二步优化中，我们大量删减了指令（多条合成一条）。这会导致一个严重的问题：**原来算好的 `jump_target`（括号跳跃的目标编号）全乱套了！**

就像你把一本书撕掉了几页，目录里的页码就全错位了。

所以在优化器的最后一步，我们必须把之前 Parser 里写过的"括号匹配"逻辑再跑一遍，重新计算一遍正确的 `jump_target`。

```cpp
// 在 optimizer.cpp 的最后
std::vector<IRInst> optimize(const std::vector<IRInst>& program) {
    auto result = merge_consecutive(program); // 1. 合并
    result = detect_set_zero(result);         // 2. 清零识别
    result = eliminate_dead_code(result);     // 3. 删死代码
    recompute_jumps(result);                  // 4. 重新计算跳跃目标！
    return result;
}
```

到这里，我们的 IR 已经处于最佳状态了。短小、精悍、目标明确。下一篇，我们将看看如何用两种不同的方式（解释器和转译器）来让这些优秀的 IR 真正跑起来！