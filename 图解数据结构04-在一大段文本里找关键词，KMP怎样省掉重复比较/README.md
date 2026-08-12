# 图解数据结构 04：在一大段文本里找关键词，KMP 怎样省掉重复比较？

本目录保存文章的配套 C11 示例，覆盖“串”这一章的主要表示、操作、模式匹配与边界处理：

- 自定义堆串的初始化、赋值、扩容、连接、求子串、比较、清空和销毁；
- 以 0 基字节偏移为统一位置语义，并显式维护 `length`、`capacity` 与末尾 `\0`；
- 连接长度溢出、求子串边界、输入输出别名和失败不破坏原值；
- 朴素模式匹配、前缀函数、KMP 搜索，以及一种明确约定下的 `next`、`nextval`；
- 空模式、未找到、重复字符、指定起点和 UTF-8 字节边界；
- 穷举所有长度不超过 6 的二元文本、长度不超过 5 的二元模式及各起始位置，用朴素结果交叉验证 KMP。

代码中的长度、位置、切片和匹配结果均按字节计算。它可以保存和搜索 UTF-8 字节序列，但不负责验证 UTF-8、Unicode 规范化、大小写折叠，也不会把字节偏移自动转换为码点或用户看到的字符位置。

编译并运行：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
  string_demo.c -o string_demo

./string_demo
```

使用 AddressSanitizer 与 UndefinedBehaviorSanitizer 检查：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  string_demo.c -o string_demo_sanitize

./string_demo_sanitize
```

应看到：

```text
堆串连接：数据结构（12 字节）
朴素匹配：位置 5；KMP：位置 5
模式串 abcac 的 prefix：0,0,0,1,0
UTF-8 说明：本示例的位置、长度和切片都按字节计算。
边界测试：全部通过。
```
