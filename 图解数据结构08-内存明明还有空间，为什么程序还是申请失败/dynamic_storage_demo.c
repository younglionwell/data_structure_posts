/*
 * Chapter 8: Dynamic storage management
 *
 * A compact, executable teaching model for:
 *   - aligned allocation, splitting and adjacent-block coalescing;
 *   - first/next/best/worst fit selection;
 *   - external fragmentation and storage compaction;
 *   - the binary buddy system;
 *   - reference counting and mark-sweep collection.
 *
 * This is not a replacement for malloc.  It deliberately uses offsets and
 * fixed arrays so that every boundary can be asserted without invoking
 * undefined behavior in the demonstration itself.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    SEGMENT_ARENA_SIZE = 128,
    ALIGNMENT = 8,
    MIN_SEGMENT_SIZE = 8,
    MAX_SEGMENTS = 32,
    MAX_RELOCATIONS = 32,
    BUDDY_ARENA_SIZE = 128,
    MIN_BUDDY_SIZE = 8,
    MAX_BUDDY_BLOCKS = 32,
    MAX_OBJECTS = 16,
    MAX_OBJECT_REFS = 4,
    MAX_ROOTS = 4
};

typedef enum {
    FIRST_FIT,
    NEXT_FIT,
    BEST_FIT,
    WORST_FIT
} FitPolicy;

typedef struct {
    size_t offset;
    size_t size;
    bool is_free;
    uint32_t generation;
} Segment;

typedef struct {
    Segment blocks[MAX_SEGMENTS];
    size_t count;
    size_t next_search_offset;
    uint32_t next_generation;
} SegmentAllocator;

typedef struct {
    size_t offset;
    size_t size;
    uint32_t generation;
    bool valid;
} SegmentHandle;

typedef struct {
    size_t old_offset;
    size_t new_offset;
    size_t size;
    uint32_t generation;
} Relocation;

static bool add_size_checked(size_t a, size_t b, size_t *out)
{
    assert(out != NULL);
    if (a > SIZE_MAX - b) {
        return false;
    }
    *out = a + b;
    return true;
}

static bool align_up_checked(size_t value, size_t alignment, size_t *out)
{
    size_t remainder;
    size_t increment;

    assert(out != NULL);
    if (alignment == 0 || (alignment & (alignment - 1U)) != 0U) {
        return false;
    }
    remainder = value & (alignment - 1U);
    if (remainder == 0) {
        *out = value;
        return true;
    }
    increment = alignment - remainder;
    return add_size_checked(value, increment, out);
}

static void segment_init(SegmentAllocator *allocator)
{
    assert(allocator != NULL);
    memset(allocator, 0, sizeof(*allocator));
    allocator->blocks[0] = (Segment){0, SEGMENT_ARENA_SIZE, true, 0};
    allocator->count = 1;
    allocator->next_generation = 1;
}

static bool segment_invariant(const SegmentAllocator *allocator)
{
    size_t cursor = 0;

    if (allocator == NULL || allocator->count == 0 ||
        allocator->count > MAX_SEGMENTS) {
        return false;
    }
    for (size_t i = 0; i < allocator->count; ++i) {
        const Segment *block = &allocator->blocks[i];
        if (block->offset != cursor || block->size == 0 ||
            block->size % ALIGNMENT != 0) {
            return false;
        }
        if (!add_size_checked(cursor, block->size, &cursor) ||
            cursor > SEGMENT_ARENA_SIZE) {
            return false;
        }
        if (block->is_free && block->generation != 0) {
            return false;
        }
        if (!block->is_free && block->generation == 0) {
            return false;
        }
        if (i > 0 && allocator->blocks[i - 1].is_free && block->is_free) {
            return false;
        }
    }
    return cursor == SEGMENT_ARENA_SIZE;
}

static void segment_remove_at(SegmentAllocator *allocator, size_t index)
{
    assert(allocator != NULL);
    assert(index < allocator->count);
    if (index + 1 < allocator->count) {
        memmove(&allocator->blocks[index], &allocator->blocks[index + 1],
                (allocator->count - index - 1) * sizeof(allocator->blocks[0]));
    }
    --allocator->count;
}

static bool segment_insert_at(SegmentAllocator *allocator, size_t index,
                              Segment block)
{
    assert(allocator != NULL);
    if (allocator->count >= MAX_SEGMENTS || index > allocator->count) {
        return false;
    }
    if (index < allocator->count) {
        memmove(&allocator->blocks[index + 1], &allocator->blocks[index],
                (allocator->count - index) * sizeof(allocator->blocks[0]));
    }
    allocator->blocks[index] = block;
    ++allocator->count;
    return true;
}

static size_t segment_find_candidate(const SegmentAllocator *allocator,
                                     size_t needed, FitPolicy policy)
{
    size_t selected = SIZE_MAX;

    assert(allocator != NULL);
    if (policy == NEXT_FIT) {
        size_t start = 0;
        while (start < allocator->count &&
               allocator->blocks[start].offset < allocator->next_search_offset) {
            ++start;
        }
        if (start == allocator->count) {
            start = 0;
        }
        for (size_t visited = 0; visited < allocator->count; ++visited) {
            size_t i = (start + visited) % allocator->count;
            if (allocator->blocks[i].is_free && allocator->blocks[i].size >= needed) {
                return i;
            }
        }
        return SIZE_MAX;
    }

    for (size_t i = 0; i < allocator->count; ++i) {
        const Segment *block = &allocator->blocks[i];
        if (!block->is_free || block->size < needed) {
            continue;
        }
        if (policy == FIRST_FIT) {
            return i;
        }
        if (selected == SIZE_MAX ||
            (policy == BEST_FIT && block->size < allocator->blocks[selected].size) ||
            (policy == WORST_FIT && block->size > allocator->blocks[selected].size)) {
            selected = i;
        }
    }
    return selected;
}

static bool segment_alloc(SegmentAllocator *allocator, size_t requested,
                          FitPolicy policy, SegmentHandle *out)
{
    size_t needed;
    size_t index;
    size_t remainder;
    Segment original;
    uint32_t generation;

    if (allocator == NULL || out == NULL || requested == 0 ||
        !align_up_checked(requested, ALIGNMENT, &needed) ||
        needed > SEGMENT_ARENA_SIZE) {
        return false;
    }
    index = segment_find_candidate(allocator, needed, policy);
    if (index == SIZE_MAX) {
        return false;
    }

    original = allocator->blocks[index];
    remainder = original.size - needed;
    if (remainder >= MIN_SEGMENT_SIZE) {
        Segment tail = {original.offset + needed, remainder, true, 0};
        if (!segment_insert_at(allocator, index + 1, tail)) {
            return false;
        }
        allocator->blocks[index].size = needed;
    }

    generation = allocator->next_generation++;
    if (generation == 0) {
        generation = allocator->next_generation++;
    }
    allocator->blocks[index].is_free = false;
    allocator->blocks[index].generation = generation;
    *out = (SegmentHandle){allocator->blocks[index].offset,
                           allocator->blocks[index].size,
                           generation, true};
    allocator->next_search_offset =
        (allocator->blocks[index].offset + allocator->blocks[index].size) %
        SEGMENT_ARENA_SIZE;
    assert(segment_invariant(allocator));
    return true;
}

static bool segment_free(SegmentAllocator *allocator, SegmentHandle *handle)
{
    size_t index = SIZE_MAX;

    if (allocator == NULL || handle == NULL || !handle->valid) {
        return false;
    }
    for (size_t i = 0; i < allocator->count; ++i) {
        const Segment *block = &allocator->blocks[i];
        if (!block->is_free && block->offset == handle->offset &&
            block->generation == handle->generation) {
            index = i;
            break;
        }
    }
    if (index == SIZE_MAX) {
        return false;
    }

    allocator->blocks[index].is_free = true;
    allocator->blocks[index].generation = 0;
    handle->valid = false;

    if (index > 0 && allocator->blocks[index - 1].is_free) {
        allocator->blocks[index - 1].size += allocator->blocks[index].size;
        segment_remove_at(allocator, index);
        --index;
    }
    if (index + 1 < allocator->count && allocator->blocks[index + 1].is_free) {
        allocator->blocks[index].size += allocator->blocks[index + 1].size;
        segment_remove_at(allocator, index + 1);
    }
    assert(segment_invariant(allocator));
    return true;
}

static void segment_free_stats(const SegmentAllocator *allocator,
                               size_t *total_free, size_t *largest_free)
{
    assert(allocator != NULL);
    assert(total_free != NULL);
    assert(largest_free != NULL);
    *total_free = 0;
    *largest_free = 0;
    for (size_t i = 0; i < allocator->count; ++i) {
        if (allocator->blocks[i].is_free) {
            *total_free += allocator->blocks[i].size;
            if (allocator->blocks[i].size > *largest_free) {
                *largest_free = allocator->blocks[i].size;
            }
        }
    }
}

static bool segment_load_layout(SegmentAllocator *allocator,
                                const size_t *sizes, const bool *free_flags,
                                const uint32_t *generations, size_t count)
{
    size_t offset = 0;
    uint32_t largest_generation = 0;

    if (allocator == NULL || sizes == NULL || free_flags == NULL ||
        generations == NULL || count == 0 || count > MAX_SEGMENTS) {
        return false;
    }
    memset(allocator, 0, sizeof(*allocator));
    for (size_t i = 0; i < count; ++i) {
        if (sizes[i] == 0 || sizes[i] % ALIGNMENT != 0 ||
            (free_flags[i] && generations[i] != 0) ||
            (!free_flags[i] && generations[i] == 0) ||
            !add_size_checked(offset, sizes[i], &offset) ||
            offset > SEGMENT_ARENA_SIZE ||
            (i > 0 && free_flags[i - 1] && free_flags[i])) {
            return false;
        }
        allocator->blocks[i] =
            (Segment){offset - sizes[i], sizes[i], free_flags[i], generations[i]};
        if (generations[i] > largest_generation) {
            largest_generation = generations[i];
        }
    }
    if (offset != SEGMENT_ARENA_SIZE) {
        return false;
    }
    allocator->count = count;
    allocator->next_generation = largest_generation + 1U;
    if (allocator->next_generation == 0) {
        allocator->next_generation = 1;
    }
    return segment_invariant(allocator);
}

static size_t segment_compact(SegmentAllocator *allocator,
                              SegmentHandle *handles, size_t handle_count,
                              Relocation *relocations, size_t relocation_capacity)
{
    Segment packed[MAX_SEGMENTS];
    size_t packed_count = 0;
    size_t cursor = 0;
    size_t relocation_count = 0;

    assert(allocator != NULL);
    for (size_t i = 0; i < allocator->count; ++i) {
        Segment block = allocator->blocks[i];
        if (block.is_free) {
            continue;
        }
        if (block.offset != cursor) {
            if (relocations != NULL && relocation_count < relocation_capacity) {
                relocations[relocation_count] =
                    (Relocation){block.offset, cursor, block.size, block.generation};
            }
            ++relocation_count;
            for (size_t h = 0; h < handle_count; ++h) {
                if (handles[h].valid && handles[h].offset == block.offset &&
                    handles[h].generation == block.generation) {
                    handles[h].offset = cursor;
                }
            }
        }
        block.offset = cursor;
        packed[packed_count++] = block;
        cursor += block.size;
    }
    if (cursor < SEGMENT_ARENA_SIZE) {
        packed[packed_count++] =
            (Segment){cursor, SEGMENT_ARENA_SIZE - cursor, true, 0};
    }
    memcpy(allocator->blocks, packed, packed_count * sizeof(packed[0]));
    allocator->count = packed_count;
    allocator->next_search_offset = cursor % SEGMENT_ARENA_SIZE;
    assert(segment_invariant(allocator));
    return relocation_count;
}

static size_t choose_from_sizes(const size_t *sizes, size_t count,
                                size_t request, FitPolicy policy,
                                size_t next_start)
{
    size_t chosen = SIZE_MAX;

    assert(sizes != NULL);
    if (count == 0 || next_start >= count) {
        return SIZE_MAX;
    }
    if (policy == NEXT_FIT) {
        for (size_t visited = 0; visited < count; ++visited) {
            size_t i = (next_start + visited) % count;
            if (sizes[i] >= request) {
                return i;
            }
        }
        return SIZE_MAX;
    }
    for (size_t i = 0; i < count; ++i) {
        if (sizes[i] < request) {
            continue;
        }
        if (policy == FIRST_FIT) {
            return i;
        }
        if (chosen == SIZE_MAX ||
            (policy == BEST_FIT && sizes[i] < sizes[chosen]) ||
            (policy == WORST_FIT && sizes[i] > sizes[chosen])) {
            chosen = i;
        }
    }
    return chosen;
}

typedef struct {
    size_t offset;
    size_t size;
    bool is_free;
    uint32_t generation;
} BuddyBlock;

typedef struct {
    BuddyBlock blocks[MAX_BUDDY_BLOCKS];
    size_t count;
    uint32_t next_generation;
} BuddyAllocator;

typedef struct {
    size_t offset;
    size_t block_size;
    size_t requested;
    uint32_t generation;
    bool valid;
} BuddyHandle;

static void buddy_init(BuddyAllocator *allocator)
{
    assert(allocator != NULL);
    memset(allocator, 0, sizeof(*allocator));
    allocator->blocks[0] = (BuddyBlock){0, BUDDY_ARENA_SIZE, true, 0};
    allocator->count = 1;
    allocator->next_generation = 1;
}

static bool is_power_of_two(size_t value)
{
    return value != 0 && (value & (value - 1U)) == 0;
}

static bool buddy_invariant(const BuddyAllocator *allocator)
{
    size_t cursor = 0;

    if (allocator == NULL || allocator->count == 0 ||
        allocator->count > MAX_BUDDY_BLOCKS) {
        return false;
    }
    for (size_t i = 0; i < allocator->count; ++i) {
        const BuddyBlock *block = &allocator->blocks[i];
        if (block->offset != cursor || !is_power_of_two(block->size) ||
            block->size < MIN_BUDDY_SIZE ||
            block->offset % block->size != 0 ||
            (block->is_free && block->generation != 0) ||
            (!block->is_free && block->generation == 0)) {
            return false;
        }
        cursor += block->size;
    }
    return cursor == BUDDY_ARENA_SIZE;
}

static bool buddy_insert_at(BuddyAllocator *allocator, size_t index,
                            BuddyBlock block)
{
    if (allocator == NULL || allocator->count >= MAX_BUDDY_BLOCKS ||
        index > allocator->count) {
        return false;
    }
    if (index < allocator->count) {
        memmove(&allocator->blocks[index + 1], &allocator->blocks[index],
                (allocator->count - index) * sizeof(allocator->blocks[0]));
    }
    allocator->blocks[index] = block;
    ++allocator->count;
    return true;
}

static void buddy_remove_at(BuddyAllocator *allocator, size_t index)
{
    assert(allocator != NULL && index < allocator->count);
    if (index + 1 < allocator->count) {
        memmove(&allocator->blocks[index], &allocator->blocks[index + 1],
                (allocator->count - index - 1) * sizeof(allocator->blocks[0]));
    }
    --allocator->count;
}

static bool next_power_of_two_checked(size_t value, size_t *out)
{
    size_t result = MIN_BUDDY_SIZE;

    assert(out != NULL);
    if (value == 0 || value > BUDDY_ARENA_SIZE) {
        return false;
    }
    while (result < value) {
        if (result > SIZE_MAX / 2U) {
            return false;
        }
        result *= 2U;
    }
    *out = result;
    return true;
}

static bool buddy_alloc(BuddyAllocator *allocator, size_t requested,
                        BuddyHandle *out)
{
    size_t needed;
    size_t index = SIZE_MAX;
    uint32_t generation;

    if (allocator == NULL || out == NULL ||
        !next_power_of_two_checked(requested, &needed)) {
        return false;
    }
    for (size_t i = 0; i < allocator->count; ++i) {
        if (allocator->blocks[i].is_free && allocator->blocks[i].size >= needed &&
            (index == SIZE_MAX ||
             allocator->blocks[i].size < allocator->blocks[index].size)) {
            index = i;
        }
    }
    if (index == SIZE_MAX) {
        return false;
    }
    while (allocator->blocks[index].size > needed) {
        size_t half = allocator->blocks[index].size / 2U;
        BuddyBlock right =
            {allocator->blocks[index].offset + half, half, true, 0};
        allocator->blocks[index].size = half;
        if (!buddy_insert_at(allocator, index + 1, right)) {
            return false;
        }
    }
    generation = allocator->next_generation++;
    if (generation == 0) {
        generation = allocator->next_generation++;
    }
    allocator->blocks[index].is_free = false;
    allocator->blocks[index].generation = generation;
    *out = (BuddyHandle){allocator->blocks[index].offset,
                         allocator->blocks[index].size,
                         requested, generation, true};
    assert(buddy_invariant(allocator));
    return true;
}

static bool buddy_free(BuddyAllocator *allocator, BuddyHandle *handle)
{
    size_t index = SIZE_MAX;

    if (allocator == NULL || handle == NULL || !handle->valid) {
        return false;
    }
    for (size_t i = 0; i < allocator->count; ++i) {
        BuddyBlock *block = &allocator->blocks[i];
        if (!block->is_free && block->offset == handle->offset &&
            block->generation == handle->generation) {
            index = i;
            break;
        }
    }
    if (index == SIZE_MAX) {
        return false;
    }
    allocator->blocks[index].is_free = true;
    allocator->blocks[index].generation = 0;
    handle->valid = false;

    while (allocator->blocks[index].size < BUDDY_ARENA_SIZE) {
        size_t size = allocator->blocks[index].size;
        size_t buddy_offset = allocator->blocks[index].offset ^ size;
        size_t buddy_index = SIZE_MAX;
        for (size_t i = 0; i < allocator->count; ++i) {
            if (allocator->blocks[i].is_free &&
                allocator->blocks[i].size == size &&
                allocator->blocks[i].offset == buddy_offset) {
                buddy_index = i;
                break;
            }
        }
        if (buddy_index == SIZE_MAX) {
            break;
        }
        {
            size_t keep = index < buddy_index ? index : buddy_index;
            size_t remove = index < buddy_index ? buddy_index : index;
            size_t new_offset = allocator->blocks[keep].offset;
            allocator->blocks[keep] =
                (BuddyBlock){new_offset, size * 2U, true, 0};
            buddy_remove_at(allocator, remove);
            index = keep;
        }
    }
    assert(buddy_invariant(allocator));
    return true;
}

typedef struct {
    bool in_use;
    bool marked;
    size_t offset;
    size_t size;
    size_t incoming;
    int refs[MAX_OBJECT_REFS];
    size_t ref_count;
    char name;
} GcObject;

typedef struct {
    GcObject objects[MAX_OBJECTS];
    int roots[MAX_ROOTS];
} GcHeap;

static void gc_init(GcHeap *heap)
{
    assert(heap != NULL);
    memset(heap, 0, sizeof(*heap));
    for (size_t i = 0; i < MAX_ROOTS; ++i) {
        heap->roots[i] = -1;
    }
    for (size_t i = 0; i < MAX_OBJECTS; ++i) {
        for (size_t j = 0; j < MAX_OBJECT_REFS; ++j) {
            heap->objects[i].refs[j] = -1;
        }
    }
}

static int gc_add_object(GcHeap *heap, char name, size_t offset, size_t size)
{
    size_t end;

    if (heap == NULL || size == 0 ||
        !add_size_checked(offset, size, &end) || end > SEGMENT_ARENA_SIZE) {
        return -1;
    }
    for (size_t i = 0; i < MAX_OBJECTS; ++i) {
        if (!heap->objects[i].in_use) {
            GcObject *object = &heap->objects[i];
            memset(object, 0, sizeof(*object));
            object->in_use = true;
            object->offset = offset;
            object->size = size;
            object->name = name;
            for (size_t j = 0; j < MAX_OBJECT_REFS; ++j) {
                object->refs[j] = -1;
            }
            return (int)i;
        }
    }
    return -1;
}

static bool gc_add_edge(GcHeap *heap, int from, int to)
{
    GcObject *source;

    if (heap == NULL || from < 0 || to < 0 || from >= MAX_OBJECTS ||
        to >= MAX_OBJECTS || !heap->objects[from].in_use ||
        !heap->objects[to].in_use) {
        return false;
    }
    source = &heap->objects[from];
    if (source->ref_count >= MAX_OBJECT_REFS) {
        return false;
    }
    source->refs[source->ref_count++] = to;
    ++heap->objects[to].incoming;
    return true;
}

static bool gc_set_root(GcHeap *heap, size_t slot, int object)
{
    if (heap == NULL || slot >= MAX_ROOTS || object < 0 ||
        object >= MAX_OBJECTS || !heap->objects[object].in_use ||
        heap->roots[slot] != -1) {
        return false;
    }
    heap->roots[slot] = object;
    ++heap->objects[object].incoming;
    return true;
}

static void gc_release_zero_count(GcHeap *heap, int object)
{
    GcObject *current;
    int targets[MAX_OBJECT_REFS];
    size_t target_count;

    assert(heap != NULL);
    assert(object >= 0 && object < MAX_OBJECTS);
    current = &heap->objects[object];
    if (!current->in_use || current->incoming != 0) {
        return;
    }
    target_count = current->ref_count;
    memcpy(targets, current->refs, target_count * sizeof(targets[0]));
    current->in_use = false;
    current->ref_count = 0;
    for (size_t i = 0; i < target_count; ++i) {
        int target = targets[i];
        if (target >= 0 && target < MAX_OBJECTS &&
            heap->objects[target].in_use) {
            assert(heap->objects[target].incoming > 0);
            --heap->objects[target].incoming;
            gc_release_zero_count(heap, target);
        }
    }
}

static bool gc_remove_root_reference_counting(GcHeap *heap, size_t slot)
{
    int object;

    if (heap == NULL || slot >= MAX_ROOTS || heap->roots[slot] < 0) {
        return false;
    }
    object = heap->roots[slot];
    heap->roots[slot] = -1;
    assert(heap->objects[object].incoming > 0);
    --heap->objects[object].incoming;
    gc_release_zero_count(heap, object);
    return true;
}

static void gc_mark_from(GcHeap *heap, int root)
{
    int stack[MAX_OBJECTS];
    size_t stack_size = 0;

    assert(heap != NULL);
    if (root < 0 || root >= MAX_OBJECTS || !heap->objects[root].in_use) {
        return;
    }
    heap->objects[root].marked = true;
    stack[stack_size++] = root;
    while (stack_size > 0) {
        int object = stack[--stack_size];
        GcObject *current = &heap->objects[object];
        if (!current->in_use) {
            continue;
        }
        for (size_t i = 0; i < current->ref_count; ++i) {
            int target = current->refs[i];
            if (target >= 0 && target < MAX_OBJECTS &&
                heap->objects[target].in_use && !heap->objects[target].marked) {
                assert(stack_size < MAX_OBJECTS);
                heap->objects[target].marked = true;
                stack[stack_size++] = target;
            }
        }
    }
}

static void gc_recompute_incoming(GcHeap *heap)
{
    assert(heap != NULL);
    for (size_t i = 0; i < MAX_OBJECTS; ++i) {
        heap->objects[i].incoming = 0;
    }
    for (size_t r = 0; r < MAX_ROOTS; ++r) {
        int object = heap->roots[r];
        if (object >= 0 && object < MAX_OBJECTS && heap->objects[object].in_use) {
            ++heap->objects[object].incoming;
        } else if (object >= 0) {
            heap->roots[r] = -1;
        }
    }
    for (size_t i = 0; i < MAX_OBJECTS; ++i) {
        GcObject *object = &heap->objects[i];
        if (!object->in_use) {
            continue;
        }
        size_t write = 0;
        for (size_t j = 0; j < object->ref_count; ++j) {
            int target = object->refs[j];
            if (target >= 0 && target < MAX_OBJECTS &&
                heap->objects[target].in_use) {
                object->refs[write++] = target;
                ++heap->objects[target].incoming;
            }
        }
        object->ref_count = write;
        while (write < MAX_OBJECT_REFS) {
            object->refs[write++] = -1;
        }
    }
}

static size_t gc_mark_sweep(GcHeap *heap)
{
    size_t reclaimed = 0;

    assert(heap != NULL);
    for (size_t i = 0; i < MAX_OBJECTS; ++i) {
        heap->objects[i].marked = false;
    }
    for (size_t r = 0; r < MAX_ROOTS; ++r) {
        gc_mark_from(heap, heap->roots[r]);
    }
    for (size_t i = 0; i < MAX_OBJECTS; ++i) {
        if (heap->objects[i].in_use && !heap->objects[i].marked) {
            heap->objects[i].in_use = false;
            heap->objects[i].ref_count = 0;
            ++reclaimed;
        } else if (heap->objects[i].in_use) {
            heap->objects[i].marked = false;
        }
    }
    gc_recompute_incoming(heap);
    return reclaimed;
}

static void test_alignment_and_fit_policies(void)
{
    const size_t candidates[] = {24, 40, 16, 32};
    size_t aligned = 0;
    size_t ignored = 0;

    assert(align_up_checked(13, 8, &aligned) && aligned == 16);
    assert(align_up_checked(16, 8, &aligned) && aligned == 16);
    assert(!align_up_checked(SIZE_MAX, 8, &ignored));
    assert(!align_up_checked(5, 3, &ignored));

    assert(choose_from_sizes(candidates, 4, 28, FIRST_FIT, 0) == 1);
    assert(choose_from_sizes(candidates, 4, 28, NEXT_FIT, 3) == 3);
    assert(choose_from_sizes(candidates, 4, 28, BEST_FIT, 0) == 3);
    assert(choose_from_sizes(candidates, 4, 28, WORST_FIT, 0) == 1);
    assert(choose_from_sizes(candidates, 4, 48, BEST_FIT, 0) == SIZE_MAX);
}

static void test_segment_allocator(void)
{
    SegmentAllocator allocator;
    SegmentHandle a = {0};
    SegmentHandle b = {0};
    SegmentHandle invalid = {999, 8, 77, true};

    segment_init(&allocator);
    assert(segment_invariant(&allocator));
    assert(segment_alloc(&allocator, 13, FIRST_FIT, &a));
    assert(a.offset == 0 && a.size == 16);
    assert(segment_alloc(&allocator, 20, BEST_FIT, &b));
    assert(b.offset == 16 && b.size == 24);
    assert(!segment_free(&allocator, &invalid));
    assert(segment_free(&allocator, &a));
    assert(!segment_free(&allocator, &a));
    assert(segment_free(&allocator, &b));
    assert(allocator.count == 1 && allocator.blocks[0].is_free &&
           allocator.blocks[0].size == SEGMENT_ARENA_SIZE);
    assert(!segment_alloc(&allocator, 0, FIRST_FIT, &a));
    assert(!segment_alloc(&allocator, SEGMENT_ARENA_SIZE + 1U,
                          FIRST_FIT, &a));
}

static void test_external_fragmentation_and_coalescing(void)
{
    SegmentAllocator allocator;
    const size_t sizes[] = {16, 16, 16, 16, 16, 16, 32};
    const bool free_flags[] = {false, true, false, true, false, true, false};
    const uint32_t generations[] = {1, 0, 2, 0, 3, 0, 4};
    SegmentHandle request = {0};
    SegmentHandle middle = {32, 16, 2, true};
    size_t total_free;
    size_t largest_free;

    assert(segment_load_layout(&allocator, sizes, free_flags, generations, 7));
    segment_free_stats(&allocator, &total_free, &largest_free);
    assert(total_free == 48 && largest_free == 16);
    assert(!segment_alloc(&allocator, 24, FIRST_FIT, &request));

    assert(segment_free(&allocator, &middle));
    segment_free_stats(&allocator, &total_free, &largest_free);
    assert(total_free == 64 && largest_free == 48);
    assert(segment_alloc(&allocator, 24, FIRST_FIT, &request));
    assert(request.offset == 16 && request.size == 24);
}

static void test_storage_compaction(void)
{
    SegmentAllocator allocator;
    const size_t sizes[] = {16, 16, 24, 16, 8, 48};
    const bool free_flags[] = {false, true, false, true, false, true};
    const uint32_t generations[] = {1, 0, 2, 0, 3, 0};
    SegmentHandle handles[] = {
        {0, 16, 1, true}, {32, 24, 2, true}, {72, 8, 3, true}
    };
    Relocation relocations[MAX_RELOCATIONS];
    size_t moved;

    assert(segment_load_layout(&allocator, sizes, free_flags, generations, 6));
    moved = segment_compact(&allocator, handles, 3, relocations,
                            MAX_RELOCATIONS);
    assert(moved == 2);
    assert(relocations[0].old_offset == 32 &&
           relocations[0].new_offset == 16 && relocations[0].size == 24);
    assert(relocations[1].old_offset == 72 &&
           relocations[1].new_offset == 40 && relocations[1].size == 8);
    assert(handles[1].offset == 16 && handles[2].offset == 40);
    assert(allocator.count == 4);
    assert(allocator.blocks[3].is_free && allocator.blocks[3].offset == 48 &&
           allocator.blocks[3].size == 80);
}

static void test_buddy_allocator(void)
{
    BuddyAllocator allocator;
    BuddyHandle a = {0};
    BuddyHandle b = {0};
    BuddyHandle c = {0};

    buddy_init(&allocator);
    assert(buddy_invariant(&allocator));
    assert(buddy_alloc(&allocator, 13, &a));
    assert(a.offset == 0 && a.block_size == 16);
    assert(buddy_alloc(&allocator, 20, &b));
    assert(b.offset == 32 && b.block_size == 32);
    assert(buddy_alloc(&allocator, 8, &c));
    assert(c.offset == 16 && c.block_size == 8);
    assert(a.block_size - a.requested == 3);

    assert(buddy_free(&allocator, &a));
    assert(allocator.count > 1); /* The 16-unit buddy has been split. */
    assert(buddy_free(&allocator, &c));
    assert(buddy_free(&allocator, &b));
    assert(allocator.count == 1 && allocator.blocks[0].is_free &&
           allocator.blocks[0].offset == 0 &&
           allocator.blocks[0].size == BUDDY_ARENA_SIZE);
    assert(!buddy_free(&allocator, &b));
    assert(!buddy_alloc(&allocator, BUDDY_ARENA_SIZE + 1U, &a));
}

