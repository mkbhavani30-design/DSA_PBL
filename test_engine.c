#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "segment_tree.h"

#define NUM_ROOMS 100
#define NUM_ITERATIONS 5000

// Brute-force reference implementation using a simple boolean array
typedef struct {
    int n;
    int* slots; // 0 = free, 1 = booked
} BruteForceArray;

BruteForceArray* create_bf(int n) {
    BruteForceArray* bf = (BruteForceArray*)malloc(sizeof(BruteForceArray));
    bf->n = n;
    bf->slots = (int*)calloc(n, sizeof(int));
    return bf;
}

void free_bf(BruteForceArray* bf) {
    free(bf->slots);
    free(bf);
}

void bf_book(BruteForceArray* bf, int l, int r) {
    for (int i = l; i <= r; i++) bf->slots[i] = 1;
}

void bf_release(BruteForceArray* bf, int l, int r) {
    for (int i = l; i <= r; i++) bf->slots[i] = 0;
}

int bf_find_first_free(BruteForceArray* bf, int req_len, int from) {
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

int main() {
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
            // Book range
            st_book(st, l, r);
            bf_book(bf, l, r);
        } else if (op == 1) {
            // Release range
            st_release(st, l, r);
            bf_release(bf, l, r);
        } else {
            // Search range
            int req_len = (rand() % 5) + 1;
            int from = rand() % NUM_ROOMS;

            int st_res = st_find_first_free(st, req_len, from);
            int bf_res = bf_find_first_free(bf, req_len, from);

            assert(st_res == bf_res);
        }
    }

    free_segment_tree(st);
    free_bf(bf);

    printf("SUCCESS: All %d differential tests matched brute-force results perfectly!\n", NUM_ITERATIONS);
    return 0;
}