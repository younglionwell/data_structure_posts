#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 第 2 章“线性表”配套示例。
 *
 * 内存所有权约定：
 *   1. SeqList、SList 和 Polynomial 都拥有自己内部申请的内存；
 *   2. init 后必须调用对应的 destroy；
 *   3. merge/add 复制输入，不会接管或修改输入；
 *   4. merge/add 的输出对象必须已初始化，且不能与任一输入对象相同。
 */

/* ============================== 动态顺序表 ============================== */

typedef struct {
    int *data;
    size_t length;
    size_t capacity;
} SeqList;

static bool seq_init(SeqList *list, size_t initial_capacity)
{
    if (list == NULL) {
        return false;
    }

    *list = (SeqList){NULL, 0, 0};
    if (initial_capacity == 0) {
        return true;
    }
    if (initial_capacity > SIZE_MAX / sizeof(list->data[0])) {
        return false;
    }

    list->data = malloc(initial_capacity * sizeof(list->data[0]));
    if (list->data == NULL) {
        return false;
    }
    list->capacity = initial_capacity;
    return true;
}

static void seq_destroy(SeqList *list)
{
    if (list == NULL) {
        return;
    }

    free(list->data);
    *list = (SeqList){NULL, 0, 0};
}

/* reserve 只扩容，不缩容；成功后 capacity 至少为 min_capacity。 */
static bool seq_reserve(SeqList *list, size_t min_capacity)
{
    if (list == NULL) {
        return false;
    }
    if (min_capacity <= list->capacity) {
        return true;
    }
    if (min_capacity > SIZE_MAX / sizeof(list->data[0])) {
        return false;
    }

    int *new_data = realloc(list->data,
                            min_capacity * sizeof(list->data[0]));
    if (new_data == NULL) {
        return false;
    }

    list->data = new_data;
    list->capacity = min_capacity;
    return true;
}

static bool seq_grow_for_one_more(SeqList *list)
{
    if (list->length < list->capacity) {
        return true;
    }
    if (list->length == SIZE_MAX) {
        return false;
    }

    const size_t needed = list->length + 1;
    size_t new_capacity = list->capacity == 0 ? 4 : list->capacity;

    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2;
    }
    return seq_reserve(list, new_capacity);
}

/*
 * 在 0 基位置 index 插入。合法范围是 [0, length]；index == length
 * 表示追加。成功插入可能触发 realloc，使此前取得的元素指针失效。
 */
static bool seq_insert(SeqList *list, size_t index, int value)
{
    if (list == NULL || index > list->length) {
        return false;
    }
    if (!seq_grow_for_one_more(list)) {
        return false;
    }

    memmove(&list->data[index + 1], &list->data[index],
            (list->length - index) * sizeof(list->data[0]));
    list->data[index] = value;
    ++list->length;
    return true;
}

/* 删除 0 基位置 index；removed 可以为 NULL。 */
static bool seq_erase(SeqList *list, size_t index, int *removed)
{
    if (list == NULL || index >= list->length) {
        return false;
    }

    if (removed != NULL) {
        *removed = list->data[index];
    }
    memmove(&list->data[index], &list->data[index + 1],
            (list->length - index - 1) * sizeof(list->data[0]));
    --list->length;
    return true;
}

/* 越界时返回 NULL；返回的指针可能在下一次插入或扩容后失效。 */
static const int *seq_at(const SeqList *list, size_t index)
{
    if (list == NULL || index >= list->length) {
        return NULL;
    }
    return &list->data[index];
}

static void seq_print(const SeqList *list)
{
    putchar('[');
    for (size_t i = 0; i < list->length; ++i) {
        printf("%s%d", i == 0 ? "" : ", ", list->data[i]);
    }
    putchar(']');
}

/* ============================ 带哨兵的单链表 ============================ */

typedef struct SListNode {
    int value;
    struct SListNode *next;
} SListNode;

typedef struct {
    SListNode head; /* 内嵌哨兵：head 本身不保存线性表元素。 */
    size_t length;
} SList;

typedef void (*SListVisitor)(int value, void *context);

static void slist_init(SList *list)
{
    assert(list != NULL);
    list->head = (SListNode){0, NULL};
    list->length = 0;
}

static void slist_destroy(SList *list)
{
    if (list == NULL) {
        return;
    }

    SListNode *node = list->head.next;
    while (node != NULL) {
        SListNode *next = node->next;
        free(node);
        node = next;
    }
    slist_init(list);
}

