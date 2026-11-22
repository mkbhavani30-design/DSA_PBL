#ifndef ENGINE_API_H
#define ENGINE_API_H

#ifdef _WIN32
  #define API_EXPORT __declspec(dllexport)
#else
  #define API_EXPORT
#endif

API_EXPORT int engine_init(const char* map_json_path);
API_EXPORT void engine_shutdown(void);

API_EXPORT int engine_get_room_count(void);
API_EXPORT const char* engine_get_room_id(int slot);
API_EXPORT const char* engine_get_room_name(int slot);
API_EXPORT const char* engine_get_room_type(int slot);
API_EXPORT const char* engine_get_room_building(int slot);
API_EXPORT int engine_get_room_floor(int slot);
API_EXPORT int engine_is_room_free(int slot);

API_EXPORT int engine_book_room(int slot);
API_EXPORT int engine_release_room(int slot);
API_EXPORT int engine_book_by_str_id(const char* str_id);
API_EXPORT int engine_release_by_str_id(const char* str_id);
API_EXPORT void engine_set_library_full(int full);

/* Finds nearest free room from `start_str_id` (optionally excluding `exclude_building`).
   Writes path node str_ids separated by '|' into `out_path_buf` and returns chosen slot (-1 if none). */
API_EXPORT int engine_find_nearest_free_room(
    const char* start_str_id,
    const char* exclude_building,
    const int* blocked_node_indices,
    int blocked_count,
    double* out_cost,
    char* out_path_buf,
    int out_path_buf_len
);

/* Computes shortest A* path between any two string node IDs.
   Returns number of nodes in path (-1 if unreachable). */
API_EXPORT int engine_shortest_path(
    const char* start_str_id,
    const char* target_str_id,
    double* out_cost,
    char* out_path_buf,
    int out_path_buf_len
);

#endif