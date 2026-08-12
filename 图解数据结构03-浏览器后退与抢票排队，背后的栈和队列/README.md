# 图解数据结构 03：浏览器后退与抢票排队，背后的栈和队列

本目录保存文章的配套 C11 示例，覆盖栈和队列这一章的主要实现、应用与边界处理：

- 可几何扩容的动态数组顺序栈；
- 三类括号的匹配、错误类型和错误位置；
- 双栈中缀表达式求值，支持非负十进制整数、二元 `+ - * /` 与圆括号；
- 带内嵌哨兵的链队列；
- 用 `size` 区分队空与队满、可使用全部数组槽位的固定容量循环队列；
- 按时间排序事件、使用 FIFO 等待队列的单服务台离散事件模拟；
- 空结构、容量环回、非法表达式、括号错误、除零、整数溢出和同一时刻事件等边界测试。

代码中的位置统一使用 0 基下标。动态栈、循环队列、事件表和链队列各自拥有申请的内存，初始化成功后必须调用对应销毁函数。中缀求值示例不支持一元正负号、小数、变量或隐式乘法，整数除法遵循 C 语言向 0 截断的规则。

编译并运行：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
  stack_queue_demo.c -o stack_queue_demo

./stack_queue_demo
```

使用 AddressSanitizer 与 UndefinedBehaviorSanitizer 检查：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  stack_queue_demo.c -o stack_queue_demo_sanitize

./stack_queue_demo_sanitize
```

应看到：

```text
顺序栈弹出：30（后进先出）
括号检查：位置 7 期待 ']'，实际 '}'
表达式 12 + 3 * (7 - 2) = 27
循环队列环回后：size=3, front=1, rear=1
服务台模拟：服务 4 人，总等待 9，结束时刻 10，最长队列 2
边界测试：全部通过。
```