/* 返回 0 基位置 index 的前驱；index 的合法范围是 [0, length]。 */
static SListNode *slist_predecessor(SList *list, size_t index)
{
    if (list == NULL || index > list->length) {
        return NULL;
    }

    SListNode *previous = &list->head;
    for (size_t i = 0; i < index; ++i) {
        previous = previous->next;
    }
    return previous;
}

/* 在 0 基位置 index 插入；寻找前驱 O(n)，改两根指针 O(1)。 */
static bool slist_insert(SList *list, size_t index, int value)
{
    SListNode *previous = slist_predecessor(list, index);
    if (previous == NULL) {
        return false;
    }

    SListNode *node = malloc(sizeof(*node));
    if (node == NULL) {
        return false;
    }
    *node = (SListNode){value, previous->next};
    previous->next = node;
    ++list->length;
    return true;
}

/* 删除 0 基位置 index；寻找前驱 O(n)，摘下结点 O(1)。 */
static bool slist_erase(SList *list, size_t index, int *removed)
{
    if (list == NULL || index >= list->length) {
        return false;
    }

    SListNode *previous = slist_predecessor(list, index);
    assert(previous != NULL && previous->next != NULL);

    SListNode *victim = previous->next;
    previous->next = victim->next;
    if (removed != NULL) {
        *removed = victim->value;
    }
    free(victim);
    --list->length;
    return true;
}

static const int *slist_at(const SList *list, size_t index)
{
    if (list == NULL || index >= list->length) {
        return NULL;
    }

    const SListNode *node = list->head.next;
    for (size_t i = 0; i < index; ++i) {
        node = node->next;
    }
    return &node->value;
}

static void slist_foreach(const SList *list, SListVisitor visit,
                          void *context)
{
    if (list == NULL || visit == NULL) {
        return;
    }

    for (const SListNode *node = list->head.next; node != NULL;
         node = node->next) {
        visit(node->value, context);
    }
}

/* 只供线性构造算法使用：tail 必须指向 list 当前的最后一个结点。 */
static bool slist_append_to_tail(SList *list, SListNode **tail, int value)
{
    SListNode *node = malloc(sizeof(*node));
    if (node == NULL) {
        return false;
    }
    *node = (SListNode){value, NULL};
    (*tail)->next = node;
    *tail = node;
    ++list->length;
    return true;
}

/* values 被复制；成功后替换 list 原有内容，失败时 list 保持不变。 */
static bool slist_assign_array(SList *list, const int values[], size_t count)
{
    if (list == NULL || (values == NULL && count != 0)) {
        return false;
    }

    SList replacement;
    slist_init(&replacement);
    SListNode *tail = &replacement.head;

    for (size_t i = 0; i < count; ++i) {
        if (!slist_append_to_tail(&replacement, &tail, values[i])) {
            slist_destroy(&replacement);
            return false;
        }
    }

    slist_destroy(list);
    *list = replacement;
    return true;
}

static bool slist_is_nondecreasing(const SList *list)
{
    if (list == NULL) {
        return false;
    }

    const SListNode *node = list->head.next;
    while (node != NULL && node->next != NULL) {
        if (node->value > node->next->value) {
            return false;
        }
        node = node->next;
    }
    return true;
}

/*
 * 合并两个非递减链表，保留重复元素。输入不变，结果拥有全新的结点。
 * out 必须已初始化并且不能与 left/right 是同一个对象。成功后替换 out；
 * 输入无序或申请内存失败时，out 保持不变。
 */
static bool slist_merge_sorted(const SList *left, const SList *right,
                               SList *out)
{
    if (left == NULL || right == NULL || out == NULL || out == left ||
        out == right || !slist_is_nondecreasing(left) ||
        !slist_is_nondecreasing(right)) {
        return false;
    }

    SList merged;
    slist_init(&merged);
    SListNode *tail = &merged.head;
    const SListNode *a = left->head.next;
    const SListNode *b = right->head.next;

    while (a != NULL || b != NULL) {
        int value;
        if (b == NULL || (a != NULL && a->value <= b->value)) {
            value = a->value;
            a = a->next;
        } else {
            value = b->value;
            b = b->next;
        }

        if (!slist_append_to_tail(&merged, &tail, value)) {
            slist_destroy(&merged);
            return false;
        }
    }

    slist_destroy(out);
    *out = merged;
    return true;
}

