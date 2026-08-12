#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 第 4 章“串”配套示例（C11）。
 *
 * 语义与内存所有权：
 *   1. HeapString 拥有 data 指向的堆内存；init/from_cstr 后必须 destroy。
 *   2. length 是有效内容的字节数，capacity 是不含末尾 '\0' 的可用字节
 *      容量。data[length] 始终为 '\0'，因此实际分配 capacity + 1 字节。
 *   3. 所有位置和匹配结果都是 0 基字节偏移。SIZE_MAX 表示“未找到”。
 *   4. 本文件按字节处理串。UTF-8 可以原样存放、复制和按字节比较，但
 *      substring 或模式本身若从多字节编码中间开始，结果也会落在字符内部；
 *      这里不伪装成 Unicode 字符语义，也不验证 UTF-8 合法性。
 *   5. assign/concat/substring 允许输出对象与输入对象相同；实现先构造临时
 *      结果，成功后再替换输出，因此申请失败时原对象保持不变。
 */

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} HeapString;

static bool checked_add_size(size_t left, size_t right, size_t *sum)
{
    if (sum == NULL || left > SIZE_MAX - right) {
        return false;
    }
    *sum = left + right;
    return true;
}

/* 为 capacity 个内容字节再多申请一个 '\0'，并检查 capacity + 1 溢出。 */
static bool heap_string_allocate(HeapString *string, size_t capacity)
{
    if (string == NULL || capacity == SIZE_MAX) {
        return false;
    }

    char *data = malloc(capacity + 1);
    if (data == NULL) {
        return false;
    }
    data[0] = '\0';
    *string = (HeapString){data, 0, capacity};
    return true;
}

/*
 * init 建立一个有效空串。即使 initial_capacity 为 0，也申请一个字节保存
 * 结尾 '\0'；这样已初始化对象的 data 永不为 NULL。
 */
static bool heap_string_init(HeapString *string, size_t initial_capacity)
{
    if (string == NULL) {
        return false;
    }
    *string = (HeapString){NULL, 0, 0};
    return heap_string_allocate(string, initial_capacity);
}

static void heap_string_destroy(HeapString *string)
{
    if (string == NULL) {
        return;
    }
    free(string->data);
    *string = (HeapString){NULL, 0, 0};
}

static void heap_string_clear(HeapString *string)
{
    if (string == NULL || string->data == NULL) {
        return;
    }
    string->length = 0;
    string->data[0] = '\0';
}

static bool heap_string_is_valid(const HeapString *string)
{
    return string != NULL && string->data != NULL &&
           string->length <= string->capacity &&
           string->data[string->length] == '\0';
}

/* source 可以含嵌入的 '\0'；length 明确给出要复制的字节数。 */
static bool heap_string_from_bytes(HeapString *out, const char *source,
                                   size_t length)
{
    if (out == NULL || (source == NULL && length != 0)) {
        return false;
    }

    HeapString replacement;
    if (!heap_string_allocate(&replacement, length)) {
        return false;
    }
    if (length != 0) {
        memcpy(replacement.data, source, length);
    }
    replacement.length = length;
    replacement.data[length] = '\0';
    *out = replacement;
    return true;
}

static bool heap_string_from_cstr(HeapString *out, const char *source)
{
    if (out == NULL || source == NULL) {
        return false;
    }
    return heap_string_from_bytes(out, source, strlen(source));
}

static bool heap_string_reserve(HeapString *string, size_t min_capacity)
{
    if (!heap_string_is_valid(string)) {
        return false;
    }
    if (min_capacity <= string->capacity) {
        return true;
    }
    if (min_capacity == SIZE_MAX) {
        return false;
    }

    char *new_data = realloc(string->data, min_capacity + 1);
    if (new_data == NULL) {
        return false;
    }
    string->data = new_data;
    string->capacity = min_capacity;
    string->data[string->length] = '\0';
    return true;
}

/* 成功后 out 的 capacity 恰好等于 source.length。允许 out == source。 */
static bool heap_string_assign(HeapString *out, const HeapString *source)
{
    if (!heap_string_is_valid(out) || !heap_string_is_valid(source)) {
        return false;
    }
    if (out == source) {
        return true;
    }

    HeapString replacement;
    if (!heap_string_from_bytes(&replacement, source->data,
                                source->length)) {
        return false;
    }
    heap_string_destroy(out);
    *out = replacement;
    return true;
}

