/*
 * Chapter 7: Graphs
 *
 * A self-contained C11 companion program covering:
 *   adjacency matrix/list, DFS/BFS, Tarjan SCC,
 *   Kruskal/Prim MST, articulation points/bridges,
 *   Dijkstra, Bellman-Ford, Floyd-Warshall,
 *   Kahn topological sorting, AOE critical path,
 *   bipartite checking and Hopcroft-Karp matching.
 *
 * Build:
 *   cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
 *      07-graph-demo.c -o graph_demo
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { MAXV = 32, MAXE = 128, INF = 1000000000 };

typedef struct {
    int to;
    int weight;
    int next;
} Arc;

typedef struct {
    int n;
    int head[MAXV];
    Arc arcs[MAXE];
    int arc_count;
    bool directed;
} Graph;

typedef struct {
    int u;
    int v;
    int weight;
} Edge;

static bool valid_vertex(const Graph *g, int v) {
    return g != NULL && v >= 0 && v < g->n;
}

static void graph_init(Graph *g, int n, bool directed) {
    assert(g != NULL);
    assert(n >= 0 && n <= MAXV);
    g->n = n;
    g->arc_count = 0;
    g->directed = directed;
    for (int i = 0; i < MAXV; ++i) {
        g->head[i] = -1;
    }
}

static bool add_arc(Graph *g, int from, int to, int weight) {
    if (!valid_vertex(g, from) || !valid_vertex(g, to) ||
        g->arc_count >= MAXE) {
        return false;
    }
    int k = g->arc_count++;
    g->arcs[k] = (Arc){to, weight, g->head[from]};
    g->head[from] = k;
    return true;
}

static bool graph_add_edge(Graph *g, int from, int to, int weight) {
    if (!add_arc(g, from, to, weight)) {
        return false;
    }
    if (!g->directed && !add_arc(g, to, from, weight)) {
        --g->arc_count;
        g->head[from] = g->arcs[g->arc_count].next;
        return false;
    }
    return true;
}

static void graph_to_matrix(const Graph *g, int matrix[MAXV][MAXV]) {
    assert(g != NULL && matrix != NULL);
    for (int i = 0; i < g->n; ++i) {
        for (int j = 0; j < g->n; ++j) {
            matrix[i][j] = (i == j) ? 0 : INF;
        }
    }
    for (int u = 0; u < g->n; ++u) {
        for (int e = g->head[u]; e != -1; e = g->arcs[e].next) {
            int v = g->arcs[e].to;
            if (g->arcs[e].weight < matrix[u][v]) {
                matrix[u][v] = g->arcs[e].weight;
            }
        }
    }
}

typedef struct {
    int data[MAXV];
    int size;
} Sequence;

static void sequence_push(Sequence *s, int value) {
    assert(s != NULL && s->size >= 0 && s->size < MAXV);
    s->data[s->size++] = value;
}

static bool sequence_equals(const Sequence *s, const int *values, int n) {
    if (s == NULL || values == NULL || s->size != n) {
        return false;
    }
    for (int i = 0; i < n; ++i) {
        if (s->data[i] != values[i]) {
            return false;
        }
    }
    return true;
}

/* Adjacency lists are stored in reverse insertion order.  To make traversal
 * independent of insertion details, collect and sort each neighbor set. */
static int sorted_neighbors(const Graph *g, int u, int out[MAXV]) {
    int count = 0;
    for (int e = g->head[u]; e != -1; e = g->arcs[e].next) {
        assert(count < MAXV);
        out[count++] = g->arcs[e].to;
    }
    for (int i = 1; i < count; ++i) {
        int x = out[i];
        int j = i;
        while (j > 0 && out[j - 1] > x) {
            out[j] = out[j - 1];
            --j;
        }
        out[j] = x;
    }
    return count;
}

static void dfs_visit(const Graph *g, int u, bool seen[MAXV], Sequence *order) {
    seen[u] = true;
    sequence_push(order, u);
    int neighbors[MAXV];
    int count = sorted_neighbors(g, u, neighbors);
    for (int i = 0; i < count; ++i) {
        int v = neighbors[i];
        if (!seen[v]) {
            dfs_visit(g, v, seen, order);
        }
    }
}

