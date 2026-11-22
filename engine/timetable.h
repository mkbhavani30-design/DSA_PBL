#ifndef TIMETABLE_H
#define TIMETABLE_H

#include "campus.h"

#define TT_DAYS 5
#define TT_SHIFTS 2 // 0 = MORNING, 1 = AFTERNOON

typedef enum {
    SLOT_FREE = 0,
    SLOT_CLASS = 1,
    SLOT_LAB = 2,
    SLOT_SEMINAR = 3
} SlotKind;

typedef enum {
    TIER_CLASSROOM = 1,
    TIER_LAB = 2,
    TIER_SEMINAR_HALL = 3,
    TIER_QUIET_ZONE = 4
} PriorityTier;

typedef struct {
    SlotKind kind;
    char label[64]; // e.g., "SUBJ_1 (2/2)", "LAB_1", "SEMINAR_301", "FREE"
} TimetableCell;

typedef struct {
    int node_idx;
    char room_id[64];
    char room_name[64];
    char room_type[32];
    char building_id[32];
    int floor;
    TimetableCell grid[TT_DAYS][TT_SHIFTS];
    int live_booked[TT_DAYS][TT_SHIFTS]; // Ad-hoc booking overlay on top of timetable
} RoomSchedule;

typedef struct {
    int count;
    int capacity;
    RoomSchedule* entries;
} TimetableStore;

typedef struct {
    int prefer_stairs;      // 1 = prefer stairs, 0 = prefer elevator
    int prefer_lower_floor; // 1 = slight bonus for lower floors
    char prefer_building[32]; // "DEPT_A", "DEPT_B", "LIB", or "" for any
} RoutingPrefs;

typedef struct {
    int found;
    int room_idx;
    int tier;
    double path_cost;
    double total_score;
    int opens_other_shift;    // 1 if other shift on same day is FREE
    char other_shift_name[16];
    char note[160];
    int* path;
    int path_len;
} OverflowCandidate;

TimetableStore* tt_create_and_seed(const Graph* graph);
void tt_free(TimetableStore* store);

int tt_find_room_index(const TimetableStore* store, const char* room_id);
int tt_parse_day(const char* day_str);
int tt_parse_shift(const char* shift_str);
const char* tt_day_name(int day);
const char* tt_shift_name(int shift);

int tt_is_slot_free(const TimetableStore* store, int room_idx, int day, int shift);
int tt_book_slot(TimetableStore* store, const char* room_id, int day, int shift);
int tt_release_slot(TimetableStore* store, const char* room_id, int day, int shift);
void tt_set_library_full_for_slot(TimetableStore* store, int day, int shift, int full);

int tt_save_state(const TimetableStore* store, const char* path);
int tt_load_state(TimetableStore* store, const char* path);

/* Finds best free room using fallback chain:
   Classroom -> Lab -> Seminar Hall -> Quiet/Study Zone,
   ranked by A* cost + suitability + user preferences. */
OverflowCandidate tt_find_best_overflow_room(
    const Graph* graph,
    TimetableStore* store,
    const char* start_node_id,
    int day,
    int shift,
    const RoutingPrefs* prefs
);

void tt_free_candidate(OverflowCandidate* cand);

#endif