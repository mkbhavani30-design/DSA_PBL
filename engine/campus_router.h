#ifndef CAMPUS_ROUTER_H
#define CAMPUS_ROUTER_H

#include "campus.h"

double* cg_dijkstra_all(Graph* graph, int start_idx, const int* blocked_nodes, int blocked_count);
int* cg_astar(Graph* graph, int start_idx, int target_idx, const int* blocked_nodes, int blocked_count, int* out_path_length);
double cg_path_cost(const Graph* graph, const int* path, int path_length);

#endif