/*
 * out = left + right。允许 out 与 left/right 相同，也允许 left == right。
 * 先检查 length 之和，再检查额外的末尾 '\0' 是否可分配。
 */
static bool heap_string_concat(HeapString *out, const HeapString *left,
                               const HeapString *right)
{
    if (!heap_string_is_valid(out) || !heap_string_is_valid(left) ||
        !heap_string_is_valid(right)) {
        return false;
    }

    size_t combined_length;
    if (!checked_add_size(left->length, right->length, &combined_length) ||
        combined_length == SIZE_MAX) {
        return false;
    }

    HeapString replacement;
    if (!heap_string_allocate(&replacement, combined_length)) {
        return false;
    }
    if (left->length != 0) {
        memcpy(replacement.data, left->data, left->length);
    }
    if (right->length != 0) {
        memcpy(replacement.data + left->length, right->data, right->length);
    }
    replacement.length = combined_length;
    replacement.data[combined_length] = '\0';

    heap_string_destroy(out);
    *out = replacement;
    return true;
}

/*
 * out = source[position, position + length)。position 是 0 基字节偏移；
 * position == source.length 仅在 length == 0 时合法。
 */
static bool heap_string_substring(HeapString *out,
                                  const HeapString *source,
                                  size_t position, size_t length)
{
    if (!heap_string_is_valid(out) || !heap_string_is_valid(source) ||
        position > source->length || length > source->length - position) {
        return false;
    }

    HeapString replacement;
    if (!heap_string_from_bytes(&replacement, source->data + position,
                                length)) {
        return false;
    }
    heap_string_destroy(out);
    *out = replacement;
    return true;
}

/* 按 unsigned byte 做字典序比较，结果语义与 strcmp 的负/零/正一致。 */
static int heap_string_compare(const HeapString *left,
                               const HeapString *right)
{
    assert(heap_string_is_valid(left));
    assert(heap_string_is_valid(right));

    const size_t common_length =
        left->length < right->length ? left->length : right->length;
    for (size_t i = 0; i < common_length; ++i) {
        const unsigned char a = (unsigned char)left->data[i];
        const unsigned char b = (unsigned char)right->data[i];
        if (a != b) {
            return a < b ? -1 : 1;
        }
    }
    if (left->length == right->length) {
        return 0;
    }
    return left->length < right->length ? -1 : 1;
}

/* ============================= 模式匹配 =============================== */

/*
 * 空模式按通常约定在 start 处匹配。start 可等于 text.length；若 start
 * 大于 text.length，则返回 SIZE_MAX。所有结果均为字节偏移。
 */
static size_t naive_find_from(const HeapString *text,
                              const HeapString *pattern, size_t start)
{
    if (!heap_string_is_valid(text) || !heap_string_is_valid(pattern) ||
        start > text->length) {
        return SIZE_MAX;
    }
    if (pattern->length == 0) {
        return start;
    }
    if (pattern->length > text->length - start) {
        return SIZE_MAX;
    }

    const size_t last_start = text->length - pattern->length;
    for (size_t i = start; i <= last_start; ++i) {
        size_t j = 0;
        while (j < pattern->length && text->data[i + j] == pattern->data[j]) {
            ++j;
        }
        if (j == pattern->length) {
            return i;
        }
    }
    return SIZE_MAX;
}

/*
 * prefix[i] 是 pattern[0..i] 的最长“真前缀且为后缀”的长度。数组由调用者
 * 提供，必须至少有 pattern.length 个 size_t；空模式允许 prefix == NULL。
 */
static bool kmp_build_prefix(const HeapString *pattern, size_t prefix[])
{
    if (!heap_string_is_valid(pattern) ||
        (pattern->length != 0 && prefix == NULL)) {
        return false;
    }
    if (pattern->length == 0) {
        return true;
    }

    prefix[0] = 0;
    for (size_t i = 1; i < pattern->length; ++i) {
        size_t matched = prefix[i - 1];
        while (matched > 0 && pattern->data[i] != pattern->data[matched]) {
            matched = prefix[matched - 1];
        }
        if (pattern->data[i] == pattern->data[matched]) {
            ++matched;
        }
        prefix[i] = matched;
    }
    return true;
}