static Sequence dfs_forest(const Graph *g) {
    assert(g != NULL);
    bool seen[MAXV] = {false};
    Sequence order = {{0}, 0};
    for (int u = 0; u < g->n; ++u) {
        if (!seen[u]) {
            dfs_visit(g, u, seen, &order);
        }
    }
    return order;
}

static Sequence bfs_forest(const Graph *g) {
    assert(g != NULL);
    bool seen[MAXV] = {false};
    Sequence order = {{0}, 0};
    int queue[MAXV];
    for (int start = 0; start < g->n; ++start) {
        if (seen[start]) {
            continue;
        }
        int front = 0;
        int rear = 0;
        queue[rear++] = start;
        seen[start] = true;
        while (front < rear) {
            int u = queue[front++];
            sequence_push(&order, u);
            int neighbors[MAXV];
            int count = sorted_neighbors(g, u, neighbors);
            for (int i = 0; i < count; ++i) {
                int v = neighbors[i];
                if (!seen[v]) {
                    assert(rear < MAXV);
                    seen[v] = true;
                    queue[rear++] = v;
                }
            }
        }
    }
    return order;
}

typedef struct {
    int index[MAXV];
    int low[MAXV];
    bool on_stack[MAXV];
    int stack[MAXV];
    int top;
    int clock;
    int component[MAXV];
    int component_count;
} SccState;

static void tarjan_scc_visit(const Graph *g, int u, SccState *s) {
    s->index[u] = s->clock;
    s->low[u] = s->clock;
    ++s->clock;
    s->stack[s->top++] = u;
    s->on_stack[u] = true;

    for (int e = g->head[u]; e != -1; e = g->arcs[e].next) {
        int v = g->arcs[e].to;
        if (s->index[v] == -1) {
            tarjan_scc_visit(g, v, s);
            if (s->low[v] < s->low[u]) {
                s->low[u] = s->low[v];
            }
        } else if (s->on_stack[v] && s->index[v] < s->low[u]) {
            s->low[u] = s->index[v];
        }
    }

    if (s->low[u] == s->index[u]) {
        for (;;) {
            assert(s->top > 0);
            int v = s->stack[--s->top];
            s->on_stack[v] = false;
            s->component[v] = s->component_count;
            if (v == u) {
                break;
            }
        }
        ++s->component_count;
    }
}

static int tarjan_scc(const Graph *g, int component[MAXV]) {
    assert(g != NULL && component != NULL);
    SccState s;
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < MAXV; ++i) {
        s.index[i] = -1;
        s.component[i] = -1;
    }
    for (int u = 0; u < g->n; ++u) {
        if (s.index[u] == -1) {
            tarjan_scc_visit(g, u, &s);
        }
    }
    for (int u = 0; u < g->n; ++u) {
        component[u] = s.component[u];
    }
    return s.component_count;
}

typedef struct {
    int parent[MAXV];
    int size[MAXV];
} Dsu;

static void dsu_init(Dsu *d, int n) {
    assert(d != NULL && n >= 0 && n <= MAXV);
    for (int i = 0; i < n; ++i) {
        d->parent[i] = i;
        d->size[i] = 1;
    }
}

static int dsu_find(Dsu *d, int x) {
    assert(d != NULL && x >= 0 && x < MAXV);
    int root = x;
    while (d->parent[root] != root) {
        root = d->parent[root];
    }
    while (d->parent[x] != x) {
        int next = d->parent[x];
        d->parent[x] = root;
        x = next;
    }
    return root;
}

static bool dsu_union(Dsu *d, int a, int b) {
    int ra = dsu_find(d, a);
    int rb = dsu_find(d, b);
    if (ra == rb) {
        return false;
    }
    if (d->size[ra] < d->size[rb]) {
        int tmp = ra;
        ra = rb;
        rb = tmp;
    }
    d->parent[rb] = ra;
    d->size[ra] += d->size[rb];
    return true;
}

static void sort_edges(Edge *edges, int m) {
    for (int i = 1; i < m; ++i) {
        Edge x = edges[i];
        int j = i;
        while (j > 0 && (edges[j - 1].weight > x.weight ||
               (edges[j - 1].weight == x.weight && edges[j - 1].u > x.u) ||
               (edges[j - 1].weight == x.weight && edges[j - 1].u == x.u &&
                edges[j - 1].v > x.v))) {
            edges[j] = edges[j - 1];
            --j;
        }
        edges[j] = x;
    }
}

