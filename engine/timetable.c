#include "timetable.h"
#include "campus_router.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <float.h>

static const char* DAY_NAMES[TT_DAYS] = { "MON", "TUE", "WED", "THU", "FRI" };
static const char* SHIFT_NAMES[TT_SHIFTS] = { "MORNING", "AFTERNOON" };

static const char* SUBJECTS[6] = {
    "Data Structures",
    "Digital Logic",
    "Computer Org",
    "Linear Algebra",
    "Quantum Comp",
    "OOP Systems"
};

static const char* LABS[3] = {
    "DSA & Systems Lab",
    "Hardware & Logic Lab",
    "Simulation & Compute Lab"
};

static int is_candidate_room_node(const Node* n) {
    if (!n) return 0;
    return (strcmp(n->type, "classroom") == 0 ||
            strcmp(n->type, "lab") == 0 ||
            strcmp(n->type, "seminar_hall") == 0 ||
            strcmp(n->type, "study_room") == 0);
}

static int classify_tier(const Node* n) {
    if (strcmp(n->type, "classroom") == 0) return TIER_CLASSROOM;
    if (strcmp(n->type, "lab") == 0) return TIER_LAB;
    if (strcmp(n->type, "seminar_hall") == 0) return TIER_SEMINAR_HALL;
    return TIER_QUIET_ZONE;
}

static double suitability_penalty(int tier) {
    switch (tier) {
        case TIER_CLASSROOM:    return 0.0;
        case TIER_SEMINAR_HALL: return 4.0;
        case TIER_QUIET_ZONE:   return 6.0;
        case TIER_LAB:          return 9.0; // Equipment/noise & lab-priority penalty
        default:                return 10.0;
    }
}

