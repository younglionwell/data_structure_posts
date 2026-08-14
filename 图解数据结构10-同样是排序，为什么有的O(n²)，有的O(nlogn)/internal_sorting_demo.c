/*
 * 第 10 章：内部排序
 *
 * C11 单文件示例，覆盖：
 *   - 直接插入、折半插入、二路插入、希尔排序
 *   - 快速排序
 *   - 简单选择、树状选择、堆排序
 *   - 自底向上归并排序
 *   - 非负十进制整数的链式 LSD 基数排序
 *
 * 记录不只有 key。serial 表示记录进入排序前的相对位置，用来检验稳定性：
 * 两条 key 相等的记录，稳定排序后 serial 仍应递增。
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int key;
    size_t serial;
    char tag;
} Item;

typedef void (*InplaceSort)(Item *, size_t);

static void item_swap(Item *x, Item *y)
{
    Item tmp = *x;
    *x = *y;
    *y = tmp;
}

static bool checked_item_bytes(size_t n, size_t *bytes)
{
    if (n > SIZE_MAX / sizeof(Item)) {
        return false;
    }
    *bytes = n * sizeof(Item);
    return true;
}

static Item *item_copy(const Item *src, size_t n)
{
    size_t bytes = 0U;
    if (!checked_item_bytes(n, &bytes)) {
        return NULL;
    }
    if (n == 0U) {
        return NULL;
    }
    Item *dst = (Item *)malloc(bytes);
    if (dst != NULL) {
        memcpy(dst, src, bytes);
    }
    return dst;
}

static bool is_sorted_by_key(const Item *a, size_t n)
{
    for (size_t i = 1U; i < n; ++i) {
        if (a[i - 1U].key > a[i].key) {
            return false;
        }
    }
    return true;
}

static bool is_stable_by_serial(const Item *a, size_t n)
{
    for (size_t i = 1U; i < n; ++i) {
        if (a[i - 1U].key == a[i].key &&
            a[i - 1U].serial > a[i].serial) {
            return false;
        }
    }
    return true;
}

static bool same_serial_set(const Item *before, const Item *after, size_t n)
{
    if (n == 0U) {
        return true;
    }
    bool *seen = (bool *)calloc(n, sizeof(*seen));
    if (seen == NULL) {
        return false;
    }

    bool ok = true;
    for (size_t i = 0U; i < n; ++i) {
        if (after[i].serial >= n || seen[after[i].serial]) {
            ok = false;
            break;
        }
        seen[after[i].serial] = true;
    }
    for (size_t i = 0U; ok && i < n; ++i) {
        if (before[i].serial >= n || !seen[before[i].serial]) {
            ok = false;
        }
    }

    free(seen);
    return ok;
}

/* ---------- 插入排序 ---------- */

static void insertion_sort(Item *a, size_t n)
{
    for (size_t i = 1U; i < n; ++i) {
        Item pending = a[i];
        size_t j = i;

        /* 只搬走严格大于 pending 的记录，相等记录不会被越过。 */
        while (j > 0U && a[j - 1U].key > pending.key) {
            a[j] = a[j - 1U];
            --j;
        }
        a[j] = pending;
    }
}

static size_t upper_bound_key(const Item *a, size_t end, int key)
{
    size_t low = 0U;
    size_t high = end;

    /* 返回第一个 key 严格大于目标的位置，让新记录落到相等段之后。 */
    while (low < high) {
        size_t mid = low + (high - low) / 2U;
        if (a[mid].key <= key) {
            low = mid + 1U;
        } else {
            high = mid;
        }
    }
    return low;
}

static void binary_insertion_sort(Item *a, size_t n)
{
    for (size_t i = 1U; i < n; ++i) {
        Item pending = a[i];
        size_t pos = upper_bound_key(a, i, pending.key);

        /* 折半只找到了位置；数组中的记录仍要整体右移。 */
        if (pos < i) {
            memmove(&a[pos + 1U], &a[pos], (i - pos) * sizeof(a[0]));
            a[pos] = pending;
        }
    }
}

static size_t ring_index(size_t first, size_t logical, size_t capacity)
{
    assert(capacity > 0U);
    assert(logical < capacity);
    if (logical >= capacity - first) {
        return logical - (capacity - first);
    }
    return first + logical;
}

