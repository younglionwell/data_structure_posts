/*
 * 第 5 章：数组和广义表
 *
 * 编译：
 *   cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
 *      -fsanitize=address,undefined 05-array-generalized-list-demo.c \
 *      -o 05-array-generalized-list-demo
 *
 * 这份程序演示四件事：
 *   1. 二维数组如何按行优先映射到一段连续内存；
 *   2. 对称矩阵如何只保存下三角；
 *   3. 稀疏矩阵如何使用三元组表，以及如何快速转置、相加；
 *   4. 广义表如何使用头尾链表表示，并完成解析、打印、求长度、
 *      求深度、深拷贝和销毁。
 *
 * 广义表约定：NULL 表示空表；原子的深度为 0；空表的深度为 1；
 * 非空表的深度为“最深元素的深度 + 1”。解析器只接受有限、树形的
 * 广义表；共享子表和递归表需要引用计数或图结构，不在本例的所有权
 * 模型之内。
 */

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool checked_add_size(size_t left, size_t right, size_t *result) {
    if (result == NULL || right > SIZE_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool checked_mul_size(size_t left, size_t right, size_t *result) {
    if (result == NULL || (left != 0U && right > SIZE_MAX / left)) {
        return false;
    }
    *result = left * right;
    return true;
}

static bool checked_add_int(int left, int right, int *result) {
    if (result == NULL ||
        (right > 0 && left > INT_MAX - right) ||
        (right < 0 && left < INT_MIN - right)) {
        return false;
    }
    *result = left + right;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 二维数组：按行优先的顺序存储                                             */

typedef struct {
    size_t rows;
    size_t cols;
    int *data;
} IntMatrix;

static void matrix_destroy(IntMatrix *matrix) {
    if (matrix == NULL) {
        return;
    }
    free(matrix->data);
    matrix->rows = 0U;
    matrix->cols = 0U;
    matrix->data = NULL;
}

static bool matrix_create(IntMatrix *matrix, size_t rows, size_t cols) {
    IntMatrix result = {0U, 0U, NULL};
    size_t count = 0U;
    size_t bytes = 0U;
    int *data = NULL;

    /* 本例把 0×n 或 n×0 视为合法空矩阵；data 保持 NULL。 */
    if (matrix == NULL ||
        !checked_mul_size(rows, cols, &count) ||
        !checked_mul_size(count, sizeof(*data), &bytes)) {
        return false;
    }

    if (bytes != 0U) {
        data = calloc(1U, bytes);
        if (data == NULL) {
            return false;
        }
    }

    result.rows = rows;
    result.cols = cols;
    result.data = data;
    matrix_destroy(matrix);
    *matrix = result;
    return true;
}

static bool matrix_offset(const IntMatrix *matrix,
                          size_t row,
                          size_t col,
                          size_t *offset) {
    size_t row_start = 0U;

    if (matrix == NULL || offset == NULL ||
        row >= matrix->rows || col >= matrix->cols ||
        !checked_mul_size(row, matrix->cols, &row_start)) {
        return false;
    }
    return checked_add_size(row_start, col, offset);
}

static bool matrix_set(IntMatrix *matrix, size_t row, size_t col, int value) {
    size_t offset = 0U;
    if (!matrix_offset(matrix, row, col, &offset)) {
        return false;
    }
    matrix->data[offset] = value;
    return true;
}

static bool matrix_get(const IntMatrix *matrix,
                       size_t row,
                       size_t col,
                       int *value) {
    size_t offset = 0U;
    if (value == NULL || !matrix_offset(matrix, row, col, &offset)) {
        return false;
    }
    *value = matrix->data[offset];
    return true;
}

/* ------------------------------------------------------------------------- */
/* 对称矩阵：只保存下三角                                                     */

typedef struct {
    size_t n;
    size_t count;
    int *lower;
} SymmetricMatrix;

/* 计算 n * (n + 1) / 2，同时避免中间乘法溢出。 */
static bool triangular_count(size_t n, size_t *count) {
    size_t left = n;
    size_t right = 0U;

    if (count == NULL || n == SIZE_MAX) {
        return false;
    }
    right = n + 1U;
    if ((left & 1U) == 0U) {
        left /= 2U;
    } else {
        right /= 2U;
    }
    return checked_mul_size(left, right, count);
}

static void symmetric_destroy(SymmetricMatrix *matrix) {
    if (matrix == NULL) {
        return;
    }
    free(matrix->lower);
    matrix->n = 0U;
    matrix->count = 0U;
    matrix->lower = NULL;
}

static bool symmetric_create(SymmetricMatrix *matrix, size_t n) {
    SymmetricMatrix result = {0U, 0U, NULL};
    size_t count = 0U;
    size_t bytes = 0U;
    int *lower = NULL;

    if (matrix == NULL ||
        !triangular_count(n, &count) ||
        !checked_mul_size(count, sizeof(*lower), &bytes)) {
        return false;
    }

    if (bytes != 0U) {
        lower = calloc(1U, bytes);
        if (lower == NULL) {
            return false;
        }
    }

    result.n = n;
    result.count = count;
    result.lower = lower;
    symmetric_destroy(matrix);
    *matrix = result;
    return true;
}

static bool symmetric_offset(const SymmetricMatrix *matrix,
                             size_t row,
                             size_t col,
                             size_t *offset) {
    size_t row_start = 0U;
    size_t swap = 0U;

    if (matrix == NULL || offset == NULL ||
        row >= matrix->n || col >= matrix->n) {
        return false;
    }

    /* 上三角元素与它关于主对角线对称的下三角元素共用一个位置。 */
    if (col > row) {
        swap = row;
        row = col;
        col = swap;
    }

    if (!triangular_count(row, &row_start) ||
        !checked_add_size(row_start, col, offset)) {
        return false;
    }
    return *offset < matrix->count;
}

static bool symmetric_set(SymmetricMatrix *matrix,
                          size_t row,
                          size_t col,
                          int value) {
    size_t offset = 0U;
    if (!symmetric_offset(matrix, row, col, &offset)) {
        return false;
    }
    matrix->lower[offset] = value;
    return true;
}

static bool symmetric_get(const SymmetricMatrix *matrix,
                          size_t row,
                          size_t col,
                          int *value) {
    size_t offset = 0U;
    if (value == NULL || !symmetric_offset(matrix, row, col, &offset)) {
        return false;
    }
    *value = matrix->lower[offset];
    return true;
}

/* ------------------------------------------------------------------------- */
/* 稀疏矩阵：按“行优先、列次优先”排序的三元组表                               */

typedef struct {
    size_t row;
    size_t col;
    int value;
} SparseTerm;

typedef struct {
    size_t rows;
    size_t cols;
    size_t count;
    size_t capacity;
    SparseTerm *terms;
} SparseMatrix;

static void sparse_destroy(SparseMatrix *matrix) {
    if (matrix == NULL) {
        return;
    }
    free(matrix->terms);
    matrix->rows = 0U;
    matrix->cols = 0U;
    matrix->count = 0U;
    matrix->capacity = 0U;
    matrix->terms = NULL;
}

static bool sparse_create(SparseMatrix *matrix,
                          size_t rows,
                          size_t cols,
                          size_t capacity) {
    size_t bytes = 0U;
    SparseTerm *terms = NULL;

    if (matrix == NULL ||
        !checked_mul_size(capacity, sizeof(*terms), &bytes)) {
        return false;
    }

    if (bytes != 0U) {
        terms = malloc(bytes);
        if (terms == NULL) {
            return false;
        }
    }

    {
        SparseMatrix result = {rows, cols, 0U, capacity, terms};
        sparse_destroy(matrix);
        *matrix = result;
    }
    return true;
}

static int sparse_position_compare(size_t left_row,
                                   size_t left_col,
                                   size_t right_row,
                                   size_t right_col) {
    if (left_row != right_row) {
        return left_row < right_row ? -1 : 1;
    }
    if (left_col != right_col) {
        return left_col < right_col ? -1 : 1;
    }
    return 0;
}

static bool sparse_append(SparseMatrix *matrix,
                          size_t row,
                          size_t col,
                          int value) {
    if (matrix == NULL || value == 0 ||
        row >= matrix->rows || col >= matrix->cols ||
        matrix->count >= matrix->capacity) {
        return false;
    }

    if (matrix->count != 0U) {
        const SparseTerm *last = &matrix->terms[matrix->count - 1U];
        if (sparse_position_compare(last->row, last->col, row, col) >= 0) {
            return false;
        }
    }

    matrix->terms[matrix->count] = (SparseTerm){row, col, value};
    matrix->count += 1U;
    return true;
}

static bool sparse_is_valid(const SparseMatrix *matrix) {
    size_t index = 0U;

    if (matrix == NULL || matrix->count > matrix->capacity ||
        (matrix->capacity != 0U && matrix->terms == NULL)) {
        return false;
    }

    for (index = 0U; index < matrix->count; ++index) {
        const SparseTerm *term = &matrix->terms[index];
        if (term->value == 0 ||
            term->row >= matrix->rows || term->col >= matrix->cols) {
            return false;
        }
        if (index != 0U) {
            const SparseTerm *previous = &matrix->terms[index - 1U];
            if (sparse_position_compare(previous->row,
                                        previous->col,
                                        term->row,
                                        term->col) >= 0) {
                return false;
            }
        }
    }
    return true;
}

static bool sparse_get(const SparseMatrix *matrix,
                       size_t row,
                       size_t col,
                       int *value) {
    size_t low = 0U;
    size_t high = 0U;

    if (value == NULL || !sparse_is_valid(matrix) ||
        row >= matrix->rows || col >= matrix->cols) {
        return false;
    }

    high = matrix->count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2U;
        const SparseTerm *term = &matrix->terms[middle];
        const int comparison = sparse_position_compare(term->row,
                                                       term->col,
                                                       row,
                                                       col);
        if (comparison < 0) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }

    if (low < matrix->count &&
        matrix->terms[low].row == row && matrix->terms[low].col == col) {
        *value = matrix->terms[low].value;
    } else {
        *value = 0;
    }
    return true;
}

/*
 * 快速转置：
 *   column_counts[c] 记录原矩阵第 c 列有多少个非零元；
 *   next_position[c] 记录这一列转置后应写入的下一个位置。
 *
 * 时间复杂度 O(cols + count)，而不是逐列反复扫描三元组表。
 */
static bool sparse_fast_transpose(const SparseMatrix *source,
                                  SparseMatrix *destination) {
    SparseMatrix result = {0U, 0U, 0U, 0U, NULL};
    size_t *column_counts = NULL;
    size_t *next_position = NULL;
    size_t aux_bytes = 0U;
    size_t index = 0U;
    bool success = false;

    if (destination == NULL || destination == source ||
        !sparse_is_valid(source) ||
        !sparse_create(&result, source->cols, source->rows, source->count)) {
        return false;
    }

    if (source->cols == 0U) {
        sparse_destroy(destination);
        *destination = result;
        return true;
    }

    if (!checked_mul_size(source->cols, sizeof(*column_counts), &aux_bytes)) {
        goto cleanup;
    }
    column_counts = calloc(1U, aux_bytes);
    next_position = malloc(aux_bytes);
    if (column_counts == NULL || next_position == NULL) {
        goto cleanup;
    }

    for (index = 0U; index < source->count; ++index) {
        column_counts[source->terms[index].col] += 1U;
    }

    next_position[0] = 0U;
    for (index = 1U; index < source->cols; ++index) {
        if (!checked_add_size(next_position[index - 1U],
                              column_counts[index - 1U],
                              &next_position[index])) {
            goto cleanup;
        }
    }

    for (index = 0U; index < source->count; ++index) {
        const SparseTerm term = source->terms[index];
        const size_t write_at = next_position[term.col];
        if (write_at >= source->count) {
            goto cleanup;
        }
        result.terms[write_at] = (SparseTerm){term.col, term.row, term.value};
        next_position[term.col] += 1U;
    }
    result.count = source->count;
    success = true;

cleanup:
    free(column_counts);
    free(next_position);
    if (!success) {
        sparse_destroy(&result);
        return false;
    }
    sparse_destroy(destination);
    *destination = result;
    return true;
}

static bool sparse_add(const SparseMatrix *left,
                       const SparseMatrix *right,
                       SparseMatrix *destination) {
    SparseMatrix result = {0U, 0U, 0U, 0U, NULL};
    size_t capacity = 0U;
    size_t left_index = 0U;
    size_t right_index = 0U;

    if (destination == NULL || destination == left || destination == right ||
        !sparse_is_valid(left) ||
        !sparse_is_valid(right) ||
        left->rows != right->rows || left->cols != right->cols ||
        !checked_add_size(left->count, right->count, &capacity) ||
        !sparse_create(&result, left->rows, left->cols, capacity)) {
        return false;
    }

    while (left_index < left->count || right_index < right->count) {
        if (right_index == right->count) {
            const SparseTerm term = left->terms[left_index++];
            if (!sparse_append(&result, term.row, term.col, term.value)) {
                goto failure;
            }
        } else if (left_index == left->count) {
            const SparseTerm term = right->terms[right_index++];
            if (!sparse_append(&result, term.row, term.col, term.value)) {
                goto failure;
            }
        } else {
            const SparseTerm left_term = left->terms[left_index];
            const SparseTerm right_term = right->terms[right_index];
            const int comparison = sparse_position_compare(left_term.row,
                                                           left_term.col,
                                                           right_term.row,
                                                           right_term.col);
            if (comparison < 0) {
                left_index += 1U;
                if (!sparse_append(&result,
                                   left_term.row,
                                   left_term.col,
                                   left_term.value)) {
                    goto failure;
                }
            } else if (comparison > 0) {
                right_index += 1U;
                if (!sparse_append(&result,
                                   right_term.row,
                                   right_term.col,
                                   right_term.value)) {
                    goto failure;
                }
            } else {
                int sum = 0;
                left_index += 1U;
                right_index += 1U;
                if (!checked_add_int(left_term.value, right_term.value, &sum)) {
                    goto failure;
                }
                if (sum != 0 &&
                    !sparse_append(&result,
                                   left_term.row,
                                   left_term.col,
                                   sum)) {
                    goto failure;
                }
            }
        }
    }

    sparse_destroy(destination);
    *destination = result;
    return true;

failure:
    sparse_destroy(&result);
    return false;
}

/* ------------------------------------------------------------------------- */
/* 广义表：头尾链表                                                           */

typedef enum {
    GL_ATOM,
    GL_LIST
} GLKind;

typedef struct GLNode GLNode;

struct GLNode {
    GLKind kind;
    union {
        char *atom;
        struct {
            GLNode *head;
            GLNode *tail;
        } list;
    } value;
};

static void gl_destroy(GLNode *node) {
    if (node == NULL) {
        return;
    }
    if (node->kind == GL_ATOM) {
        free(node->value.atom);
    } else {
        gl_destroy(node->value.list.head);
        gl_destroy(node->value.list.tail);
    }
    free(node);
}

static char *duplicate_range(const char *start, size_t length) {
    size_t bytes = 0U;
    char *copy = NULL;

    if (start == NULL || !checked_add_size(length, 1U, &bytes)) {
        return NULL;
    }
    copy = malloc(bytes);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, start, length);
    copy[length] = '\0';
    return copy;
}

static GLNode *gl_new_atom_range(const char *start, size_t length) {
    GLNode *node = NULL;
    char *atom = duplicate_range(start, length);

    if (atom == NULL) {
        return NULL;
    }
    node = malloc(sizeof(*node));
    if (node == NULL) {
        free(atom);
        return NULL;
    }
    node->kind = GL_ATOM;
    node->value.atom = atom;
    return node;
}

static GLNode *gl_new_cons(GLNode *head, GLNode *tail) {
    GLNode *node = malloc(sizeof(*node));
    if (node == NULL) {
        return NULL;
    }
    node->kind = GL_LIST;
    node->value.list.head = head;
    node->value.list.tail = tail;
    return node;
}

typedef struct {
    const char *text;
    size_t position;
    size_t recursion_depth;
    bool ok;
} GLParser;

enum { GL_MAX_PARSE_DEPTH = 256 };

static bool gl_is_space(char character) {
    return character == ' ' || character == '\t' ||
           character == '\n' || character == '\r';
}

static void gl_skip_space(GLParser *parser) {
    while (gl_is_space(parser->text[parser->position])) {
        parser->position += 1U;
    }
}

static GLNode *gl_parse_value(GLParser *parser);

static GLNode *gl_parse_list(GLParser *parser) {
    GLNode *first = NULL;
    GLNode *last = NULL;

    parser->position += 1U; /* 跳过 '(' */
    gl_skip_space(parser);
    if (parser->text[parser->position] == ')') {
        parser->position += 1U;
        return NULL; /* 空表 */
    }

    for (;;) {
        GLNode *element = gl_parse_value(parser);
        GLNode *cell = NULL;

        if (!parser->ok) {
            gl_destroy(first);
            return NULL;
        }

        cell = gl_new_cons(element, NULL);
        if (cell == NULL) {
            gl_destroy(element);
            gl_destroy(first);
            parser->ok = false;
            return NULL;
        }
        if (first == NULL) {
            first = cell;
        } else {
            last->value.list.tail = cell;
        }
        last = cell;

        gl_skip_space(parser);
        if (parser->text[parser->position] == ')') {
            parser->position += 1U;
            return first;
        }
        if (parser->text[parser->position] != ',') {
            gl_destroy(first);
            parser->ok = false;
            return NULL;
        }
        parser->position += 1U;
        gl_skip_space(parser);
        if (parser->text[parser->position] == ')' ||
            parser->text[parser->position] == ',' ||
            parser->text[parser->position] == '\0') {
            gl_destroy(first);
            parser->ok = false;
            return NULL;
        }
    }
}

static GLNode *gl_parse_atom(GLParser *parser) {
    const size_t start = parser->position;
    size_t length = 0U;
    GLNode *atom = NULL;

    while (parser->text[parser->position] != '\0' &&
           parser->text[parser->position] != '(' &&
           parser->text[parser->position] != ')' &&
           parser->text[parser->position] != ',' &&
           !gl_is_space(parser->text[parser->position])) {
        parser->position += 1U;
    }

    length = parser->position - start;
    if (length == 0U) {
        parser->ok = false;
        return NULL;
    }
    atom = gl_new_atom_range(parser->text + start, length);
    if (atom == NULL) {
        parser->ok = false;
    }
    return atom;
}

static GLNode *gl_parse_value(GLParser *parser) {
    GLNode *node = NULL;

    gl_skip_space(parser);
    if (!parser->ok || parser->recursion_depth >= GL_MAX_PARSE_DEPTH) {
        parser->ok = false;
        return NULL;
    }

    parser->recursion_depth += 1U;
    if (parser->text[parser->position] == '(') {
        node = gl_parse_list(parser);
    } else {
        node = gl_parse_atom(parser);
    }
    parser->recursion_depth -= 1U;
    return node;
}

static bool gl_parse(const char *text, GLNode **result) {
    GLParser parser = {text, 0U, 0U, true};
    GLNode *node = NULL;

    if (text == NULL || result == NULL) {
        return false;
    }
    node = gl_parse_value(&parser);
    gl_skip_space(&parser);
    if (!parser.ok || parser.text[parser.position] != '\0') {
        gl_destroy(node);
        return false;
    }
    *result = node;
    return true;
}

static bool gl_length(const GLNode *list, size_t *length) {
    const GLNode *cursor = list;
    size_t count = 0U;

    if (length == NULL) {
        return false;
    }
    while (cursor != NULL) {
        if (cursor->kind != GL_LIST || count == SIZE_MAX) {
            return false;
        }
        count += 1U;
        cursor = cursor->value.list.tail;
    }
    *length = count;
    return true;
}

static bool gl_head(const GLNode *list, const GLNode **head) {
    if (list == NULL || head == NULL || list->kind != GL_LIST) {
        return false;
    }
    *head = list->value.list.head;
    return true;
}

static bool gl_tail(const GLNode *list, const GLNode **tail) {
    if (list == NULL || tail == NULL || list->kind != GL_LIST) {
        return false;
    }
    *tail = list->value.list.tail;
    return true;
}

static bool gl_depth(const GLNode *node, size_t *depth) {
    const GLNode *cursor = NULL;
    size_t maximum = 0U;

    if (depth == NULL) {
        return false;
    }
    if (node == NULL) {
        *depth = 1U;
        return true;
    }
    if (node->kind == GL_ATOM) {
        *depth = 0U;
        return true;
    }
    if (node->kind != GL_LIST) {
        return false;
    }

    cursor = node;
    while (cursor != NULL) {
        size_t element_depth = 0U;
        if (cursor->kind != GL_LIST ||
            !gl_depth(cursor->value.list.head, &element_depth)) {
            return false;
        }
        if (element_depth > maximum) {
            maximum = element_depth;
        }
        cursor = cursor->value.list.tail;
    }
    return checked_add_size(maximum, 1U, depth);
}

static bool gl_copy(const GLNode *source, GLNode **destination) {
    GLNode *head_copy = NULL;
    GLNode *tail_copy = NULL;
    GLNode *node_copy = NULL;

    if (destination == NULL) {
        return false;
    }
    if (source == NULL) {
        *destination = NULL;
        return true;
    }
    if (source->kind == GL_ATOM) {
        node_copy = gl_new_atom_range(source->value.atom,
                                      strlen(source->value.atom));
        if (node_copy == NULL) {
            return false;
        }
        *destination = node_copy;
        return true;
    }
    if (source->kind != GL_LIST ||
        !gl_copy(source->value.list.head, &head_copy) ||
        !gl_copy(source->value.list.tail, &tail_copy)) {
        gl_destroy(head_copy);
        gl_destroy(tail_copy);
        return false;
    }
    node_copy = gl_new_cons(head_copy, tail_copy);
    if (node_copy == NULL) {
        gl_destroy(head_copy);
        gl_destroy(tail_copy);
        return false;
    }
    *destination = node_copy;
    return true;
}

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool ok;
} GLText;

