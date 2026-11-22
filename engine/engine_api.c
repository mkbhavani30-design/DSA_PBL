#include "engine_api.h"
#include "campus.h"
#include "campus_router.h"
#include "segment_tree.h"
#include "map_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

static Graph* g_graph = NULL;
static RoomRegistry* g_rooms = NULL;
static SegmentTree* g_st = NULL;
static int* g_booked_flags = NULL;

API_EXPORT int engine_init(const char* map_json_path) {
    engine_shutdown();

    g_graph = create_graph(256);
    g_rooms = create_room_registry(64);
    if (!g_graph || !g_rooms) {
        engine_shutdown();
        return 0;
    }

    if (!load_campus_map(map_json_path, g_graph, g_rooms) || g_rooms->count <= 0) {
        engine_shutdown();
        return 0;
    }

    g_st = create_segment_tree(g_rooms->count);
    g_booked_flags = (int*)calloc((size_t)g_rooms->count, sizeof(int));
    if (!g_st || !g_booked_flags) {
        engine_shutdown();
        return 0;
    }

    return 1;
}

API_EXPORT void engine_shutdown(void) {
    if (g_booked_flags) {
        free(g_booked_flags);
        g_booked_flags = NULL;
    }
    if (g_st) {
        free_segment_tree(g_st);
        g_st = NULL;
    }
    if (g_rooms) {
        free_room_registry(g_rooms);
        g_rooms = NULL;
    }
    if (g_graph) {
        free_graph(g_graph);
        g_graph = NULL;
    }
}

API_EXPORT int engine_get_room_count(void) {
    return g_rooms ? g_rooms->count : 0;
}

static const Node* get_room_node(int slot) {
    if (!g_graph || !g_rooms || slot < 0 || slot >= g_rooms->count) return NULL;
    return &g_graph->nodes[g_rooms->rooms[slot].node_idx];
}

API_EXPORT const char* engine_get_room_id(int slot) {
    const Node* n = get_room_node(slot);
    return n ? n->str_id : "";
}

API_EXPORT const char* engine_get_room_name(int slot) {
    const Node* n = get_room_node(slot);
    return n ? n->name : "";
}

API_EXPORT const char* engine_get_room_type(int slot) {
    const Node* n = get_room_node(slot);
    return n ? n->type : "";
}

API_EXPORT const char* engine_get_room_building(int slot) {
    const Node* n = get_room_node(slot);
    return n ? n->building_id : "";
}

API_EXPORT int engine_get_room_floor(int slot) {
    const Node* n = get_room_node(slot);
    return n ? n->floor : -1;
}

API_EXPORT int engine_is_room_free(int slot) {
    if (!g_booked_flags || !g_rooms || slot < 0 || slot >= g_rooms->count) return 0;
    return g_booked_flags[slot] == 0 ? 1 : 0;
}

API_EXPORT int engine_book_room(int slot) {
    if (!g_st || !g_booked_flags || !g_rooms || slot < 0 || slot >= g_rooms->count) return 0;
    st_book(g_st, slot, slot);
    g_booked_flags[slot] = 1;
    return 1;
}

API_EXPORT int engine_release_room(int slot) {
    if (!g_st || !g_booked_flags || !g_rooms || slot < 0 || slot >= g_rooms->count) return 0;
    st_release(g_st, slot, slot);
    g_booked_flags[slot] = 0;
    return 1;
}

static int find_slot_by_str_id(const char* str_id) {
    if (!g_graph || !g_rooms || !str_id) return -1;
    for (int i = 0; i < g_rooms->count; i++) {
        const Node* n = &g_graph->nodes[g_rooms->rooms[i].node_idx];
        if (strcmp(n->str_id, str_id) == 0) return i;
    }
    return -1;
}

API_EXPORT int engine_book_by_str_id(const char* str_id) {
    int slot = find_slot_by_str_id(str_id);
    return engine_book_room(slot);
}

