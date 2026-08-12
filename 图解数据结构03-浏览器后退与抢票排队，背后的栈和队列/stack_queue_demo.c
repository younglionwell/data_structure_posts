#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 第 3 章“栈和队列”配套示例（C11）。
 *
 * 约定与内存所有权：
 *   1. 本文件中的位置全部采用 0 基下标；字符串错误位置是字节偏移。
 *      本例只把 ASCII 括号、数字和运算符当作语法字符，因此字节偏移
 *      不会把这些字符截断。
 *   2. IntStack 拥有动态数组；init 成功后必须调用 destroy。push 可能
 *      realloc，因此不要长期保存 peek 返回的元素地址。
 *   3. LinkQueue 内嵌一个哨兵头结点，并拥有全部数据结点；入队时复制
 *      int，出队时释放数据结点。队列对象初始化后不要按值复制。
 *   4. CircularQueue 拥有一个固定容量数组。capacity 表示最多可保存的
 *      元素数；size 明确区分队空与队满，所以不需要浪费一个数组槽位。
 *   5. EventCalendar 拥有尚未处理的事件结点；pop 会复制事件并释放结点。
 *   6. 所有带输出参数的查询/删除函数，都允许输出指针为 NULL；失败时
 *      不会伪造一个“默认值”。
 */

/* ============================ 动态数组顺序栈 ========================== */

typedef struct {
    long long *data;
    size_t size;
    size_t capacity;
} IntStack;

static bool int_stack_init(IntStack *stack, size_t initial_capacity)
{
    if (stack == NULL) {
        return false;
    }

    *stack = (IntStack){NULL, 0, 0};
    if (initial_capacity == 0) {
        return true;
    }
    if (initial_capacity > SIZE_MAX / sizeof(stack->data[0])) {
        return false;
    }

    stack->data = malloc(initial_capacity * sizeof(stack->data[0]));
    if (stack->data == NULL) {
        return false;
    }
    stack->capacity = initial_capacity;
    return true;
}

static void int_stack_destroy(IntStack *stack)
{
    if (stack == NULL) {
        return;
    }

    free(stack->data);
    *stack = (IntStack){NULL, 0, 0};
}

static bool int_stack_reserve(IntStack *stack, size_t min_capacity)
{
    if (stack == NULL) {
        return false;
    }
    if (min_capacity <= stack->capacity) {
        return true;
    }
    if (min_capacity > SIZE_MAX / sizeof(stack->data[0])) {
        return false;
    }

    long long *new_data = realloc(
        stack->data, min_capacity * sizeof(stack->data[0]));
    if (new_data == NULL) {
        return false;
    }

    stack->data = new_data;
    stack->capacity = min_capacity;
    return true;
}

static bool int_stack_push(IntStack *stack, long long value)
{
    if (stack == NULL || stack->size == SIZE_MAX) {
        return false;
    }

    if (stack->size == stack->capacity) {
        size_t new_capacity = stack->capacity == 0 ? 4 : stack->capacity;
        if (new_capacity < stack->size + 1) {
            if (new_capacity > SIZE_MAX / 2) {
                new_capacity = stack->size + 1;
            } else {
                new_capacity *= 2;
            }
        }
        if (!int_stack_reserve(stack, new_capacity)) {
            return false;
        }
    }

    stack->data[stack->size++] = value;
    return true;
}

static bool int_stack_pop(IntStack *stack, long long *value)
{
    if (stack == NULL || stack->size == 0) {
        return false;
    }

    --stack->size;
    if (value != NULL) {
        *value = stack->data[stack->size];
    }
    return true;
}

/* 返回的地址归 stack 所有；下一次 push/destroy 后可能失效。 */
static const long long *int_stack_peek(const IntStack *stack)
{
    if (stack == NULL || stack->size == 0) {
        return NULL;
    }
    return &stack->data[stack->size - 1];
}

/* =============================== 括号匹配 ============================== */

typedef struct {
    char opening;
    size_t position;
} BracketFrame;

typedef struct {
    BracketFrame *data;
    size_t size;
    size_t capacity;
} BracketStack;

typedef enum {
    BRACKETS_OK,
    BRACKETS_INVALID_ARGUMENT,
    BRACKETS_UNEXPECTED_CLOSING,
    BRACKETS_WRONG_CLOSING,
    BRACKETS_UNCLOSED_OPENING,
    BRACKETS_NO_MEMORY
} BracketStatus;

typedef struct {
    BracketStatus status;
    size_t error_position; /* BRACKETS_OK 时为 SIZE_MAX。 */
    char expected;
    char found;
} BracketResult;

static void bracket_stack_destroy(BracketStack *stack)
{
    if (stack == NULL) {
        return;
    }
    free(stack->data);
    *stack = (BracketStack){NULL, 0, 0};
}

