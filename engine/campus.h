#ifndef CAMPUS_H
#define CAMPUS_H

#include <stdlib.h>
#include <string.h>

typedef struct Edge Edge;

struct Edge {
    int dest_id;          // Internal node index (0 .. num_nodes - 1)
    double weight;
    Edge* next;
};

typedef struct {
    int id;               // Internal node index (0 .. num_nodes - 1)
    int floor;
    double x, y;
    char str_id[64];      // e.g., "LIB_F1_HALL_01", "DEPT_A_F1_CLASS_01"
    char type[32];        // e.g., "study_room", "classroom", "lab", "seminar_hall", "stairs", "elevator"
    char building_id[32]; // e.g., "LIB", "DEPT_A", "DEPT_B"
    char name[64];
    Edge* head;
} Node;

typedef struct {
    int num_nodes;
    int capacity;
    Node* nodes;
} Graph;

Graph* create_graph(int capacity);
int add_node(Graph* graph, const char* str_id, const char* type, const char* building_id,
             int floor, double x, double y, const char* name);
int find_node_by_str_id(const Graph* graph, const char* str_id);
void add_edge(Graph* graph, int src_idx, int dest_idx, double weight);
void free_graph(Graph* graph);

#endif