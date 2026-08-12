/*
 * 第 6 章：树和二叉树
 *
 * 编译（普通）：
 *   cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
 *      06-tree-binary-tree-demo.c -o 06-tree-binary-tree-demo
 *
 * 编译（内存与未定义行为检查）：
 *   cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
 *      -fsanitize=address,undefined 06-tree-binary-tree-demo.c \
 *      -o 06-tree-binary-tree-demo-asan
 *
 * 单文件演示：二叉链表、递归与非递归遍历、层序遍历、由前序和
 * 中序序列重建、孩子兄弟表示、线索二叉树、最小堆、哈夫曼编码、
 * 并查集、回溯搜索，以及 Catalan 树计数。
 */

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool checked_add_size(size_t a, size_t b, size_t *out) {
    if (out == NULL || b > SIZE_MAX - a) {
        return false;
    }
    *out = a + b;
    return true;
}

static bool checked_mul_size(size_t a, size_t b, size_t *out) {
    if (out == NULL || (a != 0U && b > SIZE_MAX / a)) {
        return false;
    }
    *out = a * b;
    return true;
}

static bool checked_add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || b > UINT64_MAX - a) {
        return false;
    }
    *out = a + b;
    return true;
}

static bool checked_mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || (a != 0U && b > UINT64_MAX / a)) {
        return false;
    }
    *out = a * b;
    return true;
}

static bool checked_add_i64(int64_t a, int64_t b, int64_t *out) {
    if (out == NULL ||
        (b > 0 && a > INT64_MAX - b) ||
        (b < 0 && a < INT64_MIN - b)) {
        return false;
    }
    *out = a + b;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 二叉链表                                                                   */

typedef struct BinaryNode {
    char key;
    struct BinaryNode *left;
    struct BinaryNode *right;
} BinaryNode;

static BinaryNode *binary_node_create(char key) {
    BinaryNode *node = malloc(sizeof(*node));
    if (node != NULL) {
        node->key = key;
        node->left = NULL;
        node->right = NULL;
    }
    return node;
}

static void binary_tree_destroy(BinaryNode *root) {
    if (root == NULL) {
        return;
    }
    binary_tree_destroy(root->left);
    binary_tree_destroy(root->right);
    free(root);
}

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool ok;
} CharOutput;

static void char_output_init(CharOutput *out, char *buffer, size_t capacity) {
    out->data = buffer;
    out->capacity = capacity;
    out->length = 0U;
    out->ok = buffer != NULL && capacity > 0U;
    if (out->ok) {
        buffer[0] = '\0';
    }
}

static void char_output_put(CharOutput *out, char value) {
    if (!out->ok || out->length >= out->capacity - 1U) {
        out->ok = false;
        return;
    }
    out->data[out->length++] = value;
    out->data[out->length] = '\0';
}

static void preorder_recursive_impl(const BinaryNode *root, CharOutput *out) {
    if (root == NULL || !out->ok) {
        return;
    }
    char_output_put(out, root->key);
    preorder_recursive_impl(root->left, out);
    preorder_recursive_impl(root->right, out);
}

static void inorder_recursive_impl(const BinaryNode *root, CharOutput *out) {
    if (root == NULL || !out->ok) {
        return;
    }
    inorder_recursive_impl(root->left, out);
    char_output_put(out, root->key);
    inorder_recursive_impl(root->right, out);
}

static void postorder_recursive_impl(const BinaryNode *root, CharOutput *out) {
    if (root == NULL || !out->ok) {
        return;
    }
    postorder_recursive_impl(root->left, out);
    postorder_recursive_impl(root->right, out);
    char_output_put(out, root->key);
}

typedef void (*TraversalImpl)(const BinaryNode *, CharOutput *);

static bool traversal_to_string(const BinaryNode *root,
                                char *buffer,
                                size_t capacity,
                                TraversalImpl implementation) {
    CharOutput out;
    if (implementation == NULL) {
        return false;
    }
    char_output_init(&out, buffer, capacity);
    if (!out.ok) {
        return false;
    }
    implementation(root, &out);
    return out.ok;
}

static bool binary_preorder(const BinaryNode *root,
                            char *buffer,
                            size_t capacity) {
    return traversal_to_string(root, buffer, capacity, preorder_recursive_impl);
}

static bool binary_inorder(const BinaryNode *root,
                           char *buffer,
                           size_t capacity) {
    return traversal_to_string(root, buffer, capacity, inorder_recursive_impl);
}

static bool binary_postorder(const BinaryNode *root,
                             char *buffer,
                             size_t capacity) {
    return traversal_to_string(root, buffer, capacity, postorder_recursive_impl);
}

typedef struct {
    const BinaryNode **data;
    size_t size;
    size_t capacity;
} BinaryNodeStack;

static void binary_stack_destroy(BinaryNodeStack *stack) {
    if (stack == NULL) {
        return;
    }
    free(stack->data);
    stack->data = NULL;
    stack->size = 0U;
    stack->capacity = 0U;
}

static bool binary_stack_push(BinaryNodeStack *stack, const BinaryNode *node) {
    size_t new_capacity = 0U;
    size_t bytes = 0U;
    const BinaryNode **new_data = NULL;

    if (stack == NULL) {
        return false;
    }
    if (stack->size == stack->capacity) {
        if (stack->capacity == 0U) {
            new_capacity = 8U;
        } else if (!checked_mul_size(stack->capacity, 2U, &new_capacity)) {
            return false;
        }
        if (!checked_mul_size(new_capacity, sizeof(*new_data), &bytes)) {
            return false;
        }
        new_data = realloc(stack->data, bytes);
        if (new_data == NULL) {
            return false;
        }
        stack->data = new_data;
        stack->capacity = new_capacity;
    }
    stack->data[stack->size++] = node;
    return true;
}

static bool binary_inorder_iterative(const BinaryNode *root,
                                     char *buffer,
                                     size_t capacity) {
    BinaryNodeStack stack = {NULL, 0U, 0U};
    const BinaryNode *cursor = root;
    CharOutput out;

    char_output_init(&out, buffer, capacity);
    if (!out.ok) {
        return false;
    }

    while (cursor != NULL || stack.size != 0U) {
        while (cursor != NULL) {
            if (!binary_stack_push(&stack, cursor)) {
                binary_stack_destroy(&stack);
                return false;
            }
            cursor = cursor->left;
        }
        cursor = stack.data[--stack.size];
        char_output_put(&out, cursor->key);
        if (!out.ok) {
            binary_stack_destroy(&stack);
            return false;
        }
        cursor = cursor->right;
    }

    binary_stack_destroy(&stack);
    return true;
}

typedef struct {
    const BinaryNode **data;
    size_t head;
    size_t size;
    size_t capacity;
} BinaryNodeQueue;

static void binary_queue_destroy(BinaryNodeQueue *queue) {
    if (queue == NULL) {
        return;
    }
    free(queue->data);
    queue->data = NULL;
    queue->head = 0U;
    queue->size = 0U;
    queue->capacity = 0U;
}

static bool binary_queue_grow(BinaryNodeQueue *queue) {
    size_t new_capacity = queue->capacity == 0U ? 8U : 0U;
    size_t bytes = 0U;
    const BinaryNode **new_data = NULL;
    size_t i = 0U;

    if (queue->capacity != 0U &&
        !checked_mul_size(queue->capacity, 2U, &new_capacity)) {
        return false;
    }
    if (!checked_mul_size(new_capacity, sizeof(*new_data), &bytes)) {
        return false;
    }
    new_data = malloc(bytes);
    if (new_data == NULL) {
        return false;
    }
    for (i = 0U; i < queue->size; ++i) {
        new_data[i] = queue->data[(queue->head + i) % queue->capacity];
    }
    free(queue->data);
    queue->data = new_data;
    queue->head = 0U;
    queue->capacity = new_capacity;
    return true;
}

