#ifndef ENGINE_API_H
#define ENGINE_API_H

#ifdef _WIN32
  #define API_EXPORT __declspec(dllexport)
#else
  #define API_EXPORT
#endif

API_EXPORT int engine_init(const char* map_json_path);
API_EXPORT void engine_shutdown(void);

API_EXPORT int engine_load_state(const char* state_path);
API_EXPORT int engine_save_state(const char* state_path);

API_EXPORT int engine_get_room_count(void);
API_EXPORT const char* engine_get_room_id(int slot);
API_EXPORT const char* engine_get_room_name(int slot);
API_EXPORT const char* engine_get_room_type(int slot);
API_EXPORT const char* engine_get_room_building(int slot);
API_EXPORT int engine_get_room_floor(int slot);

API_EXPORT int engine_is_room_free_at(int slot, const char* day_str, const char* shift_str);
API_EXPORT const char* engine_get_slot_label(int slot, const char* day_str, const char* shift_str);

API_EXPORT int engine_book_room_at(const char* room_id, const char* day_str, const char* shift_str);
API_EXPORT int engine_release_room_at(const char* room_id, const char* day_str, const char* shift_str);
API_EXPORT void engine_set_library_full_at(const char* day_str, const char* shift_str, int full);

/* Returns 1 if a room was found, 0 otherwise. Writes JSON summary to `out_json_buf`. */
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
);

/* Writes full rooms list (with schedule status for requested day/shift) as JSON. */
API_EXPORT int engine_list_rooms_json(
    const char* day_str,
    const char* shift_str,
    const char* building_filter,
    const char* type_filter,
    int only_free,
    char* out_json_buf,
    int out_json_buf_len
);

API_EXPORT int engine_shortest_path(
    const char* start_str_id,
    const char* target_str_id,
    double* out_cost,
    char* out_path_buf,
    int out_path_buf_len
);

#endif