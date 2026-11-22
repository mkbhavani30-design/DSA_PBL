#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include "campus.h"
#include "heap.h"

#define STAIR_PENALTY 5.0
#define ELEVATOR_PENALTY 3.0

// Helper to check if a node ID is in a blocked list
static int is_node_blocked(int node_id, const int* blocked_nodes, int blocked_count) {
    if (!blocked_nodes) return 0;
    for (int i = 0; i < blocked_count; i++) {
        if (blocked_nodes[i] == node_id) return 1;
    }
    return 0;
}

// Helper to find node index by ID
static int get_node_idx(Graph* g, int id) {
    for (int i = 0; i < g->num_nodes; i++) {
        if (g->nodes[i].id == id) return i;
    }
    return -1;
}

// 1. cg_dijkstra_all: Computes distances from start to all nodes with stair/elevator penalties
double* cg_dijkstra_all(Graph* graph, int start_id, const int* blocked_nodes, int blocked_count) {
    int n = graph->num_nodes;
    double* distances = (double*)malloc(n * sizeof(double));
    for (int i = 0; i < n; i++) distances[i] = DBL_MAX;

    int start_idx = get_node_idx(graph, start_id);
    if (start_idx == -1 || is_node_blocked(start_id, blocked_nodes, blocked_count)) return distances;

    MinHeap* heap = create_min_heap(n);
    distances[start_idx] = 0.0;
    push_min_heap(heap, start_id, 0.0);

    while (!is_min_heap_empty(heap)) {
        HeapNode curr = pop_min_heap(heap);
        int u_id = curr.node_id;
        int u_idx = get_node_idx(graph, u_id);

        if (u_idx == -1 || curr.cost > distances[u_idx]) continue;

        Edge* edge = graph->nodes[u_idx].head;
        while (edge != NULL) {
            int v_id = edge->dest_id;
            int v_idx = get_node_idx(graph, v_id);

            if (v_idx != -1 && !is_node_blocked(v_id, blocked_nodes, blocked_count)) {
                double weight = edge->weight;

                // Apply vertical transition penalties
                if (graph->nodes[u_idx].floor != graph->nodes[v_idx].floor) {
                    // Check if it's stairs or elevator based on naming or ID pattern
                    weight += STAIR_PENALTY; 
                }

                if (distances[u_idx] + weight < distances[v_idx]) {
                    distances[v_idx] = distances[u_idx] + weight;
                    push_min_heap(heap, v_id, distances[v_idx]);
                }
            }
            edge = edge->next;
        }
    }

    free_min_heap(heap);
    return distances;
}

// Euclidean heuristic function for A*
static double heuristic(Node* a, Node* b) {
    double dx = a->x - b->x;
    double dy = a->y - b->y;
    double floor_diff = abs(a->floor - b->floor) * 10.0; // Penalty per floor change
    return sqrt(dx * dx + dy * dy) + floor_diff;
}

// 2 & 3. cg_astar: Planar heuristic pathfinding with exact path array generation and block support
int* cg_astar(Graph* graph, int start_id, int target_id, const int* blocked_nodes, int blocked_count, int* out_path_length) {
    int n = graph->num_nodes;
    int start_idx = get_node_idx(graph, start_id);
    int target_idx = get_node_idx(graph, target_id);

    if (start_idx == -1 || target_idx == -1 || is_node_blocked(start_id, blocked_nodes, blocked_count)) {
        *out_path_length = 0;
        return NULL;
    }

    double* g_score = (double*)malloc(n * sizeof(double));
    int* previous = (int*)malloc(n * sizeof(int));
    for (int i = 0; i < n; i++) {
        g_score[i] = DBL_MAX;
        previous[i] = -1;
    }

    MinHeap* heap = create_min_heap(n);
    g_score[start_idx] = 0.0;
    
    double initial_f = heuristic(&graph->nodes[start_idx], &graph->nodes[target_idx]);
    push_min_heap(heap, start_id, initial_f);

    while (!is_min_heap_empty(heap)) {
        HeapNode curr = pop_min_heap(heap);
        int u_id = curr.node_id;
        int u_idx = get_node_idx(graph, u_id);

        if (u_idx == target_idx) break; // Reached destination
        if (u_idx == -1) continue;

        Edge* edge = graph->nodes[u_idx].head;
        while (edge != NULL) {
            int v_id = edge->dest_id;
            int v_idx = get_node_idx(graph, v_id);

            if (v_idx != -1 && !is_node_blocked(v_id, blocked_nodes, blocked_count)) {
                double tentative_g = g_score[u_idx] + edge->weight;

                if (tentative_g < g_score[v_idx]) {
                    previous[v_idx] = u_idx;
                    g_score[v_idx] = tentative_g;
                    double f_score = tentative_g + heuristic(&graph->nodes[v_idx], &graph->nodes[target_idx]);
                    push_min_heap(heap, v_id, f_score);
                }
            }
            edge = edge->next;
        }
    }

    // Reconstruct exact node path array if a path exists
    if (g_score[target_idx] == DBL_MAX) {
        *out_path_length = 0;
        free(g_score);
        free(previous);
        free_min_heap(heap);
        return NULL;
    }

    // Count path length
    int length = 0;
    int curr = target_idx;
    while (curr != -1) {
        length++;
        curr = previous[curr];
    }

    int* path = (int*)malloc(length * sizeof(int));
    curr = target_idx;
    for (int i = length - 1; i >= 0; i--) {
        path[i] = graph->nodes[curr].id;
        curr = previous[curr];
    }

    *out_path_length = length;
    free(g_score);
    free(previous);
    free_min_heap(heap);
    return path;
}