static size_t *kmp_alloc_prefix(const HeapString *pattern);

/*
 * 把 0 基 prefix 转成常见的失配位置表：next[0] = -1；在 pattern[j]
 * 失配后令 j = next[j]。nextval 继续跳过“回退后仍比较同一模式字符”的
 * 已知无效状态。两张表都依赖这套下标与哨兵约定，不能脱离定义单独使用。
 */
static bool kmp_build_next_tables(const HeapString *pattern,
                                  ptrdiff_t next[], ptrdiff_t nextval[])
{
    if (!heap_string_is_valid(pattern) ||
        (pattern->length != 0 && (next == NULL || nextval == NULL)) ||
        pattern->length > (size_t)PTRDIFF_MAX) {
        return false;
    }
    if (pattern->length == 0) {
        return true;
    }

    size_t *prefix = kmp_alloc_prefix(pattern);
    if (prefix == NULL) {
        return false;
    }

    next[0] = -1;
    nextval[0] = -1;
    for (size_t j = 1; j < pattern->length; ++j) {
        next[j] = (ptrdiff_t)prefix[j - 1];
        const size_t fallback = (size_t)next[j];
        nextval[j] = pattern->data[j] == pattern->data[fallback]
                         ? nextval[fallback]
                         : next[j];
    }
    free(prefix);
    return true;
}

static size_t *kmp_alloc_prefix(const HeapString *pattern)
{
    if (!heap_string_is_valid(pattern)) {
        return NULL;
    }
    if (pattern->length == 0) {
        return NULL;
    }
    if (pattern->length > SIZE_MAX / sizeof(size_t)) {
        return NULL;
    }

    size_t *prefix = malloc(pattern->length * sizeof(prefix[0]));
    if (prefix == NULL) {
        return NULL;
    }
    if (!kmp_build_prefix(pattern, prefix)) {
        free(prefix);
        return NULL;
    }
    return prefix;
}

/*
 * out_of_memory 在进入函数时可以为 NULL；非 NULL 时总会被写入。返回
 * SIZE_MAX 既可能表示未找到，也可能表示内存不足，后者由 out_of_memory
 * 区分。空模式不申请 prefix 数组并直接在 start 处匹配。
 */
static size_t kmp_find_from(const HeapString *text,
                            const HeapString *pattern, size_t start,
                            bool *out_of_memory)
{
    if (out_of_memory != NULL) {
        *out_of_memory = false;
    }
    if (!heap_string_is_valid(text) || !heap_string_is_valid(pattern) ||
        start > text->length) {
        return SIZE_MAX;
    }
    if (pattern->length == 0) {
        return start;
    }
    if (pattern->length > text->length - start) {
        return SIZE_MAX;
    }

    size_t *prefix = kmp_alloc_prefix(pattern);
    if (prefix == NULL) {
        if (out_of_memory != NULL) {
            *out_of_memory = true;
        }
        return SIZE_MAX;
    }

    size_t matched = 0;
    for (size_t i = start; i < text->length; ++i) {
        while (matched > 0 && text->data[i] != pattern->data[matched]) {
            matched = prefix[matched - 1];
        }
        if (text->data[i] == pattern->data[matched]) {
            ++matched;
        }
        if (matched == pattern->length) {
            const size_t found = i + 1 - pattern->length;
            free(prefix);
            return found;
        }
    }

    free(prefix);
    return SIZE_MAX;
}

/* ================================ 测试 ================================ */

static bool heap_string_equals_bytes(const HeapString *string,
                                     const char *expected, size_t length)
{
    return heap_string_is_valid(string) && string->length == length &&
           (length == 0 || memcmp(string->data, expected, length) == 0);
}