TimetableStore* tt_create_and_seed(const Graph* graph) {
    if (!graph) return NULL;

    TimetableStore* store = (TimetableStore*)calloc(1, sizeof(TimetableStore));
    if (!store) return NULL;
    store->capacity = 64;
    store->entries = (RoomSchedule*)calloc((size_t)store->capacity, sizeof(RoomSchedule));
    if (!store->entries) {
        free(store);
        return NULL;
    }

    int room_seq = 0;
    for (int i = 0; i < graph->num_nodes; i++) {
        const Node* n = &graph->nodes[i];
        if (!is_candidate_room_node(n)) continue;

        if (store->count >= store->capacity) {
            store->capacity *= 2;
            RoomSchedule* resized = (RoomSchedule*)realloc(
                store->entries, (size_t)store->capacity * sizeof(RoomSchedule)
            );
            if (!resized) break;
            store->entries = resized;
        }

        RoomSchedule* rs = &store->entries[store->count];
        memset(rs, 0, sizeof(*rs));
        rs->node_idx = i;
        snprintf(rs->room_id, sizeof(rs->room_id), "%s", n->str_id);
        snprintf(rs->room_name, sizeof(rs->room_name), "%s", n->name);
        snprintf(rs->room_type, sizeof(rs->room_type), "%s", n->type);
        snprintf(rs->building_id, sizeof(rs->building_id), "%s", n->building_id);
        rs->floor = n->floor;

        // Default all 5x2 half-day slots to FREE
        for (int d = 0; d < TT_DAYS; d++) {
            for (int s = 0; s < TT_SHIFTS; s++) {
                rs->grid[d][s].kind = SLOT_FREE;
                snprintf(rs->grid[d][s].label, sizeof(rs->grid[d][s].label), "FREE");
                rs->live_booked[d][s] = 0;
            }
        }

        // Permuted schedules across classrooms (6 subjects x 2 half-days across week + 3 labs)
        if (strcmp(n->type, "classroom") == 0) {
            // Each classroom gets 2 subject half-days and 1 lab half-day permuted by room_seq & day
            int base_sub = room_seq % 6;
            int second_sub = (room_seq * 2 + 1) % 6;
            int base_lab = room_seq % 3;

            for (int d = 0; d < TT_DAYS; d++) {
                int s_morning = (d + room_seq) % TT_SHIFTS;
                int s_after = 1 - s_morning;

                // Permute subject & lab assignments across days so no uniform pattern repeats
                int slot_pattern = (d + room_seq) % 5;
                if (slot_pattern == 0) {
                    // Morning: Subject A (1st half-day), Afternoon: Subject B (1st half-day)
                    rs->grid[d][s_morning].kind = SLOT_CLASS;
                    snprintf(rs->grid[d][s_morning].label, sizeof(rs->grid[d][s_morning].label),
                             "%s (Session 1/2)", SUBJECTS[base_sub]);
                    rs->grid[d][s_after].kind = SLOT_CLASS;
                    snprintf(rs->grid[d][s_after].label, sizeof(rs->grid[d][s_after].label),
                             "%s (Session 1/2)", SUBJECTS[second_sub]);
                } else if (slot_pattern == 1) {
                    // Morning: Subject A (2nd half-day), Afternoon: FREE (available for overflow)
                    rs->grid[d][s_morning].kind = SLOT_CLASS;
                    snprintf(rs->grid[d][s_morning].label, sizeof(rs->grid[d][s_morning].label),
                             "%s (Session 2/2)", SUBJECTS[base_sub]);
                } else if (slot_pattern == 2) {
                    // Morning: FREE, Afternoon: Subject B (2nd half-day)
                    rs->grid[d][s_after].kind = SLOT_CLASS;
                    snprintf(rs->grid[d][s_after].label, sizeof(rs->grid[d][s_after].label),
                             "%s (Session 2/2)", SUBJECTS[second_sub]);
                } else if (slot_pattern == 3) {
                    // Morning: Lab slot, Afternoon: FREE
                    int l_idx = (base_lab + d) % 3;
                    rs->grid[d][0].kind = SLOT_LAB;
                    snprintf(rs->grid[d][0].label, sizeof(rs->grid[d][0].label),
                             "LAB: %s", LABS[l_idx]);
                } else {
                    // Morning: FREE, Afternoon: Lab slot
                    int l_idx = (base_lab + d + 1) % 3;
                    rs->grid[d][1].kind = SLOT_LAB;
                    snprintf(rs->grid[d][1].label, sizeof(rs->grid[d][1].label),
                             "LAB: %s", LABS[l_idx]);
                }
            }
        } else if (strcmp(n->type, "lab") == 0) {
            // Dedicated labs have scheduled lab blocks in permuted morning/afternoon slots
            for (int d = 0; d < TT_DAYS; d++) {
                int shift_idx = (d + room_seq) % TT_SHIFTS;
                int l_idx = (room_seq + d) % 3;
                rs->grid[d][shift_idx].kind = SLOT_LAB;
                snprintf(rs->grid[d][shift_idx].label, sizeof(rs->grid[d][shift_idx].label),
                         "SCHEDULED LAB: %s", LABS[l_idx]);
            }
        } else if (strcmp(n->type, "seminar_hall") == 0) {
            // Seminar hall on Floor 3 has periodic seminars (e.g., Tue Afternoon & Thu Morning)
            rs->grid[1][1].kind = SLOT_SEMINAR;
            snprintf(rs->grid[1][1].label, sizeof(rs->grid[1][1].label), "Department Seminar");
            rs->grid[3][0].kind = SLOT_SEMINAR;
            snprintf(rs->grid[3][0].label, sizeof(rs->grid[3][0].label), "Guest Colloquium");
        }

        store->count++;
        room_seq++;
    }

    return store;
}

void tt_free(TimetableStore* store) {
    if (!store) return;
    free(store->entries);
    free(store);
}

int tt_find_room_index(const TimetableStore* store, const char* room_id) {
    if (!store || !room_id) return -1;
    for (int i = 0; i < store->count; i++) {
        if (strcmp(store->entries[i].room_id, room_id) == 0) return i;
    }
    return -1;
}