static bool binary_queue_push(BinaryNodeQueue *queue, const BinaryNode *node) {
    size_t position = 0U;
    if (queue == NULL) {
        return false;
    }
    if (queue->size == queue->capacity && !binary_queue_grow(queue)) {
        return false;
    }
    position = queue->head >= queue->capacity - queue->size
                 ? queue->head - (queue->capacity - queue->size)
                 : queue->head + queue->size;
    queue->data[position] = node;
    ++queue->size;
    return true;
}

static const BinaryNode *binary_queue_pop(BinaryNodeQueue *queue) {
    const BinaryNode *node = NULL;
    assert(queue != NULL && queue->size != 0U);
    node = queue->data[queue->head];
    queue->head = (queue->head + 1U) % queue->capacity;
    --queue->size;
    return node;
}

static bool binary_levelorder(const BinaryNode *root,
                              char *buffer,
                              size_t capacity) {
    BinaryNodeQueue queue = {NULL, 0U, 0U, 0U};
    CharOutput out;
    const BinaryNode *node = NULL;

    char_output_init(&out, buffer, capacity);
    if (!out.ok) {
        return false;
    }
    if (root != NULL && !binary_queue_push(&queue, root)) {
        return false;
    }
    while (queue.size != 0U) {
        node = binary_queue_pop(&queue);
        char_output_put(&out, node->key);
        if (!out.ok ||
            (node->left != NULL && !binary_queue_push(&queue, node->left)) ||
            (node->right != NULL && !binary_queue_push(&queue, node->right))) {
            binary_queue_destroy(&queue);
            return false;
        }
    }
    binary_queue_destroy(&queue);
    return true;
}

typedef struct {
    size_t nodes;
    size_t leaves;
    size_t height;
    bool proper;
} BinaryStats;

static bool binary_stats_recursive(const BinaryNode *root, BinaryStats *stats) {
    BinaryStats left = {0U, 0U, 0U, true};
    BinaryStats right = {0U, 0U, 0U, true};
    size_t children = 0U;
    size_t nodes = 0U;
    size_t leaves = 0U;
    size_t greatest_height = 0U;

    if (stats == NULL) {
        return false;
    }
    if (root == NULL) {
        *stats = (BinaryStats){0U, 0U, 0U, true};
        return true;
    }
    if (!binary_stats_recursive(root->left, &left) ||
        !binary_stats_recursive(root->right, &right) ||
        !checked_add_size(left.nodes, right.nodes, &children) ||
        !checked_add_size(children, 1U, &nodes)) {
        return false;
    }
    stats->nodes = nodes;
    if (root->left == NULL && root->right == NULL) {
        leaves = 1U;
    } else if (!checked_add_size(left.leaves, right.leaves, &leaves)) {
        return false;
    }
    greatest_height = left.height > right.height ? left.height : right.height;
    if (!checked_add_size(greatest_height, 1U, &stats->height)) {
        return false;
    }
    stats->leaves = leaves;
    stats->proper = left.proper && right.proper &&
                    ((root->left == NULL) == (root->right == NULL));
    return true;
}

static bool binary_is_complete(const BinaryNode *root, bool *result) {
    BinaryNodeQueue queue = {NULL, 0U, 0U, 0U};
    const BinaryNode *node = NULL;
    bool gap_seen = false;

    if (result == NULL) {
        return false;
    }
    *result = true;
    if (root == NULL) {
        return true;
    }
    if (!binary_queue_push(&queue, root)) {
        return false;
    }
    while (queue.size != 0U) {
        node = binary_queue_pop(&queue);
        if (node->left != NULL) {
            if (gap_seen) {
                *result = false;
            }
            if (!binary_queue_push(&queue, node->left)) {
                binary_queue_destroy(&queue);
                return false;
            }
        } else {
            gap_seen = true;
        }
        if (node->right != NULL) {
            if (gap_seen) {
                *result = false;
            }
            if (!binary_queue_push(&queue, node->right)) {
                binary_queue_destroy(&queue);
                return false;
            }
        } else {
            gap_seen = true;
        }
    }
    binary_queue_destroy(&queue);
    return true;
}

/* 重建约定：键为单字节且互不重复。重复键会使切分位置不唯一。 */
static BinaryNode *binary_rebuild_impl(const char *preorder,
                                       size_t pre_begin,
                                       const char *inorder,
                                       size_t in_begin,
                                       size_t length,
                                       bool *ok) {
    BinaryNode *root = NULL;
    size_t split = 0U;
    size_t right_length = 0U;

    if (!*ok || length == 0U) {
        return NULL;
    }
    while (split < length && inorder[in_begin + split] != preorder[pre_begin]) {
        ++split;
    }
    if (split == length) {
        *ok = false;
        return NULL;
    }
    root = binary_node_create(preorder[pre_begin]);
    if (root == NULL) {
        *ok = false;
        return NULL;
    }
    root->left = binary_rebuild_impl(preorder, pre_begin + 1U,
                                     inorder, in_begin, split, ok);
    right_length = length - split - 1U;
    root->right = binary_rebuild_impl(preorder, pre_begin + 1U + split,
                                      inorder, in_begin + split + 1U,
                                      right_length, ok);
    if (!*ok) {
        binary_tree_destroy(root);
        return NULL;
    }
    return root;
}

static bool sequence_has_unique_bytes(const char *sequence, size_t length) {
    bool seen[UCHAR_MAX + 1U] = {false};
    size_t i = 0U;
    for (i = 0U; i < length; ++i) {
        unsigned char key = (unsigned char)sequence[i];
        if (seen[key]) {
            return false;
        }
        seen[key] = true;
    }
    return true;
}