static bool two_way_insertion_sort(Item *a, size_t n)
{
    if (n < 2U) {
        return true;
    }

    size_t bytes = 0U;
    if (!checked_item_bytes(n, &bytes)) {
        return false;
    }
    Item *ring = (Item *)malloc(bytes);
    if (ring == NULL) {
        return false;
    }

    size_t first = 0U;
    size_t final = 0U;
    size_t size = 1U;
    ring[0] = a[0];

    for (size_t i = 1U; i < n; ++i) {
        Item pending = a[i];

        if (pending.key < ring[first].key) {
            first = (first == 0U) ? n - 1U : first - 1U;
            ring[first] = pending;
        } else if (pending.key >= ring[final].key) {
            final = (final + 1U == n) ? 0U : final + 1U;
            ring[final] = pending;
        } else {
            /* 在逻辑有序区间中找 upper_bound，保证相等记录仍稳定。 */
            size_t low = 0U;
            size_t high = size;
            while (low < high) {
                size_t mid = low + (high - low) / 2U;
                if (ring[ring_index(first, mid, n)].key <= pending.key) {
                    low = mid + 1U;
                } else {
                    high = mid;
                }
            }
            size_t pos = low;

            /* 向更近的一端挪动，减少常数；最坏移动量仍是 O(n^2)。 */
            if (pos < size - pos) {
                size_t new_first = (first == 0U) ? n - 1U : first - 1U;
                for (size_t j = 0U; j < pos; ++j) {
                    ring[ring_index(new_first, j, n)] =
                        ring[ring_index(first, j, n)];
                }
                first = new_first;
                ring[ring_index(first, pos, n)] = pending;
            } else {
                for (size_t j = size; j > pos; --j) {
                    ring[ring_index(first, j, n)] =
                        ring[ring_index(first, j - 1U, n)];
                }
                ring[ring_index(first, pos, n)] = pending;
                final = ring_index(first, size, n);
            }
        }
        ++size;
    }

    for (size_t i = 0U; i < n; ++i) {
        a[i] = ring[ring_index(first, i, n)];
    }
    free(ring);
    return true;
}

static void shell_sort(Item *a, size_t n)
{
    for (size_t gap = n / 2U; gap > 0U; gap /= 2U) {
        for (size_t i = gap; i < n; ++i) {
            Item pending = a[i];
            size_t j = i;
            while (j >= gap && a[j - gap].key > pending.key) {
                a[j] = a[j - gap];
                j -= gap;
            }
            a[j] = pending;
        }
    }
}

/* ---------- 快速排序 ---------- */

static size_t lomuto_partition(Item *a, size_t low, size_t high)
{
    assert(high - low >= 2U);
    Item pivot = a[high - 1U];
    size_t boundary = low;

    for (size_t scan = low; scan < high - 1U; ++scan) {
        if (a[scan].key <= pivot.key) {
            item_swap(&a[boundary], &a[scan]);
            ++boundary;
        }
    }
    item_swap(&a[boundary], &a[high - 1U]);
    return boundary;
}

static void quick_sort_range(Item *a, size_t low, size_t high)
{
    while (high - low > 1U) {
        size_t pivot = lomuto_partition(a, low, high);
        size_t left_size = pivot - low;
        size_t right_size = high - (pivot + 1U);

        /* 递归较小的一侧，较大一侧用循环处理，调用栈最坏 O(log n)。 */
        if (left_size < right_size) {
            quick_sort_range(a, low, pivot);
            low = pivot + 1U;
        } else {
            quick_sort_range(a, pivot + 1U, high);
            high = pivot;
        }
    }
}

static void quick_sort(Item *a, size_t n)
{
    quick_sort_range(a, 0U, n);
}

/* ---------- 选择排序 ---------- */

static void selection_sort(Item *a, size_t n)
{
    for (size_t i = 0U; i < n; ++i) {
        size_t min_pos = i;
        for (size_t j = i + 1U; j < n; ++j) {
            if (a[j].key < a[min_pos].key) {
                min_pos = j;
            }
        }
        if (min_pos != i) {
            item_swap(&a[i], &a[min_pos]);
        }
    }
}

typedef struct {
    const Item *items;
    bool *active;
} TournamentContext;

static size_t tournament_winner(size_t x, size_t y,
                                const TournamentContext *ctx)
{
    if (x == SIZE_MAX) {
        return y;
    }
    if (y == SIZE_MAX) {
        return x;
    }
    if (!ctx->active[x]) {
        return y;
    }
    if (!ctx->active[y]) {
        return x;
    }
    if (ctx->items[x].key != ctx->items[y].key) {
        return (ctx->items[x].key < ctx->items[y].key) ? x : y;
    }
    /* 相等时让原位置小者获胜，输出便保持稳定。 */
    return (ctx->items[x].serial < ctx->items[y].serial) ? x : y;
}