typedef struct {
    bool first;
} PrintListContext;

static void print_list_value(int value, void *context)
{
    PrintListContext *state = context;
    printf("%s%d", state->first ? "" : ", ", value);
    state->first = false;
}

static void slist_print(const SList *list)
{
    PrintListContext context = {true};
    putchar('[');
    slist_foreach(list, print_list_value, &context);
    putchar(']');
}

/* ========================= 一元稀疏多项式链表 ========================== */

typedef struct PolyNode {
    long long coefficient;
    unsigned exponent;
    struct PolyNode *next;
} PolyNode;

typedef struct {
    PolyNode head; /* 哨兵不表示任何一项。 */
    size_t term_count;
} Polynomial;

typedef struct {
    long long coefficient;
    unsigned exponent;
} Term;

static void polynomial_init(Polynomial *polynomial)
{
    assert(polynomial != NULL);
    polynomial->head = (PolyNode){0, 0, NULL};
    polynomial->term_count = 0;
}

static void polynomial_destroy(Polynomial *polynomial)
{
    if (polynomial == NULL) {
        return;
    }

    PolyNode *term = polynomial->head.next;
    while (term != NULL) {
        PolyNode *next = term->next;
        free(term);
        term = next;
    }
    polynomial_init(polynomial);
}

static bool checked_add_ll(long long left, long long right, long long *sum)
{
    if ((right > 0 && left > LLONG_MAX - right) ||
        (right < 0 && left < LLONG_MIN - right)) {
        return false;
    }
    *sum = left + right;
    return true;
}

/*
 * 加入 coefficient * x^exponent。链表按指数严格递减排列；同指数项
 * 自动合并，系数变为 0 时删除该项。溢出或内存不足时返回 false。
 */
static bool polynomial_add_term(Polynomial *polynomial,
                                long long coefficient, unsigned exponent)
{
    if (polynomial == NULL || coefficient == 0) {
        return polynomial != NULL;
    }

    PolyNode *previous = &polynomial->head;
    while (previous->next != NULL &&
           previous->next->exponent > exponent) {
        previous = previous->next;
    }

    if (previous->next != NULL && previous->next->exponent == exponent) {
        long long combined;
        if (!checked_add_ll(previous->next->coefficient, coefficient,
                            &combined)) {
            return false;
        }
        if (combined == 0) {
            PolyNode *cancelled = previous->next;
            previous->next = cancelled->next;
            free(cancelled);
            --polynomial->term_count;
        } else {
            previous->next->coefficient = combined;
        }
        return true;
    }

    PolyNode *term = malloc(sizeof(*term));
    if (term == NULL) {
        return false;
    }
    *term = (PolyNode){coefficient, exponent, previous->next};
    previous->next = term;
    ++polynomial->term_count;
    return true;
}

static bool polynomial_is_normalized(const Polynomial *polynomial)
{
    if (polynomial == NULL) {
        return false;
    }

    size_t count = 0;
    const PolyNode *term = polynomial->head.next;
    while (term != NULL) {
        if (term->coefficient == 0) {
            return false;
        }
        if (term->next != NULL && term->exponent <= term->next->exponent) {
            return false;
        }
        ++count;
        term = term->next;
    }
    return count == polynomial->term_count;
}

/* terms 可无序且可含同指数项；成功后替换 polynomial 原有内容。 */
static bool polynomial_assign_terms(Polynomial *polynomial,
                                    const Term terms[], size_t count)
{
    if (polynomial == NULL || (terms == NULL && count != 0)) {
        return false;
    }

    Polynomial replacement;
    polynomial_init(&replacement);
    for (size_t i = 0; i < count; ++i) {
        if (!polynomial_add_term(&replacement, terms[i].coefficient,
                                 terms[i].exponent)) {
            polynomial_destroy(&replacement);
            return false;
        }
    }

    polynomial_destroy(polynomial);
    *polynomial = replacement;
    return true;
}

static bool polynomial_append_to_tail(Polynomial *polynomial,
                                      PolyNode **tail, long long coefficient,
                                      unsigned exponent)
{
    assert(coefficient != 0);
    PolyNode *term = malloc(sizeof(*term));
    if (term == NULL) {
        return false;
    }

    *term = (PolyNode){coefficient, exponent, NULL};
    (*tail)->next = term;
    *tail = term;
    ++polynomial->term_count;
    return true;
}

