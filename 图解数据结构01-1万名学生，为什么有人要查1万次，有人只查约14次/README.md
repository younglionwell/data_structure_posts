# 图解数据结构 01：1万名学生，为什么有人要查1万次，有人只查约14次？

本目录保存该文章的配套 C11 示例。

示例使用同一批学生记录演示：

- 无序数据上的顺序查找；
- 有序数组上的二分查找；
- 算法层面的关键字比较次数；
- 空数组、首尾元素、查找失败和重复键等边界情况。

编译并运行：

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
  introduction_search_demo.c -o introduction_search_demo

./introduction_search_demo
```

二分查找的比较次数不包含预排序成本。若原始数据无序且只查询一次，先排序未必划算。