static void gl_text_append_char(GLText *text, char character) {
    if (!text->ok || text->length >= text->capacity - 1U) {
        text->ok = false;
        return;
    }
    text->data[text->length++] = character;
    text->data[text->length] = '\0';
}

static void gl_text_append_string(GLText *text, const char *string) {
    while (*string != '\0' && text->ok) {
        gl_text_append_char(text, *string++);
    }
}

static void gl_format_node(const GLNode *node, GLText *text) {
    const GLNode *cursor = NULL;
    bool first = true;

    if (!text->ok) {
        return;
    }
    if (node == NULL) {
        gl_text_append_string(text, "()");
        return;
    }
    if (node->kind == GL_ATOM) {
        gl_text_append_string(text, node->value.atom);
        return;
    }
    if (node->kind != GL_LIST) {
        text->ok = false;
        return;
    }

    gl_text_append_char(text, '(');
    cursor = node;
    while (cursor != NULL && text->ok) {
        if (cursor->kind != GL_LIST) {
            text->ok = false;
            return;
        }
        if (!first) {
            gl_text_append_char(text, ',');
        }
        gl_format_node(cursor->value.list.head, text);
        first = false;
        cursor = cursor->value.list.tail;
    }
    gl_text_append_char(text, ')');
}

static bool gl_format(const GLNode *node, char *buffer, size_t capacity) {
    GLText text = {buffer, capacity, 0U, true};

    if (buffer == NULL || capacity == 0U) {
        return false;
    }
    buffer[0] = '\0';
    gl_format_node(node, &text);
    return text.ok;
}