static bool kruskal_mst(int n, const Edge *input, int m,
                        Edge chosen[MAXV], int *chosen_count,
                        int *total_weight) {
    if (n <= 0 || n > MAXV || input == NULL || m < 0 || m > MAXE ||
        chosen == NULL || chosen_count == NULL || total_weight == NULL) {
        return false;
    }
    Edge edges[MAXE];
    memcpy(edges, input, (size_t)m * sizeof(*edges));
    sort_edges(edges, m);
    Dsu d;
    dsu_init(&d, n);
    *chosen_count = 0;
    *total_weight = 0;
    for (int i = 0; i < m && *chosen_count < n - 1; ++i) {
        if (edges[i].u < 0 || edges[i].u >= n ||
            edges[i].v < 0 || edges[i].v >= n) {
            return false;
        }
        if (dsu_union(&d, edges[i].u, edges[i].v)) {
            chosen[(*chosen_count)++] = edges[i];
            if ((edges[i].weight > 0 && *total_weight > INT32_MAX - edges[i].weight) ||
                (edges[i].weight < 0 && *total_weight < INT32_MIN - edges[i].weight)) {
                return false;
            }
            *total_weight += edges[i].weight;
        }
    }
    return *chosen_count == n - 1;
}

static bool prim_mst(const Graph *g, int start, int parent[MAXV],
                     int *total_weight) {
    if (!valid_vertex(g, start) || g->directed || parent == NULL ||
        total_weight == NULL) {
        return false;
    }
    int key[MAXV];
    bool used[MAXV] = {false};
    for (int i = 0; i < g->n; ++i) {
        key[i] = INF;
        parent[i] = -1;
    }
    key[start] = 0;
    *total_weight = 0;
    for (int step = 0; step < g->n; ++step) {
        int u = -1;
        for (int v = 0; v < g->n; ++v) {
            if (!used[v] && (u == -1 || key[v] < key[u])) {
                u = v;
            }
        }
        if (u == -1 || key[u] == INF) {
            return false;
        }
        used[u] = true;
        *total_weight += key[u];
        for (int e = g->head[u]; e != -1; e = g->arcs[e].next) {
            int v = g->arcs[e].to;
            int w = g->arcs[e].weight;
            if (!used[v] && w < key[v]) {
                key[v] = w;
                parent[v] = u;
            }
        }
    }
    return true;
}

typedef struct {
    int disc[MAXV];
    int low[MAXV];
    int parent[MAXV];
    bool articulation[MAXV];
    Edge bridges[MAXE];
    int bridge_count;
    int clock;
} CutState;

static void cut_visit(const Graph *g, int u, int parent_arc, CutState *s) {
    s->disc[u] = s->low[u] = ++s->clock;
    int children = 0;
    for (int e = g->head[u]; e != -1; e = g->arcs[e].next) {
        int v = g->arcs[e].to;
        if (s->disc[v] == 0) {
            ++children;
            s->parent[v] = u;
            cut_visit(g, v, e, s);
            if (s->low[v] < s->low[u]) {
                s->low[u] = s->low[v];
            }
            if (s->parent[u] == -1 && children >= 2) {
                s->articulation[u] = true;
            }
            if (s->parent[u] != -1 && s->low[v] >= s->disc[u]) {
                s->articulation[u] = true;
            }
            if (s->low[v] > s->disc[u]) {
                assert(s->bridge_count < MAXE);
                s->bridges[s->bridge_count++] = (Edge){u, v, 1};
            }
        } else if ((parent_arc == -1 || e != (parent_arc ^ 1)) &&
                   s->disc[v] < s->low[u]) {
            s->low[u] = s->disc[v];
        }
    }
}

static CutState articulation_and_bridges(const Graph *g) {
    assert(g != NULL && !g->directed);
    CutState s;
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < MAXV; ++i) {
        s.parent[i] = -1;
    }
    for (int u = 0; u < g->n; ++u) {
        if (s.disc[u] == 0) {
            cut_visit(g, u, -1, &s);
        }
    }
    return s;
}

static bool safe_distance_sum(int a, int b, int *sum) {
    if (sum == NULL || a == INF || b == INF || a == -INF || b == -INF) {
        return false;
    }
    int64_t value = (int64_t)a + (int64_t)b;
    if (value <= -INF || value >= INF) {
        return false;
    }
    *sum = (int)value;
    return true;
}

