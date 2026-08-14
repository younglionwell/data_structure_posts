/*
 * 第 11 章：外部排序
 *
 * C11 单文件示例，覆盖：
 *   - 用真实二进制临时文件生成固定长度初始归并段
 *   - 使用选择树完成 k 路多趟归并
 *   - 按逻辑块统计读写量
 *   - 置换选择生成可变长初始归并段
 *   - k 路最佳归并树的代价与虚段数量
 *
 * Record.serial 是记录进入文件时的原始次序。所有比较都先看 key，
 * key 相等时再看 serial，因此示例得到稳定且确定的输出。
 *
 * 代码通过 tmpfile() 模拟外存文件。IoStats 统计的是给定块大小下的
 * 逻辑块传输数，不等于操作系统实际发出的 read/write 系统调用次数。
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int32_t key;
    uint32_t serial;
} Record;

typedef struct {
    FILE *file;
    size_t count;
} Run;

typedef struct {
    Run *data;
    size_t len;
    size_t cap;
} RunVec;

typedef struct {
    size_t block_records;
    uint64_t read_blocks;
    uint64_t write_blocks;
} IoStats;

static int record_compare_value(Record a, Record b)
{
    if (a.key < b.key) {
        return -1;
    }
    if (a.key > b.key) {
        return 1;
    }
    if (a.serial < b.serial) {
        return -1;
    }
    if (a.serial > b.serial) {
        return 1;
    }
    return 0;
}

static int record_compare_qsort(const void *lhs, const void *rhs)
{
    const Record *a = (const Record *)lhs;
    const Record *b = (const Record *)rhs;
    return record_compare_value(*a, *b);
}

static bool checked_mul_size(size_t a, size_t b, size_t *result)
{
    if (a != 0U && b > SIZE_MAX / a) {
        return false;
    }
    *result = a * b;
    return true;
}

static bool checked_add_u64(uint64_t a, uint64_t b, uint64_t *result)
{
    if (b > UINT64_MAX - a) {
        return false;
    }
    *result = a + b;
    return true;
}

static size_t blocks_for_records(size_t count, size_t block_records)
{
    assert(block_records > 0U);
    return count / block_records + (count % block_records != 0U ? 1U : 0U);
}

static bool stats_add(uint64_t *field, size_t amount)
{
    uint64_t updated = 0U;
    if (!checked_add_u64(*field, (uint64_t)amount, &updated)) {
        return false;
    }
    *field = updated;
    return true;
}

static bool runvec_push(RunVec *vec, Run run)
{
    if (vec->len == vec->cap) {
        size_t next_cap = vec->cap == 0U ? 8U : vec->cap * 2U;
        if (next_cap < vec->cap) {
            return false;
        }
        size_t bytes = 0U;
        if (!checked_mul_size(next_cap, sizeof(vec->data[0]), &bytes)) {
            return false;
        }
        Run *next = (Run *)realloc(vec->data, bytes);
        if (next == NULL) {
            return false;
        }
        vec->data = next;
        vec->cap = next_cap;
    }
    vec->data[vec->len++] = run;
    return true;
}

static void runvec_close(RunVec *vec)
{
    for (size_t i = 0U; i < vec->len; ++i) {
        if (vec->data[i].file != NULL) {
            (void)fclose(vec->data[i].file);
        }
    }
    free(vec->data);
    vec->data = NULL;
    vec->len = 0U;
    vec->cap = 0U;
}

static bool write_records(FILE *file, const Record *records, size_t count)
{
    if (count > 0U && fwrite(records, sizeof(records[0]), count, file) != count) {
        return false;
    }
    if (fflush(file) != 0) {
        return false;
    }
    rewind(file);
    return true;
}

static FILE *records_to_file(const Record *records, size_t count)
{
    FILE *file = tmpfile();
    if (file == NULL) {
        return NULL;
    }
    if (!write_records(file, records, count)) {
        (void)fclose(file);
        return NULL;
    }
    return file;
}

static bool read_exact_records(FILE *file, Record *records, size_t count)
{
    rewind(file);
    return count == 0U ||
           fread(records, sizeof(records[0]), count, file) == count;
}

static bool is_sorted_stable(const Record *records, size_t count)
{
    for (size_t i = 1U; i < count; ++i) {
        if (record_compare_value(records[i - 1U], records[i]) > 0) {
            return false;
        }
    }
    return true;
}

/* ---------- 固定内存生成初始归并段 ---------- */