/* ------------------------------------------------------------------------- */
/* 测试                                                                       */

static void test_dense_matrix(void) {
    IntMatrix matrix = {0U, 0U, NULL};
    IntMatrix replacement = {0U, 0U, NULL};
    IntMatrix impossible = {0U, 0U, NULL};
    size_t row = 0U;
    size_t col = 0U;
    size_t offset = 0U;
    int value = 0;

    assert(matrix_create(&matrix, 3U, 4U));
    for (row = 0U; row < matrix.rows; ++row) {
        for (col = 0U; col < matrix.cols; ++col) {
            assert(matrix_set(&matrix, row, col,
                              (int)(row * 10U + col)));
        }
    }

    assert(matrix_offset(&matrix, 2U, 3U, &offset));
    assert(offset == 11U);
    assert(matrix_get(&matrix, 2U, 3U, &value));
    assert(value == 23);
    assert(!matrix_get(&matrix, 3U, 0U, &value));
    assert(!matrix_set(&matrix, 0U, 4U, 99));
    assert(matrix_create(&replacement, 1U, 1U));
    assert(matrix_set(&replacement, 0U, 0U, 7));
    assert(matrix_create(&replacement, 2U, 2U));
    assert(matrix_get(&replacement, 0U, 0U, &value) && value == 0);
    assert(!matrix_create(&impossible, SIZE_MAX, 2U));

    matrix_destroy(&replacement);
    matrix_destroy(&impossible);
    matrix_destroy(&matrix);
}