static bool dijkstra(const Graph *g, int source, int dist[MAXV],
                     int prev[MAXV], Sequence *settled) {
    if (!valid_vertex(g, source) || dist == NULL || prev == NULL) {
        return false;
    }
    for (int u = 0; u < g->n; ++u) {
        for (int e = g->head[u]; e != -1; e = g->arcs[e].next) {
            if (g->arcs[e].weight < 0) {
                return false;
            }
        }
    }
    bool used[MAXV] = {false};
    for (int i = 0; i < g->n; ++i) {
        dist[i] = INF;
        prev[i] = -1;
    }
    if (settled != NULL) {
        settled->size = 0;
    }
    dist[source] = 0;
    for (int step = 0; step < g->n; ++step) {
        int u = -1;
        for (int v = 0; v < g->n; ++v) {
            if (!used[v] && (u == -1 || dist[v] < dist[u])) {
                u = v;
            }
        }
        if (u == -1 || dist[u] == INF) {
            break;
        }
        used[u] = true;
        if (settled != NULL) {
            sequence_push(settled, u);
        }
        for (int e = g->head[u]; e != -1; e = g->arcs[e].next) {
            int v = g->arcs[e].to;
            int candidate;
            if (safe_distance_sum(dist[u], g->arcs[e].weight, &candidate) &&
                candidate < dist[v]) {
                dist[v] = candidate;
                prev[v] = u;
            }
        }
    }
    return true;
}

static bool bellman_ford(int n, const Edge *edges, int m, int source,
                         int dist[MAXV], int prev[MAXV],
                         bool *has_negative_cycle) {
    if (n <= 0 || n > MAXV || edges == NULL || m < 0 || m > MAXE ||
        source < 0 || source >= n || dist == NULL || prev == NULL ||
        has_negative_cycle == NULL) {
        return false;
    }
    for (int i = 0; i < n; ++i) {
        dist[i] = INF;
        prev[i] = -1;
    }
    dist[source] = 0;
    for (int round = 1; round < n; ++round) {
        bool changed = false;
        for (int i = 0; i < m; ++i) {
            int u = edges[i].u;
            int v = edges[i].v;
            if (u < 0 || u >= n || v < 0 || v >= n) {
                return false;
            }
            int candidate;
            if (safe_distance_sum(dist[u], edges[i].weight, &candidate) &&
                candidate < dist[v]) {
                dist[v] = candidate;
                prev[v] = u;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    *has_negative_cycle = false;
    for (int i = 0; i < m; ++i) {
        int candidate;
        if (safe_distance_sum(dist[edges[i].u], edges[i].weight, &candidate) &&
            candidate < dist[edges[i].v]) {
            *has_negative_cycle = true;
            break;
        }
    }
    return true;
}

static bool floyd_warshall(int n, int dist[MAXV][MAXV],
                           int next[MAXV][MAXV], bool *negative_cycle) {
    if (n < 0 || n > MAXV || dist == NULL || next == NULL ||
        negative_cycle == NULL) {
        return false;
    }
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            next[i][j] = (dist[i][j] == INF) ? -1 : j;
        }
    }
    for (int k = 0; k < n; ++k) {
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                int candidate;
                if (safe_distance_sum(dist[i][k], dist[k][j], &candidate) &&
                    candidate < dist[i][j]) {
                    dist[i][j] = candidate;
                    next[i][j] = next[i][k];
                }
            }
        }
    }
    *negative_cycle = false;
    for (int i = 0; i < n; ++i) {
        if (dist[i][i] < 0) {
            *negative_cycle = true;
        }
    }
    return true;
}

static Sequence floyd_path(int n, int source, int target,
                           int next[MAXV][MAXV]) {
    Sequence path = {{0}, 0};
    if (source < 0 || source >= n || target < 0 || target >= n ||
        next[source][target] == -1) {
        return path;
    }
    sequence_push(&path, source);
    while (source != target) {
        source = next[source][target];
        if (source < 0 || source >= n || path.size >= n + 1) {
            path.size = 0;
            return path;
        }
        sequence_push(&path, source);
    }
    return path;
}