/*
 * 两个规范化多项式相加，复杂度 O(m+n)。输入保持不变；out 必须已
 * 初始化且不能与输入相同。失败时 out 保持不变。
 */
static bool polynomial_add(const Polynomial *left, const Polynomial *right,
                           Polynomial *out)
{
    if (left == NULL || right == NULL || out == NULL || out == left ||
        out == right || !polynomial_is_normalized(left) ||
        !polynomial_is_normalized(right)) {
        return false;
    }

    Polynomial sum;
    polynomial_init(&sum);
    PolyNode *tail = &sum.head;
    const PolyNode *a = left->head.next;
    const PolyNode *b = right->head.next;

    while (a != NULL || b != NULL) {
        long long coefficient;
        unsigned exponent;

        if (b == NULL || (a != NULL && a->exponent > b->exponent)) {
            coefficient = a->coefficient;
            exponent = a->exponent;
            a = a->next;
        } else if (a == NULL || b->exponent > a->exponent) {
            coefficient = b->coefficient;
            exponent = b->exponent;
            b = b->next;
        } else {
            exponent = a->exponent;
            if (!checked_add_ll(a->coefficient, b->coefficient,
                                &coefficient)) {
                polynomial_destroy(&sum);
                return false;
            }
            a = a->next;
            b = b->next;
        }

        if (coefficient != 0 &&
            !polynomial_append_to_tail(&sum, &tail, coefficient, exponent)) {
            polynomial_destroy(&sum);
            return false;
        }
    }

    polynomial_destroy(out);
    *out = sum;
    return true;
}

static const PolyNode *polynomial_find(const Polynomial *polynomial,
                                       unsigned exponent)
{
    if (polynomial == NULL) {
        return NULL;
    }

    const PolyNode *term = polynomial->head.next;
    while (term != NULL && term->exponent > exponent) {
        term = term->next;
    }
    return term != NULL && term->exponent == exponent ? term : NULL;
}

static void polynomial_print(const Polynomial *polynomial)
{
    if (polynomial->head.next == NULL) {
        putchar('0');
        return;
    }

    bool first = true;
    for (const PolyNode *term = polynomial->head.next; term != NULL;
         term = term->next) {
        printf("%s(%lld)x^%u", first ? "" : " + ", term->coefficient,
               term->exponent);
        first = false;
    }
}

/* ================================ 测试 ================================= */

typedef struct {
    int values[16];
    size_t length;
} IntCollector;

static void collect_value(int value, void *context)
{
    IntCollector *collector = context;
    assert(collector->length < sizeof(collector->values) /
                                   sizeof(collector->values[0]));
    collector->values[collector->length++] = value;
}

static void assert_slist_equals(const SList *list, const int expected[],
                                size_t count)
{
    assert(list->length == count);
    const SListNode *node = list->head.next;
    for (size_t i = 0; i < count; ++i) {
        assert(node != NULL);
        assert(node->value == expected[i]);
        node = node->next;
    }
    assert(node == NULL);
}

static void test_seq_list(void)
{
    SeqList list;
    assert(seq_init(&list, 0));
    assert(list.length == 0 && list.capacity == 0);
    assert(seq_at(&list, 0) == NULL);
    assert(!seq_erase(&list, 0, NULL));
    assert(!seq_insert(&list, 1, 99));

    assert(seq_insert(&list, 0, 20));             /* [20] */
    assert(seq_insert(&list, 0, 10));             /* [10, 20] */
    assert(seq_insert(&list, list.length, 40));   /* [10, 20, 40] */
    assert(seq_insert(&list, 2, 30));             /* [10, 20, 30, 40] */
    assert(list.length == 4);
    for (size_t i = 0; i < list.length; ++i) {
        assert(seq_at(&list, i) != NULL);
        assert(*seq_at(&list, i) == (int)((i + 1) * 10));
    }
    assert(seq_at(&list, list.length) == NULL);

    const size_t old_capacity = list.capacity;
    assert(seq_reserve(&list, old_capacity + 7));
    assert(list.capacity >= old_capacity + 7);
    assert(seq_reserve(&list, 1)); /* reserve 不缩容。 */
    assert(list.capacity >= old_capacity + 7);

    int removed = 0;
    assert(seq_erase(&list, 0, &removed) && removed == 10);
    assert(seq_erase(&list, 1, &removed) && removed == 30);
    assert(seq_erase(&list, list.length - 1, &removed) && removed == 40);
    assert(list.length == 1 && *seq_at(&list, 0) == 20);
    assert(seq_erase(&list, 0, NULL));
    assert(list.length == 0);
    assert(!seq_erase(&list, 0, NULL));

    seq_destroy(&list);
    assert(list.data == NULL && list.length == 0 && list.capacity == 0);
}