static bool bracket_stack_push(BracketStack *stack, BracketFrame frame)
{
    if (stack == NULL || stack->size == SIZE_MAX) {
        return false;
    }

    if (stack->size == stack->capacity) {
        size_t new_capacity = stack->capacity == 0 ? 8 : stack->capacity;
        if (new_capacity < stack->size + 1) {
            if (new_capacity > SIZE_MAX / 2) {
                new_capacity = stack->size + 1;
            } else {
                new_capacity *= 2;
            }
        }
        if (new_capacity > SIZE_MAX / sizeof(stack->data[0])) {
            return false;
        }
        BracketFrame *new_data = realloc(
            stack->data, new_capacity * sizeof(stack->data[0]));
        if (new_data == NULL) {
            return false;
        }
        stack->data = new_data;
        stack->capacity = new_capacity;
    }

    stack->data[stack->size++] = frame;
    return true;
}

static char matching_closing(char opening)
{
    switch (opening) {
    case '(':
        return ')';
    case '[':
        return ']';
    case '{':
        return '}';
    default:
        return '\0';
    }
}

/*
 * 非括号字符会被忽略。若结尾仍有多个左括号，报告最靠近栈顶、也就是
 * 最内层尚未闭合的那个左括号。
 */
static BracketResult match_brackets(const char *text)
{
    if (text == NULL) {
        return (BracketResult){BRACKETS_INVALID_ARGUMENT, 0, '\0', '\0'};
    }

    BracketStack stack = {NULL, 0, 0};
    BracketResult result = {BRACKETS_OK, SIZE_MAX, '\0', '\0'};

    for (size_t i = 0; text[i] != '\0'; ++i) {
        const char current = text[i];
        if (current == '(' || current == '[' || current == '{') {
            if (!bracket_stack_push(
                    &stack, (BracketFrame){current, i})) {
                result = (BracketResult){BRACKETS_NO_MEMORY, i, '\0',
                                         current};
                goto done;
            }
            continue;
        }

        if (current != ')' && current != ']' && current != '}') {
            continue;
        }

        if (stack.size == 0) {
            result = (BracketResult){BRACKETS_UNEXPECTED_CLOSING, i, '\0',
                                     current};
            goto done;
        }

        const BracketFrame top = stack.data[stack.size - 1];
        const char expected = matching_closing(top.opening);
        if (current != expected) {
            result = (BracketResult){BRACKETS_WRONG_CLOSING, i, expected,
                                     current};
            goto done;
        }
        --stack.size;
    }

    if (stack.size != 0) {
        const BracketFrame top = stack.data[stack.size - 1];
        result = (BracketResult){BRACKETS_UNCLOSED_OPENING, top.position,
                                 matching_closing(top.opening), '\0'};
    }

done:
    bracket_stack_destroy(&stack);
    return result;
}

/* =========================== 两个栈求中缀表达式 ======================== */

typedef struct {
    char symbol;
    size_t position;
} OperatorEntry;

typedef struct {
    OperatorEntry *data;
    size_t size;
    size_t capacity;
} OperatorStack;

typedef enum {
    EVAL_OK,
    EVAL_INVALID_ARGUMENT,
    EVAL_EMPTY,
    EVAL_SYNTAX_ERROR,
    EVAL_UNMATCHED_PAREN,
    EVAL_DIVIDE_BY_ZERO,
    EVAL_INTEGER_OVERFLOW,
    EVAL_NO_MEMORY
} EvalStatus;

typedef struct {
    EvalStatus status;
    long long value;       /* 仅 EVAL_OK 时有效。 */
    size_t error_position; /* EVAL_OK 时为 SIZE_MAX。 */
} EvalResult;

static void operator_stack_destroy(OperatorStack *stack)
{
    if (stack == NULL) {
        return;
    }
    free(stack->data);
    *stack = (OperatorStack){NULL, 0, 0};
}

static bool operator_stack_push(OperatorStack *stack, OperatorEntry value)
{
    if (stack == NULL || stack->size == SIZE_MAX) {
        return false;
    }

    if (stack->size == stack->capacity) {
        size_t new_capacity = stack->capacity == 0 ? 8 : stack->capacity;
        if (new_capacity < stack->size + 1) {
            if (new_capacity > SIZE_MAX / 2) {
                new_capacity = stack->size + 1;
            } else {
                new_capacity *= 2;
            }
        }
        if (new_capacity > SIZE_MAX / sizeof(stack->data[0])) {
            return false;
        }
        OperatorEntry *new_data = realloc(
            stack->data, new_capacity * sizeof(stack->data[0]));
        if (new_data == NULL) {
            return false;
        }
        stack->data = new_data;
        stack->capacity = new_capacity;
    }

    stack->data[stack->size++] = value;
    return true;
}

static bool operator_stack_pop(OperatorStack *stack, OperatorEntry *value)
{
    if (stack == NULL || stack->size == 0) {
        return false;
    }
    --stack->size;
    if (value != NULL) {
        *value = stack->data[stack->size];
    }
    return true;
}