static bool topological_sort(const Graph *g, Sequence *order) {
    if (g == NULL || order == NULL || !g->directed) {
        return false;
    }
    int indegree[MAXV] = {0};
    int queue[MAXV];
    int front = 0;
    int rear = 0;
    order->size = 0;
    for (int u = 0; u < g->n; ++u) {
        for (int e = g->head[u]; e != -1; e = g->arcs[e].next) {
            ++indegree[g->arcs[e].to];
        }
    }
    for (int u = 0; u < g->n; ++u) {
        if (indegree[u] == 0) {
            queue[rear++] = u;
        }
    }
    while (front < rear) {
        int u = queue[front++];
        sequence_push(order, u);
        int neighbors[MAXV];
        int count = sorted_neighbors(g, u, neighbors);
        for (int i = 0; i < count; ++i) {
            int v = neighbors[i];
            if (--indegree[v] == 0) {
                queue[rear++] = v;
            }
        }
    }
    return order->size == g->n;
}

typedef struct {
    int ve[MAXV];
    int vl[MAXV];
    bool critical_arc[MAXE];
    int duration;
    Sequence topo;
} CriticalPath;

static bool critical_path(const Graph *g, CriticalPath *out) {
    if (g == NULL || out == NULL || !g->directed) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!topological_sort(g, &out->topo)) {
        return false;
    }
    for (int pos = 0; pos < out->topo.size; ++pos) {
        int u = out->topo.data[pos];
        for (int e = g->head[u]; e != -1; e = g->arcs[e].next) {
            int v = g->arcs[e].to;
            int candidate;
            if (!safe_distance_sum(out->ve[u], g->arcs[e].weight, &candidate)) {
                return false;
            }
            if (candidate > out->ve[v]) {
                out->ve[v] = candidate;
            }
        }
    }
    for (int i = 0; i < g->n; ++i) {
        if (out->ve[i] > out->duration) {
            out->duration = out->ve[i];
        }
    }
    for (int i = 0; i < g->n; ++i) {
        out->vl[i] = out->duration;
    }
    for (int pos = out->topo.size - 1; pos >= 0; --pos) {
        int u = out->topo.data[pos];
        for (int e = g->head[u]; e != -1; e = g->arcs[e].next) {
            int v = g->arcs[e].to;
            int candidate = out->vl[v] - g->arcs[e].weight;
            if (candidate < out->vl[u]) {
                out->vl[u] = candidate;
            }
        }
    }
    for (int u = 0; u < g->n; ++u) {
        for (int e = g->head[u]; e != -1; e = g->arcs[e].next) {
            int latest = out->vl[g->arcs[e].to] - g->arcs[e].weight;
            out->critical_arc[e] = out->ve[u] == latest;
        }
    }
    return true;
}

static bool is_bipartite(const Graph *g, int color[MAXV]) {
    if (g == NULL || color == NULL || g->directed) {
        return false;
    }
    for (int i = 0; i < g->n; ++i) {
        color[i] = -1;
    }
    int queue[MAXV];
    for (int start = 0; start < g->n; ++start) {
        if (color[start] != -1) {
            continue;
        }
        int front = 0;
        int rear = 0;
        color[start] = 0;
        queue[rear++] = start;
        while (front < rear) {
            int u = queue[front++];
            for (int e = g->head[u]; e != -1; e = g->arcs[e].next) {
                int v = g->arcs[e].to;
                if (color[v] == -1) {
                    color[v] = 1 - color[u];
                    queue[rear++] = v;
                } else if (color[v] == color[u]) {
                    return false;
                }
            }
        }
    }
    return true;
}

typedef struct {
    int left_count;
    int right_count;
    bool edge[MAXV][MAXV];
    int pair_left[MAXV];
    int pair_right[MAXV];
    int distance[MAXV];
} BipartiteGraph;

static void bipartite_init(BipartiteGraph *b, int left_count,
                           int right_count) {
    assert(b != NULL && left_count >= 0 && left_count <= MAXV &&
           right_count >= 0 && right_count <= MAXV);
    memset(b, 0, sizeof(*b));
    b->left_count = left_count;
    b->right_count = right_count;
    for (int i = 0; i < MAXV; ++i) {
        b->pair_left[i] = -1;
        b->pair_right[i] = -1;
    }
}