static void test_singly_linked_list(void)
{
    SList list;
    slist_init(&list);
    assert(slist_at(&list, 0) == NULL);
    assert(!slist_erase(&list, 0, NULL));
    assert(!slist_insert(&list, 1, 99));

    assert(slist_insert(&list, 0, 20));
    assert(slist_insert(&list, 0, 10));
    assert(slist_insert(&list, 2, 40));
    assert(slist_insert(&list, 2, 30));
    const int expected[] = {10, 20, 30, 40};
    assert_slist_equals(&list, expected, 4);

    IntCollector collector = {{0}, 0};
    slist_foreach(&list, collect_value, &collector);
    assert(collector.length == 4);
    for (size_t i = 0; i < collector.length; ++i) {
        assert(collector.values[i] == expected[i]);
        assert(slist_at(&list, i) != NULL);
        assert(*slist_at(&list, i) == expected[i]);
    }
    assert(slist_at(&list, collector.length) == NULL);

    int removed = 0;
    assert(slist_erase(&list, 0, &removed) && removed == 10);
    assert(slist_erase(&list, 1, &removed) && removed == 30);
    assert(slist_erase(&list, list.length - 1, &removed) && removed == 40);
    assert(list.length == 1 && *slist_at(&list, 0) == 20);
    assert(slist_erase(&list, 0, NULL));
    assert(list.length == 0 && list.head.next == NULL);

    slist_destroy(&list);
}

static void test_sorted_merge(void)
{
    const int left_values[] = {1, 3, 3, 7};
    const int right_values[] = {2, 3, 8};
    const int expected[] = {1, 2, 3, 3, 3, 7, 8};
    SList left;
    SList right;
    SList out;
    slist_init(&left);
    slist_init(&right);
    slist_init(&out);

    assert(slist_assign_array(&left, left_values, 4));
    assert(slist_assign_array(&right, right_values, 3));
    assert(slist_insert(&out, 0, 99)); /* 验证成功时会替换旧输出。 */
    assert(slist_merge_sorted(&left, &right, &out));
    assert_slist_equals(&out, expected, 7);
    assert_slist_equals(&left, left_values, 4);   /* 输入保持不变。 */
    assert_slist_equals(&right, right_values, 3);

    assert(!slist_merge_sorted(&left, &right, &left)); /* 禁止输出别名。 */
    assert_slist_equals(&left, left_values, 4);

    const int unsorted_values[] = {2, 1};
    SList unsorted;
    slist_init(&unsorted);
    assert(slist_assign_array(&unsorted, unsorted_values, 2));
    assert(!slist_merge_sorted(&unsorted, &right, &out));
    assert_slist_equals(&out, expected, 7); /* 失败时输出保持不变。 */

    SList empty_a;
    SList empty_b;
    SList empty_out;
    slist_init(&empty_a);
    slist_init(&empty_b);
    slist_init(&empty_out);
    assert(slist_merge_sorted(&empty_a, &empty_b, &empty_out));
    assert(empty_out.length == 0 && empty_out.head.next == NULL);

    slist_destroy(&left);
    slist_destroy(&right);
    slist_destroy(&out);
    slist_destroy(&unsorted);
    slist_destroy(&empty_a);
    slist_destroy(&empty_b);
    slist_destroy(&empty_out);
}