static bool make_fixed_runs(FILE *input,
                            size_t record_count,
                            size_t memory_records,
                            IoStats *stats,
                            RunVec *runs)
{
    if (memory_records == 0U || stats->block_records == 0U) {
        return false;
    }

    size_t bytes = 0U;
    if (!checked_mul_size(memory_records, sizeof(Record), &bytes)) {
        return false;
    }
    Record *buffer = (Record *)malloc(bytes);
    if (buffer == NULL) {
        return false;
    }

    rewind(input);
    size_t remaining = record_count;
    bool ok = stats_add(&stats->read_blocks,
                        blocks_for_records(record_count, stats->block_records));

    while (ok && remaining > 0U) {
        size_t take = remaining < memory_records ? remaining : memory_records;
        if (fread(buffer, sizeof(buffer[0]), take, input) != take) {
            ok = false;
            break;
        }

        qsort(buffer, take, sizeof(buffer[0]), record_compare_qsort);
        FILE *run_file = tmpfile();
        if (run_file == NULL || !write_records(run_file, buffer, take)) {
            if (run_file != NULL) {
                (void)fclose(run_file);
            }
            ok = false;
            break;
        }

        Run run = {run_file, take};
        if (!runvec_push(runs, run)) {
            (void)fclose(run_file);
            ok = false;
            break;
        }
        ok = stats_add(&stats->write_blocks,
                       blocks_for_records(take, stats->block_records));
        remaining -= take;
    }

    free(buffer);
    if (!ok) {
        runvec_close(runs);
    }
    return ok;
}

/* ---------- k 路选择树 ---------- */

typedef struct {
    FILE *file;
    Record current;
    bool valid;
} Cursor;

typedef struct {
    size_t run_count;
    size_t base;
    size_t *winner;
    size_t *loser;
    Cursor *cursor;
} SelectionTree;

static size_t tree_choose(const SelectionTree *tree, size_t a, size_t b)
{
    if (a == SIZE_MAX) {
        return b;
    }
    if (b == SIZE_MAX) {
        return a;
    }
    if (!tree->cursor[a].valid) {
        return tree->cursor[b].valid ? b : (a < b ? a : b);
    }
    if (!tree->cursor[b].valid) {
        return a;
    }

    int cmp = record_compare_value(tree->cursor[a].current,
                                   tree->cursor[b].current);
    if (cmp != 0) {
        return cmp < 0 ? a : b;
    }
    return a < b ? a : b;
}

static size_t tree_other(size_t a, size_t b, size_t chosen)
{
    if (a == SIZE_MAX) {
        return b;
    }
    if (b == SIZE_MAX) {
        return a;
    }
    return chosen == a ? b : a;
}

static void selection_tree_recompute(SelectionTree *tree, size_t node)
{
    size_t left = tree->winner[node * 2U];
    size_t right = tree->winner[node * 2U + 1U];
    size_t chosen = tree_choose(tree, left, right);
    tree->winner[node] = chosen;
    tree->loser[node] = tree_other(left, right, chosen);
}