static bool bipartite_add_edge(BipartiteGraph *b, int u, int v) {
    if (b == NULL || u < 0 || u >= b->left_count ||
        v < 0 || v >= b->right_count) {
        return false;
    }
    b->edge[u][v] = true;
    return true;
}

static bool hk_bfs(BipartiteGraph *b) {
    int queue[MAXV];
    int front = 0;
    int rear = 0;
    bool found = false;
    for (int u = 0; u < b->left_count; ++u) {
        if (b->pair_left[u] == -1) {
            b->distance[u] = 0;
            queue[rear++] = u;
        } else {
            b->distance[u] = -1;
        }
    }
    while (front < rear) {
        int u = queue[front++];
        for (int v = 0; v < b->right_count; ++v) {
            if (!b->edge[u][v]) {
                continue;
            }
            int mate = b->pair_right[v];
            if (mate == -1) {
                found = true;
            } else if (b->distance[mate] == -1) {
                b->distance[mate] = b->distance[u] + 1;
                queue[rear++] = mate;
            }
        }
    }
    return found;
}

static bool hk_dfs(BipartiteGraph *b, int u) {
    for (int v = 0; v < b->right_count; ++v) {
        if (!b->edge[u][v]) {
            continue;
        }
        int mate = b->pair_right[v];
        if (mate == -1 ||
            (b->distance[mate] == b->distance[u] + 1 && hk_dfs(b, mate))) {
            b->pair_left[u] = v;
            b->pair_right[v] = u;
            return true;
        }
    }
    b->distance[u] = -1;
    return false;
}

static int hopcroft_karp(BipartiteGraph *b) {
    assert(b != NULL);
    int matching = 0;
    while (hk_bfs(b)) {
        for (int u = 0; u < b->left_count; ++u) {
            if (b->pair_left[u] == -1 && hk_dfs(b, u)) {
                ++matching;
            }
        }
    }
    return matching;
}

static void test_storage_and_traversal(void) {
    Graph directed;
    graph_init(&directed, 4, true);
    assert(graph_add_edge(&directed, 0, 1, 5));
    assert(graph_add_edge(&directed, 0, 2, 2));
    assert(graph_add_edge(&directed, 2, 1, 1));
    assert(graph_add_edge(&directed, 1, 3, 3));
    assert(graph_add_edge(&directed, 2, 3, 7));
    assert(graph_add_edge(&directed, 3, 0, 4));
    int matrix[MAXV][MAXV];
    graph_to_matrix(&directed, matrix);
    const int expected[4][4] = {
        {0, 5, 2, INF}, {INF, 0, INF, 3},
        {INF, 1, 0, 7}, {4, INF, INF, 0}
    };
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            assert(matrix[i][j] == expected[i][j]);
        }
    }

    Graph g;
    graph_init(&g, 8, false);
    const int edges[][2] = {
        {0, 1}, {0, 2}, {1, 3}, {1, 4},
        {2, 4}, {2, 5}, {4, 5}, {6, 7}
    };
    for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
        assert(graph_add_edge(&g, edges[i][0], edges[i][1], 1));
    }
    const int expected_dfs[] = {0, 1, 3, 4, 2, 5, 6, 7};
    const int expected_bfs[] = {0, 1, 2, 3, 4, 5, 6, 7};
    Sequence dfs = dfs_forest(&g);
    Sequence bfs = bfs_forest(&g);
    assert(sequence_equals(&dfs, expected_dfs, 8));
    assert(sequence_equals(&bfs, expected_bfs, 8));
}