static void test_symmetric_matrix(void) {
    SymmetricMatrix matrix = {0U, 0U, NULL};
    SymmetricMatrix replacement = {0U, 0U, NULL};
    SymmetricMatrix impossible = {0U, 0U, NULL};
    size_t offset = 0U;
    int value = 0;

    assert(symmetric_create(&matrix, 4U));
    assert(matrix.count == 10U);
    assert(symmetric_set(&matrix, 3U, 1U, 31));
    assert(symmetric_get(&matrix, 1U, 3U, &value));
    assert(value == 31);
    assert(symmetric_offset(&matrix, 3U, 1U, &offset));
    assert(offset == 7U);
    assert(!symmetric_get(&matrix, 4U, 0U, &value));
    assert(symmetric_create(&replacement, 2U));
    assert(symmetric_set(&replacement, 1U, 0U, 9));
    assert(symmetric_create(&replacement, 3U));
    assert(symmetric_get(&replacement, 1U, 0U, &value) && value == 0);
    assert(!symmetric_create(&impossible, SIZE_MAX));

    symmetric_destroy(&replacement);
    symmetric_destroy(&impossible);
    symmetric_destroy(&matrix);
}

static void test_sparse_matrix(void) {
    SparseMatrix left = {0U, 0U, 0U, 0U, NULL};
    SparseMatrix right = {0U, 0U, 0U, 0U, NULL};
    SparseMatrix transposed = {0U, 0U, 0U, 0U, NULL};
    SparseMatrix sum = {0U, 0U, 0U, 0U, NULL};
    SparseMatrix overflow_left = {0U, 0U, 0U, 0U, NULL};
    SparseMatrix overflow_right = {0U, 0U, 0U, 0U, NULL};
    SparseMatrix overflow_result = {0U, 0U, 0U, 0U, NULL};
    SparseMatrix replacement = {0U, 0U, 0U, 0U, NULL};
    int value = 0;

    assert(sparse_create(&left, 4U, 5U, 5U));
    assert(sparse_append(&left, 0U, 1U, 2));
    assert(sparse_append(&left, 0U, 4U, 9));
    assert(sparse_append(&left, 2U, 0U, 3));
    assert(sparse_append(&left, 2U, 3U, 5));
    assert(sparse_append(&left, 3U, 1U, 7));
    assert(!sparse_append(&left, 3U, 1U, 8));
    assert(sparse_is_valid(&left));

    assert(sparse_fast_transpose(&left, &transposed));
    assert(!sparse_fast_transpose(&left, &left));
    assert(transposed.rows == 5U && transposed.cols == 4U);
    assert(transposed.count == 5U);
    assert(transposed.terms[0].row == 0U &&
           transposed.terms[0].col == 2U &&
           transposed.terms[0].value == 3);
    assert(transposed.terms[1].row == 1U &&
           transposed.terms[1].col == 0U &&
           transposed.terms[1].value == 2);
    assert(transposed.terms[2].row == 1U &&
           transposed.terms[2].col == 3U &&
           transposed.terms[2].value == 7);
    assert(transposed.terms[3].row == 3U &&
           transposed.terms[3].col == 2U &&
           transposed.terms[3].value == 5);
    assert(transposed.terms[4].row == 4U &&
           transposed.terms[4].col == 0U &&
           transposed.terms[4].value == 9);

    assert(sparse_create(&right, 4U, 5U, 3U));
    assert(sparse_append(&right, 0U, 1U, -2));
    assert(sparse_append(&right, 1U, 2U, 4));
    assert(sparse_append(&right, 2U, 3U, 6));
    assert(sparse_add(&left, &right, &sum));
    assert(!sparse_add(&left, &right, &left));
    assert(!sparse_add(&left, &right, &right));
    assert(sum.count == 5U);
    assert(sparse_get(&sum, 0U, 1U, &value) && value == 0);
    assert(sparse_get(&sum, 0U, 4U, &value) && value == 9);
    assert(sparse_get(&sum, 1U, 2U, &value) && value == 4);
    assert(sparse_get(&sum, 2U, 3U, &value) && value == 11);

    assert(sparse_create(&overflow_left, 1U, 1U, 1U));
    assert(sparse_create(&overflow_right, 1U, 1U, 1U));
    assert(sparse_append(&overflow_left, 0U, 0U, INT_MAX));
    assert(sparse_append(&overflow_right, 0U, 0U, 1));
    assert(!sparse_add(&overflow_left, &overflow_right, &overflow_result));
    assert(overflow_result.terms == NULL);

    assert(sparse_create(&replacement, 2U, 2U, 1U));
    assert(sparse_append(&replacement, 0U, 0U, 4));
    assert(sparse_create(&replacement, 3U, 3U, 2U));
    assert(replacement.count == 0U && replacement.capacity == 2U);

    sparse_destroy(&replacement);
    sparse_destroy(&overflow_result);
    sparse_destroy(&overflow_right);
    sparse_destroy(&overflow_left);
    sparse_destroy(&sum);
    sparse_destroy(&transposed);
    sparse_destroy(&right);
    sparse_destroy(&left);
}

