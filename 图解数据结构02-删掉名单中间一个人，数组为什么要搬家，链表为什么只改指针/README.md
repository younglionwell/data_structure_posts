# 图解数据结构 02：删掉名单中间一个人，数组为什么要搬家，链表为什么只改指针？

本目录保存文章的配套 C11 示例，覆盖线性表这一章的主要实现与应用：

- 动态顺序表：初始化、扩容、按 0 基下标插入、删除和访问；
- 带内嵌哨兵的单链表：插入、删除、遍历和销毁；
- 两个非递减单链表的线性合并，保留重复元素且不修改输入；
- 一元稀疏多项式的有序链表表示、同类项合并和多项式加法；
- 空表、越界、无序输入、输出别名、零项抵消和系数溢出等边界测试。

代码中的位置统一使用 0 基下标。顺序表和链表拥有自己申请的内存，初始化后必须调用相应的销毁函数；合并与多项式加法复制输入，不接管输入对象的所有权。

编译并运行：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
  linear_list_demo.c -o linear_list_demo

./linear_list_demo
```

使用 AddressSanitizer 与 UndefinedBehaviorSanitizer 检查：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  linear_list_demo.c -o linear_list_demo_sanitize

./linear_list_demo_sanitize
```

应看到：

```text
顺序表在下标 1 插入 15: [10, 15, 20]
两个有序链表合并: [1, 2, 3, 3, 7, 8]
P(x) + Q(x) = (3)x^5 + (4)x^4
边界测试：全部通过。
```
