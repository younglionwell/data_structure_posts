/*
 * 第 9 章：查找
 *
 * 编译：
 *   cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
 *      09-search-demo.c -o 09-search-demo
 *
 * 本文件用一组可运行的小实现串起：
 *   静态查找、BST、AVL、红黑树、B 树、B+ 树范围扫描、
 *   跳跃表、Trie、链地址/开放定址散列表、布隆过滤器。
 *
 * 教学重点是机制和边界。B+ 树部分使用固定三叶页模型展示
 * “内部结点导航 + 叶链范围扫描”，不是完整数据库索引实现。
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (p == NULL) {
        fputs("out of memory\n", stderr);
        exit(EXIT_FAILURE);
    }
    return p;
}

/* ---------- 静态查找表 ---------- */

static ptrdiff_t linear_search(const int *a, size_t n, int key,
                               size_t *comparisons) {
    *comparisons = 0U;
    for (size_t i = 0U; i < n; ++i) {
        ++*comparisons;
        if (a[i] == key) {
            return (ptrdiff_t)i;
        }
    }
    return -1;
}

static ptrdiff_t binary_search_int(const int *a, size_t n, int key,
                                   size_t *comparisons) {
    size_t lo = 0U;
    size_t hi = n;
    *comparisons = 0U;

    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2U;
        ++*comparisons;
        if (a[mid] == key) {
            return (ptrdiff_t)mid;
        }
        if (a[mid] < key) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    return -1;
}

typedef struct {
    int max_key;
    size_t begin;
    size_t end; /* 半开区间 [begin, end) */
} IndexBlock;

static ptrdiff_t indexed_sequential_search(const int *a,
                                           const IndexBlock *blocks,
                                           size_t block_count,
                                           int key) {
    size_t lo = 0U;
    size_t hi = block_count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2U;
        if (blocks[mid].max_key < key) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    if (lo == block_count) {
        return -1;
    }
    for (size_t i = blocks[lo].begin; i < blocks[lo].end; ++i) {
        if (a[i] == key) {
            return (ptrdiff_t)i;
        }
    }
    return -1;
}

/* ---------- 二叉排序树 ---------- */

typedef struct BstNode {
    int key;
    struct BstNode *left;
    struct BstNode *right;
} BstNode;

static BstNode *bst_new(int key) {
    BstNode *node = xmalloc(sizeof(*node));
    node->key = key;
    node->left = NULL;
    node->right = NULL;
    return node;
}

static BstNode *bst_insert(BstNode *root, int key) {
    if (root == NULL) {
        return bst_new(key);
    }
    if (key < root->key) {
        root->left = bst_insert(root->left, key);
    } else if (key > root->key) {
        root->right = bst_insert(root->right, key);
    }
    return root; /* 重复键不插入 */
}

static const BstNode *bst_search(const BstNode *root, int key,
                                 size_t *comparisons) {
    *comparisons = 0U;
    while (root != NULL) {
        ++*comparisons;
        if (key == root->key) {
            return root;
        }
        root = key < root->key ? root->left : root->right;
    }
    return NULL;
}

static BstNode *bst_delete(BstNode *root, int key) {
    if (root == NULL) {
        return NULL;
    }
    if (key < root->key) {
        root->left = bst_delete(root->left, key);
        return root;
    }
    if (key > root->key) {
        root->right = bst_delete(root->right, key);
        return root;
    }

    if (root->left == NULL) {
        BstNode *next = root->right;
        free(root);
        return next;
    }
    if (root->right == NULL) {
        BstNode *next = root->left;
        free(root);
        return next;
    }

    BstNode *successor = root->right;
    while (successor->left != NULL) {
        successor = successor->left;
    }
    root->key = successor->key;
    root->right = bst_delete(root->right, successor->key);
    return root;
}

static bool bst_valid_between(const BstNode *root, int64_t low, int64_t high) {
    if (root == NULL) {
        return true;
    }
    const int64_t key = root->key;
    return low < key && key < high &&
           bst_valid_between(root->left, low, key) &&
           bst_valid_between(root->right, key, high);
}

static void bst_destroy(BstNode *root) {
    if (root != NULL) {
        bst_destroy(root->left);
        bst_destroy(root->right);
        free(root);
    }
}

/* ---------- AVL 树 ---------- */

typedef struct AvlNode {
    int key;
    int height;
    struct AvlNode *left;
    struct AvlNode *right;
} AvlNode;

static int avl_height(const AvlNode *node) {
    return node == NULL ? 0 : node->height;
}

static int max_int(int a, int b) {
    return a > b ? a : b;
}

static void avl_refresh(AvlNode *node) {
    node->height = 1 + max_int(avl_height(node->left), avl_height(node->right));
}