static bool selection_tree_init(SelectionTree *tree,
                                Cursor *cursor,
                                size_t run_count)
{
    memset(tree, 0, sizeof(*tree));
    tree->cursor = cursor;
    tree->run_count = run_count;
    tree->base = 1U;
    while (tree->base < run_count) {
        if (tree->base > SIZE_MAX / 2U) {
            return false;
        }
        tree->base *= 2U;
    }

    size_t winner_count = 0U;
    size_t winner_bytes = 0U;
    size_t loser_bytes = 0U;
    if (!checked_mul_size(tree->base, 2U, &winner_count) ||
        !checked_mul_size(winner_count, sizeof(size_t), &winner_bytes) ||
        !checked_mul_size(tree->base, sizeof(size_t), &loser_bytes)) {
        return false;
    }

    tree->winner = (size_t *)malloc(winner_bytes);
    tree->loser = (size_t *)malloc(loser_bytes);
    if (tree->winner == NULL || tree->loser == NULL) {
        free(tree->winner);
        free(tree->loser);
        memset(tree, 0, sizeof(*tree));
        return false;
    }

    for (size_t i = 0U; i < winner_count; ++i) {
        tree->winner[i] = SIZE_MAX;
    }
    for (size_t i = 0U; i < tree->base; ++i) {
        tree->loser[i] = SIZE_MAX;
    }
    for (size_t i = 0U; i < run_count; ++i) {
        tree->winner[tree->base + i] = i;
    }
    for (size_t node = tree->base; node-- > 1U;) {
        selection_tree_recompute(tree, node);
    }
    return true;
}

static void selection_tree_update(SelectionTree *tree, size_t run_index)
{
    assert(run_index < tree->run_count);
    size_t node = (tree->base + run_index) / 2U;
    while (node > 0U) {
        selection_tree_recompute(tree, node);
        node /= 2U;
    }
}

static size_t selection_tree_winner(const SelectionTree *tree)
{
    return tree->winner[1U];
}

static void selection_tree_destroy(SelectionTree *tree)
{
    free(tree->winner);
    free(tree->loser);
    memset(tree, 0, sizeof(*tree));
}

static bool merge_group(const Run *group,
                        size_t group_count,
                        IoStats *stats,
                        Run *output)
{
    assert(group_count >= 2U);
    Cursor *cursor = (Cursor *)calloc(group_count, sizeof(cursor[0]));
    if (cursor == NULL) {
        return false;
    }

    size_t total = 0U;
    bool ok = true;
    for (size_t i = 0U; i < group_count; ++i) {
        if (group[i].count > SIZE_MAX - total) {
            ok = false;
            break;
        }
        total += group[i].count;
        cursor[i].file = group[i].file;
        rewind(cursor[i].file);
        cursor[i].valid = fread(&cursor[i].current,
                                sizeof(cursor[i].current),
                                1U,
                                cursor[i].file) == 1U;
        if (group[i].count > 0U && !cursor[i].valid) {
            ok = false;
            break;
        }
    }

    SelectionTree tree;
    memset(&tree, 0, sizeof(tree));
    if (ok && !selection_tree_init(&tree, cursor, group_count)) {
        ok = false;
    }

    FILE *out_file = ok ? tmpfile() : NULL;
    if (ok && out_file == NULL) {
        ok = false;
    }

    size_t written = 0U;
    while (ok && written < total) {
        size_t winner = selection_tree_winner(&tree);
        if (winner == SIZE_MAX || !cursor[winner].valid) {
            ok = false;
            break;
        }
        if (fwrite(&cursor[winner].current,
                   sizeof(cursor[winner].current),
                   1U,
                   out_file) != 1U) {
            ok = false;
            break;
        }
        ++written;
        cursor[winner].valid = fread(&cursor[winner].current,
                                     sizeof(cursor[winner].current),
                                     1U,
                                     cursor[winner].file) == 1U;
        selection_tree_update(&tree, winner);
    }

    if (ok && fflush(out_file) != 0) {
        ok = false;
    }
    if (ok) {
        rewind(out_file);
        for (size_t i = 0U; i < group_count; ++i) {
            ok = stats_add(&stats->read_blocks,
                           blocks_for_records(group[i].count,
                                              stats->block_records));
            if (!ok) {
                break;
            }
        }
    }
    if (ok) {
        ok = stats_add(&stats->write_blocks,
                       blocks_for_records(total, stats->block_records));
    }

    selection_tree_destroy(&tree);
    free(cursor);
    if (!ok) {
        if (out_file != NULL) {
            (void)fclose(out_file);
        }
        return false;
    }
    output->file = out_file;
    output->count = total;
    return true;
}