static void test_heap_string_lifecycle(void)
{
    HeapString string;
    assert(heap_string_init(&string, 0));
    assert(heap_string_is_valid(&string));
    assert(string.length == 0 && string.capacity == 0);
    assert(strcmp(string.data, "") == 0);

    assert(heap_string_reserve(&string, 16));
    assert(string.capacity >= 16 && string.length == 0);
    const char *data_address_after_growth = string.data;
    assert(heap_string_reserve(&string, 2)); /* reserve 不缩容。 */
    assert(string.data == data_address_after_growth);
    assert(string.capacity >= 16);

    HeapString source;
    assert(heap_string_from_cstr(&source, "hello"));
    assert(heap_string_assign(&string, &source));
    assert(heap_string_equals_bytes(&string, "hello", 5));
    assert(heap_string_assign(&string, &string));
    assert(heap_string_equals_bytes(&string, "hello", 5));

    heap_string_clear(&string);
    assert(heap_string_is_valid(&string));
    assert(string.length == 0 && string.data[0] == '\0');
    /* clear 保留已分配容量，便于复用。 */
    assert(string.capacity == 5);

    heap_string_destroy(&source);
    heap_string_destroy(&string);
    assert(string.data == NULL && string.length == 0 && string.capacity == 0);

    HeapString embedded;
    const char bytes[] = {'a', '\0', 'b'};
    assert(heap_string_from_bytes(&embedded, bytes, sizeof(bytes)));
    assert(heap_string_equals_bytes(&embedded, bytes, sizeof(bytes)));
    assert(embedded.data[embedded.length] == '\0');
    heap_string_destroy(&embedded);

    HeapString invalid_target;
    assert(!heap_string_init(&invalid_target, SIZE_MAX));
    assert(invalid_target.data == NULL);
}

static void test_concat_substring_compare(void)
{
    HeapString left;
    HeapString right;
    HeapString out;
    assert(heap_string_from_cstr(&left, "data"));
    assert(heap_string_from_cstr(&right, "structure"));
    assert(heap_string_init(&out, 0));

    assert(heap_string_concat(&out, &left, &right));
    assert(heap_string_equals_bytes(&out, "datastructure", 13));

    /* 输出与左输入相同：应先完成临时结果，再释放原内存。 */
    assert(heap_string_concat(&left, &left, &right));
    assert(heap_string_equals_bytes(&left, "datastructure", 13));
    /* 两个输入也是同一个对象。 */
    assert(heap_string_concat(&right, &right, &right));
    assert(heap_string_equals_bytes(&right, "structurestructure", 18));

    assert(heap_string_substring(&out, &left, 4, 9));
    assert(heap_string_equals_bytes(&out, "structure", 9));
    assert(heap_string_substring(&out, &out, out.length, 0));
    assert(out.length == 0);
    assert(!heap_string_substring(&out, &left, left.length + 1, 0));
    assert(!heap_string_substring(&out, &left, 5, 99));

    HeapString a;
    HeapString ab;
    HeapString b;
    assert(heap_string_from_cstr(&a, "a"));
    assert(heap_string_from_cstr(&ab, "ab"));
    assert(heap_string_from_cstr(&b, "b"));
    assert(heap_string_compare(&a, &ab) < 0);
    assert(heap_string_compare(&ab, &a) > 0);
    assert(heap_string_compare(&a, &a) == 0);
    assert(heap_string_compare(&ab, &b) < 0);

    heap_string_destroy(&left);
    heap_string_destroy(&right);
    heap_string_destroy(&out);
    heap_string_destroy(&a);
    heap_string_destroy(&ab);
    heap_string_destroy(&b);
}

static void test_prefix_function(void)
{
    HeapString pattern;
    assert(heap_string_from_cstr(&pattern, "ababaca"));
    size_t prefix[7];
    const size_t expected[] = {0, 0, 1, 2, 3, 0, 1};
    assert(kmp_build_prefix(&pattern, prefix));
    for (size_t i = 0; i < 7; ++i) {
        assert(prefix[i] == expected[i]);
    }
    heap_string_destroy(&pattern);

    assert(heap_string_from_cstr(&pattern, "aaaaa"));
    const size_t repeated_expected[] = {0, 1, 2, 3, 4};
    size_t repeated_prefix[5];
    assert(kmp_build_prefix(&pattern, repeated_prefix));
    for (size_t i = 0; i < 5; ++i) {
        assert(repeated_prefix[i] == repeated_expected[i]);
    }
    heap_string_destroy(&pattern);

    HeapString empty;
    assert(heap_string_from_cstr(&empty, ""));
    assert(kmp_build_prefix(&empty, NULL));
    heap_string_destroy(&empty);
}

