# 图解数据结构 06：文件夹一层套一层，计算机为什么总爱把数据组织成“树”？

本目录保存文章的配套 C11 示例，覆盖“树和二叉树”一章的主要结构、算法与边界处理：

- 二叉链表的建立、销毁、结点数、高度及结构性质检查；
- 递归先序、中序、后序遍历，显式栈遍历和队列层序遍历；
- 由键值互异的先序与中序序列重建二叉树；
- 中序线索化与不借助栈的中序遍历；
- 完全二叉树上的最小堆与优先队列；
- 哈夫曼树、编码、译码和带权路径长度；
- 普通树的孩子兄弟表示；
- 按大小合并、路径压缩的并查集；
- 状态空间树上的回溯搜索，以及卡特兰数表示的二叉树形态计数。

代码统一采用 0 基数组下标；树的层次说明采用“根在第 1 层”的口径。示例中的哈夫曼树使用确定性的权重和次序比较规则，因此输出码字可重复验证；同一组权重在其他合法的并列处理或左右边标记约定下，也可能得到不同码字，但最优带权路径长度不变。

## 编译运行

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
  binary_tree_demo.c -o binary_tree_demo
./binary_tree_demo
```

使用 AddressSanitizer 与 UndefinedBehaviorSanitizer 检查：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  binary_tree_demo.c -o binary_tree_demo_sanitize
./binary_tree_demo_sanitize
```

程序最后应输出：

```text
All tree, heap, Huffman, DSU, backtracking and counting tests passed.
```