static void test_generalized_list(void) {
    GLNode *list = NULL;
    GLNode *copy = NULL;
    GLNode *empty = NULL;
    GLNode *atom = NULL;
    GLNode *nested = NULL;
    GLNode *invalid = NULL;
    const GLNode *head = NULL;
    const GLNode *tail = NULL;
    size_t length = 0U;
    size_t depth = 0U;
    char text[128];

    assert(gl_parse("(a, (b,c), (), d)", &list));
    assert(gl_format(list, text, sizeof(text)));
    assert(strcmp(text, "(a,(b,c),(),d)") == 0);
    assert(gl_length(list, &length) && length == 4U);
    assert(gl_depth(list, &depth) && depth == 2U);

    assert(gl_head(list, &head));
    assert(head != NULL && head->kind == GL_ATOM);
    assert(strcmp(head->value.atom, "a") == 0);
    assert(gl_tail(list, &tail));
    assert(gl_length(tail, &length) && length == 3U);

    assert(gl_copy(list, &copy));
    assert(copy != list);
    assert(gl_format(copy, text, sizeof(text)));
    assert(strcmp(text, "(a,(b,c),(),d)") == 0);

    assert(gl_parse("()", &empty));
    assert(empty == NULL);
    assert(gl_length(empty, &length) && length == 0U);
    assert(gl_depth(empty, &depth) && depth == 1U);
    assert(gl_format(empty, text, sizeof(text)));
    assert(strcmp(text, "()") == 0);

    assert(gl_parse("alpha", &atom));
    assert(gl_depth(atom, &depth) && depth == 0U);
    assert(!gl_length(atom, &length));

    assert(gl_parse("((()))", &nested));
    assert(gl_depth(nested, &depth) && depth == 3U);

    assert(!gl_parse("(a,,b)", &invalid));
    assert(!gl_parse("(a,b", &invalid));
    assert(!gl_parse("a extra", &invalid));

    gl_destroy(invalid);
    gl_destroy(nested);
    gl_destroy(atom);
    gl_destroy(empty);
    gl_destroy(copy);
    gl_destroy(list);
}

int main(void) {
    test_dense_matrix();
    test_symmetric_matrix();
    test_sparse_matrix();
    test_generalized_list();

    puts("All array, sparse-matrix and generalized-list tests passed.");
    return EXIT_SUCCESS;
}