static void test_polynomial(void)
{
    /* 输入故意不按指数排序，assign 会整理并合并同类项。 */
    const Term left_terms[] = {
        {2, 2}, {5, 4}, {-3, 0}, {4, 2}, {-4, 2},
    }; /* 5x^4 + 2x^2 - 3 */
    const Term right_terms[] = {
        {9, 1}, {-5, 4}, {3, 0}, {7, 3}, {-2, 2},
    }; /* -5x^4 + 7x^3 - 2x^2 + 9x + 3 */

    Polynomial left;
    Polynomial right;
    Polynomial sum;
    polynomial_init(&left);
    polynomial_init(&right);
    polynomial_init(&sum);
    assert(polynomial_assign_terms(&left, left_terms, 5));
    assert(polynomial_assign_terms(&right, right_terms, 5));
    assert(polynomial_is_normalized(&left));
    assert(polynomial_is_normalized(&right));

    assert(polynomial_add_term(&sum, 123, 99));
    assert(polynomial_add(&left, &right, &sum));
    assert(sum.term_count == 2);
    assert(polynomial_find(&sum, 3) != NULL);
    assert(polynomial_find(&sum, 3)->coefficient == 7);
    assert(polynomial_find(&sum, 1) != NULL);
    assert(polynomial_find(&sum, 1)->coefficient == 9);
    assert(polynomial_find(&sum, 4) == NULL);
    assert(polynomial_find(&sum, 2) == NULL);
    assert(polynomial_find(&sum, 0) == NULL);
    assert(!polynomial_add(&left, &right, &left));
    assert(polynomial_find(&left, 4)->coefficient == 5);

    /* 同指数相加为 0 时，结点应被删除。 */
    assert(polynomial_add_term(&sum, -7, 3));
    assert(sum.term_count == 1 && polynomial_find(&sum, 3) == NULL);

    /* 系数溢出时加法失败，旧输出保持不变。 */
    const Term huge_term[] = {{LLONG_MAX, 6}};
    const Term one_term[] = {{1, 6}};
    Polynomial huge;
    Polynomial one;
    polynomial_init(&huge);
    polynomial_init(&one);
    assert(polynomial_assign_terms(&huge, huge_term, 1));
    assert(polynomial_assign_terms(&one, one_term, 1));
    assert(!polynomial_add(&huge, &one, &sum));
    assert(sum.term_count == 1);
    assert(polynomial_find(&sum, 1)->coefficient == 9);

    Polynomial empty_a;
    Polynomial empty_b;
    Polynomial empty_sum;
    polynomial_init(&empty_a);
    polynomial_init(&empty_b);
    polynomial_init(&empty_sum);
    assert(polynomial_add(&empty_a, &empty_b, &empty_sum));
    assert(empty_sum.term_count == 0 && empty_sum.head.next == NULL);

    polynomial_destroy(&left);
    polynomial_destroy(&right);
    polynomial_destroy(&sum);
    polynomial_destroy(&huge);
    polynomial_destroy(&one);
    polynomial_destroy(&empty_a);
    polynomial_destroy(&empty_b);
    polynomial_destroy(&empty_sum);
}

static void run_boundary_tests(void)
{
    test_seq_list();
    test_singly_linked_list();
    test_sorted_merge();
    test_polynomial();
}

/* ================================ 演示 ================================= */

int main(void)
{
    run_boundary_tests();

    SeqList sequence;
    assert(seq_init(&sequence, 2));
    assert(seq_insert(&sequence, 0, 10));
    assert(seq_insert(&sequence, 1, 20));
    assert(seq_insert(&sequence, 1, 15));
    printf("顺序表在下标 1 插入 15: ");
    seq_print(&sequence);
    putchar('\n');

    SList left;
    SList right;
    SList merged;
    const int left_values[] = {1, 3, 7};
    const int right_values[] = {2, 3, 8};
    slist_init(&left);
    slist_init(&right);
    slist_init(&merged);
    assert(slist_assign_array(&left, left_values, 3));
    assert(slist_assign_array(&right, right_values, 3));
    assert(slist_merge_sorted(&left, &right, &merged));
    printf("两个有序链表合并: ");
    slist_print(&merged);
    putchar('\n');

    const Term p_terms[] = {{3, 5}, {2, 2}, {-7, 0}};
    const Term q_terms[] = {{4, 4}, {-2, 2}, {7, 0}};
    Polynomial p;
    Polynomial q;
    Polynomial sum;
    polynomial_init(&p);
    polynomial_init(&q);
    polynomial_init(&sum);
    assert(polynomial_assign_terms(&p, p_terms,
                                   sizeof(p_terms) / sizeof(p_terms[0])));
    assert(polynomial_assign_terms(&q, q_terms,
                                   sizeof(q_terms) / sizeof(q_terms[0])));
    assert(polynomial_add(&p, &q, &sum));
    printf("P(x) + Q(x) = ");
    polynomial_print(&sum);
    putchar('\n');

    puts("边界测试：全部通过。");

    seq_destroy(&sequence);
    slist_destroy(&left);
    slist_destroy(&right);
    slist_destroy(&merged);
    polynomial_destroy(&p);
    polynomial_destroy(&q);
    polynomial_destroy(&sum);
    return 0;
}
