#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int id;
    const char *name;
    int score;
} Student;

typedef struct {
    const Student *student;
    size_t key_comparisons;
} SearchResult;

static const Student STUDENTS[] = {
    {2024007, "赵敏", 91},
    {2024002, "李雷", 86},
    {2024009, "韩梅梅", 94},
    {2024001, "王强", 78},
    {2024006, "陈晨", 88},
    {2024003, "刘洋", 82},
    {2024008, "周宁", 90},
    {2024004, "孙悦", 85},
};

#define STUDENT_COUNT (sizeof(STUDENTS) / sizeof(STUDENTS[0]))

/*
 * 本例把“比较一次”定义为：取出一条记录，用它的学号和目标学号
 * 做一次三路比较（小于、等于或大于）。这是算法层面的关键字比较，
 * 不等同于处理器实际执行的指令条数。
 */
static int compare_id(int record_id, int target_id)
{
    return (record_id > target_id) - (record_id < target_id);
}

static SearchResult linear_search(const Student records[], size_t count,
                                  int target_id)
{
    SearchResult result = {NULL, 0};

    for (size_t i = 0; i < count; ++i) {
        ++result.key_comparisons;
        if (compare_id(records[i].id, target_id) == 0) {
            result.student = &records[i];
            break;
        }
    }

    return result;
}

/* records 必须已经按 id 非递减排列。 */
static SearchResult binary_search_by_id(const Student records[], size_t count,
                                        int target_id)
{
    SearchResult result = {NULL, 0};
    size_t left = 0;
    size_t right = count; /* 搜索区间为 [left, right)。 */

    while (left < right) {
        const size_t middle = left + (right - left) / 2;
        ++result.key_comparisons;
        const int relation = compare_id(records[middle].id, target_id);

        if (relation == 0) {
            result.student = &records[middle];
            break;
        }
        if (relation < 0) {
            left = middle + 1;
        } else {
            right = middle;
        }
    }

    return result;
}

static int compare_students_for_qsort(const void *left, const void *right)
{
    const Student *a = left;
    const Student *b = right;

    return (a->id > b->id) - (a->id < b->id);
}

static void sort_students_by_id(Student records[], size_t count)
{
    qsort(records, count, sizeof(records[0]), compare_students_for_qsort);
}

static bool is_sorted_by_id(const Student records[], size_t count)
{
    for (size_t i = 1; i < count; ++i) {
        if (records[i - 1].id > records[i].id) {
            return false;
        }
    }
    return true;
}

static void assert_found(const SearchResult result, int expected_id)
{
    assert(result.student != NULL);
    assert(result.student->id == expected_id);
    assert(result.key_comparisons > 0);
}

static void run_boundary_tests(void)
{
    Student sorted[STUDENT_COUNT];

    memcpy(sorted, STUDENTS, sizeof(STUDENTS));
    sort_students_by_id(sorted, STUDENT_COUNT);
    assert(is_sorted_by_id(sorted, STUDENT_COUNT));

    /* 空数组不会解引用 records；两种查找均应返回 0 次比较。 */
    SearchResult result = linear_search(NULL, 0, 1);
    assert(result.student == NULL && result.key_comparisons == 0);
    result = binary_search_by_id(NULL, 0, 1);
    assert(result.student == NULL && result.key_comparisons == 0);

    /* 顺序查找：首条、末条和不存在三种边界。 */
    result = linear_search(STUDENTS, STUDENT_COUNT, STUDENTS[0].id);
    assert_found(result, STUDENTS[0].id);
    assert(result.key_comparisons == 1);

    result = linear_search(STUDENTS, STUDENT_COUNT,
                           STUDENTS[STUDENT_COUNT - 1].id);
    assert_found(result, STUDENTS[STUDENT_COUNT - 1].id);
    assert(result.key_comparisons == STUDENT_COUNT);

    result = linear_search(STUDENTS, STUDENT_COUNT, 9999999);
    assert(result.student == NULL);
    assert(result.key_comparisons == STUDENT_COUNT);

    /* 二分查找：有序数组的最小值、最大值和不存在三种边界。 */
    result = binary_search_by_id(sorted, STUDENT_COUNT, sorted[0].id);
    assert_found(result, sorted[0].id);
    assert(result.key_comparisons <= 4);

    result = binary_search_by_id(sorted, STUDENT_COUNT,
                                 sorted[STUDENT_COUNT - 1].id);
    assert_found(result, sorted[STUDENT_COUNT - 1].id);
    assert(result.key_comparisons <= 4);

    result = binary_search_by_id(sorted, STUDENT_COUNT, 2024005);
    assert(result.student == NULL);
    assert(result.key_comparisons <= 4);

    /*
     * 学号重复时，本二分实现只保证找到“某一条”，不保证第一条。
     * 若业务要求第一条，应改用 lower_bound 风格的边界查找。
     */
    const Student duplicates[] = {
        {1, "甲", 80},
        {2, "乙", 81},
        {2, "丙", 82},
        {3, "丁", 83},
    };
    assert_found(linear_search(duplicates, 4, 2), 2);
    assert_found(binary_search_by_id(duplicates, 4, 2), 2);
}

static void print_result(const char *algorithm, int target_id,
                         SearchResult result)
{
    printf("%-12s 目标=%d  ", algorithm, target_id);
    if (result.student != NULL) {
        printf("找到 %-8s 分数=%d  ", result.student->name,
               result.student->score);
    } else {
        printf("未找到                 ");
    }
    printf("关键字比较=%zu 次\n", result.key_comparisons);
}

int main(void)
{
    Student sorted[STUDENT_COUNT];
    const int targets[] = {2024007, 2024004, 2024005};

    run_boundary_tests();

    memcpy(sorted, STUDENTS, sizeof(STUDENTS));
    sort_students_by_id(sorted, STUDENT_COUNT);

    puts("同一批学生记录的查找结果：");
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
        print_result("顺序查找", targets[i],
                     linear_search(STUDENTS, STUDENT_COUNT, targets[i]));
        print_result("二分查找", targets[i],
                     binary_search_by_id(sorted, STUDENT_COUNT, targets[i]));
        putchar('\n');
    }

    puts("说明：二分查找的比较次数不包含排序成本；");
    puts("若数据尚未有序且只查一次，先排序未必划算。比较次数也不等于运行时间。");
    puts("边界测试：全部通过。");

    return 0;
}
