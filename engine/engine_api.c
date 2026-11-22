#include "engine_api.h"
#include "campus.h"
#include "campus_router.h"
#include "map_loader.h"
#include "timetable.h"

#include <stdio.h>
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