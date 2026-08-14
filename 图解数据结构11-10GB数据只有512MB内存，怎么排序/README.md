# 图解数据结构 11：10GB 数据只有 512MB 内存，怎么排序？

本目录保存文章的配套 C11 示例，通过真实二进制临时文件演示外部排序：

- 固定长度初始归并段的生成；
- 使用选择树完成 k 路多趟归并；
- 按逻辑块统计读写量；
- 置换选择生成可变长初始归并段；
- k 路最佳归并树的代价与虚段数量；
- 通过原始序号保证重复关键字的输出稳定且可重复验证。

代码使用 `tmpfile()` 模拟外存文件；统计的是指定块大小下的逻辑块传输数，不等同于操作系统实际发出的系统调用次数。

## 编译运行

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
  external_sorting_demo.c -o external_sorting_demo
./external_sorting_demo
```

使用 AddressSanitizer 与 UndefinedBehaviorSanitizer 检查：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  external_sorting_demo.c -o external_sorting_demo_sanitize
./external_sorting_demo_sanitize
```

程序最后应输出：

```text
All external-sorting tests passed.
```