static bool next_power_of_two(size_t n, size_t *result)
{
    size_t value = 1U;
    while (value < n) {
        if (value > SIZE_MAX / 2U) {
            return false;
        }
        value *= 2U;
    }
    *result = value;
    return true;
}

static bool tournament_sort(Item *a, size_t n)
{
    if (n < 2U) {
        return true;
    }

    size_t leaf_count = 0U;
    if (!next_power_of_two(n, &leaf_count) || leaf_count > SIZE_MAX / 2U) {
        return false;
    }
    size_t tree_count = leaf_count * 2U;
    if (tree_count > SIZE_MAX / sizeof(size_t)) {
        return false;
    }

    Item *source = item_copy(a, n);
    Item *output = (Item *)malloc(n * sizeof(*output));
    size_t *tree = (size_t *)malloc(tree_count * sizeof(*tree));
    bool *active = (bool *)malloc(n * sizeof(*active));
    if (source == NULL || output == NULL || tree == NULL || active == NULL) {
        free(source);
        free(output);
        free(tree);
        free(active);
        return false;
    }

    for (size_t i = 0U; i < n; ++i) {
        active[i] = true;
    }
    for (size_t i = 0U; i < leaf_count; ++i) {
        tree[leaf_count + i] = (i < n) ? i : SIZE_MAX;
    }

    TournamentContext ctx = {source, active};
    for (size_t node = leaf_count; node-- > 1U;) {
        tree[node] = tournament_winner(tree[node * 2U],
                                       tree[node * 2U + 1U], &ctx);
    }

    for (size_t out = 0U; out < n; ++out) {
        size_t winner = tree[1U];
        assert(winner != SIZE_MAX);
        output[out] = source[winner];
        active[winner] = false;

        size_t node = leaf_count + winner;
        tree[node] = SIZE_MAX;
        while (node > 1U) {
            node /= 2U;
            tree[node] = tournament_winner(tree[node * 2U],
                                           tree[node * 2U + 1U], &ctx);
        }
    }

    memcpy(a, output, n * sizeof(*a));
    free(source);
    free(output);
    free(tree);
    free(active);
    return true;
}

static void sift_down_max(Item *a, size_t root, size_t end)
{
    for (;;) {
        if (root > (SIZE_MAX - 1U) / 2U) {
            return;
        }
        size_t left = root * 2U + 1U;
        if (left >= end) {
            return;
        }
        size_t larger = left;
        size_t right = left + 1U;
        if (right < end && a[right].key > a[left].key) {
            larger = right;
        }
        if (a[root].key >= a[larger].key) {
            return;
        }
        item_swap(&a[root], &a[larger]);
        root = larger;
    }
}

static void heap_sort(Item *a, size_t n)
{
    for (size_t parent = n / 2U; parent > 0U; --parent) {
        sift_down_max(a, parent - 1U, n);
    }
    for (size_t end = n; end > 1U; --end) {
        item_swap(&a[0], &a[end - 1U]);
        sift_down_max(a, 0U, end - 1U);
    }
}

/* ---------- 归并排序 ---------- */

static size_t min_size(size_t x, size_t y)
{
    return x < y ? x : y;
}

static bool merge_sort(Item *a, size_t n)
{
    if (n < 2U) {
        return true;
    }

    size_t bytes = 0U;
    if (!checked_item_bytes(n, &bytes)) {
        return false;
    }
    Item *tmp = (Item *)malloc(bytes);
    if (tmp == NULL) {
        return false;
    }

    for (size_t width = 1U; width < n;) {
        size_t left = 0U;
        while (left < n) {
            size_t mid = min_size(left + width, n);
            size_t span = (width > SIZE_MAX / 2U) ? SIZE_MAX : width * 2U;
            size_t right = (span > n - left) ? n : left + span;
            size_t i = left;
            size_t j = mid;
            size_t out = left;

            while (i < mid && j < right) {
                /* 相等时先取左段，稳定性就在这个 <= 里。 */
                if (a[i].key <= a[j].key) {
                    tmp[out++] = a[i++];
                } else {
                    tmp[out++] = a[j++];
                }
            }
            while (i < mid) {
                tmp[out++] = a[i++];
            }
            while (j < right) {
                tmp[out++] = a[j++];
            }
            left = right;
        }
        memcpy(a, tmp, bytes);
        if (width > n / 2U) {
            break;
        }
        width *= 2U;
    }

    free(tmp);
    return true;
}

/* ---------- 链式 LSD 基数排序 ---------- */

