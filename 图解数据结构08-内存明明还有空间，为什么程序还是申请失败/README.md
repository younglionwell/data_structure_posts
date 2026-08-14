# 图解数据结构 08：内存明明还有空间，为什么程序还是申请失败？

本目录保存文章的配套 C11 示例，用固定数组和偏移量建立一套可检查的动态存储管理模型，覆盖：

- 对齐分配、空闲块切分与相邻块合并；
- 首次适配、循环首次适配、最佳适配和最坏适配；
- 外部碎片与存储紧缩；
- 二进制伙伴系统；
- 引用计数和标记—清除垃圾回收。

这是一份教学模型，不是 `malloc` 的替代实现。代码刻意避免直接操作未定义的裸内存边界，方便用断言验证每次分配、回收和合并。

## 编译运行

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
  dynamic_storage_demo.c -o dynamic_storage_demo
./dynamic_storage_demo
```

使用 AddressSanitizer 与 UndefinedBehaviorSanitizer 检查：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  dynamic_storage_demo.c -o dynamic_storage_demo_sanitize
./dynamic_storage_demo_sanitize
```

程序最后应输出：

```text
All dynamic-storage-management tests passed.
```
