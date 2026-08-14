/*
 * 第 12 章：文件和索引结构
 *
 * C11 单文件教学示例，覆盖：
 *   - 定长记录文件、RRN 与顺序扫描
 *   - 稠密有序索引和二叉排序树索引
 *   - ISAM 风格的主区、稀疏索引和溢出链
 *   - 小型 B+ 树的等值查找、范围查找、叶分裂和内部结点分裂
 *   - 拉链法散列文件
 *   - 多重表文件
 *   - 散列词典加倒排表，以及 AND 查询
 *
 * 这是用于解释访问路径的内存/临时文件模型，不是数据库存储引擎。
 * 真实系统还要处理页缓存、日志、并发控制、崩溃恢复和磁盘格式兼容。
 */

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    DEPT_SIZE = 12,
    STATUS_SIZE = 12,
    TEXT_SIZE = 80,
    ISAM_PAGE_COUNT = 3,
    ISAM_PRIMARY_CAP = 3,
    BPLUS_MAX_KEYS = 3,
    HASH_BUCKETS = 7,
    MULTI_RECORDS = 4,
    TERM_BUCKETS = 17,
    TERM_SIZE = 24,
    MAX_POSTINGS = 32
};

typedef struct {
    int32_t key;
    char dept[DEPT_SIZE];
    char status[STATUS_SIZE];
    char text[TEXT_SIZE];
} Record;

typedef struct {
    FILE *file;
    size_t count;
} RecordFile;

static Record record_make(int32_t key, const char *dept, const char *status,
                          const char *text)
{
    Record record;
    (void)memset(&record, 0, sizeof(record));
    record.key = key;
    (void)snprintf(record.dept, sizeof(record.dept), "%s", dept);
    (void)snprintf(record.status, sizeof(record.status), "%s", status);
    (void)snprintf(record.text, sizeof(record.text), "%s", text);
    return record;
}

static bool record_file_open(RecordFile *store)
{
    store->file = tmpfile();
    store->count = 0U;
    return store->file != NULL;
}

static void record_file_close(RecordFile *store)
{
    if (store->file != NULL) {
        (void)fclose(store->file);
    }
    store->file = NULL;
    store->count = 0U;
}

static bool record_file_append(RecordFile *store, const Record *record,
                               size_t *rrn_out)
{
    if (store->file == NULL || store->count > (size_t)LONG_MAX / sizeof(*record)) {
        return false;
    }
    if (fseek(store->file, 0L, SEEK_END) != 0) {
        return false;
    }
    if (fwrite(record, sizeof(*record), 1U, store->file) != 1U) {
        return false;
    }
    if (fflush(store->file) != 0) {
        return false;
    }
    if (rrn_out != NULL) {
        *rrn_out = store->count;
    }
    ++store->count;
    return true;
}

static bool record_file_read(RecordFile *store, size_t rrn, Record *record_out)
{
    if (store->file == NULL || rrn >= store->count ||
        rrn > (size_t)LONG_MAX / sizeof(*record_out)) {
        return false;
    }
    long offset = (long)(rrn * sizeof(*record_out));
    if (fseek(store->file, offset, SEEK_SET) != 0) {
        return false;
    }
    return fread(record_out, sizeof(*record_out), 1U, store->file) == 1U;
}

static bool record_file_sequential_find(RecordFile *store, int32_t key,
                                        size_t *rrn_out, size_t *visited_out)
{
    size_t visited = 0U;
    for (size_t rrn = 0U; rrn < store->count; ++rrn) {
        Record record;
        if (!record_file_read(store, rrn, &record)) {
            return false;
        }
        ++visited;
        if (record.key == key) {
            if (rrn_out != NULL) {
                *rrn_out = rrn;
            }
            if (visited_out != NULL) {
                *visited_out = visited;
            }
            return true;
        }
    }
    if (visited_out != NULL) {
        *visited_out = visited;
    }
    return false;
}

typedef struct {
    int32_t key;
    size_t rrn;
} IndexEntry;

typedef struct {
    IndexEntry *entries;
    size_t count;
} DenseIndex;

static int index_entry_compare(const void *lhs, const void *rhs)
{
    const IndexEntry *a = (const IndexEntry *)lhs;
    const IndexEntry *b = (const IndexEntry *)rhs;
    if (a->key < b->key) {
        return -1;
    }
    if (a->key > b->key) {
        return 1;
    }
    if (a->rrn < b->rrn) {
        return -1;
    }
    return a->rrn > b->rrn ? 1 : 0;
}

