# 图解数据结构 09：从 1 亿条记录里找一个值，二分、红黑树、B+ 树和哈希表该选谁？

本目录保存文章的配套 C11 示例，覆盖查找这一章的主要结构与边界：

- 顺序查找、哨兵查找、折半查找、插值查找和分块查找；
- 二叉排序树、AVL 树和红黑树；
- B 树，以及用于说明内部结点导航和叶链范围扫描的 B+ 树模型；
- 跳跃表与 Trie；
- 链地址法、开放定址法散列表；
- 布隆过滤器及其“可能存在、一定不存在”的边界。

## 编译运行

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
  search_demo.c -o search_demo
./search_demo
```

使用 AddressSanitizer 与 UndefinedBehaviorSanitizer 检查：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  search_demo.c -o search_demo_sanitize
./search_demo_sanitize
```

程序最后应输出：

```text
All search-structure tests passed.
```