static const OperatorEntry *operator_stack_peek(const OperatorStack *stack)
{
    if (stack == NULL || stack->size == 0) {
        return NULL;
    }
    return &stack->data[stack->size - 1];
}

static bool checked_add_ll(long long left, long long right, long long *out)
{
    if ((right > 0 && left > LLONG_MAX - right) ||
        (right < 0 && left < LLONG_MIN - right)) {
        return false;
    }
    *out = left + right;
    return true;
}

static bool checked_sub_ll(long long left, long long right, long long *out)
{
    if ((right > 0 && left < LLONG_MIN + right) ||
        (right < 0 && left > LLONG_MAX + right)) {
        return false;
    }
    *out = left - right;
    return true;
}

static bool checked_mul_ll(long long left, long long right, long long *out)
{
    if (left == 0 || right == 0) {
        *out = 0;
        return true;
    }

    if (left > 0) {
        if ((right > 0 && left > LLONG_MAX / right) ||
            (right < 0 && right < LLONG_MIN / left)) {
            return false;
        }
    } else {
        if ((right > 0 && left < LLONG_MIN / right) ||
            (right < 0 && left < LLONG_MAX / right)) {
            return false;
        }
    }

    *out = left * right;
    return true;
}

static int operator_precedence(char symbol)
{
    return symbol == '*' || symbol == '/' ? 2 : 1;
}

/*
 * 弹出一个运算符和两个操作数并计算。遇到除零、溢出或栈内结构异常时，
 * 用运算符原本在输入中的位置报告错误。
 */
static EvalStatus apply_top_operator(IntStack *values,
                                     OperatorStack *operators,
                                     size_t *error_position)
{
    OperatorEntry operation;
    long long right;
    long long left;
    long long answer;

    if (!operator_stack_pop(operators, &operation) ||
        operation.symbol == '(' || !int_stack_pop(values, &right) ||
        !int_stack_pop(values, &left)) {
        if (error_position != NULL) {
            *error_position = operators != NULL && operators->size != 0
                                  ? operators->data[operators->size - 1].position
                                  : 0;
        }
        return EVAL_SYNTAX_ERROR;
    }

    if (error_position != NULL) {
        *error_position = operation.position;
    }

    bool arithmetic_ok = false;
    switch (operation.symbol) {
    case '+':
        arithmetic_ok = checked_add_ll(left, right, &answer);
        break;
    case '-':
        arithmetic_ok = checked_sub_ll(left, right, &answer);
        break;
    case '*':
        arithmetic_ok = checked_mul_ll(left, right, &answer);
        break;
    case '/':
        if (right == 0) {
            return EVAL_DIVIDE_BY_ZERO;
        }
        if (left == LLONG_MIN && right == -1) {
            return EVAL_INTEGER_OVERFLOW;
        }
        answer = left / right; /* C 的整数除法向 0 截断。 */
        arithmetic_ok = true;
        break;
    default:
        return EVAL_SYNTAX_ERROR;
    }

    if (!arithmetic_ok) {
        return EVAL_INTEGER_OVERFLOW;
    }
    if (!int_stack_push(values, answer)) {
        return EVAL_NO_MEMORY;
    }
    return EVAL_OK;
}

static bool is_ascii_space(char current)
{
    return current == ' ' || current == '\t' || current == '\n' ||
           current == '\r' || current == '\f' || current == '\v';
}

static bool is_ascii_digit(char current)
{
    return current >= '0' && current <= '9';
}

static bool is_binary_operator(char current)
{
    return current == '+' || current == '-' || current == '*' ||
           current == '/';
}

/*
 * 支持：非负十进制整数、二元 + - * /、圆括号和 ASCII 空白。
 * 不支持一元正负号、隐式乘法和小数。运算使用 long long；整数除法向 0
 * 截断。结果只在 EVAL_OK 时写入 EvalResult.value。
 */