static bool linked_lsd_radix_sort(Item *a, size_t n)
{
    if (n < 2U) {
        return true;
    }

    int max_key = 0;
    for (size_t i = 0U; i < n; ++i) {
        if (a[i].key < 0) {
            return false;
        }
        if (a[i].key > max_key) {
            max_key = a[i].key;
        }
    }

    if (n > SIZE_MAX / sizeof(size_t)) {
        return false;
    }
    size_t *next = (size_t *)malloc(n * sizeof(*next));
    Item *output = (Item *)malloc(n * sizeof(*output));
    if (next == NULL || output == NULL) {
        free(next);
        free(output);
        return false;
    }

    for (size_t i = 0U; i < n; ++i) {
        next[i] = (i + 1U < n) ? i + 1U : SIZE_MAX;
    }
    size_t order_head = 0U;

    for (int divisor = 1; max_key / divisor > 0;) {
        size_t front[10];
        size_t rear[10];
        for (size_t bucket = 0U; bucket < 10U; ++bucket) {
            front[bucket] = SIZE_MAX;
            rear[bucket] = SIZE_MAX;
        }

        for (size_t node = order_head; node != SIZE_MAX;) {
            size_t saved_next = next[node];
            size_t digit = (size_t)((a[node].key / divisor) % 10);
            next[node] = SIZE_MAX;
            if (front[digit] == SIZE_MAX) {
                front[digit] = node;
            } else {
                next[rear[digit]] = node;
            }
            rear[digit] = node;
            node = saved_next;
        }

        order_head = SIZE_MAX;
        size_t order_rear = SIZE_MAX;
        for (size_t bucket = 0U; bucket < 10U; ++bucket) {
            if (front[bucket] == SIZE_MAX) {
                continue;
            }
            if (order_head == SIZE_MAX) {
                order_head = front[bucket];
            } else {
                next[order_rear] = front[bucket];
            }
            order_rear = rear[bucket];
        }

        if (divisor > max_key / 10) {
            break;
        }
        divisor *= 10;
    }

    size_t out = 0U;
    for (size_t node = order_head; node != SIZE_MAX; node = next[node]) {
        output[out++] = a[node];
    }
    assert(out == n);
    memcpy(a, output, n * sizeof(*a));
    free(next);
    free(output);
    return true;
}

/* ---------- 测试 ---------- */

static int compare_int(const void *lhs, const void *rhs)
{
    int x = *(const int *)lhs;
    int y = *(const int *)rhs;
    return (x > y) - (x < y);
}

static void assert_matches_qsort(const Item *before, const Item *after, size_t n)
{
    int *keys = NULL;
    if (n > 0U) {
        assert(n <= SIZE_MAX / sizeof(*keys));
        keys = (int *)malloc(n * sizeof(*keys));
        assert(keys != NULL);
    }
    for (size_t i = 0U; i < n; ++i) {
        keys[i] = before[i].key;
    }
    qsort(keys, n, sizeof(*keys), compare_int);
    for (size_t i = 0U; i < n; ++i) {
        assert(after[i].key == keys[i]);
    }
    free(keys);
}

static void check_inplace_sort(InplaceSort sort, const Item *input, size_t n,
                               bool require_stable)
{
    Item *work = item_copy(input, n);
    if (n == 0U) {
        sort(NULL, 0U);
        return;
    }
    assert(work != NULL);
    sort(work, n);
    assert(is_sorted_by_key(work, n));
    assert(same_serial_set(input, work, n));
    assert_matches_qsort(input, work, n);
    if (require_stable) {
        assert(is_stable_by_serial(work, n));
    }
    free(work);
}

typedef bool (*FallibleSort)(Item *, size_t);

