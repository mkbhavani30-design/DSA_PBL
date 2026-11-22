#ifndef MAP_LOADER_H
#define MAP_LOADER_H

#include "campus.h"

typedef struct {
    int node_idx; // Index inside Graph->nodes
} RoomSlot;

typedef struct {
    int count;
    int capacity;
    RoomSlot* rooms;
} RoomRegistry;

RoomRegistry* create_room_registry(int initial_cap);
void free_room_registry(RoomRegistry* reg);
int is_bookable_room_type(const char* type);
int load_campus_map(const char* json_path, Graph* graph, RoomRegistry* rooms);

#endif