static bool binary_rebuild_pre_in(const char *preorder,
                                  const char *inorder,
                                  size_t length,
                                  BinaryNode **result) {
    BinaryNode *root = NULL;
    bool ok = true;
    size_t i = 0U;
    bool present[UCHAR_MAX + 1U] = {false};

    if (result == NULL || (length != 0U && (preorder == NULL || inorder == NULL))) {
        return false;
    }
    if (length == 0U) {
        binary_tree_destroy(*result);
        *result = NULL;
        return true;
    }
    if (!sequence_has_unique_bytes(preorder, length) ||
        !sequence_has_unique_bytes(inorder, length)) {
        return false;
    }
    for (i = 0U; i < length; ++i) {
        present[(unsigned char)preorder[i]] = true;
    }
    for (i = 0U; i < length; ++i) {
        if (!present[(unsigned char)inorder[i]]) {
            return false;
        }
    }
    root = binary_rebuild_impl(preorder, 0U, inorder, 0U, length, &ok);
    if (!ok) {
        binary_tree_destroy(root);
        return false;
    }
    binary_tree_destroy(*result);
    *result = root;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 普通树和森林：孩子—兄弟表示法                                             */

typedef struct ChildSiblingNode {
    char key;
    struct ChildSiblingNode *first_child;
    struct ChildSiblingNode *next_sibling;
} ChildSiblingNode;

static ChildSiblingNode *cs_node_create(char key) {
    ChildSiblingNode *node = malloc(sizeof(*node));
    if (node != NULL) {
        node->key = key;
        node->first_child = NULL;
        node->next_sibling = NULL;
    }
    return node;
}

/* 销毁从 root 开始的整片森林；每个结点只有一个所有者。 */
static void cs_forest_destroy(ChildSiblingNode *root) {
    if (root == NULL) {
        return;
    }
    cs_forest_destroy(root->first_child);
    cs_forest_destroy(root->next_sibling);
    free(root);
}

static bool cs_count_impl(const ChildSiblingNode *root,
                          size_t depth,
                          size_t *nodes,
                          size_t *height) {
    const ChildSiblingNode *cursor = root;
    size_t local_nodes = 0U;
    size_t local_height = depth;

    while (cursor != NULL) {
        size_t child_nodes = 0U;
        size_t child_height = depth;
        if (cursor->first_child != NULL) {
            size_t child_depth = 0U;
            if (!checked_add_size(depth, 1U, &child_depth) ||
                !cs_count_impl(cursor->first_child, child_depth,
                               &child_nodes, &child_height)) {
                return false;
            }
        }
        if (!checked_add_size(local_nodes, child_nodes, &local_nodes) ||
            !checked_add_size(local_nodes, 1U, &local_nodes)) {
            return false;
        }
        if (child_height > local_height) {
            local_height = child_height;
        }
        cursor = cursor->next_sibling;
    }
    *nodes = local_nodes;
    *height = local_height;
    return true;
}

static bool cs_forest_stats(const ChildSiblingNode *forest,
                            size_t *tree_count,
                            size_t *node_count,
                            size_t *max_height) {
    const ChildSiblingNode *cursor = forest;
    size_t trees = 0U;

    if (tree_count == NULL || node_count == NULL || max_height == NULL) {
        return false;
    }
    if (forest == NULL) {
        *tree_count = 0U;
        *node_count = 0U;
        *max_height = 0U;
        return true;
    }
    while (cursor != NULL) {
        if (!checked_add_size(trees, 1U, &trees)) {
            return false;
        }
        cursor = cursor->next_sibling;
    }
    if (!cs_count_impl(forest, 1U, node_count, max_height)) {
        return false;
    }
    *tree_count = trees;
    return true;
}

static void cs_preorder_impl(const ChildSiblingNode *forest, CharOutput *out) {
    const ChildSiblingNode *cursor = forest;
    while (cursor != NULL && out->ok) {
        char_output_put(out, cursor->key);
        cs_preorder_impl(cursor->first_child, out);
        cursor = cursor->next_sibling;
    }
}

static bool cs_forest_preorder(const ChildSiblingNode *forest,
                               char *buffer,
                               size_t capacity) {
    CharOutput out;
    char_output_init(&out, buffer, capacity);
    if (!out.ok) {
        return false;
    }
    cs_preorder_impl(forest, &out);
    return out.ok;
}

/* ------------------------------------------------------------------------- */
/* 中序线索二叉树                                                             */

typedef enum {
    THREAD_CHILD = 0,
    THREAD_LINK = 1
} ThreadTag;

typedef struct ThreadNode {
    char key;
    struct ThreadNode *left;
    struct ThreadNode *right;
    ThreadTag left_tag;
    ThreadTag right_tag;
} ThreadNode;

static void threaded_tree_destroy(ThreadNode *root) {
    ThreadNode *left = NULL;
    ThreadNode *right = NULL;
    if (root == NULL) {
        return;
    }
    left = root->left_tag == THREAD_CHILD ? root->left : NULL;
    right = root->right_tag == THREAD_CHILD ? root->right : NULL;
    threaded_tree_destroy(left);
    threaded_tree_destroy(right);
    free(root);
}

static ThreadNode *thread_clone_safe(const BinaryNode *root, bool *ok) {
    ThreadNode *node = NULL;
    if (root == NULL || !*ok) {
        return NULL;
    }
    node = malloc(sizeof(*node));
    if (node == NULL) {
        *ok = false;
        return NULL;
    }
    node->key = root->key;
    node->left = NULL;
    node->right = NULL;
    node->left_tag = THREAD_CHILD;
    node->right_tag = THREAD_CHILD;
    node->left = thread_clone_safe(root->left, ok);
    node->right = thread_clone_safe(root->right, ok);
    if (!*ok) {
        threaded_tree_destroy(node);
        return NULL;
    }
    return node;
}

static void thread_inorder_build(ThreadNode *node, ThreadNode **previous) {
    if (node == NULL) {
        return;
    }
    thread_inorder_build(node->left, previous);
    if (node->left == NULL) {
        node->left_tag = THREAD_LINK;
        node->left = *previous;
    }
    if (*previous != NULL && (*previous)->right == NULL) {
        (*previous)->right_tag = THREAD_LINK;
        (*previous)->right = node;
    }
    *previous = node;
    thread_inorder_build(node->right, previous);
}

static bool threaded_tree_from_binary(const BinaryNode *source,
                                      ThreadNode **result) {
    ThreadNode *root = NULL;
    ThreadNode *previous = NULL;
    bool ok = true;

    if (result == NULL) {
        return false;
    }
    root = thread_clone_safe(source, &ok);
    if (!ok) {
        return false;
    }
    thread_inorder_build(root, &previous);
    if (previous != NULL && previous->right == NULL) {
        previous->right_tag = THREAD_LINK;
    }
    threaded_tree_destroy(*result);
    *result = root;
    return true;
}

static const ThreadNode *threaded_first(const ThreadNode *root) {
    const ThreadNode *cursor = root;
    while (cursor != NULL && cursor->left_tag == THREAD_CHILD &&
           cursor->left != NULL) {
        cursor = cursor->left;
    }
    return cursor;
}

static const ThreadNode *threaded_next(const ThreadNode *node) {
    if (node == NULL) {
        return NULL;
    }
    if (node->right_tag == THREAD_LINK) {
        return node->right;
    }
    return threaded_first(node->right);
}

static bool threaded_inorder(const ThreadNode *root,
                             char *buffer,
                             size_t capacity) {
    const ThreadNode *cursor = threaded_first(root);
    CharOutput out;
    char_output_init(&out, buffer, capacity);
    if (!out.ok) {
        return false;
    }
    while (cursor != NULL) {
        char_output_put(&out, cursor->key);
        cursor = threaded_next(cursor);
    }
    return out.ok;
}

/* ------------------------------------------------------------------------- */
/* 最小堆和稳定优先队列                                                       */

typedef struct {
    int priority;
    uint64_t sequence;
    char label;
} PriorityItem;

typedef struct {
    PriorityItem *items;
    size_t size;
    size_t capacity;
    uint64_t next_sequence;
} MinPriorityQueue;

static void priority_queue_destroy(MinPriorityQueue *queue) {
    if (queue == NULL) {
        return;
    }
    free(queue->items);
    queue->items = NULL;
    queue->size = 0U;
    queue->capacity = 0U;
    queue->next_sequence = 0U;
}

static bool priority_item_less(const PriorityItem *a, const PriorityItem *b) {
    return a->priority < b->priority ||
           (a->priority == b->priority && a->sequence < b->sequence);
}

static bool priority_queue_reserve(MinPriorityQueue *queue, size_t minimum) {
    size_t capacity = queue->capacity == 0U ? 8U : queue->capacity;
    size_t bytes = 0U;
    PriorityItem *items = NULL;

    if (minimum <= queue->capacity) {
        return true;
    }
    while (capacity < minimum) {
        if (!checked_mul_size(capacity, 2U, &capacity)) {
            return false;
        }
    }
    if (!checked_mul_size(capacity, sizeof(*items), &bytes)) {
        return false;
    }
    items = realloc(queue->items, bytes);
    if (items == NULL) {
        return false;
    }
    queue->items = items;
    queue->capacity = capacity;
    return true;
}

static bool priority_queue_push(MinPriorityQueue *queue,
                                int priority,
                                char label) {
    size_t index = 0U;
    size_t new_size = 0U;
    PriorityItem item;

    if (queue == NULL || queue->next_sequence == UINT64_MAX ||
        !checked_add_size(queue->size, 1U, &new_size) ||
        !priority_queue_reserve(queue, new_size)) {
        return false;
    }
    item.priority = priority;
    item.sequence = queue->next_sequence++;
    item.label = label;
    index = queue->size;
    queue->size = new_size;
    queue->items[index] = item;
    while (index != 0U) {
        size_t parent = (index - 1U) / 2U;
        PriorityItem swap;
        if (!priority_item_less(&queue->items[index], &queue->items[parent])) {
            break;
        }
        swap = queue->items[index];
        queue->items[index] = queue->items[parent];
        queue->items[parent] = swap;
        index = parent;
    }
    return true;
}

static bool priority_queue_pop(MinPriorityQueue *queue, PriorityItem *result) {
    size_t index = 0U;

    if (queue == NULL || result == NULL || queue->size == 0U) {
        return false;
    }
    *result = queue->items[0];
    --queue->size;
    if (queue->size == 0U) {
        return true;
    }
    queue->items[0] = queue->items[queue->size];
    for (;;) {
        size_t left = index * 2U + 1U;
        size_t right = left + 1U;
        size_t smallest = index;
        PriorityItem swap;

        if (left < queue->size &&
            priority_item_less(&queue->items[left], &queue->items[smallest])) {
            smallest = left;
        }
        if (right < queue->size &&
            priority_item_less(&queue->items[right], &queue->items[smallest])) {
            smallest = right;
        }
        if (smallest == index) {
            break;
        }
        swap = queue->items[index];
        queue->items[index] = queue->items[smallest];
        queue->items[smallest] = swap;
        index = smallest;
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* 哈夫曼树、编码与解码                                                       */

typedef struct HuffmanNode {
    unsigned char symbol;
    uint64_t weight;
    unsigned char minimum_symbol;
    uint64_t serial;
    struct HuffmanNode *left;
    struct HuffmanNode *right;
} HuffmanNode;

typedef struct {
    HuffmanNode **items;
    size_t size;
    size_t capacity;
} HuffmanHeap;

typedef struct {
    HuffmanNode *root;
    size_t leaf_count;
} HuffmanTree;

typedef struct {
    unsigned char symbol;
    char *bits;
} HuffmanCode;

typedef struct {
    HuffmanCode *codes;
    size_t count;
} HuffmanCodeTable;

static void huffman_node_destroy(HuffmanNode *node) {
    if (node == NULL) {
        return;
    }
    huffman_node_destroy(node->left);
    huffman_node_destroy(node->right);
    free(node);
}

static void huffman_tree_destroy(HuffmanTree *tree) {
    if (tree == NULL) {
        return;
    }
    huffman_node_destroy(tree->root);
    tree->root = NULL;
    tree->leaf_count = 0U;
}

static bool huffman_less(const HuffmanNode *a, const HuffmanNode *b) {
    if (a->weight != b->weight) {
        return a->weight < b->weight;
    }
    if (a->minimum_symbol != b->minimum_symbol) {
        return a->minimum_symbol < b->minimum_symbol;
    }
    return a->serial < b->serial;
}

static void huffman_heap_destroy_forest(HuffmanHeap *heap) {
    size_t i = 0U;
    if (heap == NULL) {
        return;
    }
    for (i = 0U; i < heap->size; ++i) {
        huffman_node_destroy(heap->items[i]);
    }
    free(heap->items);
    heap->items = NULL;
    heap->size = 0U;
    heap->capacity = 0U;
}

static bool huffman_heap_reserve(HuffmanHeap *heap, size_t minimum) {
    size_t capacity = heap->capacity == 0U ? 8U : heap->capacity;
    size_t bytes = 0U;
    HuffmanNode **items = NULL;
    if (minimum <= heap->capacity) {
        return true;
    }
    while (capacity < minimum) {
        if (!checked_mul_size(capacity, 2U, &capacity)) {
            return false;
        }
    }
    if (!checked_mul_size(capacity, sizeof(*items), &bytes)) {
        return false;
    }
    items = realloc(heap->items, bytes);
    if (items == NULL) {
        return false;
    }
    heap->items = items;
    heap->capacity = capacity;
    return true;
}

static bool huffman_heap_push(HuffmanHeap *heap, HuffmanNode *node) {
    size_t index = 0U;
    if (heap == NULL || node == NULL ||
        !huffman_heap_reserve(heap, heap->size + 1U)) {
        return false;
    }
    index = heap->size++;
    heap->items[index] = node;
    while (index != 0U) {
        size_t parent = (index - 1U) / 2U;
        HuffmanNode *swap = NULL;
        if (!huffman_less(heap->items[index], heap->items[parent])) {
            break;
        }
        swap = heap->items[index];
        heap->items[index] = heap->items[parent];
        heap->items[parent] = swap;
        index = parent;
    }
    return true;
}

static HuffmanNode *huffman_heap_pop(HuffmanHeap *heap) {
    HuffmanNode *result = NULL;
    size_t index = 0U;
    assert(heap != NULL && heap->size != 0U);
    result = heap->items[0];
    --heap->size;
    if (heap->size == 0U) {
        return result;
    }
    heap->items[0] = heap->items[heap->size];
    for (;;) {
        size_t left = index * 2U + 1U;
        size_t right = left + 1U;
        size_t smallest = index;
        HuffmanNode *swap = NULL;
        if (left < heap->size &&
            huffman_less(heap->items[left], heap->items[smallest])) {
            smallest = left;
        }
        if (right < heap->size &&
            huffman_less(heap->items[right], heap->items[smallest])) {
            smallest = right;
        }
        if (smallest == index) {
            break;
        }
        swap = heap->items[index];
        heap->items[index] = heap->items[smallest];
        heap->items[smallest] = swap;
        index = smallest;
    }
    return result;
}

static HuffmanNode *huffman_leaf_create(unsigned char symbol,
                                        uint64_t weight,
                                        uint64_t serial) {
    HuffmanNode *node = malloc(sizeof(*node));
    if (node != NULL) {
        node->symbol = symbol;
        node->weight = weight;
        node->minimum_symbol = symbol;
        node->serial = serial;
        node->left = NULL;
        node->right = NULL;
    }
    return node;
}

static bool huffman_tree_build(const unsigned char *symbols,
                               const uint64_t *weights,
                               size_t count,
                               HuffmanTree *result) {
    HuffmanHeap heap = {NULL, 0U, 0U};
    bool seen[UCHAR_MAX + 1U] = {false};
    uint64_t serial = 0U;
    size_t i = 0U;

    if (result == NULL || symbols == NULL || weights == NULL ||
        count == 0U || count > UCHAR_MAX + 1U) {
        return false;
    }
    for (i = 0U; i < count; ++i) {
        HuffmanNode *leaf = NULL;
        if (weights[i] == 0U || seen[symbols[i]]) {
            huffman_heap_destroy_forest(&heap);
            return false;
        }
        seen[symbols[i]] = true;
        leaf = huffman_leaf_create(symbols[i], weights[i], serial++);
        if (leaf == NULL || !huffman_heap_push(&heap, leaf)) {
            huffman_node_destroy(leaf);
            huffman_heap_destroy_forest(&heap);
            return false;
        }
    }

    while (heap.size > 1U) {
        HuffmanNode *left = huffman_heap_pop(&heap);
        HuffmanNode *right = huffman_heap_pop(&heap);
        HuffmanNode *parent = NULL;
        uint64_t combined = 0U;

        if (!checked_add_u64(left->weight, right->weight, &combined)) {
            huffman_node_destroy(left);
            huffman_node_destroy(right);
            huffman_heap_destroy_forest(&heap);
            return false;
        }
        parent = malloc(sizeof(*parent));
        if (parent == NULL) {
            huffman_node_destroy(left);
            huffman_node_destroy(right);
            huffman_heap_destroy_forest(&heap);
            return false;
        }
        parent->symbol = 0U;
        parent->weight = combined;
        parent->minimum_symbol = left->minimum_symbol < right->minimum_symbol
                                   ? left->minimum_symbol
                                   : right->minimum_symbol;
        parent->serial = serial++;
        parent->left = left;
        parent->right = right;
        if (!huffman_heap_push(&heap, parent)) {
            huffman_node_destroy(parent);
            huffman_heap_destroy_forest(&heap);
            return false;
        }
    }

    {
        HuffmanTree replacement = {huffman_heap_pop(&heap), count};
        free(heap.items);
        huffman_tree_destroy(result);
        *result = replacement;
    }
    return true;
}

static void huffman_code_table_destroy(HuffmanCodeTable *table) {
    size_t i = 0U;
    if (table == NULL) {
        return;
    }
    for (i = 0U; i < table->count; ++i) {
        free(table->codes[i].bits);
    }
    free(table->codes);
    table->codes = NULL;
    table->count = 0U;
}

typedef struct {
    HuffmanCode *codes;
    size_t capacity;
    size_t count;
    char *path;
    bool ok;
} HuffmanCodeBuilder;

static void huffman_codes_build_impl(const HuffmanNode *node,
                                     size_t depth,
                                     HuffmanCodeBuilder *builder) {
    size_t length = depth == 0U ? 1U : depth;
    char *bits = NULL;

    if (node == NULL || !builder->ok) {
        return;
    }
    if (node->left == NULL && node->right == NULL) {
        if (builder->count >= builder->capacity) {
            builder->ok = false;
            return;
        }
        bits = malloc(length + 1U);
        if (bits == NULL) {
            builder->ok = false;
            return;
        }
        if (depth == 0U) {
            bits[0] = '0';
        } else {
            memcpy(bits, builder->path, depth);
        }
        bits[length] = '\0';
        builder->codes[builder->count].symbol = node->symbol;
        builder->codes[builder->count].bits = bits;
        ++builder->count;
        return;
    }
    if (node->left == NULL || node->right == NULL || depth >= builder->capacity) {
        builder->ok = false;
        return;
    }
    builder->path[depth] = '0';
    huffman_codes_build_impl(node->left, depth + 1U, builder);
    builder->path[depth] = '1';
    huffman_codes_build_impl(node->right, depth + 1U, builder);
}

static bool huffman_make_code_table(const HuffmanTree *tree,
                                    HuffmanCodeTable *result) {
    HuffmanCodeTable replacement = {NULL, 0U};
    HuffmanCodeBuilder builder;
    size_t bytes = 0U;

    if (tree == NULL || result == NULL || tree->root == NULL ||
        tree->leaf_count == 0U ||
        !checked_mul_size(tree->leaf_count, sizeof(*replacement.codes), &bytes)) {
        return false;
    }
    replacement.codes = calloc(1U, bytes);
    builder.path = malloc(tree->leaf_count + 1U);
    if (replacement.codes == NULL || builder.path == NULL) {
        free(builder.path);
        huffman_code_table_destroy(&replacement);
        return false;
    }
    builder.codes = replacement.codes;
    builder.capacity = tree->leaf_count;
    builder.count = 0U;
    builder.ok = true;
    huffman_codes_build_impl(tree->root, 0U, &builder);
    free(builder.path);
    replacement.count = builder.count;
    if (!builder.ok || builder.count != tree->leaf_count) {
        huffman_code_table_destroy(&replacement);
        return false;
    }
    huffman_code_table_destroy(result);
    *result = replacement;
    return true;
}

static const char *huffman_code_find(const HuffmanCodeTable *table,
                                     unsigned char symbol) {
    size_t i = 0U;
    if (table == NULL) {
        return NULL;
    }
    for (i = 0U; i < table->count; ++i) {
        if (table->codes[i].symbol == symbol) {
            return table->codes[i].bits;
        }
    }
    return NULL;
}

static bool huffman_encode(const HuffmanCodeTable *table,
                           const unsigned char *input,
                           size_t input_length,
                           char *bits,
                           size_t capacity) {
    size_t used = 0U;
    size_t i = 0U;
    if (table == NULL || bits == NULL || capacity == 0U ||
        (input_length != 0U && input == NULL)) {
        return false;
    }
    bits[0] = '\0';
    for (i = 0U; i < input_length; ++i) {
        const char *code = huffman_code_find(table, input[i]);
        size_t length = code == NULL ? 0U : strlen(code);
        size_t next = 0U;
        if (code == NULL || !checked_add_size(used, length, &next) ||
            next >= capacity) {
            bits[0] = '\0';
            return false;
        }
        memcpy(bits + used, code, length);
        used = next;
        bits[used] = '\0';
    }
    return true;
}

static bool huffman_decode(const HuffmanTree *tree,
                           const char *bits,
                           unsigned char *output,
                           size_t capacity,
                           size_t *output_length) {
    const HuffmanNode *cursor = NULL;
    size_t used = 0U;
    size_t i = 0U;

    if (tree == NULL || tree->root == NULL || bits == NULL ||
        output == NULL || output_length == NULL) {
        return false;
    }
    if (tree->root->left == NULL && tree->root->right == NULL) {
        for (i = 0U; bits[i] != '\0'; ++i) {
            if (bits[i] != '0' || used >= capacity) {
                return false;
            }
            output[used++] = tree->root->symbol;
        }
        *output_length = used;
        return true;
    }
    cursor = tree->root;
    for (i = 0U; bits[i] != '\0'; ++i) {
        if (bits[i] == '0') {
            cursor = cursor->left;
        } else if (bits[i] == '1') {
            cursor = cursor->right;
        } else {
            return false;
        }
        if (cursor == NULL) {
            return false;
        }
        if (cursor->left == NULL && cursor->right == NULL) {
            if (used >= capacity) {
                return false;
            }
            output[used++] = cursor->symbol;
            cursor = tree->root;
        }
    }
    if (cursor != tree->root) {
        return false;
    }
    *output_length = used;
    return true;
}

static bool huffman_wpl_impl(const HuffmanNode *node,
                             uint64_t depth,
                             uint64_t *result) {
    uint64_t left = 0U;
    uint64_t right = 0U;
    uint64_t product = 0U;
    if (node == NULL || result == NULL) {
        return false;
    }
    if (node->left == NULL && node->right == NULL) {
        if (!checked_mul_u64(node->weight, depth, &product)) {
            return false;
        }
        *result = product;
        return true;
    }
    if (node->left == NULL || node->right == NULL || depth == UINT64_MAX ||
        !huffman_wpl_impl(node->left, depth + 1U, &left) ||
        !huffman_wpl_impl(node->right, depth + 1U, &right)) {
        return false;
    }
    return checked_add_u64(left, right, result);
}

static bool huffman_wpl(const HuffmanTree *tree, uint64_t *result) {
    if (tree == NULL || tree->root == NULL) {
        return false;
    }
    return huffman_wpl_impl(tree->root, 0U, result);
}

/* ------------------------------------------------------------------------- */
/* 并查集：按大小合并 + 路径压缩                                             */

typedef struct {
    size_t *parent;
    size_t *set_size;
    size_t count;
    size_t components;
} DisjointSet;

static void dsu_destroy(DisjointSet *dsu) {
    if (dsu == NULL) {
        return;
    }
    free(dsu->parent);
    free(dsu->set_size);
    dsu->parent = NULL;
    dsu->set_size = NULL;
    dsu->count = 0U;
    dsu->components = 0U;
}

static bool dsu_create(DisjointSet *dsu, size_t count) {
    DisjointSet replacement = {NULL, NULL, count, count};
    size_t bytes = 0U;
    size_t i = 0U;

    if (dsu == NULL || !checked_mul_size(count, sizeof(*replacement.parent), &bytes)) {
        return false;
    }
    if (count != 0U) {
        replacement.parent = malloc(bytes);
        replacement.set_size = malloc(bytes);
        if (replacement.parent == NULL || replacement.set_size == NULL) {
            dsu_destroy(&replacement);
            return false;
        }
        for (i = 0U; i < count; ++i) {
            replacement.parent[i] = i;
            replacement.set_size[i] = 1U;
        }
    }
    dsu_destroy(dsu);
    *dsu = replacement;
    return true;
}

static bool dsu_find(DisjointSet *dsu, size_t element, size_t *root) {
    size_t cursor = element;
    size_t representative = 0U;
    if (dsu == NULL || root == NULL || element >= dsu->count) {
        return false;
    }
    while (dsu->parent[cursor] != cursor) {
        cursor = dsu->parent[cursor];
    }
    representative = cursor;
    cursor = element;
    while (dsu->parent[cursor] != cursor) {
        size_t next = dsu->parent[cursor];
        dsu->parent[cursor] = representative;
        cursor = next;
    }
    *root = representative;
    return true;
}

static bool dsu_union(DisjointSet *dsu,
                      size_t first,
                      size_t second,
                      bool *merged) {
    size_t a = 0U;
    size_t b = 0U;
    size_t combined = 0U;
    size_t swap = 0U;

    if (merged == NULL || !dsu_find(dsu, first, &a) ||
        !dsu_find(dsu, second, &b)) {
        return false;
    }
    if (a == b) {
        *merged = false;
        return true;
    }
    /* 大集合做根；大小相同时让编号较小的根留下，结果可复现。 */
    if (dsu->set_size[a] < dsu->set_size[b] ||
        (dsu->set_size[a] == dsu->set_size[b] && a > b)) {
        swap = a;
        a = b;
        b = swap;
    }
    if (!checked_add_size(dsu->set_size[a], dsu->set_size[b], &combined)) {
        return false;
    }
    dsu->parent[b] = a;
    dsu->set_size[a] = combined;
    --dsu->components;
    *merged = true;
    return true;
}

static bool dsu_connected(DisjointSet *dsu,
                          size_t first,
                          size_t second,
                          bool *connected) {
    size_t a = 0U;
    size_t b = 0U;
    if (connected == NULL || !dsu_find(dsu, first, &a) ||
        !dsu_find(dsu, second, &b)) {
        return false;
    }
    *connected = a == b;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 回溯：在选择/不选择构成的二叉搜索树中求子集和                           */

typedef struct {
    bool *first_choice;
    size_t length;
    uint64_t solution_count;
    uint64_t visited_states;
    bool found;
} SubsetSumResult;

static void subset_sum_result_destroy(SubsetSumResult *result) {
    if (result == NULL) {
        return;
    }
    free(result->first_choice);
    result->first_choice = NULL;
    result->length = 0U;
    result->solution_count = 0U;
    result->visited_states = 0U;
    result->found = false;
}

typedef struct {
    const int64_t *values;
    size_t length;
    int64_t target;
    bool *current;
    SubsetSumResult *result;
    bool ok;
} SubsetSumContext;

static void subset_sum_backtrack(SubsetSumContext *context,
                                 size_t index,
                                 int64_t sum) {
    int64_t included_sum = 0;
    if (!context->ok) {
        return;
    }
    if (context->result->visited_states == UINT64_MAX) {
        context->ok = false;
        return;
    }
    ++context->result->visited_states;
    if (index == context->length) {
        if (sum == context->target) {
            if (context->result->solution_count == UINT64_MAX) {
                context->ok = false;
                return;
            }
            ++context->result->solution_count;
            if (!context->result->found) {
                if (context->length != 0U) {
                    memcpy(context->result->first_choice, context->current,
                           context->length * sizeof(*context->current));
                }
                context->result->found = true;
            }
        }
        return;
    }

    /* 先走“选择”分支，因此第一组答案具有确定性。 */
    context->current[index] = true;
    if (!checked_add_i64(sum, context->values[index], &included_sum)) {
        context->ok = false;
        return;
    }
    subset_sum_backtrack(context, index + 1U, included_sum);
    context->current[index] = false;
    subset_sum_backtrack(context, index + 1U, sum);
}

static bool subset_sum_solve(const int64_t *values,
                             size_t length,
                             int64_t target,
                             SubsetSumResult *result) {
    SubsetSumResult replacement = {NULL, length, 0U, 0U, false};
    SubsetSumContext context;
    bool *current = NULL;
    size_t bytes = 0U;

    /* 教学示例明确限制规模，避免意外启动 2^n 次搜索。 */
    if (result == NULL || length > 24U ||
        (length != 0U && values == NULL) ||
        !checked_mul_size(length, sizeof(*current), &bytes)) {
        return false;
    }
    if (length != 0U) {
        replacement.first_choice = calloc(1U, bytes);
        current = calloc(1U, bytes);
        if (replacement.first_choice == NULL || current == NULL) {
            free(current);
            subset_sum_result_destroy(&replacement);
            return false;
        }
    }
    context.values = values;
    context.length = length;
    context.target = target;
    context.current = current;
    context.result = &replacement;
    context.ok = true;
    subset_sum_backtrack(&context, 0U, 0);
    free(current);
    if (!context.ok) {
        subset_sum_result_destroy(&replacement);
        return false;
    }
    subset_sum_result_destroy(result);
    *result = replacement;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 树的计数：Catalan 数                                                       */

/* n 个互异键能构成的二叉搜索树形状数，也是 n 对括号的合法配对数。 */
static bool catalan_count(size_t n, uint64_t *result) {
    uint64_t *values = NULL;
    size_t count = 0U;
    size_t bytes = 0U;
    size_t i = 0U;

    if (result == NULL || !checked_add_size(n, 1U, &count) ||
        !checked_mul_size(count, sizeof(*values), &bytes)) {
        return false;
    }
    values = calloc(1U, bytes);
    if (values == NULL) {
        return false;
    }
    values[0] = 1U;
    for (i = 1U; i <= n; ++i) {
        size_t left = 0U;
        for (left = 0U; left < i; ++left) {
            uint64_t product = 0U;
            uint64_t sum = 0U;
            if (!checked_mul_u64(values[left], values[i - 1U - left], &product) ||
                !checked_add_u64(values[i], product, &sum)) {
                free(values);
                return false;
            }
            values[i] = sum;
        }
    }
    *result = values[n];
    free(values);
    return true;
}

/* 每个内部结点恰有两个孩子的有序满二叉树：总节点数必须为奇数。 */
static bool full_ordered_tree_count(size_t total_nodes, uint64_t *result) {
    if (result == NULL) {
        return false;
    }
    if (total_nodes == 0U || (total_nodes & 1U) == 0U) {
        *result = 0U;
        return true;
    }
    return catalan_count((total_nodes - 1U) / 2U, result);
}

/* 高度按“层数”计：空树高度 0；高度 h 的满二叉树有 2^h - 1 个结点。 */
static bool perfect_binary_tree_node_count(size_t height, uint64_t *result) {
    uint64_t nodes = 0U;
    size_t level = 0U;
    if (result == NULL) {
        return false;
    }
    for (level = 0U; level < height; ++level) {
        uint64_t doubled = 0U;
        if (!checked_mul_u64(nodes, 2U, &doubled) ||
            !checked_add_u64(doubled, 1U, &nodes)) {
            return false;
        }
    }
    *result = nodes;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 自检                                                                       */

static void test_binary_tree(void) {
    BinaryNode *tree = NULL;
    BinaryNode *skewed = NULL;
    BinaryNode *unchanged = NULL;
    BinaryStats stats;
    bool complete = false;
    char text[32];

    assert(binary_rebuild_pre_in("ABDECFG", "DBEAFCG", 7U, &tree));
    assert(binary_preorder(tree, text, sizeof(text)));
    assert(strcmp(text, "ABDECFG") == 0);
    assert(binary_inorder(tree, text, sizeof(text)));
    assert(strcmp(text, "DBEAFCG") == 0);
    assert(binary_postorder(tree, text, sizeof(text)));
    assert(strcmp(text, "DEBFGCA") == 0);
    assert(binary_inorder_iterative(tree, text, sizeof(text)));
    assert(strcmp(text, "DBEAFCG") == 0);
    assert(binary_levelorder(tree, text, sizeof(text)));
    assert(strcmp(text, "ABCDEFG") == 0);
    assert(!binary_preorder(tree, text, 4U));

    assert(binary_stats_recursive(tree, &stats));
    assert(stats.nodes == 7U && stats.leaves == 4U && stats.height == 3U);
    assert(stats.proper);
    assert(binary_is_complete(tree, &complete) && complete);

    assert(binary_rebuild_pre_in("ABCD", "ABCD", 4U, &skewed));
    assert(binary_stats_recursive(skewed, &stats));
    assert(stats.nodes == 4U && stats.leaves == 1U && stats.height == 4U);
    assert(!stats.proper);
    assert(binary_is_complete(skewed, &complete) && !complete);

    assert(binary_rebuild_pre_in("Z", "Z", 1U, &unchanged));
    assert(!binary_rebuild_pre_in("AAB", "ABA", 3U, &unchanged));
    assert(!binary_rebuild_pre_in("ABC", "ABD", 3U, &unchanged));
    assert(!binary_rebuild_pre_in("ABC", "CAB", 3U, &unchanged));
    assert(unchanged != NULL && unchanged->key == 'Z');
    assert(binary_rebuild_pre_in(NULL, NULL, 0U, &unchanged));
    assert(unchanged == NULL);
    assert(binary_preorder(NULL, text, sizeof(text)) && strcmp(text, "") == 0);
    assert(binary_inorder_iterative(NULL, text, sizeof(text)) &&
           strcmp(text, "") == 0);
    assert(binary_levelorder(NULL, text, sizeof(text)) && strcmp(text, "") == 0);
    assert(binary_stats_recursive(NULL, &stats));
    assert(stats.nodes == 0U && stats.leaves == 0U && stats.height == 0U &&
           stats.proper);
    assert(binary_is_complete(NULL, &complete) && complete);

    binary_tree_destroy(unchanged);
    binary_tree_destroy(skewed);
    binary_tree_destroy(tree);
}

static void test_child_sibling_forest(void) {
    ChildSiblingNode *a = cs_node_create('A');
    ChildSiblingNode *b = cs_node_create('B');
    ChildSiblingNode *c = cs_node_create('C');
    ChildSiblingNode *d = cs_node_create('D');
    ChildSiblingNode *x = cs_node_create('X');
    ChildSiblingNode *y = cs_node_create('Y');
    size_t trees = 0U;
    size_t nodes = 0U;
    size_t height = 0U;
    char text[16];

    assert(a != NULL && b != NULL && c != NULL && d != NULL &&
           x != NULL && y != NULL);
    a->first_child = b;
    b->next_sibling = c;
    b->first_child = d;
    a->next_sibling = x;
    x->first_child = y;

    assert(cs_forest_preorder(a, text, sizeof(text)));
    assert(strcmp(text, "ABDCXY") == 0);
    assert(cs_forest_stats(a, &trees, &nodes, &height));
    assert(trees == 2U && nodes == 6U && height == 3U);
    assert(cs_forest_stats(NULL, &trees, &nodes, &height));
    assert(trees == 0U && nodes == 0U && height == 0U);
    assert(cs_forest_preorder(NULL, text, sizeof(text)) && strcmp(text, "") == 0);

    cs_forest_destroy(a);
}

static void test_threaded_tree(void) {
    BinaryNode *source = NULL;
    ThreadNode *threaded = NULL;
    ThreadNode *single = NULL;
    char text[32];

    assert(binary_rebuild_pre_in("ABDECFG", "DBEAFCG", 7U, &source));
    assert(threaded_tree_from_binary(source, &threaded));
    assert(threaded_inorder(threaded, text, sizeof(text)));
    assert(strcmp(text, "DBEAFCG") == 0);

    assert(binary_rebuild_pre_in("Q", "Q", 1U, &source));
    assert(threaded_tree_from_binary(source, &single));
    assert(single != NULL && single->left_tag == THREAD_LINK &&
           single->right_tag == THREAD_LINK);
    assert(threaded_inorder(single, text, sizeof(text)));
    assert(strcmp(text, "Q") == 0);
    assert(threaded_tree_from_binary(NULL, &single));
    assert(single == NULL);
    assert(threaded_inorder(NULL, text, sizeof(text)) && strcmp(text, "") == 0);

    threaded_tree_destroy(single);
    threaded_tree_destroy(threaded);
    binary_tree_destroy(source);
}

static void test_priority_queue(void) {
    MinPriorityQueue queue = {NULL, 0U, 0U, 0U};
    PriorityItem item;

    assert(priority_queue_push(&queue, 3, 'C'));
    assert(priority_queue_push(&queue, 1, 'A'));
    assert(priority_queue_push(&queue, 1, 'B'));
    assert(priority_queue_push(&queue, -2, 'Z'));
    assert(priority_queue_pop(&queue, &item) && item.label == 'Z');
    assert(priority_queue_pop(&queue, &item) && item.label == 'A');
    assert(priority_queue_pop(&queue, &item) && item.label == 'B');
    assert(priority_queue_pop(&queue, &item) && item.label == 'C');
    assert(!priority_queue_pop(&queue, &item));
    queue.next_sequence = UINT64_MAX;
    assert(!priority_queue_push(&queue, 0, 'X'));
    priority_queue_destroy(&queue);
}

static void test_huffman(void) {
    const unsigned char symbols[] = {'A', 'B', 'C', 'D', 'E', 'F'};
    const uint64_t weights[] = {5U, 7U, 10U, 15U, 20U, 45U};
    const unsigned char message[] = {'F', 'C', 'A', 'B', 'D', 'E'};
    const unsigned char equal_symbols_a[] = {'Z', 'A', 'M'};
    const unsigned char equal_symbols_b[] = {'M', 'Z', 'A'};
    const uint64_t equal_weights[] = {1U, 1U, 1U};
    const unsigned char duplicate_symbols[] = {'A', 'A'};
    const uint64_t duplicate_weights[] = {1U, 2U};
    const unsigned char overflow_symbols[] = {'X', 'Y'};
    const uint64_t overflow_weights[] = {UINT64_MAX, 1U};
    HuffmanTree tree = {NULL, 0U};
    HuffmanTree equal_a = {NULL, 0U};
    HuffmanTree equal_b = {NULL, 0U};
    HuffmanTree single = {NULL, 0U};
    HuffmanTree invalid = {NULL, 0U};
    HuffmanCodeTable table = {NULL, 0U};
    HuffmanCodeTable equal_table_a = {NULL, 0U};
    HuffmanCodeTable equal_table_b = {NULL, 0U};
    HuffmanCodeTable single_table = {NULL, 0U};
    unsigned char decoded[16];
    size_t decoded_length = 0U;
    uint64_t wpl = 0U;
    char bits[64];

    assert(huffman_tree_build(symbols, weights, 6U, &tree));
    assert(huffman_make_code_table(&tree, &table));
    assert(strcmp(huffman_code_find(&table, 'F'), "0") == 0);
    assert(strcmp(huffman_code_find(&table, 'C'), "100") == 0);
    assert(strcmp(huffman_code_find(&table, 'A'), "1010") == 0);
    assert(strcmp(huffman_code_find(&table, 'B'), "1011") == 0);
    assert(strcmp(huffman_code_find(&table, 'D'), "110") == 0);
    assert(strcmp(huffman_code_find(&table, 'E'), "111") == 0);
    assert(huffman_code_find(&table, 'X') == NULL);
    assert(huffman_wpl(&tree, &wpl) && wpl == 228U);
    assert(huffman_encode(&table, message, sizeof(message), bits, sizeof(bits)));
    assert(strcmp(bits, "010010101011110111") == 0);
    assert(huffman_decode(&tree, bits, decoded, sizeof(decoded), &decoded_length));
    assert(decoded_length == sizeof(message));
    assert(memcmp(decoded, message, sizeof(message)) == 0);
    assert(!huffman_decode(&tree, "10", decoded, sizeof(decoded), &decoded_length));
    assert(!huffman_decode(&tree, "2", decoded, sizeof(decoded), &decoded_length));
    assert(!huffman_encode(&table, (const unsigned char *)"X", 1U,
                           bits, sizeof(bits)));
    assert(!huffman_encode(&table, message, sizeof(message), bits, 4U));

    assert(huffman_tree_build(equal_symbols_a, equal_weights, 3U, &equal_a));
    assert(huffman_tree_build(equal_symbols_b, equal_weights, 3U, &equal_b));
    assert(huffman_make_code_table(&equal_a, &equal_table_a));
    assert(huffman_make_code_table(&equal_b, &equal_table_b));
    assert(strcmp(huffman_code_find(&equal_table_a, 'Z'),
                  huffman_code_find(&equal_table_b, 'Z')) == 0);
    assert(strcmp(huffman_code_find(&equal_table_a, 'A'),
                  huffman_code_find(&equal_table_b, 'A')) == 0);
    assert(strcmp(huffman_code_find(&equal_table_a, 'M'),
                  huffman_code_find(&equal_table_b, 'M')) == 0);

    assert(huffman_tree_build((const unsigned char *)"Q",
                              (const uint64_t[]){9U}, 1U, &single));
    assert(huffman_make_code_table(&single, &single_table));
    assert(strcmp(huffman_code_find(&single_table, 'Q'), "0") == 0);
    assert(huffman_encode(&single_table, (const unsigned char *)"QQQ", 3U,
                          bits, sizeof(bits)));
    assert(strcmp(bits, "000") == 0);
    assert(huffman_decode(&single, bits, decoded, sizeof(decoded), &decoded_length));
    assert(decoded_length == 3U && decoded[0] == 'Q' && decoded[2] == 'Q');
    assert(huffman_wpl(&single, &wpl) && wpl == 0U);

    assert(!huffman_tree_build(duplicate_symbols, duplicate_weights, 2U, &invalid));
    assert(!huffman_tree_build(overflow_symbols, overflow_weights, 2U, &invalid));
    assert(!huffman_tree_build(symbols, weights, 0U, &invalid));

    huffman_code_table_destroy(&single_table);
    huffman_code_table_destroy(&equal_table_b);
    huffman_code_table_destroy(&equal_table_a);
    huffman_code_table_destroy(&table);
    huffman_tree_destroy(&invalid);
    huffman_tree_destroy(&single);
    huffman_tree_destroy(&equal_b);
    huffman_tree_destroy(&equal_a);
    huffman_tree_destroy(&tree);
}

static void test_disjoint_set(void) {
    DisjointSet dsu = {NULL, NULL, 0U, 0U};
    bool merged = false;
    bool connected = false;
    size_t root = 0U;

    assert(dsu_create(&dsu, 7U));
    assert(dsu.components == 7U);
    assert(dsu_union(&dsu, 0U, 1U, &merged) && merged);
    assert(dsu_union(&dsu, 2U, 3U, &merged) && merged);
    assert(dsu_union(&dsu, 0U, 2U, &merged) && merged);
    assert(dsu_union(&dsu, 4U, 5U, &merged) && merged);
    assert(dsu_union(&dsu, 0U, 4U, &merged) && merged);
    assert(dsu.components == 2U);
    assert(dsu_union(&dsu, 3U, 5U, &merged) && !merged);
    assert(dsu_connected(&dsu, 1U, 5U, &connected) && connected);
    assert(dsu_connected(&dsu, 1U, 6U, &connected) && !connected);
    assert(dsu_find(&dsu, 3U, &root) && root == 0U);
    assert(dsu.parent[3U] == 0U);
    assert(!dsu_find(&dsu, 7U, &root));
    assert(!dsu_union(&dsu, 0U, 7U, &merged));

    assert(dsu_create(&dsu, 0U));
    assert(dsu.count == 0U && dsu.components == 0U);
    assert(!dsu_find(&dsu, 0U, &root));
    dsu_destroy(&dsu);
}

static void test_backtracking(void) {
    const int64_t values[] = {7, 3, 2, 5, 8};
    const int64_t overflow_values[] = {INT64_MAX, 1};
    SubsetSumResult result = {NULL, 0U, 0U, 0U, false};
    SubsetSumResult empty = {NULL, 0U, 0U, 0U, false};
    SubsetSumResult invalid = {NULL, 0U, 0U, 0U, false};

    assert(subset_sum_solve(values, 5U, 10, &result));
    assert(result.found && result.solution_count == 3U);
    assert(result.visited_states == 63U);
    assert(result.first_choice[0] && result.first_choice[1]);
    assert(!result.first_choice[2] && !result.first_choice[3] &&
           !result.first_choice[4]);

    assert(subset_sum_solve(values, 5U, 100, &result));
    assert(!result.found && result.solution_count == 0U);
    assert(subset_sum_solve(NULL, 0U, 0, &empty));
    assert(empty.found && empty.solution_count == 1U &&
           empty.visited_states == 1U);
    assert(!subset_sum_solve(NULL, 1U, 0, &invalid));
    assert(!subset_sum_solve(values, 25U, 0, &invalid));
    assert(!subset_sum_solve(overflow_values, 2U, 0, &invalid));

    subset_sum_result_destroy(&invalid);
    subset_sum_result_destroy(&empty);
    subset_sum_result_destroy(&result);
}

static void test_tree_counting(void) {
    uint64_t count = 0U;
    assert(catalan_count(0U, &count) && count == 1U);
    assert(catalan_count(1U, &count) && count == 1U);
    assert(catalan_count(3U, &count) && count == 5U);
    assert(catalan_count(10U, &count) && count == 16796U);
    assert(catalan_count(36U, &count) && count == UINT64_C(11959798385860453492));
    assert(!catalan_count(37U, &count));
    assert(full_ordered_tree_count(7U, &count) && count == 5U);
    assert(full_ordered_tree_count(1U, &count) && count == 1U);
    assert(full_ordered_tree_count(6U, &count) && count == 0U);
    assert(full_ordered_tree_count(0U, &count) && count == 0U);
    assert(perfect_binary_tree_node_count(0U, &count) && count == 0U);
    assert(perfect_binary_tree_node_count(4U, &count) && count == 15U);
    assert(perfect_binary_tree_node_count(64U, &count) && count == UINT64_MAX);
    assert(!perfect_binary_tree_node_count(65U, &count));
}

int main(void) {
    test_binary_tree();
    test_child_sibling_forest();
    test_threaded_tree();
    test_priority_queue();
    test_huffman();
    test_disjoint_set();
    test_backtracking();
    test_tree_counting();

    puts("All tree, heap, Huffman, DSU, backtracking and counting tests passed.");
    return EXIT_SUCCESS;
}