static bool dense_index_build(RecordFile *store, DenseIndex *index)
{
    index->entries = NULL;
    index->count = 0U;
    if (store->count == 0U) {
        return true;
    }
    if (store->count > SIZE_MAX / sizeof(index->entries[0])) {
        return false;
    }
    index->entries = (IndexEntry *)malloc(store->count * sizeof(index->entries[0]));
    if (index->entries == NULL) {
        return false;
    }
    for (size_t rrn = 0U; rrn < store->count; ++rrn) {
        Record record;
        if (!record_file_read(store, rrn, &record)) {
            free(index->entries);
            index->entries = NULL;
            return false;
        }
        index->entries[rrn] = (IndexEntry){record.key, rrn};
    }
    index->count = store->count;
    qsort(index->entries, index->count, sizeof(index->entries[0]),
          index_entry_compare);
    return true;
}

static void dense_index_destroy(DenseIndex *index)
{
    free(index->entries);
    index->entries = NULL;
    index->count = 0U;
}

static bool dense_index_find(const DenseIndex *index, int32_t key,
                             size_t *rrn_out)
{
    size_t low = 0U;
    size_t high = index->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2U;
        if (index->entries[mid].key < key) {
            low = mid + 1U;
        } else {
            high = mid;
        }
    }
    if (low == index->count || index->entries[low].key != key) {
        return false;
    }
    if (rrn_out != NULL) {
        *rrn_out = index->entries[low].rrn;
    }
    return true;
}

typedef struct BstIndexNode {
    int32_t key;
    size_t rrn;
    struct BstIndexNode *left;
    struct BstIndexNode *right;
} BstIndexNode;

static bool bst_index_insert(BstIndexNode **root, int32_t key, size_t rrn)
{
    while (*root != NULL) {
        if (key == (*root)->key) {
            return false;
        }
        root = key < (*root)->key ? &(*root)->left : &(*root)->right;
    }
    BstIndexNode *node = (BstIndexNode *)calloc(1U, sizeof(*node));
    if (node == NULL) {
        return false;
    }
    node->key = key;
    node->rrn = rrn;
    *root = node;
    return true;
}

static bool bst_index_find(const BstIndexNode *root, int32_t key,
                           size_t *rrn_out, size_t *visited_out)
{
    size_t visited = 0U;
    while (root != NULL) {
        ++visited;
        if (key == root->key) {
            if (rrn_out != NULL) {
                *rrn_out = root->rrn;
            }
            if (visited_out != NULL) {
                *visited_out = visited;
            }
            return true;
        }
        root = key < root->key ? root->left : root->right;
    }
    if (visited_out != NULL) {
        *visited_out = visited;
    }
    return false;
}

static void bst_index_destroy(BstIndexNode *root)
{
    if (root == NULL) {
        return;
    }
    bst_index_destroy(root->left);
    bst_index_destroy(root->right);
    free(root);
}

typedef struct OverflowRecord {
    int32_t key;
    size_t rrn;
    struct OverflowRecord *next;
} OverflowRecord;

typedef struct {
    int32_t primary_keys[ISAM_PRIMARY_CAP];
    size_t primary_rrns[ISAM_PRIMARY_CAP];
    size_t primary_count;
    OverflowRecord *overflow;
} IsamPage;

typedef struct {
    int32_t first_key[ISAM_PAGE_COUNT];
    IsamPage pages[ISAM_PAGE_COUNT];
} IsamFile;

static void isam_init(IsamFile *isam)
{
    (void)memset(isam, 0, sizeof(*isam));
    const int32_t values[ISAM_PAGE_COUNT][ISAM_PRIMARY_CAP] = {
        {10, 20, 30}, {40, 50, 60}, {70, 80, 90}
    };
    for (size_t p = 0U; p < ISAM_PAGE_COUNT; ++p) {
        isam->first_key[p] = values[p][0];
        isam->pages[p].primary_count = ISAM_PRIMARY_CAP;
        for (size_t i = 0U; i < ISAM_PRIMARY_CAP; ++i) {
            isam->pages[p].primary_keys[i] = values[p][i];
            isam->pages[p].primary_rrns[i] = p * ISAM_PRIMARY_CAP + i;
        }
    }
}

