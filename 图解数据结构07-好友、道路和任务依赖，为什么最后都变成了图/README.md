# 图解数据结构 07：好友、道路和任务依赖，为什么最后都变成了“图”？

本目录保存文章的配套 C11 示例，覆盖图这一章的主要结构与算法：

- 邻接矩阵、邻接表，以及深度优先和广度优先遍历；
- Tarjan 强连通分量；
- Kruskal、Prim 最小生成树；
- 关节点与桥；
- Dijkstra、Bellman-Ford、Floyd-Warshall 最短路径；
- Kahn 拓扑排序与 AOE 网关键路径；
- 二分图判定与 Hopcroft-Karp 最大匹配。

## 编译运行

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
  graph_demo.c -o graph_demo
./graph_demo
```

使用 AddressSanitizer 与 UndefinedBehaviorSanitizer 检查：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  graph_demo.c -o graph_demo_sanitize
./graph_demo_sanitize
```

程序最后应输出：

```text
All graph tests passed.
```