static void test_reference_counting_and_mark_sweep(void)
{
    GcHeap heap;
    int a;
    int b;
    int x;
    int y;

    gc_init(&heap);
    a = gc_add_object(&heap, 'A', 0, 16);
    b = gc_add_object(&heap, 'B', 16, 16);
    assert(a >= 0 && b >= 0);
    assert(gc_add_edge(&heap, a, b));
    assert(gc_set_root(&heap, 0, a));
    assert(heap.objects[a].incoming == 1 && heap.objects[b].incoming == 1);
    assert(gc_remove_root_reference_counting(&heap, 0));
    assert(!heap.objects[a].in_use && !heap.objects[b].in_use);

    gc_init(&heap);
    x = gc_add_object(&heap, 'X', 32, 16);
    y = gc_add_object(&heap, 'Y', 48, 16);
    assert(x >= 0 && y >= 0);
    assert(gc_add_edge(&heap, x, y));
    assert(gc_add_edge(&heap, y, x));
    assert(gc_set_root(&heap, 0, x));
    assert(heap.objects[x].incoming == 2 && heap.objects[y].incoming == 1);
    assert(gc_remove_root_reference_counting(&heap, 0));
    assert(heap.objects[x].in_use && heap.objects[y].in_use);
    assert(heap.objects[x].incoming == 1 && heap.objects[y].incoming == 1);
    assert(gc_mark_sweep(&heap) == 2);
    assert(!heap.objects[x].in_use && !heap.objects[y].in_use);

    gc_init(&heap);
    a = gc_add_object(&heap, 'R', 0, 8);
    b = gc_add_object(&heap, 'A', 8, 8);
    x = gc_add_object(&heap, 'X', 32, 8);
    assert(gc_add_edge(&heap, a, b));
    assert(gc_set_root(&heap, 0, a));
    assert(gc_mark_sweep(&heap) == 1);
    assert(heap.objects[a].in_use && heap.objects[b].in_use);
    assert(!heap.objects[x].in_use);
}

int main(void)
{
    test_alignment_and_fit_policies();
    test_segment_allocator();
    test_external_fragmentation_and_coalescing();
    test_storage_compaction();
    test_buddy_allocator();
    test_reference_counting_and_mark_sweep();

    puts("Fit policies: first=40, next=32, best=32, worst=40.");
    puts("External fragmentation: total free 48, largest block 16, request 24 fails.");
    puts("Coalescing: freeing the middle block creates a contiguous block of 48.");
    puts("Buddy system: 13->16, 20->32, 8->8; all buddies merge back to 128.");
    puts("Garbage collection: reference counting keeps a cycle; mark-sweep reclaims it.");
    puts("All dynamic-storage-management tests passed.");
    return 0;
}