static size_t isam_choose_page(const IsamFile *isam, int32_t key)
{
    size_t page = 0U;
    for (size_t i = 1U; i < ISAM_PAGE_COUNT; ++i) {
        if (key < isam->first_key[i]) {
            break;
        }
        page = i;
    }
    return page;
}

static bool isam_find(const IsamFile *isam, int32_t key, size_t *rrn_out,
                      bool *used_overflow_out)
{
    size_t page_no = isam_choose_page(isam, key);
    const IsamPage *page = &isam->pages[page_no];
    for (size_t i = 0U; i < page->primary_count; ++i) {
        if (page->primary_keys[i] == key) {
            if (rrn_out != NULL) {
                *rrn_out = page->primary_rrns[i];
            }
            if (used_overflow_out != NULL) {
                *used_overflow_out = false;
            }
            return true;
        }
    }
    for (const OverflowRecord *node = page->overflow; node != NULL;
         node = node->next) {
        if (node->key == key) {
            if (rrn_out != NULL) {
                *rrn_out = node->rrn;
            }
            if (used_overflow_out != NULL) {
                *used_overflow_out = true;
            }
            return true;
        }
    }
    return false;
}

static bool isam_insert_overflow(IsamFile *isam, int32_t key, size_t rrn)
{
    if (isam_find(isam, key, NULL, NULL)) {
        return false;
    }
    size_t page_no = isam_choose_page(isam, key);
    OverflowRecord **cursor = &isam->pages[page_no].overflow;
    while (*cursor != NULL && (*cursor)->key < key) {
        cursor = &(*cursor)->next;
    }
    OverflowRecord *node = (OverflowRecord *)malloc(sizeof(*node));
    if (node == NULL) {
        return false;
    }
    *node = (OverflowRecord){key, rrn, *cursor};
    *cursor = node;
    return true;
}

static void isam_destroy(IsamFile *isam)
{
    for (size_t p = 0U; p < ISAM_PAGE_COUNT; ++p) {
        OverflowRecord *node = isam->pages[p].overflow;
        while (node != NULL) {
            OverflowRecord *next = node->next;
            free(node);
            node = next;
        }
        isam->pages[p].overflow = NULL;
    }
}

typedef struct BPlusNode {
    bool leaf;
    size_t nkeys;
    int32_t keys[BPLUS_MAX_KEYS + 1];
    size_t rrns[BPLUS_MAX_KEYS + 1];
    struct BPlusNode *children[BPLUS_MAX_KEYS + 2];
    struct BPlusNode *parent;
    struct BPlusNode *next;
} BPlusNode;

typedef struct {
    BPlusNode *root;
} BPlusTree;

static BPlusNode *bplus_node_new(bool leaf)
{
    BPlusNode *node = (BPlusNode *)calloc(1U, sizeof(*node));
    if (node != NULL) {
        node->leaf = leaf;
    }
    return node;
}

static BPlusNode *bplus_find_leaf(const BPlusTree *tree, int32_t key)
{
    BPlusNode *node = tree->root;
    if (node == NULL) {
        return NULL;
    }
    while (!node->leaf) {
        size_t child = 0U;
        while (child < node->nkeys && key >= node->keys[child]) {
            ++child;
        }
        node = node->children[child];
    }
    return node;
}

static bool bplus_find(const BPlusTree *tree, int32_t key, size_t *rrn_out)
{
    BPlusNode *leaf = bplus_find_leaf(tree, key);
    if (leaf == NULL) {
        return false;
    }
    for (size_t i = 0U; i < leaf->nkeys; ++i) {
        if (leaf->keys[i] == key) {
            if (rrn_out != NULL) {
                *rrn_out = leaf->rrns[i];
            }
            return true;
        }
    }
    return false;
}

static bool bplus_insert_parent(BPlusTree *tree, BPlusNode *left,
                                int32_t separator, BPlusNode *right);