int tt_parse_day(const char* day_str) {
    if (!day_str || day_str[0] == '\0') return 0; // Default MON
    char upper[16] = {0};
    for (size_t i = 0; i < sizeof(upper) - 1 && day_str[i]; i++) {
        upper[i] = (char)toupper((unsigned char)day_str[i]);
    }
    if (strncmp(upper, "MON", 3) == 0) return 0;
    if (strncmp(upper, "TUE", 3) == 0) return 1;
    if (strncmp(upper, "WED", 3) == 0) return 2;
    if (strncmp(upper, "THU", 3) == 0) return 3;
    if (strncmp(upper, "FRI", 3) == 0) return 4;
    return 0;
}

int tt_parse_shift(const char* shift_str) {
    if (!shift_str || shift_str[0] == '\0') return 0; // Default MORNING
    char upper[16] = {0};
    for (size_t i = 0; i < sizeof(upper) - 1 && shift_str[i]; i++) {
        upper[i] = (char)toupper((unsigned char)shift_str[i]);
    }
    if (strncmp(upper, "AFT", 3) == 0 || strncmp(upper, "PM", 2) == 0 || strcmp(upper, "1") == 0) {
        return 1;
    }
    return 0;
}

const char* tt_day_name(int day) {
    if (day < 0 || day >= TT_DAYS) return "MON";
    return DAY_NAMES[day];
}

const char* tt_shift_name(int shift) {
    if (shift < 0 || shift >= TT_SHIFTS) return "MORNING";
    return SHIFT_NAMES[shift];
}

int tt_is_slot_free(const TimetableStore* store, int room_idx, int day, int shift) {
    if (!store || room_idx < 0 || room_idx >= store->count) return 0;
    if (day < 0 || day >= TT_DAYS || shift < 0 || shift >= TT_SHIFTS) return 0;
    const RoomSchedule* rs = &store->entries[room_idx];
    if (rs->live_booked[day][shift]) return 0;
    return rs->grid[day][shift].kind == SLOT_FREE ? 1 : 0;
}

int tt_book_slot(TimetableStore* store, const char* room_id, int day, int shift) {
    int idx = tt_find_room_index(store, room_id);
    if (idx < 0 || day < 0 || day >= TT_DAYS || shift < 0 || shift >= TT_SHIFTS) return 0;
    store->entries[idx].live_booked[day][shift] = 1;
    return 1;
}

int tt_release_slot(TimetableStore* store, const char* room_id, int day, int shift) {
    int idx = tt_find_room_index(store, room_id);
    if (idx < 0 || day < 0 || day >= TT_DAYS || shift < 0 || shift >= TT_SHIFTS) return 0;
    store->entries[idx].live_booked[day][shift] = 0;
    return 1;
}

void tt_set_library_full_for_slot(TimetableStore* store, int day, int shift, int full) {
    if (!store || day < 0 || day >= TT_DAYS || shift < 0 || shift >= TT_SHIFTS) return;
    for (int i = 0; i < store->count; i++) {
        if (strcmp(store->entries[i].building_id, "LIB") == 0) {
            store->entries[i].live_booked[day][shift] = full ? 1 : 0;
        }
    }
}

int tt_save_state(const TimetableStore* store, const char* path) {
    if (!store || !path || path[0] == '\0') return 0;
    FILE* fp = fopen(path, "wb");
    if (!fp) return 0;

    fprintf(fp, "{\n  \"bookings\": [");
    int first = 1;
    for (int i = 0; i < store->count; i++) {
        for (int d = 0; d < TT_DAYS; d++) {
            for (int s = 0; s < TT_SHIFTS; s++) {
                if (store->entries[i].live_booked[d][s]) {
                    fprintf(fp, "%s\n    {\"room_id\":\"%s\",\"day\":\"%s\",\"shift\":\"%s\"}",
                            first ? "" : ",",
                            store->entries[i].room_id,
                            DAY_NAMES[d],
                            SHIFT_NAMES[s]);
                    first = 0;
                }
            }
        }
    }
    fprintf(fp, "%s]\n}\n", first ? "" : "\n  ");
    fclose(fp);
    return 1;
}

