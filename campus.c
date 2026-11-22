#include "campus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Graph* create_graph(int capacity) {
    Graph* g = (Graph*)malloc(sizeof(Graph));
    g->num_nodes = 0;
    g->capacity = capacity;
    g->nodes = (Node*)malloc(capacity * sizeof(Node));
    return g;
}

void add_node(Graph* graph, int id, int floor, double x, double y, const char* name) {
    if (graph->num_nodes >= graph->capacity) {
        graph->capacity *= 2;
        graph->nodes = (Node*)realloc(graph->nodes, graph->capacity * sizeof(Node));
    }
    Node* node = &graph->nodes[graph->num_nodes++];
    node->id = id;
    node->floor = floor;
    node->x = x;
    node->y = y;
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = '\0';
    node->head = NULL;
}

void add_edge(Graph* graph, int src_id, int dest_id, double weight) {
    int src_idx = -1, dest_idx = -1;
    for (int i = 0; i < graph->num_nodes; i++) {
        if (graph->nodes[i].id == src_id) src_idx = i;
        if (graph->nodes[i].id == dest_id) dest_idx = i;
    }

    if (src_idx != -1 && dest_idx != -1) {
        Edge* new_edge = (Edge*)malloc(sizeof(Edge));
        new_edge->dest_id = dest_id;
        new_edge->weight = weight;
        new_edge->next = graph->nodes[src_idx].head;
        graph->nodes[src_idx].head = new_edge;
    }
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