static bool merge_pass(RunVec *runs, size_t fan_in, IoStats *stats)
{
    RunVec next = {0};
    bool ok = true;

    for (size_t begin = 0U; ok && begin < runs->len; begin += fan_in) {
        size_t remaining = runs->len - begin;
        size_t group_count = remaining < fan_in ? remaining : fan_in;

        if (group_count == 1U) {
            if (!runvec_push(&next, runs->data[begin])) {
                ok = false;
                break;
            }
            runs->data[begin].file = NULL;
            continue;
        }

        Run merged = {0};
        if (!merge_group(&runs->data[begin], group_count, stats, &merged) ||
            !runvec_push(&next, merged)) {
            if (merged.file != NULL) {
                (void)fclose(merged.file);
            }
            ok = false;
            break;
        }

        for (size_t i = 0U; i < group_count; ++i) {
            (void)fclose(runs->data[begin + i].file);
            runs->data[begin + i].file = NULL;
        }
    }

    if (!ok) {
        runvec_close(&next);
        runvec_close(runs);
        return false;
    }

    runvec_close(runs);
    *runs = next;
    return true;
}

typedef struct {
    FILE *sorted_file;
    size_t initial_runs;
    size_t merge_passes;
    IoStats io;
} ExternalSortResult;

static bool external_sort(FILE *input,
                          size_t record_count,
                          size_t memory_records,
                          size_t fan_in,
                          size_t block_records,
                          ExternalSortResult *result)
{
    memset(result, 0, sizeof(*result));
    if (memory_records == 0U || fan_in < 2U || block_records == 0U) {
        return false;
    }
    result->io.block_records = block_records;

    RunVec runs = {0};
    if (!make_fixed_runs(input,
                         record_count,
                         memory_records,
                         &result->io,
                         &runs)) {
        return false;
    }
    result->initial_runs = runs.len;

    if (runs.len == 0U) {
        result->sorted_file = tmpfile();
        return result->sorted_file != NULL;
    }

    while (runs.len > 1U) {
        if (!merge_pass(&runs, fan_in, &result->io)) {
            return false;
        }
        ++result->merge_passes;
    }

    result->sorted_file = runs.data[0].file;
    runs.data[0].file = NULL;
    runvec_close(&runs);
    return true;
}

static void external_sort_result_close(ExternalSortResult *result)
{
    if (result->sorted_file != NULL) {
        (void)fclose(result->sorted_file);
    }
    memset(result, 0, sizeof(*result));
}

/* ---------- 置换选择 ---------- */

typedef struct {
    Record record;
    size_t generation;
} ReplacementNode;

typedef struct {
    ReplacementNode *data;
    size_t len;
    size_t cap;
} ReplacementHeap;

static bool replacement_less(ReplacementNode a, ReplacementNode b)
{
    if (a.generation != b.generation) {
        return a.generation < b.generation;
    }
    return record_compare_value(a.record, b.record) < 0;
}

static void replacement_swap(ReplacementNode *a, ReplacementNode *b)
{
    ReplacementNode tmp = *a;
    *a = *b;
    *b = tmp;
}

static void replacement_sift_up(ReplacementHeap *heap, size_t index)
{
    while (index > 0U) {
        size_t parent = (index - 1U) / 2U;
        if (!replacement_less(heap->data[index], heap->data[parent])) {
            break;
        }
        replacement_swap(&heap->data[index], &heap->data[parent]);
        index = parent;
    }
}

