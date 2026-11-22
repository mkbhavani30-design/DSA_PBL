#ifndef CAMPUS_H
#define CAMPUS_H

#include <stdlib.h>
#include <string.h>

typedef struct Edge Edge;

struct Edge {
    int dest_id;          
    double weight;        
    Edge* next;           
};

typedef struct {
    int id;               
    int floor;            
    double x, y;          // Added planar coordinates for heuristics
    char name[64];        
    Edge* head;           
} Node;

typedef struct {
    int num_nodes;        
    int capacity;         
    Node* nodes;          
} Graph;

Graph* create_graph(int capacity);
void add_node(Graph* graph, int id, int floor, double x, double y, const char* name);
void add_edge(Graph* graph, int src_id, int dest_id, double weight);
void free_graph(Graph* graph);

#endif