static void test_connectivity(void) {
    Graph scc_graph;
    graph_init(&scc_graph, 5, true);
    const int arcs[][2] = {{0,1},{1,2},{2,0},{2,3},{3,4},{4,3}};
    for (size_t i = 0; i < sizeof(arcs) / sizeof(arcs[0]); ++i) {
        assert(graph_add_edge(&scc_graph, arcs[i][0], arcs[i][1], 1));
    }
    int component[MAXV];
    assert(tarjan_scc(&scc_graph, component) == 2);
    assert(component[0] == component[1] && component[1] == component[2]);
    assert(component[3] == component[4]);
    assert(component[0] != component[3]);

    Edge input[] = {
        {0,1,4},{0,2,2},{1,2,1},{1,3,5},
        {2,3,8},{2,4,10},{3,4,2},{1,4,7}
    };
    Edge chosen[MAXV];
    int chosen_count;
    int total;
    assert(kruskal_mst(5, input, 8, chosen, &chosen_count, &total));
    assert(chosen_count == 4 && total == 10);
    const int expected_weights[] = {1, 2, 2, 5};
    for (int i = 0; i < 4; ++i) {
        assert(chosen[i].weight == expected_weights[i]);
    }

    Graph mst_graph;
    graph_init(&mst_graph, 5, false);
    for (size_t i = 0; i < sizeof(input) / sizeof(input[0]); ++i) {
        assert(graph_add_edge(&mst_graph, input[i].u, input[i].v,
                              input[i].weight));
    }
    int parent[MAXV];
    assert(prim_mst(&mst_graph, 0, parent, &total));
    assert(total == 10);

    Graph cut_graph;
    graph_init(&cut_graph, 6, false);
    const int cut_edges[][2] = {
        {0,1},{1,2},{2,0},{1,3},{3,4},{4,5},{5,3}
    };
    for (size_t i = 0; i < sizeof(cut_edges) / sizeof(cut_edges[0]); ++i) {
        assert(graph_add_edge(&cut_graph, cut_edges[i][0], cut_edges[i][1], 1));
    }
    CutState cuts = articulation_and_bridges(&cut_graph);
    assert(cuts.articulation[1] && cuts.articulation[3]);
    assert(!cuts.articulation[0] && !cuts.articulation[2] &&
           !cuts.articulation[4] && !cuts.articulation[5]);
    assert(cuts.bridge_count == 1);
    Edge bridge = cuts.bridges[0];
    assert((bridge.u == 1 && bridge.v == 3) ||
           (bridge.u == 3 && bridge.v == 1));
}

static void test_shortest_paths(void) {
    Graph g;
    graph_init(&g, 6, true);
    const Edge edges[] = {
        {0,1,4},{0,2,2},{2,1,1},{1,3,5},{2,3,8},
        {2,4,10},{3,4,2},{3,5,6},{4,5,3}
    };
    for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
        assert(graph_add_edge(&g, edges[i].u, edges[i].v, edges[i].weight));
    }
    int dist[MAXV];
    int prev[MAXV];
    Sequence settled = {{0}, 0};
    assert(dijkstra(&g, 0, dist, prev, &settled));
    const int expected_dist[] = {0, 3, 2, 8, 10, 13};
    const int expected_settled[] = {0, 2, 1, 3, 4, 5};
    for (int i = 0; i < 6; ++i) {
        assert(dist[i] == expected_dist[i]);
    }
    assert(sequence_equals(&settled, expected_settled, 6));
    const int expected_prev[] = {-1, 2, 0, 1, 3, 4};
    for (int i = 0; i < 6; ++i) {
        assert(prev[i] == expected_prev[i]);
    }

    const Edge negative_edges[] = {
        {0,1,4},{0,2,5},{1,2,-2},{2,3,3}
    };
    bool negative_cycle;
    assert(bellman_ford(4, negative_edges, 4, 0, dist, prev,
                        &negative_cycle));
    assert(!negative_cycle);
    assert(dist[0] == 0 && dist[1] == 4 && dist[2] == 2 && dist[3] == 5);
    Graph negative_graph;
    graph_init(&negative_graph, 2, true);
    assert(graph_add_edge(&negative_graph, 0, 1, -1));
    assert(!dijkstra(&negative_graph, 0, dist, prev, NULL));
    const Edge cycle_edges[] = {{0,1,1},{1,2,-3},{2,0,1}};
    assert(bellman_ford(3, cycle_edges, 3, 0, dist, prev,
                        &negative_cycle));
    assert(negative_cycle);

    int matrix[MAXV][MAXV];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            matrix[i][j] = INF;
        }
        matrix[i][i] = 0;
    }
    matrix[0][1] = 3; matrix[0][3] = 7;
    matrix[1][0] = 8; matrix[1][2] = 2;
    matrix[2][0] = 5; matrix[2][3] = 1;
    matrix[3][0] = 2;
    int next[MAXV][MAXV];
    assert(floyd_warshall(4, matrix, next, &negative_cycle));
    assert(!negative_cycle);
    const int expected[4][4] = {
        {0,3,5,6},{5,0,2,3},{3,6,0,1},{2,5,7,0}
    };
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            assert(matrix[i][j] == expected[i][j]);
        }
    }
    const int expected_path[] = {0,1,2,3};
    Sequence path = floyd_path(4, 0, 3, next);
    assert(sequence_equals(&path, expected_path, 4));
}

