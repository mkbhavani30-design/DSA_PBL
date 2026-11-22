#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include "campus.h"
#include "campus_router.h"
#include "segment_tree.h"
#include "map_loader.h"

static void print_route(const Graph* g, const int* path, int path_len) {
    for (int i = 0; i < path_len; i++) {
        const Node* n = &g->nodes[path[i]];
        printf("%s (F%d)%s", n->str_id, n->floor, (i == path_len - 1) ? "" : " -> ");
    }
    printf("\n");
}

int main(void) {
    const char* map_paths[] = {
        "../db/campus_map.json",
        "db/campus_map.json",
        "campus_map.json"
    };

    Graph* campus_graph = create_graph(256);
    RoomRegistry* room_reg = create_room_registry(64);
    int loaded = 0;

    for (size_t i = 0; i < sizeof(map_paths) / sizeof(map_paths[0]); i++) {
        if (load_campus_map(map_paths[i], campus_graph, room_reg)) {
            printf("Loaded campus map from '%s' (%d nodes, %d bookable rooms).\n\n",
                   map_paths[i], campus_graph->num_nodes, room_reg->count);
            loaded = 1;
            break;
        }
    }

    if (!loaded || room_reg->count == 0) {
        fprintf(stderr, "Failed to load campus_map.json or no bookable rooms found.\n");
        free_room_registry(room_reg);
        free_graph(campus_graph);
        return 1;
    }

    SegmentTree* room_st = create_segment_tree(room_reg->count);

    // Simulate Central Library (LIB) filling up: book all LIB room slots
    for (int i = 0; i < room_reg->count; i++) {
        const Node* rnode = &campus_graph->nodes[room_reg->rooms[i].node_idx];
        if (strcmp(rnode->building_id, "LIB") == 0) {
            st_book(room_st, i, i);
        }
    }
    printf("Central Library study spaces marked full. Searching departments for 1 free room...\n");

    int start_idx = find_node_by_str_id(campus_graph, "LIB_F1_HALL_01");
    if (start_idx == -1) start_idx = 0;

    // Find nearest free room across remaining slots by path cost from start_idx
    int best_room_slot = -1;
    double best_cost = DBL_MAX;
    int* best_path = NULL;
    int best_path_len = 0;

    int search_from = 0;
    while (search_from < room_reg->count) {
        int slot = st_find_first_free(room_st, 1, search_from);
        if (slot == -1) break;

        int target_idx = room_reg->rooms[slot].node_idx;
        int plen = 0;
        int* candidate_path = cg_astar(campus_graph, start_idx, target_idx, NULL, 0, &plen);
        if (candidate_path && plen > 0) {
            double cost = cg_path_cost(campus_graph, candidate_path, plen);
            if (cost >= 0.0 && cost < best_cost) {
                free(best_path);
                best_cost = cost;
                best_room_slot = slot;
                best_path = candidate_path;
                best_path_len = plen;
            } else {
                free(candidate_path);
            }
        }
        search_from = slot + 1;
    }

    if (best_room_slot != -1 && best_path) {
        const Node* chosen = &campus_graph->nodes[room_reg->rooms[best_room_slot].node_idx];
        st_book(room_st, best_room_slot, best_room_slot);
        printf("Nearest free room assigned: %s (%s, Floor %d, Cost: %.2f)\n",
               chosen->name, chosen->building_id, chosen->floor, best_cost);
        printf("Route: ");
        print_route(campus_graph, best_path, best_path_len);
        free(best_path);
    } else {
        printf("No free rooms available.\n");
    }

    free_segment_tree(room_st);
    free_room_registry(room_reg);
    free_graph(campus_graph);
    return 0;
}