static AvlNode *avl_new(int key) {
    AvlNode *node = xmalloc(sizeof(*node));
    node->key = key;
    node->height = 1;
    node->left = NULL;
    node->right = NULL;
    return node;
}

static AvlNode *avl_rotate_right(AvlNode *y) {
    AvlNode *x = y->left;
    AvlNode *middle = x->right;
    x->right = y;
    y->left = middle;
    avl_refresh(y);
    avl_refresh(x);
    return x;
}

static AvlNode *avl_rotate_left(AvlNode *x) {
    AvlNode *y = x->right;
    AvlNode *middle = y->left;
    y->left = x;
    x->right = middle;
    avl_refresh(x);
    avl_refresh(y);
    return y;
}

static AvlNode *avl_insert(AvlNode *root, int key) {
    if (root == NULL) {
        return avl_new(key);
    }
    if (key < root->key) {
        root->left = avl_insert(root->left, key);
    } else if (key > root->key) {
        root->right = avl_insert(root->right, key);
    } else {
        return root;
    }

    avl_refresh(root);
    const int balance = avl_height(root->left) - avl_height(root->right);
    if (balance > 1 && key < root->left->key) {       /* LL */
        return avl_rotate_right(root);
    }
    if (balance < -1 && key > root->right->key) {     /* RR */
        return avl_rotate_left(root);
    }
    if (balance > 1 && key > root->left->key) {       /* LR */
        root->left = avl_rotate_left(root->left);
        return avl_rotate_right(root);
    }
    if (balance < -1 && key < root->right->key) {     /* RL */
        root->right = avl_rotate_right(root->right);
        return avl_rotate_left(root);
    }
    return root;
}

static bool avl_validate(const AvlNode *root, int64_t low, int64_t high,
                         int *height_out) {
    if (root == NULL) {
        *height_out = 0;
        return true;
    }
    int left_height = 0;
    int right_height = 0;
    if (!(low < root->key && root->key < high) ||
        !avl_validate(root->left, low, root->key, &left_height) ||
        !avl_validate(root->right, root->key, high, &right_height)) {
        return false;
    }
    const int diff = left_height - right_height;
    const int expected = 1 + max_int(left_height, right_height);
    *height_out = expected;
    return diff >= -1 && diff <= 1 && root->height == expected;
}

static void avl_destroy(AvlNode *root) {
    if (root != NULL) {
        avl_destroy(root->left);
        avl_destroy(root->right);
        free(root);
    }
}

/* ---------- 红黑树：标准插入修复 ---------- */

typedef enum { RB_RED, RB_BLACK } RbColor;

typedef struct RbNode {
    int key;
    RbColor color;
    struct RbNode *left;
    struct RbNode *right;
    struct RbNode *parent;
} RbNode;

typedef struct {
    RbNode *root;
} RbTree;

static RbColor rb_color(const RbNode *node) {
    return node == NULL ? RB_BLACK : node->color;
}

static RbNode *rb_new(int key) {
    RbNode *node = xmalloc(sizeof(*node));
    node->key = key;
    node->color = RB_RED;
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;
    return node;
}