static bool bplus_split_internal(BPlusTree *tree, BPlusNode *node)
{
    assert(!node->leaf && node->nkeys == BPLUS_MAX_KEYS + 1U);
    const size_t promote_at = node->nkeys / 2U;
    const int32_t promoted = node->keys[promote_at];
    BPlusNode *right = bplus_node_new(false);
    if (right == NULL) {
        return false;
    }
    right->parent = node->parent;
    right->nkeys = node->nkeys - promote_at - 1U;
    for (size_t i = 0U; i < right->nkeys; ++i) {
        right->keys[i] = node->keys[promote_at + 1U + i];
    }
    for (size_t i = 0U; i <= right->nkeys; ++i) {
        right->children[i] = node->children[promote_at + 1U + i];
        assert(right->children[i] != NULL);
        right->children[i]->parent = right;
    }
    node->nkeys = promote_at;
    if (!bplus_insert_parent(tree, node, promoted, right)) {
        free(right);
        return false;
    }
    return true;
}

static bool bplus_insert_parent(BPlusTree *tree, BPlusNode *left,
                                int32_t separator, BPlusNode *right)
{
    if (left->parent == NULL) {
        BPlusNode *root = bplus_node_new(false);
        if (root == NULL) {
            return false;
        }
        root->nkeys = 1U;
        root->keys[0] = separator;
        root->children[0] = left;
        root->children[1] = right;
        left->parent = root;
        right->parent = root;
        tree->root = root;
        return true;
    }

    BPlusNode *parent = left->parent;
    size_t left_pos = 0U;
    while (left_pos <= parent->nkeys && parent->children[left_pos] != left) {
        ++left_pos;
    }
    assert(left_pos <= parent->nkeys);
    for (size_t i = parent->nkeys; i > left_pos; --i) {
        parent->keys[i] = parent->keys[i - 1U];
        parent->children[i + 1U] = parent->children[i];
    }
    parent->keys[left_pos] = separator;
    parent->children[left_pos + 1U] = right;
    right->parent = parent;
    ++parent->nkeys;
    if (parent->nkeys > BPLUS_MAX_KEYS) {
        return bplus_split_internal(tree, parent);
    }
    return true;
}

static bool bplus_split_leaf(BPlusTree *tree, BPlusNode *leaf)
{
    assert(leaf->leaf && leaf->nkeys == BPLUS_MAX_KEYS + 1U);
    BPlusNode *right = bplus_node_new(true);
    if (right == NULL) {
        return false;
    }
    const size_t left_count = (leaf->nkeys + 1U) / 2U;
    right->nkeys = leaf->nkeys - left_count;
    for (size_t i = 0U; i < right->nkeys; ++i) {
        right->keys[i] = leaf->keys[left_count + i];
        right->rrns[i] = leaf->rrns[left_count + i];
    }
    leaf->nkeys = left_count;
    right->next = leaf->next;
    leaf->next = right;
    right->parent = leaf->parent;
    if (!bplus_insert_parent(tree, leaf, right->keys[0], right)) {
        leaf->next = right->next;
        free(right);
        return false;
    }
    return true;
}

static bool bplus_insert(BPlusTree *tree, int32_t key, size_t rrn)
{
    if (tree->root == NULL) {
        tree->root = bplus_node_new(true);
        if (tree->root == NULL) {
            return false;
        }
    }
    BPlusNode *leaf = bplus_find_leaf(tree, key);
    size_t pos = 0U;
    while (pos < leaf->nkeys && leaf->keys[pos] < key) {
        ++pos;
    }
    if (pos < leaf->nkeys && leaf->keys[pos] == key) {
        return false;
    }
    for (size_t i = leaf->nkeys; i > pos; --i) {
        leaf->keys[i] = leaf->keys[i - 1U];
        leaf->rrns[i] = leaf->rrns[i - 1U];
    }
    leaf->keys[pos] = key;
    leaf->rrns[pos] = rrn;
    ++leaf->nkeys;
    if (leaf->nkeys > BPLUS_MAX_KEYS) {
        return bplus_split_leaf(tree, leaf);
    }
    return true;
}

static size_t bplus_range(const BPlusTree *tree, int32_t low, int32_t high,
                          int32_t *keys_out, size_t capacity)
{
    if (low > high || tree->root == NULL) {
        return 0U;
    }
    BPlusNode *leaf = bplus_find_leaf(tree, low);
    size_t count = 0U;
    while (leaf != NULL) {
        for (size_t i = 0U; i < leaf->nkeys; ++i) {
            if (leaf->keys[i] < low) {
                continue;
            }
            if (leaf->keys[i] > high) {
                return count;
            }
            if (count < capacity) {
                keys_out[count] = leaf->keys[i];
            }
            ++count;
        }
        leaf = leaf->next;
    }
    return count;
}