static void test_next_tables(void)
{
    HeapString pattern;
    assert(heap_string_from_cstr(&pattern, "ababac"));
    ptrdiff_t next[6];
    ptrdiff_t nextval[6];
    const ptrdiff_t expected_next[] = {-1, 0, 0, 1, 2, 3};
    const ptrdiff_t expected_nextval[] = {-1, 0, -1, 0, -1, 3};
    assert(kmp_build_next_tables(&pattern, next, nextval));
    for (size_t i = 0; i < pattern.length; ++i) {
        assert(next[i] == expected_next[i]);
        assert(nextval[i] == expected_nextval[i]);
    }
    heap_string_destroy(&pattern);

    assert(heap_string_from_cstr(&pattern, "aaaab"));
    const ptrdiff_t repeated_next[] = {-1, 0, 1, 2, 3};
    const ptrdiff_t repeated_nextval[] = {-1, -1, -1, -1, 3};
    assert(kmp_build_next_tables(&pattern, next, nextval));
    for (size_t i = 0; i < pattern.length; ++i) {
        assert(next[i] == repeated_next[i]);
        assert(nextval[i] == repeated_nextval[i]);
    }
    heap_string_destroy(&pattern);

    HeapString empty;
    assert(heap_string_from_cstr(&empty, ""));
    assert(kmp_build_next_tables(&empty, NULL, NULL));
    heap_string_destroy(&empty);
}

static void assert_both_match(const HeapString *text,
                              const HeapString *pattern, size_t start,
                              size_t expected)
{
    const size_t naive = naive_find_from(text, pattern, start);
    if (naive != expected) {
        fprintf(stderr,
                "match test failed: text='%s' pattern='%s' start=%zu "
                "expected=%zu actual=%zu\n",
                text->data, pattern->data, start, expected, naive);
    }
    assert(naive == expected);
    bool out_of_memory = true;
    assert(kmp_find_from(text, pattern, start, &out_of_memory) == expected);
    assert(!out_of_memory);
}

static void test_pattern_matching(void)
{
    HeapString text;
    HeapString pattern;
    assert(heap_string_from_cstr(&text, "ababcabcacbab"));
    assert(heap_string_from_cstr(&pattern, "abcac"));
    assert_both_match(&text, &pattern, 0, 5);
    assert_both_match(&text, &pattern, 6, SIZE_MAX);

    heap_string_destroy(&pattern);
    assert(heap_string_from_cstr(&pattern, "cab"));
    assert_both_match(&text, &pattern, 0, 4);
    assert_both_match(&text, &pattern, 5, SIZE_MAX);

    heap_string_destroy(&text);
    heap_string_destroy(&pattern);
    assert(heap_string_from_cstr(&text, "aaaaaaaaab"));
    assert(heap_string_from_cstr(&pattern, "aaaab"));
    assert_both_match(&text, &pattern, 0, 5);

    heap_string_destroy(&pattern);
    assert(heap_string_from_cstr(&pattern, ""));
    assert_both_match(&text, &pattern, 0, 0);
    assert_both_match(&text, &pattern, text.length, text.length);
    assert_both_match(&text, &pattern, text.length + 1, SIZE_MAX);

    heap_string_destroy(&text);
    heap_string_destroy(&pattern);
    assert(heap_string_from_cstr(&text, "short"));
    assert(heap_string_from_cstr(&pattern, "longer"));
    assert_both_match(&text, &pattern, 0, SIZE_MAX);

    heap_string_destroy(&text);
    heap_string_destroy(&pattern);
}

/* 用穷举小串交叉验证 KMP 与朴素算法在重复字符上的所有边界。 */
static void fill_binary_word(char buffer[], size_t length, size_t bits)
{
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = ((bits >> i) & 1U) == 0 ? 'a' : 'b';
    }
}

static void test_kmp_against_naive_exhaustively(void)
{
    char text_bytes[6];
    char pattern_bytes[5];

    for (size_t text_length = 0; text_length <= sizeof(text_bytes);
         ++text_length) {
        const size_t text_variants = (size_t)1U << text_length;
        for (size_t text_bits = 0; text_bits < text_variants; ++text_bits) {
            fill_binary_word(text_bytes, text_length, text_bits);

            HeapString text;
            assert(heap_string_from_bytes(&text, text_bytes, text_length));
            for (size_t pattern_length = 0;
                 pattern_length <= sizeof(pattern_bytes); ++pattern_length) {
                const size_t pattern_variants =
                    (size_t)1U << pattern_length;
                for (size_t pattern_bits = 0;
                     pattern_bits < pattern_variants; ++pattern_bits) {
                    fill_binary_word(pattern_bytes, pattern_length,
                                     pattern_bits);
                    HeapString pattern;
                    assert(heap_string_from_bytes(&pattern, pattern_bytes,
                                                  pattern_length));
                    for (size_t start = 0; start <= text_length + 1;
                         ++start) {
                        const size_t expected =
                            naive_find_from(&text, &pattern, start);
                        bool out_of_memory = true;
                        const size_t actual = kmp_find_from(
                            &text, &pattern, start, &out_of_memory);
                        assert(!out_of_memory);
                        assert(actual == expected);
                    }
                    heap_string_destroy(&pattern);
                }
            }
            heap_string_destroy(&text);
        }
    }
}