static void check_fallible_sort(FallibleSort sort, const Item *input, size_t n,
                                bool require_stable)
{
    if (n == 0U) {
        assert(sort(NULL, 0U));
        return;
    }
    Item *work = item_copy(input, n);
    assert(work != NULL);
    assert(sort(work, n));
    assert(is_sorted_by_key(work, n));
    assert(same_serial_set(input, work, n));
    assert_matches_qsort(input, work, n);
    if (require_stable) {
        assert(is_stable_by_serial(work, n));
    }
    free(work);
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void fill_random_items(Item *a, size_t n, uint32_t *state,
                              bool nonnegative)
{
    for (size_t i = 0U; i < n; ++i) {
        uint32_t value = next_random(state);
        int key = (int)(value % 41U);
        a[i].key = nonnegative ? key : key - 20;
        a[i].serial = i;
        a[i].tag = (char)('A' + (int)(i % 26U));
    }
}

static void run_fixed_tests(void)
{
    Item sample[] = {
        {49, 0U, 'A'}, {38, 1U, 'B'}, {65, 2U, 'C'}, {97, 3U, 'D'},
        {76, 4U, 'E'}, {13, 5U, 'F'}, {27, 6U, 'G'}, {49, 7U, 'H'}
    };
    const size_t n = sizeof(sample) / sizeof(sample[0]);

    check_inplace_sort(insertion_sort, sample, n, true);
    check_inplace_sort(binary_insertion_sort, sample, n, true);
    check_fallible_sort(two_way_insertion_sort, sample, n, true);
    check_inplace_sort(shell_sort, sample, n, false);
    check_inplace_sort(quick_sort, sample, n, false);
    check_inplace_sort(selection_sort, sample, n, false);
    check_fallible_sort(tournament_sort, sample, n, true);
    check_inplace_sort(heap_sort, sample, n, false);
    check_fallible_sort(merge_sort, sample, n, true);
    check_fallible_sort(linked_lsd_radix_sort, sample, n, true);

    Item unstable_example[] = {
        {2, 0U, 'A'}, {2, 1U, 'B'}, {1, 2U, 'C'}
    };
    selection_sort(unstable_example, 3U);
    assert(unstable_example[0].key == 1);
    assert(unstable_example[1].tag == 'B');
    assert(unstable_example[2].tag == 'A');

    Item quick_example[] = {
        {4, 0U, 'A'}, {7, 1U, 'B'}, {2, 2U, 'C'},
        {8, 3U, 'D'}, {1, 4U, 'E'}, {5, 5U, 'F'}
    };
    size_t pivot = lomuto_partition(quick_example, 0U, 6U);
    assert(pivot == 3U && quick_example[pivot].key == 5);
    for (size_t i = 0U; i < pivot; ++i) {
        assert(quick_example[i].key <= 5);
    }
    for (size_t i = pivot + 1U; i < 6U; ++i) {
        assert(quick_example[i].key > 5);
    }

    Item radix_example[] = {
        {329, 0U, 'A'}, {457, 1U, 'B'}, {657, 2U, 'C'},
        {839, 3U, 'D'}, {436, 4U, 'E'}, {720, 5U, 'F'},
        {355, 6U, 'G'}
    };
    assert(linked_lsd_radix_sort(radix_example, 7U));
    const int expected[] = {329, 355, 436, 457, 657, 720, 839};
    for (size_t i = 0U; i < 7U; ++i) {
        assert(radix_example[i].key == expected[i]);
    }

    Item negative[] = {{3, 0U, 'A'}, {-1, 1U, 'B'}};
    Item negative_before[2];
    memcpy(negative_before, negative, sizeof(negative));
    assert(!linked_lsd_radix_sort(negative, 2U));
    assert(memcmp(negative, negative_before, sizeof(negative)) == 0);
}

static void run_property_tests(void)
{
    uint32_t state = UINT32_C(0xC0FFEE);

    for (size_t n = 0U; n <= 72U; ++n) {
        for (size_t round = 0U; round < 120U; ++round) {
            Item input[72];
            fill_random_items(input, n, &state, false);

            check_inplace_sort(insertion_sort, input, n, true);
            check_inplace_sort(binary_insertion_sort, input, n, true);
            check_fallible_sort(two_way_insertion_sort, input, n, true);
            check_inplace_sort(shell_sort, input, n, false);
            check_inplace_sort(quick_sort, input, n, false);
            check_inplace_sort(selection_sort, input, n, false);
            check_fallible_sort(tournament_sort, input, n, true);
            check_inplace_sort(heap_sort, input, n, false);
            check_fallible_sort(merge_sort, input, n, true);

            fill_random_items(input, n, &state, true);
            check_fallible_sort(linked_lsd_radix_sort, input, n, true);
        }
    }
}

static void print_items(const Item *a, size_t n)
{
    for (size_t i = 0U; i < n; ++i) {
        printf("%d%c%s", a[i].key, a[i].tag,
               (i + 1U == n) ? "\n" : " ");
    }
}

int main(void)
{
    run_fixed_tests();
    run_property_tests();

    Item demo[] = {
        {49, 0U, 'A'}, {38, 1U, 'B'}, {65, 2U, 'C'}, {97, 3U, 'D'},
        {76, 4U, 'E'}, {13, 5U, 'F'}, {27, 6U, 'G'}, {49, 7U, 'H'}
    };
    const size_t n = sizeof(demo) / sizeof(demo[0]);
    assert(merge_sort(demo, n));

    puts("稳定归并后的完整记录：");
    print_items(demo, n);
    puts("All internal-sorting tests passed.");
    return 0;
}