static void test_dag_and_matching(void) {
    Graph tasks;
    graph_init(&tasks, 7, true);
    const int dependencies[][2] = {
        {0,1},{0,2},{1,3},{2,4},{3,5},{4,5},{5,6}
    };
    for (size_t i = 0; i < sizeof(dependencies) / sizeof(dependencies[0]); ++i) {
        assert(graph_add_edge(&tasks, dependencies[i][0], dependencies[i][1], 1));
    }
    Sequence topo = {{0}, 0};
    const int expected_topo[] = {0,1,2,3,4,5,6};
    assert(topological_sort(&tasks, &topo));
    assert(sequence_equals(&topo, expected_topo, 7));
    assert(graph_add_edge(&tasks, 6, 0, 1));
    assert(!topological_sort(&tasks, &topo));

    Graph aoe;
    graph_init(&aoe, 6, true);
    const Edge activities[] = {
        {0,1,3},{0,2,2},{1,3,2},{2,3,4},
        {2,4,3},{3,5,2},{4,5,3}
    };
    for (size_t i = 0; i < sizeof(activities) / sizeof(activities[0]); ++i) {
        assert(graph_add_edge(&aoe, activities[i].u, activities[i].v,
                              activities[i].weight));
    }
    CriticalPath cp;
    assert(critical_path(&aoe, &cp));
    const int expected_ve[] = {0,3,2,6,5,8};
    const int expected_vl[] = {0,4,2,6,5,8};
    assert(cp.duration == 8);
    for (int i = 0; i < 6; ++i) {
        assert(cp.ve[i] == expected_ve[i]);
        assert(cp.vl[i] == expected_vl[i]);
    }
    bool activity_is_critical[7] = {false};
    for (int u = 0; u < aoe.n; ++u) {
        for (int e = aoe.head[u]; e != -1; e = aoe.arcs[e].next) {
            for (int i = 0; i < 7; ++i) {
                if (activities[i].u == u && activities[i].v == aoe.arcs[e].to &&
                    activities[i].weight == aoe.arcs[e].weight) {
                    activity_is_critical[i] = cp.critical_arc[e];
                }
            }
        }
    }
    const bool expected_critical[] = {false,true,false,true,true,true,true};
    for (int i = 0; i < 7; ++i) {
        assert(activity_is_critical[i] == expected_critical[i]);
    }

    Graph even_cycle;
    graph_init(&even_cycle, 4, false);
    assert(graph_add_edge(&even_cycle, 0, 1, 1));
    assert(graph_add_edge(&even_cycle, 1, 2, 1));
    assert(graph_add_edge(&even_cycle, 2, 3, 1));
    assert(graph_add_edge(&even_cycle, 3, 0, 1));
    int color[MAXV];
    assert(is_bipartite(&even_cycle, color));
    assert(graph_add_edge(&even_cycle, 0, 2, 1));
    assert(!is_bipartite(&even_cycle, color));

    BipartiteGraph b;
    bipartite_init(&b, 4, 4);
    const int pairs[][2] = {
        {0,0},{0,1},{1,0},{1,2},
        {2,1},{2,2},{3,2},{3,3}
    };
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        assert(bipartite_add_edge(&b, pairs[i][0], pairs[i][1]));
    }
    assert(hopcroft_karp(&b) == 4);
    bool used_right[MAXV] = {false};
    for (int u = 0; u < 4; ++u) {
        int v = b.pair_left[u];
        assert(v >= 0 && v < 4 && b.edge[u][v] && !used_right[v]);
        used_right[v] = true;
        assert(b.pair_right[v] == u);
    }
}

int main(void) {
    test_storage_and_traversal();
    test_connectivity();
    test_shortest_paths();
    test_dag_and_matching();

    puts("DFS forest: A B D E C F G H");
    puts("BFS forest: A B C D E F G H");
    puts("MST total weight: 10");
    puts("Dijkstra S->T: S B A C D T, distance 13");
    puts("AOE duration: 8; critical activities: B D E F G");
    puts("Bipartite maximum matching: 4");
    puts("All graph tests passed.");
    return 0;
}