int tt_load_state(TimetableStore* store, const char* path) {
    if (!store || !path || path[0] == '\0') return 0;
    FILE* fp = fopen(path, "rb");
    if (!fp) return 0;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    if (sz <= 0 || sz > 2097152) {
        fclose(fp);
        return 0;
    }

    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return 0;
    }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    buf[n] = '\0';
    fclose(fp);

    const char* p = buf;
    while ((p = strstr(p, "\"room_id\"")) != NULL) {
        const char* q1 = strchr(p + 9, '"');
        const char* q2 = q1 ? strchr(q1 + 1, '"') : NULL;
        if (!q1 || !q2) break;

        char rid[64] = {0};
        size_t rlen = (size_t)(q2 - (q1 + 1));
        if (rlen >= sizeof(rid)) rlen = sizeof(rid) - 1;
        memcpy(rid, q1 + 1, rlen);

        int d = 0, s = 0;
        const char* d_key = strstr(q2, "\"day\"");
        if (d_key && d_key - q2 < 120) {
            const char* dq1 = strchr(d_key + 5, '"');
            const char* dq2 = dq1 ? strchr(dq1 + 1, '"') : NULL;
            if (dq1 && dq2) {
                char dstr[16] = {0};
                size_t dlen = (size_t)(dq2 - (dq1 + 1));
                if (dlen >= sizeof(dstr)) dlen = sizeof(dstr) - 1;
                memcpy(dstr, dq1 + 1, dlen);
                d = tt_parse_day(dstr);
            }
        }
        const char* s_key = strstr(q2, "\"shift\"");
        if (s_key && s_key - q2 < 160) {
            const char* sq1 = strchr(s_key + 7, '"');
            const char* sq2 = sq1 ? strchr(sq1 + 1, '"') : NULL;
            if (sq1 && sq2) {
                char sstr[16] = {0};
                size_t slen = (size_t)(sq2 - (sq1 + 1));
                if (slen >= sizeof(sstr)) slen = sizeof(sstr) - 1;
                memcpy(sstr, sq1 + 1, slen);
                s = tt_parse_shift(sstr);
            }
        }

        tt_book_slot(store, rid, d, s);
        p = q2 + 1;
    }

    free(buf);
    return 1;
}

static int find_node_idx_by_str(const Graph* g, const char* str_id) {
    if (!g || !str_id) return -1;
    for (int i = 0; i < g->num_nodes; i++) {
        if (strcmp(g->nodes[i].str_id, str_id) == 0) return i;
    }
    return -1;
}

static void build_blocked_list(const Graph* g, const RoutingPrefs* prefs, int** out_blocked, int* out_count) {
    *out_blocked = NULL;
    *out_count = 0;
    if (!g || !prefs) return;

    const char* target_avoid = prefs->prefer_stairs ? "elevator" : "stairs";
    int* arr = (int*)malloc((size_t)g->num_nodes * sizeof(int));
    if (!arr) return;

    int cnt = 0;
    for (int i = 0; i < g->num_nodes; i++) {
        if (strcmp(g->nodes[i].type, target_avoid) == 0) {
            arr[cnt++] = i;
        }
    }
    *out_blocked = arr;
    *out_count = cnt;
}

static double score_candidate(const RoomSchedule* rs, int tier, double path_cost, const RoutingPrefs* prefs) {
    double score = path_cost + suitability_penalty(tier);
    if (prefs) {
        if (prefs->prefer_building[0] != '\0') {
            if (strcmp(rs->building_id, prefs->prefer_building) == 0) {
                score -= 8.0;
            } else {
                score += 4.0;
            }
        }
        if (prefs->prefer_lower_floor) {
            score += (double)(rs->floor - 1) * 1.5;
        }
    }
    return score;
}