API_EXPORT int engine_release_by_str_id(const char* str_id) {
    int slot = find_slot_by_str_id(str_id);
    return engine_release_room(slot);
}

API_EXPORT void engine_set_library_full(int full) {
    if (!g_graph || !g_rooms) return;
    for (int i = 0; i < g_rooms->count; i++) {
        const Node* n = &g_graph->nodes[g_rooms->rooms[i].node_idx];
        if (strcmp(n->building_id, "LIB") == 0) {
            if (full) engine_book_room(i);
            else engine_release_room(i);
        }
    }
}

static void serialize_path(const Graph* g, const int* path, int plen, char* buf, int buf_len) {
    if (!buf || buf_len <= 0) return;
    buf[0] = '\0';
    int offset = 0;
    for (int i = 0; i < plen; i++) {
        const char* sid = g->nodes[path[i]].str_id;
        int written = snprintf(buf + offset, (size_t)(buf_len - offset), "%s%s",
                               sid, (i == plen - 1) ? "" : "|");
        if (written < 0 || offset + written >= buf_len) break;
        offset += written;
    }
}

API_EXPORT int engine_find_nearest_free_room(
    const char* start_str_id,
    const char* exclude_building,
    const int* blocked_node_indices,
    int blocked_count,
    double* out_cost,
    char* out_path_buf,
    int out_path_buf_len
) {
    if (out_cost) *out_cost = -1.0;
    if (out_path_buf && out_path_buf_len > 0) out_path_buf[0] = '\0';
    if (!g_graph || !g_rooms || !g_st) return -1;

    int start_idx = find_node_by_str_id(g_graph, start_str_id);
    if (start_idx == -1) return -1;

    int best_slot = -1;
    double best_cost = DBL_MAX;
    int* best_path = NULL;
    int best_plen = 0;

    int search_from = 0;
    while (search_from < g_rooms->count) {
        int slot = st_find_first_free(g_st, 1, search_from);
        if (slot == -1) break;

        int target_idx = g_rooms->rooms[slot].node_idx;
        const Node* target_node = &g_graph->nodes[target_idx];

        if (!exclude_building || exclude_building[0] == '\0' ||
            strcmp(target_node->building_id, exclude_building) != 0) {
            int plen = 0;
            int* path = cg_astar(g_graph, start_idx, target_idx, blocked_node_indices, blocked_count, &plen);
            if (path && plen > 0) {
                double cost = cg_path_cost(g_graph, path, plen);
                if (cost >= 0.0 && cost < best_cost) {
                    free(best_path);
                    best_cost = cost;
                    best_slot = slot;
                    best_path = path;
                    best_plen = plen;
                } else {
                    free(path);
                }
            }
        }
        search_from = slot + 1;
    }

    if (best_slot != -1 && best_path) {
        if (out_cost) *out_cost = best_cost;
        serialize_path(g_graph, best_path, best_plen, out_path_buf, out_path_buf_len);
        free(best_path);
    }

    return best_slot;
}

API_EXPORT int engine_shortest_path(
    const char* start_str_id,
    const char* target_str_id,
    double* out_cost,
    char* out_path_buf,
    int out_path_buf_len
) {
    if (out_cost) *out_cost = -1.0;
    if (out_path_buf && out_path_buf_len > 0) out_path_buf[0] = '\0';
    if (!g_graph) return -1;

    int s = find_node_by_str_id(g_graph, start_str_id);
    int t = find_node_by_str_id(g_graph, target_str_id);
    if (s == -1 || t == -1) return -1;

    int plen = 0;
    int* path = cg_astar(g_graph, s, t, NULL, 0, &plen);
    if (!path || plen <= 0) return -1;

    if (out_cost) *out_cost = cg_path_cost(g_graph, path, plen);
    serialize_path(g_graph, path, plen, out_path_buf, out_path_buf_len);
    free(path);
    return plen;
}