static int32_t bplus_subtree_min(const BPlusNode *node)
{
    while (!node->leaf) {
        node = node->children[0];
    }
    assert(node->nkeys > 0U);
    return node->keys[0];
}

static size_t bplus_verify_node(const BPlusNode *node, bool is_root,
                                size_t depth, size_t *leaf_depth,
                                int32_t *previous, bool *has_previous)
{
    assert(node != NULL);
    assert(node->nkeys <= BPLUS_MAX_KEYS);
    if (!is_root) {
        assert(node->nkeys >= 1U);
    }
    for (size_t i = 1U; i < node->nkeys; ++i) {
        assert(node->keys[i - 1U] < node->keys[i]);
    }
    if (node->leaf) {
        if (*leaf_depth == SIZE_MAX) {
            *leaf_depth = depth;
        }
        assert(*leaf_depth == depth);
        for (size_t i = 0U; i < node->nkeys; ++i) {
            if (*has_previous) {
                assert(*previous < node->keys[i]);
            }
            *previous = node->keys[i];
            *has_previous = true;
        }
        return node->nkeys;
    }
    size_t total = 0U;
    for (size_t i = 0U; i <= node->nkeys; ++i) {
        assert(node->children[i] != NULL);
        assert(node->children[i]->parent == node);
        if (i > 0U) {
            assert(node->keys[i - 1U] == bplus_subtree_min(node->children[i]));
        }
        total += bplus_verify_node(node->children[i], false, depth + 1U,
                                   leaf_depth, previous, has_previous);
    }
    return total;
}

static size_t bplus_verify(const BPlusTree *tree)
{
    if (tree->root == NULL) {
        return 0U;
    }
    size_t leaf_depth = SIZE_MAX;
    int32_t previous = 0;
    bool has_previous = false;
    return bplus_verify_node(tree->root, true, 0U, &leaf_depth, &previous,
                             &has_previous);
}

static void bplus_destroy_node(BPlusNode *node)
{
    if (node == NULL) {
        return;
    }
    if (!node->leaf) {
        for (size_t i = 0U; i <= node->nkeys; ++i) {
            bplus_destroy_node(node->children[i]);
        }
    }
    free(node);
}

static void bplus_destroy(BPlusTree *tree)
{
    bplus_destroy_node(tree->root);
    tree->root = NULL;
}

typedef struct HashRecord {
    int32_t key;
    size_t rrn;
    struct HashRecord *next;
} HashRecord;

typedef struct {
    HashRecord *buckets[HASH_BUCKETS];
} HashFile;

static size_t hash_bucket_for(int32_t key)
{
    int32_t mod = key % HASH_BUCKETS;
    if (mod < 0) {
        mod += HASH_BUCKETS;
    }
    return (size_t)mod;
}

static bool hash_file_find(const HashFile *hash, int32_t key, size_t *rrn_out,
                           size_t *comparisons_out)
{
    size_t comparisons = 0U;
    for (const HashRecord *node = hash->buckets[hash_bucket_for(key)];
         node != NULL; node = node->next) {
        ++comparisons;
        if (node->key == key) {
            if (rrn_out != NULL) {
                *rrn_out = node->rrn;
            }
            if (comparisons_out != NULL) {
                *comparisons_out = comparisons;
            }
            return true;
        }
    }
    if (comparisons_out != NULL) {
        *comparisons_out = comparisons;
    }
    return false;
}

static bool hash_file_insert(HashFile *hash, int32_t key, size_t rrn)
{
    if (hash_file_find(hash, key, NULL, NULL)) {
        return false;
    }
    size_t bucket = hash_bucket_for(key);
    HashRecord *node = (HashRecord *)malloc(sizeof(*node));
    if (node == NULL) {
        return false;
    }
    *node = (HashRecord){key, rrn, hash->buckets[bucket]};
    hash->buckets[bucket] = node;
    return true;
}

static void hash_file_destroy(HashFile *hash)
{
    for (size_t i = 0U; i < HASH_BUCKETS; ++i) {
        HashRecord *node = hash->buckets[i];
        while (node != NULL) {
            HashRecord *next = node->next;
            free(node);
            node = next;
        }
        hash->buckets[i] = NULL;
    }
}