static void replacement_sift_down(ReplacementHeap *heap, size_t index)
{
    for (;;) {
        size_t left = index * 2U + 1U;
        if (left >= heap->len) {
            return;
        }
        size_t best = left;
        size_t right = left + 1U;
        if (right < heap->len &&
            replacement_less(heap->data[right], heap->data[left])) {
            best = right;
        }
        if (!replacement_less(heap->data[best], heap->data[index])) {
            return;
        }
        replacement_swap(&heap->data[index], &heap->data[best]);
        index = best;
    }
}

static bool replacement_push(ReplacementHeap *heap, ReplacementNode node)
{
    if (heap->len >= heap->cap) {
        return false;
    }
    heap->data[heap->len] = node;
    replacement_sift_up(heap, heap->len);
    ++heap->len;
    return true;
}

static ReplacementNode replacement_pop(ReplacementHeap *heap)
{
    assert(heap->len > 0U);
    ReplacementNode result = heap->data[0];
    --heap->len;
    if (heap->len > 0U) {
        heap->data[0] = heap->data[heap->len];
        replacement_sift_down(heap, 0U);
    }
    return result;
}

static bool finish_replacement_run(RunVec *runs,
                                   FILE **current_file,
                                   size_t current_count,
                                   IoStats *stats)
{
    if (*current_file == NULL) {
        return current_count == 0U;
    }
    if (fflush(*current_file) != 0) {
        return false;
    }
    rewind(*current_file);
    Run run = {*current_file, current_count};
    if (!runvec_push(runs, run)) {
        return false;
    }
    *current_file = NULL;
    return stats_add(&stats->write_blocks,
                     blocks_for_records(current_count, stats->block_records));
}

static bool replacement_selection_runs(FILE *input,
                                       size_t record_count,
                                       size_t heap_capacity,
                                       size_t block_records,
                                       RunVec *runs,
                                       IoStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->block_records = block_records;
    if (heap_capacity == 0U || block_records == 0U) {
        return false;
    }
    if (record_count == 0U) {
        return true;
    }

    size_t bytes = 0U;
    if (!checked_mul_size(heap_capacity,
                          sizeof(ReplacementNode),
                          &bytes)) {
        return false;
    }
    ReplacementHeap heap = {(ReplacementNode *)malloc(bytes), 0U,
                            heap_capacity};
    if (heap.data == NULL) {
        return false;
    }

    rewind(input);
    size_t consumed = 0U;
    bool ok = stats_add(&stats->read_blocks,
                        blocks_for_records(record_count, block_records));
    while (ok && consumed < record_count && heap.len < heap.cap) {
        Record record;
        if (fread(&record, sizeof(record), 1U, input) != 1U ||
            !replacement_push(&heap,
                              (ReplacementNode){record, 0U})) {
            ok = false;
            break;
        }
        ++consumed;
    }

    size_t current_generation = 0U;
    FILE *current_file = ok ? tmpfile() : NULL;
    if (ok && current_file == NULL) {
        ok = false;
    }
    size_t current_count = 0U;
    Record last = {0};
    bool have_last = false;

    while (ok && heap.len > 0U) {
        ReplacementNode node = replacement_pop(&heap);
        if (node.generation != current_generation) {
            if (!finish_replacement_run(runs,
                                        &current_file,
                                        current_count,
                                        stats)) {
                ok = false;
                break;
            }
            current_generation = node.generation;
            current_file = tmpfile();
            current_count = 0U;
            have_last = false;
            if (current_file == NULL) {
                ok = false;
                break;
            }
        }

        if (have_last && record_compare_value(last, node.record) > 0) {
            ok = false;
            break;
        }
        if (fwrite(&node.record, sizeof(node.record), 1U, current_file) != 1U) {
            ok = false;
            break;
        }
        last = node.record;
        have_last = true;
        ++current_count;

        if (consumed < record_count) {
            Record incoming;
            if (fread(&incoming, sizeof(incoming), 1U, input) != 1U) {
                ok = false;
                break;
            }
            ++consumed;
            size_t generation =
                record_compare_value(incoming, last) >= 0
                    ? current_generation
                    : current_generation + 1U;
            if (generation < current_generation ||
                !replacement_push(&heap,
                                  (ReplacementNode){incoming, generation})) {
                ok = false;
                break;
            }
        }
    }

    if (ok) {
        ok = finish_replacement_run(runs,
                                    &current_file,
                                    current_count,
                                    stats);
    }
    if (current_file != NULL) {
        (void)fclose(current_file);
    }
    free(heap.data);
    if (!ok) {
        runvec_close(runs);
    }
    return ok;
}

