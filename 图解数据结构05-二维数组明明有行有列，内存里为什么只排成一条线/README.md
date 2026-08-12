# 数组和广义表示例

配套文章：**《图解数据结构 05：二维数组明明有行有列，内存里为什么只排成一条线？》**

`array_generalized_list_demo.c` 是一份自包含的 C11 教学程序，演示：

- 二维数组按行优先映射到连续内存，并检查下标和长度乘法溢出；
- 对称矩阵只保存下三角，`A[i][j]` 与 `A[j][i]` 映射到同一位置；
- 稀疏矩阵的有序三元组表、快速转置和归并式加法；
- 广义表的头尾链表表示、括号文本解析、规范化输出、长度、深度、表头、表尾、深复制和销毁。

代码统一采用 0 基下标。广义表部分用 `NULL` 表示空表，约定原子深度为 0、空表深度为 1；解析器处理有限、无共享、无环的树形广义表。

## 编译运行

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
  array_generalized_list_demo.c -o array_generalized_list_demo
./array_generalized_list_demo
```

也可以打开 AddressSanitizer 与 UndefinedBehaviorSanitizer：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined \
  array_generalized_list_demo.c -o array_generalized_list_demo_san
./array_generalized_list_demo_san
```

正常输出：

```text
All array, sparse-matrix and generalized-list tests passed.
```
