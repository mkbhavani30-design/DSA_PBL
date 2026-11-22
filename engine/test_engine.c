#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "segment_tree.h"
#include "campus.h"
#include "campus_router.h"
#include "map_loader.h"

#define NUM_ROOMS 100
#define NUM_ITERATIONS 5000

typedef struct {
    int n;
    int* slots;
} BruteForceArray;

static BruteForceArray* create_bf(int n) {
    BruteForceArray* bf = (BruteForceArray*)malloc(sizeof(BruteForceArray));
    bf->n = n;
    bf->slots = (int*)calloc((size_t)n, sizeof(int));
    return bf;
}

static void free_bf(BruteForceArray* bf) {
    free(bf->slots);
    free(bf);
}

static void bf_book(BruteForceArray* bf, int l, int r) {
    for (int i = l; i <= r; i++) bf->slots[i] = 1;
}

static void bf_release(BruteForceArray* bf, int l, int r) {
    for (int i = l; i <= r; i++) bf->slots[i] = 0;
}

static int bf_find_first_free(BruteForceArray* bf, int req_len, int from) {
    if (from < 0) from = 0;
    for (int i = from; i <= bf->n - req_len; i++) {
        int consecutive = 1;
        for (int j = 0; j < req_len; j++) {
            if (bf->slots[i + j] == 1) {
                consecutive = 0;
                break;
            }
        }
        if (consecutive) return i;
    }
    return -1;
}

int main(void) {
    printf("Starting Differential Testing (%d random operations)...\n", NUM_ITERATIONS);

    SegmentTree* st = create_segment_tree(NUM_ROOMS);
    BruteForceArray* bf = create_bf(NUM_ROOMS);

    srand((unsigned int)time(NULL));

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        int l = rand() % NUM_ROOMS;
        int r = rand() % NUM_ROOMS;
        if (l > r) { int temp = l; l = r; r = temp; }

        int op = rand() % 3;
        if (op == 0) {
            st_book(st, l, r);
            bf_book(bf, l, r);
        } else if (op == 1) {
            st_release(st, l, r);
            bf_release(bf, l, r);
        } else {
            int req_len = (rand() % 5) + 1;
            int from = rand() % NUM_ROOMS;

            int st_res = st_find_first_free(st, req_len, from);
            int bf_res = bf_find_first_free(bf, req_len, from);

            assert(st_res == bf_res);
        }
    }

    free_segment_tree(st);
    free_bf(bf);
    printf("SUCCESS: All %d differential tests matched brute-force results!\n", NUM_ITERATIONS);

    // Verify campus_map.json loading & multi-floor A* pathing
    Graph* g = create_graph(256);
    RoomRegistry* reg = create_room_registry(64);
    int ok = load_campus_map("../db/campus_map.json", g, reg);
    if (!ok) ok = load_campus_map("db/campus_map.json", g, reg);
    assert(ok);
    assert(g->num_nodes > 0);
    assert(reg->count > 0);

    int s = find_node_by_str_id(g, "LIB_F1_HALL_01");
    int t = find_node_by_str_id(g, "DEPT_A_F3_SEMINAR_01");
    assert(s != -1 && t != -1);

    int plen = 0;
    int* path = cg_astar(g, s, t, NULL, 0, &plen);
    assert(path != NULL && plen >= 2);
    free(path);

    free_room_registry(reg);
    free_graph(g);
    printf("SUCCESS: Campus map and cross-building A* routing verified!\n");
    return 0;
}