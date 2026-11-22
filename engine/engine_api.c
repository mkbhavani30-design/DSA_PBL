#include "engine_api.h"
#include "campus.h"
#include "campus_router.h"
#include "map_loader.h"
#include "timetable.h"

#include <stdio.h>
#include<float.h>
#include <stdlib.h>
#include <string.h>

static Graph* g_graph = NULL;
static RoomRegistry* g_rooms = NULL;
static TimetableStore* g_tt = NULL;

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

    g_tt = tt_create_and_seed(g_graph);
    if (!g_tt) {
        engine_shutdown();
        return 0;
    }

    return 1;
}

API_EXPORT void engine_shutdown(void) {
    if (g_tt) {
        tt_free(g_tt);
        g_tt = NULL;
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

API_EXPORT int engine_load_state(const char* state_path) {
    return tt_load_state(g_tt, state_path);
}

API_EXPORT int engine_save_state(const char* state_path) {
    return tt_save_state(g_tt, state_path);
}

API_EXPORT int engine_get_room_count(void) {
    return g_tt ? g_tt->count : 0;
}

API_EXPORT const char* engine_get_room_id(int slot) {
    if (!g_tt || slot < 0 || slot >= g_tt->count) return "";
    return g_tt->entries[slot].room_id;
}

API_EXPORT const char* engine_get_room_name(int slot) {
    if (!g_tt || slot < 0 || slot >= g_tt->count) return "";
    return g_tt->entries[slot].room_name;
}

API_EXPORT const char* engine_get_room_type(int slot) {
    if (!g_tt || slot < 0 || slot >= g_tt->count) return "";
    return g_tt->entries[slot].room_type;
}

API_EXPORT const char* engine_get_room_building(int slot) {
    if (!g_tt || slot < 0 || slot >= g_tt->count) return "";
    return g_tt->entries[slot].building_id;
}

API_EXPORT int engine_get_room_floor(int slot) {
    if (!g_tt || slot < 0 || slot >= g_tt->count) return -1;
    return g_tt->entries[slot].floor;
}

API_EXPORT int engine_is_room_free_at(int slot, const char* day_str, const char* shift_str) {
    int d = tt_parse_day(day_str);
    int s = tt_parse_shift(shift_str);
    return tt_is_slot_free(g_tt, slot, d, s);
}

API_EXPORT const char* engine_get_slot_label(int slot, const char* day_str, const char* shift_str) {
    if (!g_tt || slot < 0 || slot >= g_tt->count) return "";
    int d = tt_parse_day(day_str);
    int s = tt_parse_shift(shift_str);
    if (g_tt->entries[slot].live_booked[d][s]) return "LIVE_BOOKED";
    return g_tt->entries[slot].grid[d][s].label;
}

API_EXPORT int engine_book_room_at(const char* room_id, const char* day_str, const char* shift_str) {
    int d = tt_parse_day(day_str);
    int s = tt_parse_shift(shift_str);
    return tt_book_slot(g_tt, room_id, d, s);
}

API_EXPORT int engine_release_room_at(const char* room_id, const char* day_str, const char* shift_str) {
    int d = tt_parse_day(day_str);
    int s = tt_parse_shift(shift_str);
    return tt_release_slot(g_tt, room_id, d, s);
}

API_EXPORT void engine_set_library_full_at(const char* day_str, const char* shift_str, int full) {
    int d = tt_parse_day(day_str);
    int s = tt_parse_shift(shift_str);
    tt_set_library_full_for_slot(g_tt, d, s, full);
}

API_EXPORT int engine_list_rooms_json(
    const char* day_str,
    const char* shift_str,
    const char* building_filter,
    const char* type_filter,
    int only_free,
    char* out_json_buf,
    int out_json_buf_len
) {
    if (!g_tt || !out_json_buf || out_json_buf_len <= 0) return 0;
    int d = tt_parse_day(day_str);
    int s = tt_parse_shift(shift_str);
    int other_s = 1 - s;

    size_t pos = (size_t)snprintf(out_json_buf, (size_t)out_json_buf_len,
                                  "{\"day\":\"%s\",\"shift\":\"%s\",\"rooms\":[",
                                  tt_day_name(d), tt_shift_name(s));
    int first = 1;
    int matched = 0;

    for (int i = 0; i < g_tt->count; i++) {
        const RoomSchedule* rs = &g_tt->entries[i];
        if (building_filter && building_filter[0] != '\0' && strcmp(rs->building_id, building_filter) != 0)
            continue;
        if (type_filter && type_filter[0] != '\0' && strcmp(rs->room_type, type_filter) != 0)
            continue;

        int is_free = tt_is_slot_free(g_tt, i, d, s);
        if (only_free && !is_free) continue;

        int other_free = tt_is_slot_free(g_tt, i, d, other_s);
        const char* slot_info = rs->live_booked[d][s] ? "LIVE_BOOKED" : rs->grid[d][s].label;

        if (pos + 320 < (size_t)out_json_buf_len) {
            pos += (size_t)snprintf(
                out_json_buf + pos,
                (size_t)out_json_buf_len - pos,
                "%s{\"slot\":%d,\"id\":\"%s\",\"name\":\"%s\",\"type\":\"%s\","
                "\"building_id\":\"%s\",\"floor\":%d,\"is_free\":%s,\"slot_status\":\"%s\","
                "\"free_in_%s\":%s}",
                first ? "" : ",",
                i, rs->room_id, rs->room_name, rs->room_type,
                rs->building_id, rs->floor,
                is_free ? "true" : "false",
                slot_info,
                s == 0 ? "afternoon" : "morning",
                other_free ? "true" : "false"
            );
            first = 0;
            matched++;
        }
    }

    snprintf(out_json_buf + pos, (size_t)out_json_buf_len - pos, "],\"count\":%d}", matched);
    return 1;
}

API_EXPORT int engine_find_overflow_json(
    const char* start_node_id,
    const char* day_str,
    const char* shift_str,
    const char* prefer_building,
    int prefer_stairs,
    int prefer_lower_floor,
    int auto_book,
    char* out_json_buf,
    int out_json_buf_len
) {
    if (!g_graph || !g_tt || !out_json_buf || out_json_buf_len <= 0) return 0;

    int d = tt_parse_day(day_str);
    int s = tt_parse_shift(shift_str);

    RoutingPrefs prefs;
    memset(&prefs, 0, sizeof(prefs));
    prefs.prefer_stairs = prefer_stairs ? 1 : 0;
    prefs.prefer_lower_floor = prefer_lower_floor ? 1 : 0;
    if (prefer_building) {
        snprintf(prefs.prefer_building, sizeof(prefs.prefer_building), "%s", prefer_building);
    }

    OverflowCandidate cand = tt_find_best_overflow_room(g_graph, g_tt, start_node_id, d, s, &prefs);
    if (!cand.found || cand.room_idx < 0) {
        tt_free_candidate(&cand);
        return 0;
    }

    const RoomSchedule* rs = &g_tt->entries[cand.room_idx];
    if (auto_book) {
        tt_book_slot(g_tt, rs->room_id, d, s);
    }

    size_t pos = (size_t)snprintf(
        out_json_buf,
        (size_t)out_json_buf_len,
        "{\"room_id\":\"%s\",\"room_name\":\"%s\",\"room_type\":\"%s\","
        "\"building_id\":\"%s\",\"floor\":%d,\"day\":\"%s\",\"shift\":\"%s\","
        "\"priority_tier\":%d,\"path_cost\":%.2f,\"score\":%.2f,\"booked\":%s,"
        "\"opens_other_shift\":%s,\"other_shift\":\"%s\",\"note\":\"%s\",\"path\":[",
        rs->room_id, rs->room_name, rs->room_type,
        rs->building_id, rs->floor,
        tt_day_name(d), tt_shift_name(s),
        cand.tier, cand.path_cost, cand.total_score,
        auto_book ? "true" : "false",
        cand.opens_other_shift ? "true" : "false",
        cand.other_shift_name,
        cand.note
    );

    for (int i = 0; i < cand.path_len; i++) {
        const char* sid = g_graph->nodes[cand.path[i]].str_id;
        pos += (size_t)snprintf(out_json_buf + pos, (size_t)out_json_buf_len - pos,
                                "%s\"%s\"", i == 0 ? "" : ",", sid);
    }

    snprintf(out_json_buf + pos, (size_t)out_json_buf_len - pos, "]}");
    tt_free_candidate(&cand);
    return 1;
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

    int s = -1, t = -1;
    for (int i = 0; i < g_graph->num_nodes; i++) {
        if (strcmp(g_graph->nodes[i].str_id, start_str_id) == 0) s = i;
        if (strcmp(g_graph->nodes[i].str_id, target_str_id) == 0) t = i;
    }
    if (s == -1 || t == -1) return -1;

    int plen = 0;
    int* path = cg_astar(g_graph, s, t, NULL, 0, &plen);
    if (!path || plen <= 0) {
        free(path);
        return -1;
    }

    if (out_cost) *out_cost = cg_path_cost(g_graph, path, plen);
    int offset = 0;
    for (int i = 0; i < plen; i++) {
        int written = snprintf(out_path_buf + offset, (size_t)(out_path_buf_len - offset),
                               "%s%s", g_graph->nodes[path[i]].str_id, (i == plen - 1) ? "" : "|");
        if (written < 0 || offset + written >= out_path_buf_len) break;
        offset += written;
    }
    free(path);
    return plen;
}
API_EXPORT int engine_top3_overflow_json(
    const char* start_node_id,
    const char* day_str,
    const char* shift_str,
    const char* prefer_building,
    int prefer_stairs,
    int prefer_lower_floor,
    char* out_json_buf,
    int out_json_buf_len
) {
    if (!g_graph || !g_tt || !out_json_buf || out_json_buf_len <= 0) return 0;

    int d = tt_parse_day(day_str);
    int s = tt_parse_shift(shift_str);

    int start_idx = -1;
    for (int i = 0; i < g_graph->num_nodes; i++) {
        if (strcmp(g_graph->nodes[i].str_id, start_node_id) == 0) {
            start_idx = i;
            break;
        }
    }
    if (start_idx == -1) start_idx = 0;

    const char* avoid_type = prefer_stairs ? "elevator" : "stairs";
    int* blocked = (int*)malloc((size_t)g_graph->num_nodes * sizeof(int));
    int blocked_cnt = 0;
    if (blocked) {
        for (int i = 0; i < g_graph->num_nodes; i++) {
            if (strcmp(g_graph->nodes[i].type, avoid_type) == 0) {
                blocked[blocked_cnt++] = i;
            }
        }
    }

    typedef struct {
        int room_idx;
        int tier;
        double cost;
        double score;
        int* path;
        int path_len;
    } RankedRoom;

    RankedRoom top[3];
    for (int k = 0; k < 3; k++) {
        top[k].room_idx = -1;
        top[k].tier = 4;
        top[k].cost = DBL_MAX;
        top[k].score = DBL_MAX;
        top[k].path = NULL;
        top[k].path_len = 0;
    }

    for (int r = 0; r < g_tt->count; r++) {
        const RoomSchedule* rs = &g_tt->entries[r];
        if (!tt_is_slot_free(g_tt, r, d, s)) continue;

        const Node* rn = &g_graph->nodes[rs->node_idx];
        int tier = 4;
        double suit = 6.0;
        if (strcmp(rn->type, "classroom") == 0) { tier = 1; suit = 0.0; }
        else if (strcmp(rn->type, "seminar_hall") == 0) { tier = 3; suit = 4.0; }
        else if (strcmp(rn->type, "lab") == 0) { tier = 2; suit = 9.0; }

        int plen = 0;
        int* path = cg_astar(g_graph, start_idx, rs->node_idx, blocked, blocked_cnt, &plen);
        if (!path && blocked_cnt > 0) {
            path = cg_astar(g_graph, start_idx, rs->node_idx, NULL, 0, &plen);
        }
        if (!path || plen <= 0) {
            free(path);
            continue;
        }

        double cost = cg_path_cost(g_graph, path, plen);
        double score = cost + suit + ((tier - 1) * 12.0);
        if (prefer_building && prefer_building[0] != '\0') {
            score += (strcmp(rs->building_id, prefer_building) == 0) ? -8.0 : 4.0;
        }
        if (prefer_lower_floor) {
            score += (double)(rs->floor - 1) * 1.5;
        }

        for (int k = 0; k < 3; k++) {
            if (score < top[k].score) {
                free(top[2].path);
                for (int m = 2; m > k; m--) {
                    top[m] = top[m - 1];
                }
                top[k].room_idx = r;
                top[k].tier = tier;
                top[k].cost = cost;
                top[k].score = score;
                top[k].path = path;
                top[k].path_len = plen;
                path = NULL;
                break;
            }
        }
        free(path);
    }

    free(blocked);

    size_t pos = (size_t)snprintf(out_json_buf, (size_t)out_json_buf_len,
                                  "{\"start\":\"%s\",\"day\":\"%s\",\"shift\":\"%s\",\"rooms\":[",
                                  start_node_id, tt_day_name(d), tt_shift_name(s));
    int first = 1;
    int count = 0;
    for (int k = 0; k < 3; k++) {
        if (top[k].room_idx < 0) continue;
        const RoomSchedule* rs = &g_tt->entries[top[k].room_idx];
        int other_s = 1 - s;
        int other_free = tt_is_slot_free(g_tt, top[k].room_idx, d, other_s);

        pos += (size_t)snprintf(
            out_json_buf + pos,
            (size_t)out_json_buf_len - pos,
            "%s{\"rank\":%d,\"room_id\":\"%s\",\"room_name\":\"%s\",\"room_type\":\"%s\","
            "\"building_id\":\"%s\",\"floor\":%d,\"priority_tier\":%d,\"path_cost\":%.2f,"
            "\"score\":%.2f,\"free_in_%s\":%s,\"path\":[",
            first ? "" : ",",
            k + 1, rs->room_id, rs->room_name, rs->room_type,
            rs->building_id, rs->floor, top[k].tier, top[k].cost, top[k].score,
            s == 0 ? "afternoon" : "morning",
            other_free ? "true" : "false"
        );
        for (int p = 0; p < top[k].path_len; p++) {
            pos += (size_t)snprintf(out_json_buf + pos, (size_t)out_json_buf_len - pos,
                                    "%s\"%s\"", p == 0 ? "" : ",", g_graph->nodes[top[k].path[p]].str_id);
        }
        pos += (size_t)snprintf(out_json_buf + pos, (size_t)out_json_buf_len - pos, "]}");
        first = 0;
        count++;
        free(top[k].path);
    }

    snprintf(out_json_buf + pos, (size_t)out_json_buf_len - pos, "],\"count\":%d}", count);
    return count > 0 ? 1 : 0;
}