/* ---------- k 路最佳归并树 ---------- */

typedef struct {
    uint64_t *data;
    size_t len;
    size_t cap;
} U64Heap;

static void u64_sift_up(U64Heap *heap, size_t index)
{
    while (index > 0U) {
        size_t parent = (index - 1U) / 2U;
        if (heap->data[parent] <= heap->data[index]) {
            break;
        }
        uint64_t tmp = heap->data[parent];
        heap->data[parent] = heap->data[index];
        heap->data[index] = tmp;
        index = parent;
    }
}

static void u64_sift_down(U64Heap *heap, size_t index)
{
    for (;;) {
        size_t left = index * 2U + 1U;
        if (left >= heap->len) {
            return;
        }
        size_t best = left;
        size_t right = left + 1U;
        if (right < heap->len && heap->data[right] < heap->data[left]) {
            best = right;
        }
        if (heap->data[index] <= heap->data[best]) {
            return;
        }
        uint64_t tmp = heap->data[index];
        heap->data[index] = heap->data[best];
        heap->data[best] = tmp;
        index = best;
    }
}

static void u64_push(U64Heap *heap, uint64_t value)
{
    assert(heap->len < heap->cap);
    heap->data[heap->len] = value;
    u64_sift_up(heap, heap->len);
    ++heap->len;
}

static uint64_t u64_pop(U64Heap *heap)
{
    assert(heap->len > 0U);
    uint64_t result = heap->data[0];
    --heap->len;
    if (heap->len > 0U) {
        heap->data[0] = heap->data[heap->len];
        u64_sift_down(heap, 0U);
    }
    return result;
}

static bool optimal_kway_merge_cost(const uint64_t *weights,
                                    size_t count,
                                    size_t fan_in,
                                    size_t *dummy_runs,
                                    uint64_t *cost)
{
    *dummy_runs = 0U;
    *cost = 0U;
    if (fan_in < 2U) {
        return false;
    }
    if (count <= 1U) {
        return true;
    }

    size_t remainder = (count - 1U) % (fan_in - 1U);
    *dummy_runs = remainder == 0U ? 0U : (fan_in - 1U) - remainder;
    if (*dummy_runs > SIZE_MAX - count) {
        return false;
    }
    size_t leaf_count = count + *dummy_runs;

    size_t bytes = 0U;
    if (!checked_mul_size(leaf_count, sizeof(uint64_t), &bytes)) {
        return false;
    }
    U64Heap heap = {(uint64_t *)malloc(bytes), 0U, leaf_count};
    if (heap.data == NULL) {
        return false;
    }

    for (size_t i = 0U; i < *dummy_runs; ++i) {
        u64_push(&heap, 0U);
    }
    for (size_t i = 0U; i < count; ++i) {
        u64_push(&heap, weights[i]);
    }

    bool ok = true;
    while (heap.len > 1U) {
        uint64_t merged = 0U;
        for (size_t i = 0U; i < fan_in; ++i) {
            if (heap.len == 0U) {
                ok = false;
                break;
            }
            uint64_t next = u64_pop(&heap);
            if (!checked_add_u64(merged, next, &merged)) {
                ok = false;
                break;
            }
        }
        if (!ok || !checked_add_u64(*cost, merged, cost)) {
            ok = false;
            break;
        }
        u64_push(&heap, merged);
    }

    free(heap.data);
    return ok;
}

/* ---------- 测试辅助 ---------- */