static EvalResult evaluate_infix(const char *expression)
{
    if (expression == NULL) {
        return (EvalResult){EVAL_INVALID_ARGUMENT, 0, 0};
    }

    IntStack values;
    if (!int_stack_init(&values, 8)) {
        return (EvalResult){EVAL_NO_MEMORY, 0, 0};
    }
    OperatorStack operators = {NULL, 0, 0};
    EvalResult result = {EVAL_SYNTAX_ERROR, 0, 0};
    bool expect_operand = true;
    bool saw_number = false;
    size_t i = 0;

    while (expression[i] != '\0') {
        if (is_ascii_space(expression[i])) {
            ++i;
            continue;
        }

        if (expect_operand) {
            if (is_ascii_digit(expression[i])) {
                const size_t number_start = i;
                long long value = 0;
                while (is_ascii_digit(expression[i])) {
                    const int digit = expression[i] - '0';
                    if (value > (LLONG_MAX - digit) / 10) {
                        result = (EvalResult){EVAL_INTEGER_OVERFLOW, 0,
                                              number_start};
                        goto done;
                    }
                    value = value * 10 + digit;
                    ++i;
                }
                if (!int_stack_push(&values, value)) {
                    result = (EvalResult){EVAL_NO_MEMORY, 0, number_start};
                    goto done;
                }
                saw_number = true;
                expect_operand = false;
                continue;
            }

            if (expression[i] == '(') {
                if (!operator_stack_push(
                        &operators, (OperatorEntry){'(', i})) {
                    result = (EvalResult){EVAL_NO_MEMORY, 0, i};
                    goto done;
                }
                ++i;
                continue;
            }

            result = (EvalResult){EVAL_SYNTAX_ERROR, 0, i};
            goto done;
        }

        if (is_binary_operator(expression[i])) {
            const char current = expression[i];
            const OperatorEntry *top = operator_stack_peek(&operators);
            while (top != NULL && top->symbol != '(' &&
                   operator_precedence(top->symbol) >=
                       operator_precedence(current)) {
                size_t error_position = i;
                const EvalStatus status = apply_top_operator(
                    &values, &operators, &error_position);
                if (status != EVAL_OK) {
                    result = (EvalResult){status, 0, error_position};
                    goto done;
                }
                top = operator_stack_peek(&operators);
            }

            if (!operator_stack_push(
                    &operators, (OperatorEntry){current, i})) {
                result = (EvalResult){EVAL_NO_MEMORY, 0, i};
                goto done;
            }
            expect_operand = true;
            ++i;
            continue;
        }

        if (expression[i] == ')') {
            const OperatorEntry *top = operator_stack_peek(&operators);
            while (top != NULL && top->symbol != '(') {
                size_t error_position = i;
                const EvalStatus status = apply_top_operator(
                    &values, &operators, &error_position);
                if (status != EVAL_OK) {
                    result = (EvalResult){status, 0, error_position};
                    goto done;
                }
                top = operator_stack_peek(&operators);
            }
            if (top == NULL) {
                result = (EvalResult){EVAL_UNMATCHED_PAREN, 0, i};
                goto done;
            }
            assert(top->symbol == '(');
            (void)operator_stack_pop(&operators, NULL);
            ++i;
            continue;
        }

        result = (EvalResult){EVAL_SYNTAX_ERROR, 0, i};
        goto done;
    }

    if (!saw_number) {
        result = (EvalResult){EVAL_EMPTY, 0, i};
        goto done;
    }
    if (expect_operand) {
        result = (EvalResult){EVAL_SYNTAX_ERROR, 0, i};
        goto done;
    }

    while (operators.size != 0) {
        const OperatorEntry *top = operator_stack_peek(&operators);
        assert(top != NULL);
        if (top->symbol == '(') {
            result = (EvalResult){EVAL_UNMATCHED_PAREN, 0, top->position};
            goto done;
        }

        size_t error_position = i;
        const EvalStatus status = apply_top_operator(
            &values, &operators, &error_position);
        if (status != EVAL_OK) {
            result = (EvalResult){status, 0, error_position};
            goto done;
        }
    }

    if (values.size != 1) {
        result = (EvalResult){EVAL_SYNTAX_ERROR, 0, i};
        goto done;
    }
    result = (EvalResult){EVAL_OK, *int_stack_peek(&values), SIZE_MAX};

done:
    int_stack_destroy(&values);
    operator_stack_destroy(&operators);
    return result;
}

/* =============================== 链队列 ================================ */

typedef struct LinkQueueNode {
    int value;
    struct LinkQueueNode *next;
} LinkQueueNode;

typedef struct {
    LinkQueueNode sentinel;
    LinkQueueNode *front;
    LinkQueueNode *rear;
    size_t size;
} LinkQueue;

static void link_queue_init(LinkQueue *queue)
{
    assert(queue != NULL);
    queue->sentinel = (LinkQueueNode){0, NULL};
    queue->front = &queue->sentinel;
    queue->rear = &queue->sentinel;
    queue->size = 0;
}

static void link_queue_destroy(LinkQueue *queue)
{
    if (queue == NULL) {
        return;
    }

    LinkQueueNode *node = queue->front->next;
    while (node != NULL) {
        LinkQueueNode *next = node->next;
        free(node);
        node = next;
    }
    link_queue_init(queue);
}

static bool link_queue_enqueue(LinkQueue *queue, int value)
{
    if (queue == NULL || queue->size == SIZE_MAX) {
        return false;
    }

    LinkQueueNode *node = malloc(sizeof(*node));
    if (node == NULL) {
        return false;
    }
    *node = (LinkQueueNode){value, NULL};

    queue->rear->next = node;
    queue->rear = node;
    ++queue->size;
    return true;
}

static bool link_queue_dequeue(LinkQueue *queue, int *value)
{
    if (queue == NULL || queue->front->next == NULL) {
        return false;
    }

    LinkQueueNode *old_front = queue->front->next;
    queue->front->next = old_front->next;
    if (value != NULL) {
        *value = old_front->value;
    }
    free(old_front);
    --queue->size;

    if (queue->front->next == NULL) {
        queue->rear = queue->front;
        assert(queue->size == 0);
    }
    return true;
}

