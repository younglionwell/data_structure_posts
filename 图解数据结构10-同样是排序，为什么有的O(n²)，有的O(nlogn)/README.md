# 图解数据结构 10：同样是排序，为什么有的 O(n²)，有的 O(n log n)？

本目录保存文章的配套 C11 示例。记录同时带有 `key` 和原始序号，除验证排序结果外，还会检查算法是否保持相等关键字的原始相对次序。

示例覆盖：

- 直接插入、折半插入和二路插入排序；
- 希尔排序与快速排序；
- 简单选择、树状选择和堆排序；
- 自底向上的归并排序；
- 非负十进制整数的链式 LSD 基数排序；
- 空序列、单元素、重复键、逆序和极值等边界。

## 编译运行

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
  internal_sorting_demo.c -o internal_sorting_demo
./internal_sorting_demo
```

使用 AddressSanitizer 与 UndefinedBehaviorSanitizer 检查：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  internal_sorting_demo.c -o internal_sorting_demo_sanitize
./internal_sorting_demo_sanitize
```

程序最后应输出：

```text
All internal-sorting tests passed.
```