static void test_utf8_byte_semantics(void)
{
    /* “你好世界”在 UTF-8 中每个汉字占 3 字节，本实现不负责解码字符。 */
    HeapString text;
    HeapString pattern;
    HeapString piece;
    assert(heap_string_from_cstr(&text, "你好世界"));
    assert(text.length == 12);
    assert(heap_string_from_cstr(&pattern, "世界"));
    assert(pattern.length == 6);
    assert_both_match(&text, &pattern, 0, 6); /* 字节偏移，不是字符下标 2。 */

    assert(heap_string_init(&piece, 0));
    assert(heap_string_substring(&piece, &text, 3, 6));
    assert(heap_string_equals_bytes(&piece, "好世", 6));

    /* 这是合法的字节切片操作，但结果从 UTF-8 编码单元内部开始。 */
    assert(heap_string_substring(&piece, &text, 1, 2));
    assert(piece.length == 2);
    assert((unsigned char)piece.data[0] == 0xBD);
    assert((unsigned char)piece.data[1] == 0xA0);

    /* 模式也可从一个多字节字符内部开始；匹配仍只承诺字节语义。 */
    const char inside_utf8[] = {(char)0xBD, (char)0xA0};
    HeapString byte_pattern;
    assert(heap_string_from_bytes(&byte_pattern, inside_utf8,
                                  sizeof(inside_utf8)));
    assert_both_match(&text, &byte_pattern, 0, 1);

    heap_string_destroy(&text);
    heap_string_destroy(&pattern);
    heap_string_destroy(&piece);
    heap_string_destroy(&byte_pattern);
}

static void run_boundary_tests(void)
{
    test_heap_string_lifecycle();
    test_concat_substring_compare();
    test_prefix_function();
    test_next_tables();
    test_pattern_matching();
    test_kmp_against_naive_exhaustively();
    test_utf8_byte_semantics();
}

/* ================================ 演示 ================================ */

int main(void)
{
    run_boundary_tests();

    HeapString text;
    HeapString pattern;
    HeapString greeting;
    HeapString suffix;
    assert(heap_string_from_cstr(&text, "ababcabcacbab"));
    assert(heap_string_from_cstr(&pattern, "abcac"));
    assert(heap_string_from_cstr(&greeting, "数据"));
    assert(heap_string_from_cstr(&suffix, "结构"));

    HeapString joined;
    assert(heap_string_init(&joined, 0));
    assert(heap_string_concat(&joined, &greeting, &suffix));
    printf("堆串连接：%s（%zu 字节）\n", joined.data, joined.length);

    const size_t naive_position = naive_find_from(&text, &pattern, 0);
    bool out_of_memory = false;
    const size_t kmp_position =
        kmp_find_from(&text, &pattern, 0, &out_of_memory);
    assert(!out_of_memory);
    printf("朴素匹配：位置 %zu；KMP：位置 %zu\n", naive_position,
           kmp_position);

    size_t *prefix = kmp_alloc_prefix(&pattern);
    assert(prefix != NULL);
    printf("模式串 %s 的 prefix：", pattern.data);
    for (size_t i = 0; i < pattern.length; ++i) {
        printf("%s%zu", i == 0 ? "" : ",", prefix[i]);
    }
    putchar('\n');
    free(prefix);

    puts("UTF-8 说明：本示例的位置、长度和切片都按字节计算。");
    puts("边界测试：全部通过。");

    heap_string_destroy(&text);
    heap_string_destroy(&pattern);
    heap_string_destroy(&greeting);
    heap_string_destroy(&suffix);
    heap_string_destroy(&joined);
    return 0;
}