static bool link_queue_front(const LinkQueue *queue, int *value)
{
    if (queue == NULL || queue->front->next == NULL) {
        return false;
    }
    if (value != NULL) {
        *value = queue->front->next->value;
    }
    return true;
}

/* =============================== 循环队列 ============================== */

typedef struct {
    int *data;
    size_t capacity; /* 可保存的最大元素个数，数组恰好有 capacity 个槽。 */
    size_t front;    /* 队非空时，指向队头元素。 */
    size_t rear;     /* 指向下一个可写位置。 */
    size_t size;     /* 已保存元素数，0 表示空，capacity 表示满。 */
} CircularQueue;

static bool circular_queue_init(CircularQueue *queue, size_t capacity)
{
    if (queue == NULL) {
        return false;
    }
    *queue = (CircularQueue){NULL, 0, 0, 0, 0};

    if (capacity == 0) {
        return true;
    }
    if (capacity > SIZE_MAX / sizeof(queue->data[0])) {
        return false;
    }
    queue->data = malloc(capacity * sizeof(queue->data[0]));
    if (queue->data == NULL) {
        return false;
    }
    queue->capacity = capacity;
    return true;
}

static void circular_queue_destroy(CircularQueue *queue)
{
    if (queue == NULL) {
        return;
    }
    free(queue->data);
    *queue = (CircularQueue){NULL, 0, 0, 0, 0};
}

static bool circular_queue_enqueue(CircularQueue *queue, int value)
{
    if (queue == NULL || queue->size == queue->capacity) {
        return false;
    }

    queue->data[queue->rear] = value;
    queue->rear = (queue->rear + 1) % queue->capacity;
    ++queue->size;
    return true;
}

static bool circular_queue_dequeue(CircularQueue *queue, int *value)
{
    if (queue == NULL || queue->size == 0) {
        return false;
    }

    if (value != NULL) {
        *value = queue->data[queue->front];
    }
    queue->front = (queue->front + 1) % queue->capacity;
    --queue->size;
    return true;
}

static bool circular_queue_front(const CircularQueue *queue, int *value)
{
    if (queue == NULL || queue->size == 0) {
        return false;
    }
    if (value != NULL) {
        *value = queue->data[queue->front];
    }
    return true;
}

/* ========================= 单服务台离散事件模拟 ======================== */

typedef struct {
    int id;
    int arrival_time;
    int service_time;
} Customer;

typedef enum {
    EVENT_DEPARTURE,
    EVENT_ARRIVAL
} EventType;

typedef struct {
    int time;
    EventType type;
    size_t customer_index;
    size_t sequence;
} ScheduledEvent;

typedef struct EventNode {
    ScheduledEvent event;
    struct EventNode *next;
} EventNode;

typedef struct {
    EventNode *head;
    size_t size;
    size_t next_sequence;
} EventCalendar;

typedef struct {
    size_t served_customers;
    long long total_waiting_time;
    int finish_time;
    size_t max_waiting_queue;
} SimulationResult;

static void event_calendar_init(EventCalendar *calendar)
{
    assert(calendar != NULL);
    *calendar = (EventCalendar){NULL, 0, 0};
}

static void event_calendar_destroy(EventCalendar *calendar)
{
    if (calendar == NULL) {
        return;
    }
    EventNode *node = calendar->head;
    while (node != NULL) {
        EventNode *next = node->next;
        free(node);
        node = next;
    }
    event_calendar_init(calendar);
}

/*
 * 同一时刻先处理离开，再处理到达：服务台在 t 时刻完成服务后，t 时刻
 * 到达的新客户可以立即看到已经空出的服务台。其余同键事件保持插入顺序。
 */
static bool event_comes_before(const ScheduledEvent *left,
                               const ScheduledEvent *right)
{
    if (left->time != right->time) {
        return left->time < right->time;
    }
    if (left->type != right->type) {
        return left->type < right->type;
    }
    return left->sequence < right->sequence;
}

/* 有序事件表是教学用实现；大规模模拟通常会用最小堆保存事件。 */
static bool event_calendar_schedule(EventCalendar *calendar, int time,
                                    EventType type, size_t customer_index)
{
    if (calendar == NULL || time < 0 || calendar->size == SIZE_MAX ||
        calendar->next_sequence == SIZE_MAX) {
        return false;
    }

    EventNode *node = malloc(sizeof(*node));
    if (node == NULL) {
        return false;
    }
    node->event = (ScheduledEvent){time, type, customer_index,
                                   calendar->next_sequence++};
    node->next = NULL;

    EventNode **link = &calendar->head;
    while (*link != NULL && event_comes_before(&(*link)->event,
                                                &node->event)) {
        link = &(*link)->next;
    }
    node->next = *link;
    *link = node;
    ++calendar->size;
    return true;
}

