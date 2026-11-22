#ifndef CAMPUS_ROUTER_H
#define CAMPUS_ROUTER_H

#include "campus.h"

double* cg_dijkstra_all(Graph* graph, int start_id, const int* blocked_nodes, int blocked_count);
int* cg_astar(Graph* graph, int start_id, int target_id, const int* blocked_nodes, int blocked_count, int* out_path_length);

#endif