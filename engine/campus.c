#include "campus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Graph* create_graph(int capacity) {
    if (capacity <= 0) capacity = 16;
    Graph* g = (Graph*)malloc(sizeof(Graph));
    if (!g) return NULL;
    g->num_nodes = 0;
    g->capacity = capacity;
    g->nodes = (Node*)malloc((size_t)capacity * sizeof(Node));
    if (!g->nodes) {
        free(g);
        return NULL;
    }
    return g;
}

int find_node_by_str_id(const Graph* graph, const char* str_id) {
    if (!graph || !str_id) return -1;
    for (int i = 0; i < graph->num_nodes; i++) {
        if (strcmp(graph->nodes[i].str_id, str_id) == 0) {
            return i;
        }
    }
    return -1;
}

int add_node(Graph* graph, const char* str_id, const char* type, const char* building_id,
             int floor, double x, double y, const char* name) {
    if (!graph) return -1;

    int existing = find_node_by_str_id(graph, str_id);
    if (existing != -1) return existing;

    if (graph->num_nodes >= graph->capacity) {
        int new_cap = graph->capacity * 2;
        Node* resized = (Node*)realloc(graph->nodes, (size_t)new_cap * sizeof(Node));
        if (!resized) return -1;
        graph->nodes = resized;
        graph->capacity = new_cap;
    }

    int idx = graph->num_nodes++;
    Node* node = &graph->nodes[idx];
    node->id = idx;
    node->floor = floor;
    node->x = x;
    node->y = y;

    snprintf(node->str_id, sizeof(node->str_id), "%s", str_id ? str_id : "");
    snprintf(node->type, sizeof(node->type), "%s", type ? type : "");
    snprintf(node->building_id, sizeof(node->building_id), "%s", building_id ? building_id : "");
    snprintf(node->name, sizeof(node->name), "%s", name ? name : "");
    node->head = NULL;
    return idx;
}

void add_edge(Graph* graph, int src_idx, int dest_idx, double weight) {
    if (!graph) return;
    if (src_idx < 0 || src_idx >= graph->num_nodes || dest_idx < 0 || dest_idx >= graph->num_nodes) {
        return;
    }

    // Avoid duplicate edges between the same pair
    Edge* cur = graph->nodes[src_idx].head;
    while (cur != NULL) {
        if (cur->dest_id == dest_idx) return;
        cur = cur->next;
    }

    Edge* new_edge = (Edge*)malloc(sizeof(Edge));
    if (!new_edge) return;
    new_edge->dest_id = dest_idx;
    new_edge->weight = weight;
    new_edge->next = graph->nodes[src_idx].head;
    graph->nodes[src_idx].head = new_edge;
}

void free_graph(Graph* graph) {
    if (!graph) return;
    for (int i = 0; i < graph->num_nodes; i++) {
        Edge* current = graph->nodes[i].head;
        while (current != NULL) {
            Edge* temp = current;
            current = current->next;
            free(temp);
        }
    }
    free(graph->nodes);
    free(graph);
}