static bool event_calendar_pop(EventCalendar *calendar,
                               ScheduledEvent *event)
{
    if (calendar == NULL || calendar->head == NULL) {
        return false;
    }

    EventNode *first = calendar->head;
    calendar->head = first->next;
    if (event != NULL) {
        *event = first->event;
    }
    free(first);
    --calendar->size;
    return true;
}

static bool schedule_departure(EventCalendar *calendar, int start_time,
                               const Customer *customer,
                               size_t customer_index)
{
    if (customer == NULL || customer->service_time <= 0 ||
        start_time > INT_MAX - customer->service_time) {
        return false;
    }
    return event_calendar_schedule(calendar,
                                   start_time + customer->service_time,
                                   EVENT_DEPARTURE, customer_index);
}

/*
 * customers 只在调用期间被借用，不会被保存或修改；事件和等待队列保存的
 * 是 customer 的 0 基下标。输入到达时刻可以无序，事件日历会按时间重排。
 * 成功时才写入 out。
 */
static bool simulate_single_server(const Customer customers[], size_t count,
                                   SimulationResult *out)
{
    if (out == NULL || (customers == NULL && count != 0) ||
        count > (size_t)INT_MAX) {
        return false;
    }

    SimulationResult result = {0, 0, 0, 0};
    EventCalendar calendar;
    LinkQueue waiting;
    event_calendar_init(&calendar);
    link_queue_init(&waiting);

    for (size_t i = 0; i < count; ++i) {
        if (customers[i].arrival_time < 0 || customers[i].service_time <= 0 ||
            !event_calendar_schedule(&calendar, customers[i].arrival_time,
                                     EVENT_ARRIVAL, i)) {
            goto failure;
        }
    }

    bool server_busy = false;
    ScheduledEvent event;
    while (event_calendar_pop(&calendar, &event)) {
        assert(event.customer_index < count);
        const Customer *customer = &customers[event.customer_index];

        if (event.type == EVENT_ARRIVAL) {
            if (!server_busy) {
                if (!schedule_departure(&calendar, event.time, customer,
                                        event.customer_index)) {
                    goto failure;
                }
                server_busy = true;
            } else {
                if (!link_queue_enqueue(&waiting,
                                        (int)event.customer_index)) {
                    goto failure;
                }
                if (waiting.size > result.max_waiting_queue) {
                    result.max_waiting_queue = waiting.size;
                }
            }
            continue;
        }

        assert(server_busy && event.type == EVENT_DEPARTURE);
        ++result.served_customers;
        result.finish_time = event.time;

        int next_index_as_int;
        if (link_queue_dequeue(&waiting, &next_index_as_int)) {
            assert(next_index_as_int >= 0);
            const size_t next_index = (size_t)next_index_as_int;
            assert(next_index < count);
            const Customer *next_customer = &customers[next_index];
            const int waiting_time =
                event.time - next_customer->arrival_time;
            assert(waiting_time >= 0);
            if (result.total_waiting_time >
                LLONG_MAX - (long long)waiting_time) {
                goto failure;
            }
            result.total_waiting_time += waiting_time;
            if (!schedule_departure(&calendar, event.time, next_customer,
                                    next_index)) {
                goto failure;
            }
        } else {
            server_busy = false;
        }
    }

    assert(!server_busy && waiting.size == 0);
    assert(result.served_customers == count);
    link_queue_destroy(&waiting);
    event_calendar_destroy(&calendar);
    *out = result;
    return true;

failure:
    link_queue_destroy(&waiting);
    event_calendar_destroy(&calendar);
    return false;
}

/* ================================ 测试 ================================= */

static void test_dynamic_stack(void)
{
    IntStack stack;
    assert(int_stack_init(&stack, 0));
    assert(stack.size == 0 && stack.capacity == 0);
    assert(int_stack_peek(&stack) == NULL);
    assert(!int_stack_pop(&stack, NULL));

    for (long long value = 0; value < 40; ++value) {
        assert(int_stack_push(&stack, value));
        assert(int_stack_peek(&stack) != NULL);
        assert(*int_stack_peek(&stack) == value);
    }
    assert(stack.size == 40 && stack.capacity >= 40);

    for (long long expected = 39; expected >= 0; --expected) {
        long long actual = -1;
        assert(int_stack_pop(&stack, &actual));
        assert(actual == expected);
    }
    assert(stack.size == 0 && int_stack_peek(&stack) == NULL);
    assert(!int_stack_pop(&stack, NULL));

    int_stack_destroy(&stack);
    assert(stack.data == NULL && stack.size == 0 && stack.capacity == 0);
}

