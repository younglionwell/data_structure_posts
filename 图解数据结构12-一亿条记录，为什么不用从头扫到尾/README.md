# 图解数据结构 12：一亿条记录，为什么不用从头扫到尾？

本目录保存文章的配套 C11 示例，用内存结构和临时文件串起文件与索引的主要访问路径：

- 定长二进制记录、RRN 与顺序扫描；
- 稠密有序索引和二叉排序树索引；
- ISAM 风格的主区、稀疏索引与溢出链；
- 小型 B+ 树的等值查找、范围查找、叶分裂和内部结点分裂；
- 拉链法散列文件；
- 多重表文件；
- 散列词典、倒排表和 AND 查询。

这是用于解释访问路径的教学模型，不是完整数据库存储引擎。真实系统还需要处理页缓存、日志、并发控制、崩溃恢复和磁盘格式兼容。

## 编译运行

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
  file_index_structures_demo.c -o file_index_structures_demo
./file_index_structures_demo
```

使用 AddressSanitizer 与 UndefinedBehaviorSanitizer 检查：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  file_index_structures_demo.c -o file_index_structures_demo_sanitize
./file_index_structures_demo_sanitize
```

程序最后应输出：

```text
All file and index structure tests passed.
```