typedef struct {
    int id;
    const char *dept;
    const char *status;
    int next_dept;
    int next_status;
} MultiRecord;

typedef struct {
    MultiRecord records[MULTI_RECORDS];
    int ai_head;
    int db_head;
    int os_head;
    int active_head;
    int closed_head;
} MultiListFile;

static void multilist_init(MultiListFile *file)
{
    file->records[0] = (MultiRecord){101, "AI", "active", 2, 1};
    file->records[1] = (MultiRecord){102, "DB", "active", -1, 3};
    file->records[2] = (MultiRecord){103, "AI", "closed", -1, -1};
    file->records[3] = (MultiRecord){104, "OS", "active", -1, -1};
    file->ai_head = 0;
    file->db_head = 1;
    file->os_head = 3;
    file->active_head = 0;
    file->closed_head = 2;
}

static size_t multilist_collect_dept(const MultiListFile *file, int head,
                                     int *ids_out, size_t capacity)
{
    size_t count = 0U;
    for (int i = head; i >= 0; i = file->records[(size_t)i].next_dept) {
        assert((size_t)i < MULTI_RECORDS);
        if (count < capacity) {
            ids_out[count] = file->records[(size_t)i].id;
        }
        ++count;
    }
    return count;
}

static size_t multilist_collect_status(const MultiListFile *file, int head,
                                       int *ids_out, size_t capacity)
{
    size_t count = 0U;
    for (int i = head; i >= 0; i = file->records[(size_t)i].next_status) {
        assert((size_t)i < MULTI_RECORDS);
        if (count < capacity) {
            ids_out[count] = file->records[(size_t)i].id;
        }
        ++count;
    }
    return count;
}

typedef struct TermEntry {
    char term[TERM_SIZE];
    int doc_ids[MAX_POSTINGS];
    size_t count;
    struct TermEntry *next;
} TermEntry;

typedef struct {
    TermEntry *buckets[TERM_BUCKETS];
} InvertedIndex;

