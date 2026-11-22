#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <float.h>
#include "campus.h"
#include "heap.h"

// Standard C helper function to find node index by ID
static int get_node_idx(Graph* g, int id) {
    for (int i = 0; i < g->num_nodes; i++) {
        if (g->nodes[i].id == id) return i;
    }
    return -1;
}

void find_shortest_path(Graph* graph, int start_id, int target_id) {
    int start_idx = get_node_idx(graph, start_id);
    int target_idx = get_node_idx(graph, target_id);

    if (start_idx == -1 || target_idx == -1) {
        printf("Start or target node not found in graph!\n");
        return;
    }

    int n = graph->num_nodes;
    double* distances = (double*)malloc(n * sizeof(double));
    int* previous = (int*)malloc(n * sizeof(int));
    
    for (int i = 0; i < n; i++) {
        distances[i] = DBL_MAX;
        previous[i] = -1;
    }

    MinHeap* heap = create_min_heap(n);

    distances[start_idx] = 0.0;
    push_min_heap(heap, start_id, 0.0);

    while (!is_min_heap_empty(heap)) {
        HeapNode current_hn = pop_min_heap(heap);
        int u_id = current_hn.node_id;
        int u_idx = get_node_idx(graph, u_id);

        if (u_idx == -1) continue;

        // If we reached the target, we can stop or break
        if (u_id == target_id) break;

        // Skip if a shorter path to u has already been found
        if (current_hn.cost > distances[u_idx]) continue;

        // Traverse neighbors using the adjacency list
        Edge* edge = graph->nodes[u_idx].head;
        while (edge != NULL) {
            int v_id = edge->dest_id;
            int v_idx = get_node_idx(graph, v_id);
            double weight = edge->weight;

            if (v_idx != -1 && distances[u_idx] + weight < distances[v_idx]) {
                distances[v_idx] = distances[u_idx] + weight;
                previous[v_idx] = u_idx;
                push_min_heap(heap, v_id, distances[v_idx]);
            }
            edge = edge->next;
        }
    }

    // Output results
    if (distances[target_idx] == DBL_MAX) {
        printf("No path found between node %d and node %d.\n", start_id, target_id);
    } else {
        printf("Shortest distance from %d to %d is: %.2f\n", start_id, target_id, distances[target_idx]);
    }

    // Cleanup
    free(distances);
    free(previous);
    free_min_heap(heap);
}