OverflowCandidate tt_find_best_overflow_room(
    const Graph* graph,
    TimetableStore* store,
    const char* start_node_id,
    int day,
    int shift,
    const RoutingPrefs* prefs
) {
    OverflowCandidate best;
    memset(&best, 0, sizeof(best));
    best.room_idx = -1;
    best.total_score = DBL_MAX;
    best.path_cost = -1.0;

    if (!graph || !store || day < 0 || day >= TT_DAYS || shift < 0 || shift >= TT_SHIFTS) {
        return best;
    }

    int start_idx = find_node_idx_by_str(graph, start_node_id);
    if (start_idx == -1) start_idx = 0;

    int* blocked = NULL;
    int blocked_cnt = 0;
    if (prefs) {
        build_blocked_list(graph, prefs, &blocked, &blocked_cnt);
    }

    // Fallback Priority Chain: Classrooms -> Labs -> Seminar Hall -> Quiet/Study Zones
    int tiers_order[4] = { TIER_CLASSROOM, TIER_LAB, TIER_SEMINAR_HALL, TIER_QUIET_ZONE };

    for (int t_i = 0; t_i < 4; t_i++) {
        int desired_tier = tiers_order[t_i];
        int found_in_tier = 0;

        for (int r = 0; r < store->count; r++) {
            const RoomSchedule* rs = &store->entries[r];
            const Node* rn = &graph->nodes[rs->node_idx];
            if (classify_tier(rn) != desired_tier) continue;
            if (!tt_is_slot_free(store, r, day, shift)) continue;

            int plen = 0;
            int* path = cg_astar(graph, start_idx, rs->node_idx, blocked, blocked_cnt, &plen);
            if (!path && blocked && blocked_cnt > 0) {
                // Retry without transit block if preferred transit cannot reach room
                path = cg_astar(graph, start_idx, rs->node_idx, NULL, 0, &plen);
            }
            if (!path || plen <= 0) {
                free(path);
                continue;
            }

            double cost = cg_path_cost(graph, path, plen);
            double sc = score_candidate(rs, desired_tier, cost, prefs);

            if (sc < best.total_score) {
                free(best.path);
                best.found = 1;
                best.room_idx = r;
                best.tier = desired_tier;
                best.path_cost = cost;
                best.total_score = sc;
                best.path = path;
                best.path_len = plen;

                int other_shift = 1 - shift;
                best.opens_other_shift = tt_is_slot_free(store, r, day, other_shift);
                snprintf(best.other_shift_name, sizeof(best.other_shift_name), "%s", SHIFT_NAMES[other_shift]);

                if (desired_tier == TIER_LAB) {
                    snprintf(best.note, sizeof(best.note),
                             "Quiet reading / open seats only — active lab sessions take priority");
                } else if (desired_tier == TIER_SEMINAR_HALL) {
                    snprintf(best.note, sizeof(best.note),
                             "3rd-floor seminar hall overflow — check for scheduled events");
                } else if (desired_tier == TIER_QUIET_ZONE) {
                    snprintf(best.note, sizeof(best.note),
                             "Fallback study / quiet zone space");
                } else if (best.opens_other_shift) {
                    snprintf(best.note, sizeof(best.note),
                             "Free during %s; also free in %s",
                             SHIFT_NAMES[shift], SHIFT_NAMES[other_shift]);
                } else {
                    snprintf(best.note, sizeof(best.note),
                             "Free during %s; %s is occupied",
                             SHIFT_NAMES[shift], SHIFT_NAMES[other_shift]);
                }
                found_in_tier = 1;
            } else {
                free(path);
            }
        }

        if (found_in_tier) break;
    }

    free(blocked);
    return best;
}

void tt_free_candidate(OverflowCandidate* cand) {
    if (!cand) return;
    free(cand->path);
    cand->path = NULL;
    cand->path_len = 0;
}