static Record *records_from_keys(const int32_t *keys, size_t count)
{
    if (count == 0U) {
        return NULL;
    }
    size_t bytes = 0U;
    if (!checked_mul_size(count, sizeof(Record), &bytes)) {
        return NULL;
    }
    Record *records = (Record *)malloc(bytes);
    if (records == NULL) {
        return NULL;
    }
    for (size_t i = 0U; i < count; ++i) {
        records[i].key = keys[i];
        records[i].serial = (uint32_t)i;
    }
    return records;
}

static void assert_same_records(const Record *a,
                                const Record *b,
                                size_t count)
{
    for (size_t i = 0U; i < count; ++i) {
        assert(a[i].key == b[i].key);
        assert(a[i].serial == b[i].serial);
    }
}

static void test_external_sort_case(const int32_t *keys,
                                    size_t count,
                                    size_t memory_records,
                                    size_t fan_in,
                                    size_t block_records)
{
    Record *input_records = records_from_keys(keys, count);
    if (count > 0U) {
        assert(input_records != NULL);
    }
    FILE *input = records_to_file(input_records, count);
    assert(input != NULL);

    ExternalSortResult result;
    assert(external_sort(input,
                         count,
                         memory_records,
                         fan_in,
                         block_records,
                         &result));

    Record *actual = NULL;
    Record *expected = NULL;
    if (count > 0U) {
        size_t bytes = 0U;
        assert(checked_mul_size(count, sizeof(Record), &bytes));
        actual = (Record *)malloc(bytes);
        expected = (Record *)malloc(bytes);
        assert(actual != NULL && expected != NULL);
        memcpy(expected, input_records, bytes);
        qsort(expected, count, sizeof(expected[0]), record_compare_qsort);
        assert(read_exact_records(result.sorted_file, actual, count));
        assert(is_sorted_stable(actual, count));
        assert_same_records(actual, expected, count);
    }

    free(actual);
    free(expected);
    free(input_records);
    (void)fclose(input);
    external_sort_result_close(&result);
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void test_selection_tree_example(void)
{
    const int32_t a_keys[] = {3, 14, 27};
    const int32_t b_keys[] = {5, 9, 20};
    const int32_t c_keys[] = {4, 18, 30};
    const int32_t d_keys[] = {7, 11, 25};
    const int32_t *all_keys[] = {a_keys, b_keys, c_keys, d_keys};
    Run group[4] = {{0}};

    for (size_t i = 0U; i < 4U; ++i) {
        Record records[3];
        for (size_t j = 0U; j < 3U; ++j) {
            records[j].key = all_keys[i][j];
            records[j].serial = (uint32_t)(i * 3U + j);
        }
        group[i].file = records_to_file(records, 3U);
        group[i].count = 3U;
        assert(group[i].file != NULL);
    }

    IoStats stats = {2U, 0U, 0U};
    Run merged = {0};
    assert(merge_group(group, 4U, &stats, &merged));

    const int32_t expected_keys[] = {3, 4, 5, 7, 9, 11,
                                     14, 18, 20, 25, 27, 30};
    Record actual[12];
    assert(read_exact_records(merged.file, actual, 12U));
    for (size_t i = 0U; i < 12U; ++i) {
        assert(actual[i].key == expected_keys[i]);
    }

    (void)fclose(merged.file);
    for (size_t i = 0U; i < 4U; ++i) {
        (void)fclose(group[i].file);
    }
}

static void test_replacement_selection(void)
{
    const int32_t keys[] = {51, 49, 39, 46, 38, 29,
                            14, 61, 15, 30, 70, 21};
    const size_t count = sizeof(keys) / sizeof(keys[0]);
    Record *records = records_from_keys(keys, count);
    assert(records != NULL);
    FILE *input = records_to_file(records, count);
    assert(input != NULL);

    RunVec runs = {0};
    IoStats stats;
    assert(replacement_selection_runs(input, count, 4U, 3U, &runs, &stats));
    assert(runs.len == 3U);
    const int32_t expected0[] = {39, 46, 49, 51, 61};
    const int32_t expected1[] = {14, 15, 29, 30, 38, 70};
    const int32_t expected2[] = {21};
    const int32_t *expected[] = {expected0, expected1, expected2};
    const size_t lengths[] = {5U, 6U, 1U};

    for (size_t r = 0U; r < runs.len; ++r) {
        assert(runs.data[r].count == lengths[r]);
        Record actual[6];
        assert(read_exact_records(runs.data[r].file, actual, lengths[r]));
        assert(is_sorted_stable(actual, lengths[r]));
        for (size_t i = 0U; i < lengths[r]; ++i) {
            assert(actual[i].key == expected[r][i]);
        }
    }
    assert(stats.read_blocks == 4U);
    assert(stats.write_blocks == 5U);

    runvec_close(&runs);
    (void)fclose(input);
    free(records);
}

static void test_optimal_merge(void)
{
    const uint64_t weights1[] = {2U, 3U, 6U, 8U, 9U};
    size_t dummy = 99U;
    uint64_t cost = 0U;
    assert(optimal_kway_merge_cost(weights1, 5U, 3U, &dummy, &cost));
    assert(dummy == 0U);
    assert(cost == 39U);

    const uint64_t weights2[] = {2U, 3U, 6U, 8U};
    assert(optimal_kway_merge_cost(weights2, 4U, 3U, &dummy, &cost));
    assert(dummy == 1U);
    assert(cost == 24U);

    const uint64_t huge[] = {UINT64_MAX, 1U};
    assert(!optimal_kway_merge_cost(huge, 2U, 2U, &dummy, &cost));
}

static void test_random_external_sorts(void)
{
    uint32_t state = UINT32_C(0x5eed1234);
    for (size_t round = 0U; round < 120U; ++round) {
        size_t count = (size_t)(next_random(&state) % 180U);
        int32_t keys[180];
        for (size_t i = 0U; i < count; ++i) {
            keys[i] = (int32_t)(next_random(&state) % 41U) - 20;
        }
        size_t memory_records = 1U + next_random(&state) % 17U;
        size_t fan_in = 2U + next_random(&state) % 5U;
        size_t block_records = 1U + next_random(&state) % 9U;
        test_external_sort_case(keys,
                                count,
                                memory_records,
                                fan_in,
                                block_records);
    }
}

int main(void)
{
    const int32_t sample[] = {42, -7, 19, 19, 88, 3, 61, 5,
                              77, 14, 0, 33, 29, 90, 12, 45,
                              18, 18, 71, 2, 64, 27, 8, 53};
    const size_t sample_count = sizeof(sample) / sizeof(sample[0]);

    Record *records = records_from_keys(sample, sample_count);
    assert(records != NULL);
    FILE *input = records_to_file(records, sample_count);
    assert(input != NULL);

    ExternalSortResult result;
    assert(external_sort(input, sample_count, 4U, 3U, 3U, &result));
    assert(result.initial_runs == 6U);
    assert(result.merge_passes == 2U);

    Record sorted[24];
    assert(read_exact_records(result.sorted_file, sorted, sample_count));
    assert(is_sorted_stable(sorted, sample_count));

    printf("固定分段：%zu 个初始段，%zu 趟三路归并\n",
           result.initial_runs,
           result.merge_passes);
    printf("逻辑块传输：读 %llu 块，写 %llu 块\n",
           (unsigned long long)result.io.read_blocks,
           (unsigned long long)result.io.write_blocks);
    printf("排序结果：");
    for (size_t i = 0U; i < sample_count; ++i) {
        printf("%s%d", i == 0U ? "" : " ", sorted[i].key);
    }
    putchar('\n');

    external_sort_result_close(&result);
    (void)fclose(input);
    free(records);

    test_selection_tree_example();
    test_replacement_selection();
    test_optimal_merge();
    test_external_sort_case(NULL, 0U, 4U, 3U, 2U);
    test_random_external_sorts();

    puts("All external-sorting tests passed.");
    return 0;
}