static void test_bracket_matching(void)
{
    BracketResult result = match_brackets("call(a[2], {x + y})");
    assert(result.status == BRACKETS_OK);
    assert(result.error_position == SIZE_MAX);

    result = match_brackets("abc]");
    assert(result.status == BRACKETS_UNEXPECTED_CLOSING);
    assert(result.error_position == 3 && result.found == ']');

    result = match_brackets("{[)]}");
    assert(result.status == BRACKETS_WRONG_CLOSING);
    assert(result.error_position == 2);
    assert(result.expected == ']' && result.found == ')');

    result = match_brackets("x({");
    assert(result.status == BRACKETS_UNCLOSED_OPENING);
    assert(result.error_position == 2 && result.expected == '}');

    result = match_brackets(NULL);
    assert(result.status == BRACKETS_INVALID_ARGUMENT);
}

static void assert_eval_ok(const char *expression, long long expected)
{
    const EvalResult result = evaluate_infix(expression);
    assert(result.status == EVAL_OK);
    assert(result.value == expected);
    assert(result.error_position == SIZE_MAX);
}

static void test_expression_evaluation(void)
{
    assert_eval_ok("2 + 3 * (4 + 5)", 29);
    assert_eval_ok("18 / (3 * 2)", 3);
    assert_eval_ok("8 - 10 / 2", 3);
    assert_eval_ok("7 - 10", -3);
    assert_eval_ok("8 / 3", 2);
    assert_eval_ok("((42))", 42);
    assert_eval_ok("  12\t+\n3 * (7 - 2) ", 27);

    EvalResult result = evaluate_infix("   ");
    assert(result.status == EVAL_EMPTY && result.error_position == 3);

    result = evaluate_infix("2 +");
    assert(result.status == EVAL_SYNTAX_ERROR);
    assert(result.error_position == 3);

    result = evaluate_infix("(2 + 3");
    assert(result.status == EVAL_UNMATCHED_PAREN);
    assert(result.error_position == 0);

    result = evaluate_infix("2 + 3)");
    assert(result.status == EVAL_UNMATCHED_PAREN);
    assert(result.error_position == 5);

    result = evaluate_infix("2(3)");
    assert(result.status == EVAL_SYNTAX_ERROR);
    assert(result.error_position == 1);

    result = evaluate_infix("()");
    assert(result.status == EVAL_SYNTAX_ERROR);
    assert(result.error_position == 1);

    result = evaluate_infix("-2 + 3"); /* 一元负号不在本例语法内。 */
    assert(result.status == EVAL_SYNTAX_ERROR);
    assert(result.error_position == 0);

    result = evaluate_infix("10 / (3 - 3)");
    assert(result.status == EVAL_DIVIDE_BY_ZERO);
    assert(result.error_position == 3);

    result = evaluate_infix("9223372036854775808");
    assert(result.status == EVAL_INTEGER_OVERFLOW);
    assert(result.error_position == 0);

    result = evaluate_infix("9223372036854775807 + 1");
    assert(result.status == EVAL_INTEGER_OVERFLOW);
    assert(result.error_position == 20);

    result = evaluate_infix(
        "(0 - 9223372036854775807 - 1) * (0 - 1)");
    assert(result.status == EVAL_INTEGER_OVERFLOW);
    assert(result.error_position == 30);

    result = evaluate_infix(
        "(0 - 9223372036854775807 - 1) / (0 - 1)");
    assert(result.status == EVAL_INTEGER_OVERFLOW);
    assert(result.error_position == 30);

    result = evaluate_infix(NULL);
    assert(result.status == EVAL_INVALID_ARGUMENT);
}

static void test_link_queue(void)
{
    LinkQueue queue;
    link_queue_init(&queue);
    assert(queue.size == 0 && queue.front == &queue.sentinel &&
           queue.rear == &queue.sentinel && queue.front->next == NULL);
    assert(!link_queue_front(&queue, NULL));
    assert(!link_queue_dequeue(&queue, NULL));

    assert(link_queue_enqueue(&queue, 10));
    assert(link_queue_enqueue(&queue, 20));
    assert(link_queue_enqueue(&queue, 30));
    int value = 0;
    assert(link_queue_front(&queue, &value) && value == 10);
    assert(link_queue_dequeue(&queue, &value) && value == 10);
    assert(link_queue_dequeue(&queue, &value) && value == 20);
    assert(link_queue_dequeue(&queue, &value) && value == 30);
    assert(queue.size == 0 && queue.front == &queue.sentinel &&
           queue.rear == &queue.sentinel && queue.front->next == NULL);

    assert(link_queue_enqueue(&queue, 99));
    assert(link_queue_dequeue(&queue, &value) && value == 99);
    link_queue_destroy(&queue);
}

