#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include "campus.h"
#include "heap.h"
#include "campus_router.h"

#define STAIR_PENALTY 5.0
#define ELEVATOR_PENALTY 3.0

static int is_node_blocked(int node_idx, const int* blocked_nodes, int blocked_count) {
    if (!blocked_nodes) return 0;
    for (int i = 0; i < blocked_count; i++) {
        if (blocked_nodes[i] == node_idx) return 1;
    }
    return 0;
}

static int is_valid_idx(const Graph* g, int idx) {
    return g && idx >= 0 && idx < g->num_nodes;
}

static int is_elevator_transition(const Node* a, const Node* b) {
    if (strcmp(a->type, "elevator") == 0 || strcmp(b->type, "elevator") == 0) return 1;
    if (strstr(a->str_id, "ELEV") != NULL || strstr(b->str_id, "ELEV") != NULL) return 1;
    return 0;
}

static double edge_traversal_weight(const Graph* g, int u_idx, int v_idx, double base_weight) {
    double weight = base_weight;
    const Node* u = &g->nodes[u_idx];
    const Node* v = &g->nodes[v_idx];

    if (u->floor != v->floor) {
        int floor_delta = abs(u->floor - v->floor);
        if (is_elevator_transition(u, v)) {
            weight += ELEVATOR_PENALTY * floor_delta;
        } else {
            weight += STAIR_PENALTY * floor_delta;
        }
    }
    return weight;
}

double* cg_dijkstra_all(Graph* graph, int start_idx, const int* blocked_nodes, int blocked_count) {
    if (!graph || graph->num_nodes <= 0) return NULL;
    int n = graph->num_nodes;
    double* distances = (double*)malloc((size_t)n * sizeof(double));
    if (!distances) return NULL;

    for (int i = 0; i < n; i++) distances[i] = DBL_MAX;

    if (!is_valid_idx(graph, start_idx) || is_node_blocked(start_idx, blocked_nodes, blocked_count)) {
        return distances;
    }

    MinHeap* heap = create_min_heap(n);
    distances[start_idx] = 0.0;
    push_min_heap(heap, start_idx, 0.0);

    while (!is_min_heap_empty(heap)) {
        HeapNode curr = pop_min_heap(heap);
        int u_idx = curr.node_id;

        if (!is_valid_idx(graph, u_idx) || curr.cost > distances[u_idx]) continue;

        Edge* edge = graph->nodes[u_idx].head;
        while (edge != NULL) {
            int v_idx = edge->dest_id;
            if (is_valid_idx(graph, v_idx) && !is_node_blocked(v_idx, blocked_nodes, blocked_count)) {
                double w = edge_traversal_weight(graph, u_idx, v_idx, edge->weight);
                if (distances[u_idx] + w < distances[v_idx]) {
                    distances[v_idx] = distances[u_idx] + w;
                    push_min_heap(heap, v_idx, distances[v_idx]);
                }
            }
            edge = edge->next;
        }
    }

    free_min_heap(heap);
    return distances;
}

static double heuristic(const Node* a, const Node* b) {
    double dx = a->x - b->x;
    double dy = a->y - b->y;
    double floor_diff = fabs((double)(a->floor - b->floor)) * ELEVATOR_PENALTY;
    return sqrt(dx * dx + dy * dy) + floor_diff;
}

int* cg_astar(Graph* graph, int start_idx, int target_idx, const int* blocked_nodes, int blocked_count, int* out_path_length) {
    if (out_path_length) *out_path_length = 0;
    if (!graph || !is_valid_idx(graph, start_idx) || !is_valid_idx(graph, target_idx)) {
        return NULL;
    }
    if (is_node_blocked(start_idx, blocked_nodes, blocked_count) ||
        is_node_blocked(target_idx, blocked_nodes, blocked_count)) {
        return NULL;
    }

    int n = graph->num_nodes;
    double* g_score = (double*)malloc((size_t)n * sizeof(double));
    int* previous = (int*)malloc((size_t)n * sizeof(int));
    if (!g_score || !previous) {
        free(g_score);
        free(previous);
        return NULL;
    }

    for (int i = 0; i < n; i++) {
        g_score[i] = DBL_MAX;
        previous[i] = -1;
    }

    MinHeap* heap = create_min_heap(n);
    g_score[start_idx] = 0.0;
    double initial_f = heuristic(&graph->nodes[start_idx], &graph->nodes[target_idx]);
    push_min_heap(heap, start_idx, initial_f);

    while (!is_min_heap_empty(heap)) {
        HeapNode curr = pop_min_heap(heap);
        int u_idx = curr.node_id;

        if (!is_valid_idx(graph, u_idx)) continue;
        if (u_idx == target_idx) break;
        if (curr.cost > g_score[u_idx] + heuristic(&graph->nodes[u_idx], &graph->nodes[target_idx])) {
            continue;
        }

        Edge* edge = graph->nodes[u_idx].head;
        while (edge != NULL) {
            int v_idx = edge->dest_id;
            if (is_valid_idx(graph, v_idx) && !is_node_blocked(v_idx, blocked_nodes, blocked_count)) {
                double w = edge_traversal_weight(graph, u_idx, v_idx, edge->weight);
                double tentative_g = g_score[u_idx] + w;

                if (tentative_g < g_score[v_idx]) {
                    previous[v_idx] = u_idx;
                    g_score[v_idx] = tentative_g;
                    double f_score = tentative_g + heuristic(&graph->nodes[v_idx], &graph->nodes[target_idx]);
                    push_min_heap(heap, v_idx, f_score);
                }
            }
            edge = edge->next;
        }
    }

    if (g_score[target_idx] == DBL_MAX) {
        free(g_score);
        free(previous);
        free_min_heap(heap);
        return NULL;
    }

    int length = 0;
    int curr = target_idx;
    while (curr != -1) {
        length++;
        curr = previous[curr];
    }

    int* path = (int*)malloc((size_t)length * sizeof(int));
    if (!path) {
        free(g_score);
        free(previous);
        free_min_heap(heap);
        return NULL;
    }

    curr = target_idx;
    for (int i = length - 1; i >= 0; i--) {
        path[i] = curr;
        curr = previous[curr];
    }

    if (out_path_length) *out_path_length = length;
    free(g_score);
    free(previous);
    free_min_heap(heap);
    return path;
}

double cg_path_cost(const Graph* graph, const int* path, int path_length) {
    if (!graph || !path || path_length <= 1) return 0.0;
    double total = 0.0;
    for (int i = 0; i < path_length - 1; i++) {
        int u = path[i];
        int v = path[i + 1];
        if (!is_valid_idx(graph, u) || !is_valid_idx(graph, v)) return -1.0;

        Edge* e = graph->nodes[u].head;
        double base = -1.0;
        while (e != NULL) {
            if (e->dest_id == v) {
                base = e->weight;
                break;
            }
            e = e->next;
        }
        if (base < 0.0) return -1.0;
        total += edge_traversal_weight(graph, u, v, base);
    }
    return total;
}