static uint32_t term_hash(const char *term)
{
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char *p = (const unsigned char *)term; *p != '\0'; ++p) {
        hash ^= (uint32_t)*p;
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static TermEntry *inverted_find_entry(const InvertedIndex *index,
                                      const char *term)
{
    size_t bucket = (size_t)(term_hash(term) % TERM_BUCKETS);
    for (TermEntry *entry = index->buckets[bucket]; entry != NULL;
         entry = entry->next) {
        if (strcmp(entry->term, term) == 0) {
            return entry;
        }
    }
    return NULL;
}

static bool inverted_add_term(InvertedIndex *index, const char *term, int doc_id)
{
    if (strlen(term) >= TERM_SIZE) {
        return false;
    }
    TermEntry *entry = inverted_find_entry(index, term);
    if (entry == NULL) {
        size_t bucket = (size_t)(term_hash(term) % TERM_BUCKETS);
        entry = (TermEntry *)calloc(1U, sizeof(*entry));
        if (entry == NULL) {
            return false;
        }
        (void)snprintf(entry->term, sizeof(entry->term), "%s", term);
        entry->next = index->buckets[bucket];
        index->buckets[bucket] = entry;
    }
    if (entry->count > 0U && entry->doc_ids[entry->count - 1U] == doc_id) {
        return true;
    }
    if (entry->count == MAX_POSTINGS ||
        (entry->count > 0U && entry->doc_ids[entry->count - 1U] > doc_id)) {
        return false;
    }
    entry->doc_ids[entry->count++] = doc_id;
    return true;
}

static bool inverted_add_document(InvertedIndex *index, int doc_id,
                                  const char *text)
{
    char token[TERM_SIZE];
    size_t len = 0U;
    for (const unsigned char *p = (const unsigned char *)text;; ++p) {
        bool token_char = *p != '\0' && (isalnum(*p) != 0 || *p == '+');
        if (token_char) {
            if (len + 1U >= sizeof(token)) {
                return false;
            }
            token[len++] = (char)tolower(*p);
        }
        if (!token_char && len > 0U) {
            token[len] = '\0';
            if (!inverted_add_term(index, token, doc_id)) {
                return false;
            }
            len = 0U;
        }
        if (*p == '\0') {
            break;
        }
    }
    return true;
}

static size_t postings_intersect(const TermEntry *a, const TermEntry *b,
                                 int *doc_ids_out, size_t capacity)
{
    if (a == NULL || b == NULL) {
        return 0U;
    }
    size_t i = 0U;
    size_t j = 0U;
    size_t count = 0U;
    while (i < a->count && j < b->count) {
        if (a->doc_ids[i] < b->doc_ids[j]) {
            ++i;
        } else if (a->doc_ids[i] > b->doc_ids[j]) {
            ++j;
        } else {
            if (count < capacity) {
                doc_ids_out[count] = a->doc_ids[i];
            }
            ++count;
            ++i;
            ++j;
        }
    }
    return count;
}

static void inverted_destroy(InvertedIndex *index)
{
    for (size_t i = 0U; i < TERM_BUCKETS; ++i) {
        TermEntry *entry = index->buckets[i];
        while (entry != NULL) {
            TermEntry *next = entry->next;
            free(entry);
            entry = next;
        }
        index->buckets[i] = NULL;
    }
}

static void test_record_and_basic_indexes(void)
{
    RecordFile store;
    assert(record_file_open(&store));
    const Record records[] = {
        record_make(105, "AI", "active", "vector index"),
        record_make(17, "DB", "active", "bplus page"),
        record_make(83, "OS", "closed", "page cache"),
        record_make(42, "Search", "active", "inverted index")
    };
    for (size_t i = 0U; i < sizeof(records) / sizeof(records[0]); ++i) {
        size_t rrn = SIZE_MAX;
        assert(record_file_append(&store, &records[i], &rrn));
        assert(rrn == i);
    }

    Record read_back;
    assert(record_file_read(&store, 3U, &read_back));
    assert(read_back.key == 42);
    assert(strcmp(read_back.text, "inverted index") == 0);
    assert(!record_file_read(&store, 4U, &read_back));

    size_t rrn = SIZE_MAX;
    size_t visited = 0U;
    assert(record_file_sequential_find(&store, 42, &rrn, &visited));
    assert(rrn == 3U && visited == 4U);

    DenseIndex dense;
    assert(dense_index_build(&store, &dense));
    assert(dense.count == 4U);
    assert(dense.entries[0].key == 17 && dense.entries[0].rrn == 1U);
    assert(dense.entries[1].key == 42 && dense.entries[1].rrn == 3U);
    assert(dense_index_find(&dense, 42, &rrn) && rrn == 3U);
    assert(!dense_index_find(&dense, 99, NULL));

    BstIndexNode *bst = NULL;
    const size_t insertion_order[] = {2U, 1U, 0U, 3U};
    for (size_t i = 0U; i < 4U; ++i) {
        size_t pos = insertion_order[i];
        assert(bst_index_insert(&bst, records[pos].key, pos));
    }
    assert(bst_index_find(bst, 42, &rrn, &visited));
    assert(rrn == 3U && visited == 3U);
    assert(!bst_index_find(bst, 99, NULL, NULL));

    bst_index_destroy(bst);
    dense_index_destroy(&dense);
    record_file_close(&store);
}

static void test_isam(void)
{
    IsamFile isam;
    isam_init(&isam);
    size_t rrn = SIZE_MAX;
    bool overflow = true;
    assert(isam_find(&isam, 50, &rrn, &overflow));
    assert(rrn == 4U && !overflow);
    assert(isam_insert_overflow(&isam, 55, 99U));
    assert(isam_find(&isam, 55, &rrn, &overflow));
    assert(rrn == 99U && overflow);
    assert(isam.pages[1].overflow != NULL);
    assert(isam.pages[1].overflow->key == 55);
    assert(!isam_insert_overflow(&isam, 55, 100U));
    assert(!isam_find(&isam, 66, NULL, NULL));
    isam_destroy(&isam);
}

static void test_bplus(void)
{
    BPlusTree tree = {NULL};
    const int32_t keys[] = {
        40, 10, 70, 20, 50, 80, 30, 60, 90, 55,
        5, 15, 25, 35, 45, 65, 75, 85, 95
    };
    const size_t count = sizeof(keys) / sizeof(keys[0]);
    for (size_t i = 0U; i < count; ++i) {
        assert(bplus_insert(&tree, keys[i], (size_t)keys[i]));
        assert(bplus_verify(&tree) == i + 1U);
    }
    assert(!bplus_insert(&tree, 55, 999U));
    for (size_t i = 0U; i < count; ++i) {
        size_t rrn = 0U;
        assert(bplus_find(&tree, keys[i], &rrn));
        assert(rrn == (size_t)keys[i]);
    }
    assert(!bplus_find(&tree, 54, NULL));

    int32_t range[16];
    size_t range_count = bplus_range(&tree, 45, 80, range, 16U);
    const int32_t expected[] = {45, 50, 55, 60, 65, 70, 75, 80};
    assert(range_count == sizeof(expected) / sizeof(expected[0]));
    assert(memcmp(range, expected, sizeof(expected)) == 0);
    assert(bplus_range(&tree, 80, 45, range, 16U) == 0U);
    bplus_destroy(&tree);

    /* 73 与素数 211 互素，因此会无重复地遍历 0..210，持续触发多层分裂。 */
    for (size_t i = 0U; i < 211U; ++i) {
        int32_t key = (int32_t)((i * 73U) % 211U);
        assert(bplus_insert(&tree, key, (size_t)key + 1000U));
        assert(bplus_verify(&tree) == i + 1U);
    }
    for (int32_t key = 0; key < 211; ++key) {
        size_t rrn = 0U;
        assert(bplus_find(&tree, key, &rrn));
        assert(rrn == (size_t)key + 1000U);
    }
    int32_t stress_range[32];
    assert(bplus_range(&tree, 73, 91, stress_range, 32U) == 19U);
    for (size_t i = 0U; i < 19U; ++i) {
        assert(stress_range[i] == (int32_t)(73U + i));
    }
    bplus_destroy(&tree);
}

static void test_hash_file(void)
{
    HashFile hash = {{NULL}};
    assert(hash_file_insert(&hash, 10, 0U));
    assert(hash_file_insert(&hash, 24, 1U));
    assert(hash_file_insert(&hash, -4, 2U));
    assert(hash_bucket_for(10) == 3U);
    assert(hash_bucket_for(24) == 3U);
    size_t rrn = SIZE_MAX;
    size_t comparisons = 0U;
    assert(hash_file_find(&hash, 24, &rrn, &comparisons));
    assert(rrn == 1U && comparisons >= 1U);
    assert(!hash_file_find(&hash, 31, NULL, NULL));
    assert(!hash_file_insert(&hash, 24, 9U));
    hash_file_destroy(&hash);
}

static void test_multilist(void)
{
    MultiListFile file;
    multilist_init(&file);
    int ids[4];
    size_t count = multilist_collect_dept(&file, file.ai_head, ids, 4U);
    assert(count == 2U && ids[0] == 101 && ids[1] == 103);
    count = multilist_collect_status(&file, file.active_head, ids, 4U);
    assert(count == 3U);
    assert(ids[0] == 101 && ids[1] == 102 && ids[2] == 104);
    count = multilist_collect_status(&file, file.closed_head, ids, 4U);
    assert(count == 1U && ids[0] == 103);
}

static void test_inverted_index(void)
{
    InvertedIndex index = {{NULL}};
    assert(inverted_add_document(&index, 1, "b+tree supports range query"));
    assert(inverted_add_document(&index, 2, "hash supports equality query"));
    assert(inverted_add_document(&index, 3,
                                 "inverted index supports keyword query keyword"));
    const TermEntry *supports = inverted_find_entry(&index, "supports");
    const TermEntry *keyword = inverted_find_entry(&index, "keyword");
    assert(supports != NULL && supports->count == 3U);
    assert(supports->doc_ids[0] == 1 && supports->doc_ids[1] == 2 &&
           supports->doc_ids[2] == 3);
    assert(keyword != NULL && keyword->count == 1U && keyword->doc_ids[0] == 3);
    int intersection[4];
    size_t count = postings_intersect(supports, keyword, intersection, 4U);
    assert(count == 1U && intersection[0] == 3);
    assert(inverted_find_entry(&index, "missing") == NULL);
    inverted_destroy(&index);
}

int main(void)
{
    test_record_and_basic_indexes();
    test_isam();
    test_bplus();
    test_hash_file();
    test_multilist();
    test_inverted_index();

    puts("顺序文件、稠密索引与 BST 索引：通过");
    puts("ISAM 主区与溢出链：通过");
    puts("B+ 树插入、分裂、等值与范围查询：通过");
    puts("散列文件、多重表与散列倒排索引：通过");
    puts("All file and index structure tests passed.");
    return 0;
}