static void test_circular_queue(void)
{
    CircularQueue queue;
    assert(circular_queue_init(&queue, 3));
    assert(!circular_queue_front(&queue, NULL));
    assert(!circular_queue_dequeue(&queue, NULL));

    assert(circular_queue_enqueue(&queue, 1));
    assert(circular_queue_enqueue(&queue, 2));
    assert(circular_queue_enqueue(&queue, 3));
    assert(queue.size == 3);
    assert(!circular_queue_enqueue(&queue, 4)); /* 三个槽全部可用。 */

    int value = 0;
    assert(circular_queue_dequeue(&queue, &value) && value == 1);
    assert(circular_queue_dequeue(&queue, &value) && value == 2);
    assert(circular_queue_enqueue(&queue, 4));
    assert(circular_queue_enqueue(&queue, 5)); /* 尾下标已经环回。 */

    const int expected[] = {3, 4, 5};
    for (size_t i = 0; i < 3; ++i) {
        assert(circular_queue_front(&queue, &value));
        assert(value == expected[i]);
        assert(circular_queue_dequeue(&queue, &value));
    }
    assert(queue.size == 0);
    assert(!circular_queue_dequeue(&queue, NULL));
    circular_queue_destroy(&queue);

    CircularQueue zero_capacity;
    assert(circular_queue_init(&zero_capacity, 0));
    assert(!circular_queue_enqueue(&zero_capacity, 1));
    assert(!circular_queue_dequeue(&zero_capacity, NULL));
    circular_queue_destroy(&zero_capacity);
}

static void test_event_simulation(void)
{
    /* 输入故意不完全按到达时间排列，事件日历会自行排序。 */
    const Customer customers[] = {
        {101, 0, 4},
        {104, 8, 1},
        {102, 1, 3},
        {103, 2, 2},
    };
    SimulationResult result;
    assert(simulate_single_server(
        customers, sizeof(customers) / sizeof(customers[0]), &result));
    assert(result.served_customers == 4);
    assert(result.total_waiting_time == 9); /* 0 + 3 + 5 + 1 */
    assert(result.finish_time == 10);
    assert(result.max_waiting_queue == 2);

    /* 离开与到达同为时刻 2；先处理离开，所以第二位客户无需等待。 */
    const Customer tie_at_time_two[] = {{1, 0, 2}, {2, 2, 1}};
    assert(simulate_single_server(tie_at_time_two, 2, &result));
    assert(result.served_customers == 2);
    assert(result.total_waiting_time == 0);
    assert(result.finish_time == 3 && result.max_waiting_queue == 0);

    assert(simulate_single_server(NULL, 0, &result));
    assert(result.served_customers == 0);
    assert(result.total_waiting_time == 0);
    assert(result.finish_time == 0 && result.max_waiting_queue == 0);

    const Customer invalid[] = {{1, -1, 2}};
    assert(!simulate_single_server(invalid, 1, &result));
    assert(!simulate_single_server(customers, 4, NULL));
}

static void run_boundary_tests(void)
{
    test_dynamic_stack();
    test_bracket_matching();
    test_expression_evaluation();
    test_link_queue();
    test_circular_queue();
    test_event_simulation();
}

/* ================================ 演示 ================================= */

int main(void)
{
    run_boundary_tests();

    IntStack stack;
    assert(int_stack_init(&stack, 0));
    assert(int_stack_push(&stack, 10));
    assert(int_stack_push(&stack, 20));
    assert(int_stack_push(&stack, 30));
    long long top = 0;
    assert(int_stack_pop(&stack, &top));
    printf("顺序栈弹出：%lld（后进先出）\n", top);

    const char *bracket_text = "if (a[2} > 0)";
    const BracketResult brackets = match_brackets(bracket_text);
    printf("括号检查：位置 %zu 期待 '%c'，实际 '%c'\n",
           brackets.error_position, brackets.expected, brackets.found);

    const char *expression = "12 + 3 * (7 - 2)";
    const EvalResult evaluated = evaluate_infix(expression);
    assert(evaluated.status == EVAL_OK);
    printf("表达式 %s = %lld\n", expression, evaluated.value);

    CircularQueue ring;
    assert(circular_queue_init(&ring, 3));
    assert(circular_queue_enqueue(&ring, 1));
    assert(circular_queue_enqueue(&ring, 2));
    int dequeued = 0;
    assert(circular_queue_dequeue(&ring, &dequeued));
    assert(circular_queue_enqueue(&ring, 3));
    assert(circular_queue_enqueue(&ring, 4)); /* 写入已经腾出的数组槽。 */
    printf("循环队列环回后：size=%zu, front=%zu, rear=%zu\n", ring.size,
           ring.front, ring.rear);

    const Customer customers[] = {
        {101, 0, 4}, {102, 1, 3}, {103, 2, 2}, {104, 8, 1},
    };
    SimulationResult simulation;
    assert(simulate_single_server(
        customers, sizeof(customers) / sizeof(customers[0]), &simulation));
    printf("服务台模拟：服务 %zu 人，总等待 %lld，结束时刻 %d，"
           "最长队列 %zu\n",
           simulation.served_customers, simulation.total_waiting_time,
           simulation.finish_time, simulation.max_waiting_queue);

    puts("边界测试：全部通过。");

    int_stack_destroy(&stack);
    circular_queue_destroy(&ring);
    return 0;
}