static void rb_rotate_left(RbTree *tree, RbNode *x) {
    RbNode *y = x->right;
    assert(y != NULL);
    x->right = y->left;
    if (y->left != NULL) {
        y->left->parent = x;
    }
    y->parent = x->parent;
    if (x->parent == NULL) {
        tree->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
}

static void rb_rotate_right(RbTree *tree, RbNode *y) {
    RbNode *x = y->left;
    assert(x != NULL);
    y->left = x->right;
    if (x->right != NULL) {
        x->right->parent = y;
    }
    x->parent = y->parent;
    if (y->parent == NULL) {
        tree->root = x;
    } else if (y == y->parent->left) {
        y->parent->left = x;
    } else {
        y->parent->right = x;
    }
    x->right = y;
    y->parent = x;
}

static void rb_fix_insert(RbTree *tree, RbNode *node) {
    while (node->parent != NULL && node->parent->color == RB_RED) {
        RbNode *parent = node->parent;
        RbNode *grand = parent->parent;
        assert(grand != NULL);
        if (parent == grand->left) {
            RbNode *uncle = grand->right;
            if (rb_color(uncle) == RB_RED) {
                parent->color = RB_BLACK;
                uncle->color = RB_BLACK;
                grand->color = RB_RED;
                node = grand;
            } else {
                if (node == parent->right) {
                    node = parent;
                    rb_rotate_left(tree, node);
                    parent = node->parent;
                    grand = parent->parent;
                }
                parent->color = RB_BLACK;
                grand->color = RB_RED;
                rb_rotate_right(tree, grand);
            }
        } else {
            RbNode *uncle = grand->left;
            if (rb_color(uncle) == RB_RED) {
                parent->color = RB_BLACK;
                uncle->color = RB_BLACK;
                grand->color = RB_RED;
                node = grand;
            } else {
                if (node == parent->left) {
                    node = parent;
                    rb_rotate_right(tree, node);
                    parent = node->parent;
                    grand = parent->parent;
                }
                parent->color = RB_BLACK;
                grand->color = RB_RED;
                rb_rotate_left(tree, grand);
            }
        }
    }
    tree->root->color = RB_BLACK;
}

static bool rb_insert(RbTree *tree, int key) {
    RbNode *parent = NULL;
    RbNode *cursor = tree->root;
    while (cursor != NULL) {
        parent = cursor;
        if (key == cursor->key) {
            return false;
        }
        cursor = key < cursor->key ? cursor->left : cursor->right;
    }
    RbNode *node = rb_new(key);
    node->parent = parent;
    if (parent == NULL) {
        tree->root = node;
    } else if (key < parent->key) {
        parent->left = node;
    } else {
        parent->right = node;
    }
    rb_fix_insert(tree, node);
    return true;
}

static const RbNode *rb_search(const RbTree *tree, int key) {
    const RbNode *node = tree->root;
    while (node != NULL && node->key != key) {
        node = key < node->key ? node->left : node->right;
    }
    return node;
}

static bool rb_validate_rec(const RbNode *node, int64_t low, int64_t high,
                            int black_count, int *expected_black_count) {
    if (node == NULL) {
        ++black_count; /* 黑色 NIL */
        if (*expected_black_count < 0) {
            *expected_black_count = black_count;
        }
        return black_count == *expected_black_count;
    }
    if (!(low < node->key && node->key < high)) {
        return false;
    }
    if (node->color == RB_RED &&
        (rb_color(node->left) == RB_RED || rb_color(node->right) == RB_RED)) {
        return false;
    }
    if (node->left != NULL && node->left->parent != node) {
        return false;
    }
    if (node->right != NULL && node->right->parent != node) {
        return false;
    }
    if (node->color == RB_BLACK) {
        ++black_count;
    }
    return rb_validate_rec(node->left, low, node->key, black_count,
                           expected_black_count) &&
           rb_validate_rec(node->right, node->key, high, black_count,
                           expected_black_count);
}

static bool rb_validate(const RbTree *tree) {
    if (tree->root == NULL) {
        return true;
    }
    if (tree->root->parent != NULL || tree->root->color != RB_BLACK) {
        return false;
    }
    int expected_black_count = -1;
    return rb_validate_rec(tree->root, INT64_MIN, INT64_MAX, 0,
                           &expected_black_count);
}

static void rb_destroy_nodes(RbNode *node) {
    if (node != NULL) {
        rb_destroy_nodes(node->left);
        rb_destroy_nodes(node->right);
        free(node);
    }
}

/* ---------- B 树：最小度数 t=2 ---------- */

enum { BTREE_T = 2, BTREE_MAX_KEYS = 3, BTREE_MAX_CHILDREN = 4 };

typedef struct BTreeNode {
    bool leaf;
    size_t count;
    int keys[BTREE_MAX_KEYS];
    struct BTreeNode *children[BTREE_MAX_CHILDREN];
} BTreeNode;

typedef struct {
    BTreeNode *root;
} BTree;

static BTreeNode *btree_new_node(bool leaf) {
    BTreeNode *node = xmalloc(sizeof(*node));
    node->leaf = leaf;
    node->count = 0U;
    for (size_t i = 0U; i < BTREE_MAX_CHILDREN; ++i) {
        node->children[i] = NULL;
    }
    return node;
}

static bool btree_search_node(const BTreeNode *node, int key) {
    if (node == NULL) {
        return false;
    }
    size_t i = 0U;
    while (i < node->count && key > node->keys[i]) {
        ++i;
    }
    if (i < node->count && key == node->keys[i]) {
        return true;
    }
    return node->leaf ? false : btree_search_node(node->children[i], key);
}

static void btree_split_child(BTreeNode *parent, size_t index) {
    BTreeNode *full = parent->children[index];
    assert(full != NULL && full->count == BTREE_MAX_KEYS);
    BTreeNode *right = btree_new_node(full->leaf);
    right->count = BTREE_T - 1U;
    right->keys[0] = full->keys[BTREE_T];
    if (!full->leaf) {
        right->children[0] = full->children[BTREE_T];
        right->children[1] = full->children[BTREE_T + 1U];
        full->children[BTREE_T] = NULL;
        full->children[BTREE_T + 1U] = NULL;
    }
    const int promoted = full->keys[BTREE_T - 1U];
    full->count = BTREE_T - 1U;

    for (size_t j = parent->count + 1U; j > index + 1U; --j) {
        parent->children[j] = parent->children[j - 1U];
    }
    parent->children[index + 1U] = right;
    for (size_t j = parent->count; j > index; --j) {
        parent->keys[j] = parent->keys[j - 1U];
    }
    parent->keys[index] = promoted;
    ++parent->count;
}

static void btree_insert_nonfull(BTreeNode *node, int key) {
    size_t i = node->count;
    if (node->leaf) {
        while (i > 0U && key < node->keys[i - 1U]) {
            node->keys[i] = node->keys[i - 1U];
            --i;
        }
        if ((i > 0U && node->keys[i - 1U] == key) ||
            (i < node->count && node->keys[i] == key)) {
            return;
        }
        node->keys[i] = key;
        ++node->count;
        return;
    }

    while (i > 0U && key < node->keys[i - 1U]) {
        --i;
    }
    if (i > 0U && node->keys[i - 1U] == key) {
        return;
    }
    if (node->children[i]->count == BTREE_MAX_KEYS) {
        btree_split_child(node, i);
        if (key == node->keys[i]) {
            return;
        }
        if (key > node->keys[i]) {
            ++i;
        }
    }
    btree_insert_nonfull(node->children[i], key);
}

static void btree_insert(BTree *tree, int key) {
    if (tree->root == NULL) {
        tree->root = btree_new_node(true);
        tree->root->keys[0] = key;
        tree->root->count = 1U;
        return;
    }
    if (btree_search_node(tree->root, key)) {
        return;
    }
    if (tree->root->count == BTREE_MAX_KEYS) {
        BTreeNode *new_root = btree_new_node(false);
        new_root->children[0] = tree->root;
        btree_split_child(new_root, 0U);
        tree->root = new_root;
    }
    btree_insert_nonfull(tree->root, key);
}

static bool btree_validate_rec(const BTreeNode *node, bool is_root,
                               int64_t low, int64_t high, size_t depth,
                               size_t *leaf_depth) {
    if (node == NULL || node->count > BTREE_MAX_KEYS ||
        (!is_root && node->count < BTREE_T - 1U) ||
        (is_root && node->count == 0U)) {
        return false;
    }
    for (size_t i = 0U; i < node->count; ++i) {
        if (!(low < node->keys[i] && node->keys[i] < high) ||
            (i > 0U && node->keys[i - 1U] >= node->keys[i])) {
            return false;
        }
    }
    if (node->leaf) {
        for (size_t i = 0U; i < BTREE_MAX_CHILDREN; ++i) {
            if (node->children[i] != NULL) {
                return false;
            }
        }
        if (*leaf_depth == SIZE_MAX) {
            *leaf_depth = depth;
        }
        return depth == *leaf_depth;
    }
    for (size_t i = 0U; i <= node->count; ++i) {
        if (node->children[i] == NULL) {
            return false;
        }
        const int64_t child_low = i == 0U ? low : node->keys[i - 1U];
        const int64_t child_high = i == node->count ? high : node->keys[i];
        if (!btree_validate_rec(node->children[i], false, child_low,
                                child_high, depth + 1U, leaf_depth)) {
            return false;
        }
    }
    return true;
}

static bool btree_validate(const BTree *tree) {
    if (tree->root == NULL) {
        return true;
    }
    size_t leaf_depth = SIZE_MAX;
    return btree_validate_rec(tree->root, true, INT64_MIN, INT64_MAX, 0U,
                              &leaf_depth);
}

static void btree_destroy_node(BTreeNode *node) {
    if (node != NULL) {
        if (!node->leaf) {
            for (size_t i = 0U; i <= node->count; ++i) {
                btree_destroy_node(node->children[i]);
            }
        }
        free(node);
    }
}

/* ---------- B+ 树叶链：固定小模型 ---------- */

typedef struct BPlusLeaf {
    int keys[3];
    size_t count;
    struct BPlusLeaf *next;
} BPlusLeaf;

typedef struct {
    int separators[2];
    BPlusLeaf leaves[3];
} BPlusDemo;

static void bplus_demo_init(BPlusDemo *tree) {
    tree->separators[0] = 30;
    tree->separators[1] = 50;
    tree->leaves[0] = (BPlusLeaf){{10, 20, 0}, 2U, &tree->leaves[1]};
    tree->leaves[1] = (BPlusLeaf){{30, 40, 0}, 2U, &tree->leaves[2]};
    tree->leaves[2] = (BPlusLeaf){{50, 60, 70}, 3U, NULL};
}

static size_t bplus_range(const BPlusDemo *tree, int low, int high,
                          int *out, size_t capacity) {
    size_t leaf_index = 0U;
    while (leaf_index < 2U && low >= tree->separators[leaf_index]) {
        ++leaf_index;
    }
    const BPlusLeaf *leaf = &tree->leaves[leaf_index];
    size_t used = 0U;
    while (leaf != NULL) {
        for (size_t i = 0U; i < leaf->count; ++i) {
            const int key = leaf->keys[i];
            if (key > high) {
                return used;
            }
            if (key >= low && used < capacity) {
                out[used++] = key;
            }
        }
        leaf = leaf->next;
    }
    return used;
}

/* ---------- 跳跃表 ---------- */

enum { SKIP_MAX_LEVEL = 8 };

typedef struct SkipNode {
    int key;
    int level;
    struct SkipNode *forward[SKIP_MAX_LEVEL];
} SkipNode;

typedef struct {
    SkipNode *head;
    int level;
    uint32_t random_state;
} SkipList;

static SkipNode *skip_new_node(int key, int level) {
    SkipNode *node = xmalloc(sizeof(*node));
    node->key = key;
    node->level = level;
    for (int i = 0; i < SKIP_MAX_LEVEL; ++i) {
        node->forward[i] = NULL;
    }
    return node;
}

static void skip_init(SkipList *list, uint32_t seed) {
    list->head = skip_new_node(0, SKIP_MAX_LEVEL);
    list->level = 1;
    list->random_state = seed == 0U ? UINT32_C(0x9e3779b9) : seed;
}

static uint32_t skip_random(SkipList *list) {
    uint32_t x = list->random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    list->random_state = x;
    return x;
}

static int skip_random_level(SkipList *list) {
    int level = 1;
    while (level < SKIP_MAX_LEVEL && (skip_random(list) & 1U) == 0U) {
        ++level;
    }
    return level;
}

static bool skip_insert(SkipList *list, int key) {
    SkipNode *update[SKIP_MAX_LEVEL];
    SkipNode *cursor = list->head;
    for (int i = list->level - 1; i >= 0; --i) {
        while (cursor->forward[i] != NULL &&
               cursor->forward[i]->key < key) {
            cursor = cursor->forward[i];
        }
        update[i] = cursor;
    }
    cursor = cursor->forward[0];
    if (cursor != NULL && cursor->key == key) {
        return false;
    }
    const int new_level = skip_random_level(list);
    if (new_level > list->level) {
        for (int i = list->level; i < new_level; ++i) {
            update[i] = list->head;
        }
        list->level = new_level;
    }
    SkipNode *node = skip_new_node(key, new_level);
    for (int i = 0; i < new_level; ++i) {
        node->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = node;
    }
    return true;
}

static bool skip_search(const SkipList *list, int key) {
    const SkipNode *cursor = list->head;
    for (int i = list->level - 1; i >= 0; --i) {
        while (cursor->forward[i] != NULL &&
               cursor->forward[i]->key < key) {
            cursor = cursor->forward[i];
        }
    }
    cursor = cursor->forward[0];
    return cursor != NULL && cursor->key == key;
}

static bool skip_validate(const SkipList *list) {
    for (int level = 0; level < list->level; ++level) {
        const SkipNode *node = list->head->forward[level];
        int previous = 0;
        bool first = true;
        while (node != NULL) {
            if (node->level <= level || (!first && previous >= node->key)) {
                return false;
            }
            first = false;
            previous = node->key;
            node = node->forward[level];
        }
    }
    return true;
}

static void skip_destroy(SkipList *list) {
    SkipNode *node = list->head;
    while (node != NULL) {
        SkipNode *next = node->forward[0];
        free(node);
        node = next;
    }
    list->head = NULL;
    list->level = 0;
}

/* ---------- Trie 键树 ---------- */

enum { TRIE_ALPHABET = 26 };

typedef struct TrieNode {
    bool terminal;
    struct TrieNode *children[TRIE_ALPHABET];
} TrieNode;

static TrieNode *trie_new(void) {
    TrieNode *node = xmalloc(sizeof(*node));
    node->terminal = false;
    for (size_t i = 0U; i < TRIE_ALPHABET; ++i) {
        node->children[i] = NULL;
    }
    return node;
}

static bool trie_index(char c, size_t *index) {
    if (c < 'a' || c > 'z') {
        return false;
    }
    *index = (size_t)(c - 'a');
    return true;
}

static bool trie_insert(TrieNode *root, const char *word) {
    if (*word == '\0') {
        const bool added = !root->terminal;
        root->terminal = true;
        return added;
    }
    TrieNode *node = root;
    for (const char *p = word; *p != '\0'; ++p) {
        size_t index = 0U;
        if (!trie_index(*p, &index)) {
            return false;
        }
        if (node->children[index] == NULL) {
            node->children[index] = trie_new();
        }
        node = node->children[index];
    }
    const bool added = !node->terminal;
    node->terminal = true;
    return added;
}

static const TrieNode *trie_walk(const TrieNode *root, const char *text) {
    const TrieNode *node = root;
    for (const char *p = text; *p != '\0'; ++p) {
        size_t index = 0U;
        if (!trie_index(*p, &index) || node->children[index] == NULL) {
            return NULL;
        }
        node = node->children[index];
    }
    return node;
}

static bool trie_contains(const TrieNode *root, const char *word) {
    const TrieNode *node = trie_walk(root, word);
    return node != NULL && node->terminal;
}

static bool trie_has_prefix(const TrieNode *root, const char *prefix) {
    return trie_walk(root, prefix) != NULL;
}

static void trie_destroy(TrieNode *root) {
    if (root != NULL) {
        for (size_t i = 0U; i < TRIE_ALPHABET; ++i) {
            trie_destroy(root->children[i]);
        }
        free(root);
    }
}

/* ---------- 散列表 ---------- */

enum { HASH_CAPACITY = 11 };

typedef struct HashEntry {
    int key;
    struct HashEntry *next;
} HashEntry;

typedef struct {
    HashEntry *buckets[HASH_CAPACITY];
} ChainHash;

static size_t hash_index_int(int key) {
    int value = key % HASH_CAPACITY;
    if (value < 0) {
        value += HASH_CAPACITY;
    }
    return (size_t)value;
}

static bool chain_hash_insert(ChainHash *table, int key) {
    const size_t index = hash_index_int(key);
    for (HashEntry *e = table->buckets[index]; e != NULL; e = e->next) {
        if (e->key == key) {
            return false;
        }
    }
    HashEntry *entry = xmalloc(sizeof(*entry));
    entry->key = key;
    entry->next = table->buckets[index];
    table->buckets[index] = entry;
    return true;
}

static bool chain_hash_contains(const ChainHash *table, int key) {
    const size_t index = hash_index_int(key);
    for (const HashEntry *e = table->buckets[index]; e != NULL; e = e->next) {
        if (e->key == key) {
            return true;
        }
    }
    return false;
}

static void chain_hash_destroy(ChainHash *table) {
    for (size_t i = 0U; i < HASH_CAPACITY; ++i) {
        HashEntry *entry = table->buckets[i];
        while (entry != NULL) {
            HashEntry *next = entry->next;
            free(entry);
            entry = next;
        }
        table->buckets[i] = NULL;
    }
}

typedef enum { SLOT_EMPTY, SLOT_OCCUPIED, SLOT_DELETED } SlotState;

typedef struct {
    int key;
    SlotState state;
} HashSlot;

typedef struct {
    HashSlot slots[HASH_CAPACITY];
    size_t size;
} OpenHash;

static bool open_hash_insert(OpenHash *table, int key) {
    const size_t start = hash_index_int(key);
    size_t first_deleted = HASH_CAPACITY;
    for (size_t step = 0U; step < HASH_CAPACITY; ++step) {
        const size_t index = (start + step) % HASH_CAPACITY;
        HashSlot *slot = &table->slots[index];
        if (slot->state == SLOT_OCCUPIED && slot->key == key) {
            return false;
        }
        if (slot->state == SLOT_DELETED && first_deleted == HASH_CAPACITY) {
            first_deleted = index;
        }
        if (slot->state == SLOT_EMPTY) {
            const size_t target = first_deleted == HASH_CAPACITY ? index : first_deleted;
            table->slots[target].key = key;
            table->slots[target].state = SLOT_OCCUPIED;
            ++table->size;
            return true;
        }
    }
    if (first_deleted != HASH_CAPACITY) {
        table->slots[first_deleted].key = key;
        table->slots[first_deleted].state = SLOT_OCCUPIED;
        ++table->size;
        return true;
    }
    return false;
}

static bool open_hash_contains(const OpenHash *table, int key) {
    const size_t start = hash_index_int(key);
    for (size_t step = 0U; step < HASH_CAPACITY; ++step) {
        const size_t index = (start + step) % HASH_CAPACITY;
        const HashSlot *slot = &table->slots[index];
        if (slot->state == SLOT_EMPTY) {
            return false;
        }
        if (slot->state == SLOT_OCCUPIED && slot->key == key) {
            return true;
        }
    }
    return false;
}

static bool open_hash_delete(OpenHash *table, int key) {
    const size_t start = hash_index_int(key);
    for (size_t step = 0U; step < HASH_CAPACITY; ++step) {
        const size_t index = (start + step) % HASH_CAPACITY;
        HashSlot *slot = &table->slots[index];
        if (slot->state == SLOT_EMPTY) {
            return false;
        }
        if (slot->state == SLOT_OCCUPIED && slot->key == key) {
            slot->state = SLOT_DELETED;
            --table->size;
            return true;
        }
    }
    return false;
}

/* ---------- 布隆过滤器与计数型改进 ---------- */

enum { BLOOM_BITS = 128, BLOOM_HASHES = 4 };

typedef struct {
    uint64_t words[2];
} BloomFilter;

typedef struct {
    uint8_t counters[BLOOM_BITS];
} CountingBloom;

static uint64_t hash_bytes_seeded(const char *text, uint64_t seed) {
    uint64_t hash = seed;
    for (const unsigned char *p = (const unsigned char *)text; *p != 0U; ++p) {
        hash ^= (uint64_t)*p;
        hash *= UINT64_C(1099511628211);
        hash ^= hash >> 32;
    }
    return hash;
}

static size_t bloom_position(const char *text, size_t round) {
    const uint64_t h1 = hash_bytes_seeded(text, UINT64_C(1469598103934665603));
    uint64_t h2 = hash_bytes_seeded(text, UINT64_C(7809847782465536322));
    h2 |= UINT64_C(1);
    return (size_t)((h1 + (uint64_t)round * h2) % BLOOM_BITS);
}

static void bloom_add(BloomFilter *filter, const char *text) {
    for (size_t i = 0U; i < BLOOM_HASHES; ++i) {
        const size_t bit = bloom_position(text, i);
        filter->words[bit / 64U] |= UINT64_C(1) << (bit % 64U);
    }
}

static bool bloom_maybe_contains(const BloomFilter *filter, const char *text) {
    for (size_t i = 0U; i < BLOOM_HASHES; ++i) {
        const size_t bit = bloom_position(text, i);
        if ((filter->words[bit / 64U] & (UINT64_C(1) << (bit % 64U))) == 0U) {
            return false;
        }
    }
    return true;
}

static bool counting_bloom_add(CountingBloom *filter, const char *text) {
    uint8_t increments[BLOOM_BITS] = {0U};
    for (size_t i = 0U; i < BLOOM_HASHES; ++i) {
        const size_t position = bloom_position(text, i);
        ++increments[position];
    }
    for (size_t position = 0U; position < BLOOM_BITS; ++position) {
        if (increments[position] > UINT8_MAX - filter->counters[position]) {
            return false;
        }
    }
    for (size_t position = 0U; position < BLOOM_BITS; ++position) {
        filter->counters[position] =
            (uint8_t)(filter->counters[position] + increments[position]);
    }
    return true;
}

static bool counting_bloom_remove(CountingBloom *filter, const char *text) {
    uint8_t decrements[BLOOM_BITS] = {0U};
    for (size_t i = 0U; i < BLOOM_HASHES; ++i) {
        const size_t position = bloom_position(text, i);
        ++decrements[position];
    }
    for (size_t position = 0U; position < BLOOM_BITS; ++position) {
        if (filter->counters[position] < decrements[position]) {
            return false;
        }
    }
    for (size_t position = 0U; position < BLOOM_BITS; ++position) {
        filter->counters[position] =
            (uint8_t)(filter->counters[position] - decrements[position]);
    }
    return true;
}

static bool counting_bloom_maybe_contains(const CountingBloom *filter,
                                          const char *text) {
    for (size_t i = 0U; i < BLOOM_HASHES; ++i) {
        if (filter->counters[bloom_position(text, i)] == 0U) {
            return false;
        }
    }
    return true;
}

/* ---------- 可执行断言 ---------- */

static void test_static_search(void) {
    const int a[] = {3, 7, 12, 18, 25, 31, 42, 56, 63, 78, 91};
    const IndexBlock blocks[] = {
        {18, 0U, 4U}, {42, 4U, 7U}, {78, 7U, 10U}, {91, 10U, 11U}
    };
    size_t linear_count = 0U;
    size_t binary_count = 0U;
    assert(linear_search(a, 11U, 56, &linear_count) == 7);
    assert(binary_search_int(a, 11U, 56, &binary_count) == 7);
    assert(linear_count == 8U);
    assert(binary_count == 3U);
    assert(indexed_sequential_search(a, blocks, 4U, 56) == 7);
    assert(indexed_sequential_search(a, blocks, 4U, 99) == -1);
    printf("静态表：顺序 %zu 次，二分 %zu 次。\n", linear_count, binary_count);
}

static void test_search_trees(void) {
    const int balanced[] = {30, 20, 40, 10, 25, 35, 50};
    BstNode *bst = NULL;
    for (size_t i = 0U; i < sizeof(balanced) / sizeof(balanced[0]); ++i) {
        bst = bst_insert(bst, balanced[i]);
    }
    assert(bst_valid_between(bst, INT64_MIN, INT64_MAX));
    size_t comparisons = 0U;
    assert(bst_search(bst, 35, &comparisons) != NULL);
    bst = bst_delete(bst, 30);
    assert(bst_valid_between(bst, INT64_MIN, INT64_MAX));
    assert(bst_search(bst, 30, &comparisons) == NULL);
    bst_destroy(bst);

    AvlNode *avl = NULL;
    const int ordered[] = {10, 20, 30, 40, 50, 25};
    for (size_t i = 0U; i < sizeof(ordered) / sizeof(ordered[0]); ++i) {
        avl = avl_insert(avl, ordered[i]);
        int height = 0;
        assert(avl_validate(avl, INT64_MIN, INT64_MAX, &height));
    }
    assert(avl->key == 30);
    avl_destroy(avl);

    RbTree rb = {NULL};
    const int rb_keys[] = {41, 38, 31, 12, 19, 8, 25, 50, 60};
    for (size_t i = 0U; i < sizeof(rb_keys) / sizeof(rb_keys[0]); ++i) {
        assert(rb_insert(&rb, rb_keys[i]));
        assert(rb_validate(&rb));
    }
    assert(!rb_insert(&rb, 19));
    assert(rb_search(&rb, 25) != NULL);
    assert(rb_search(&rb, 99) == NULL);
    rb_destroy_nodes(rb.root);
    puts("BST、AVL 与红黑树：不变量检查通过。");
}

static void test_external_and_probabilistic_structures(void) {
    BTree btree = {NULL};
    const int keys[] = {10, 20, 5, 6, 12, 30, 7, 17, 4, 25, 40, 50};
    for (size_t i = 0U; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        btree_insert(&btree, keys[i]);
        assert(btree_validate(&btree));
    }
    assert(btree_search_node(btree.root, 17));
    assert(!btree_search_node(btree.root, 99));
    btree_destroy_node(btree.root);

    BPlusDemo bplus;
    bplus_demo_init(&bplus);
    int range[8] = {0};
    const size_t count = bplus_range(&bplus, 22, 55, range, 8U);
    assert(count == 3U && range[0] == 30 && range[1] == 40 && range[2] == 50);

    SkipList skip;
    skip_init(&skip, UINT32_C(20260814));
    const int skip_keys[] = {3, 7, 12, 19, 26, 31, 44, 57, 68};
    for (size_t i = 0U; i < sizeof(skip_keys) / sizeof(skip_keys[0]); ++i) {
        assert(skip_insert(&skip, skip_keys[i]));
        assert(skip_validate(&skip));
    }
    assert(skip_search(&skip, 57));
    assert(!skip_search(&skip, 58));
    skip_destroy(&skip);

    TrieNode *trie = trie_new();
    assert(trie_insert(trie, "app"));
    assert(trie_insert(trie, "apple"));
    assert(trie_insert(trie, "apt"));
    assert(trie_insert(trie, "bat"));
    assert(trie_has_prefix(trie, "ap"));
    assert(!trie_contains(trie, "ap"));
    assert(trie_contains(trie, "app"));
    assert(!trie_insert(trie, "a-b"));
    trie_destroy(trie);
    puts("B 树、B+ 叶链、跳跃表与 Trie：测试通过。");
}

static void test_hash_and_bloom(void) {
    ChainHash chained = {{NULL}};
    assert(chain_hash_insert(&chained, 22));
    assert(chain_hash_insert(&chained, 11));
    assert(chain_hash_insert(&chained, 33));
    assert(!chain_hash_insert(&chained, 22));
    assert(chain_hash_contains(&chained, 33));
    chain_hash_destroy(&chained);

    OpenHash open = {{{0, SLOT_EMPTY}}, 0U};
    assert(open_hash_insert(&open, 22));
    assert(open_hash_insert(&open, 11));
    assert(open_hash_insert(&open, 33));
    assert(open.slots[0].key == 22 && open.slots[0].state == SLOT_OCCUPIED);
    assert(open.slots[1].key == 11 && open.slots[1].state == SLOT_OCCUPIED);
    assert(open.slots[2].key == 33 && open.slots[2].state == SLOT_OCCUPIED);
    assert(open_hash_delete(&open, 11));
    assert(open.slots[1].state == SLOT_DELETED);
    assert(open_hash_contains(&open, 33)); /* 必须越过墓碑 */
    assert(!open_hash_contains(&open, 44));

    BloomFilter bloom = {{0U, 0U}};
    bloom_add(&bloom, "cat");
    bloom_add(&bloom, "dog");
    assert(bloom_maybe_contains(&bloom, "cat"));
    assert(bloom_maybe_contains(&bloom, "dog"));

    CountingBloom counting = {{0U}};
    assert(counting_bloom_add(&counting, "cat"));
    assert(counting_bloom_add(&counting, "dog"));
    assert(counting_bloom_maybe_contains(&counting, "cat"));
    assert(counting_bloom_remove(&counting, "cat"));
    assert(!counting_bloom_maybe_contains(&counting, "cat"));
    assert(counting_bloom_maybe_contains(&counting, "dog"));
    assert(!counting_bloom_remove(&counting, "cat"));
    puts("散列表与布隆过滤器：边界测试通过。");
}

int main(void) {
    test_static_search();
    test_search_trees();
    test_external_and_probabilistic_structures();
    test_hash_and_bloom();
    puts("All search-structure tests passed.");
    return 0;
}
