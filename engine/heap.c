#include "heap.h"
#include <stdlib.h>

MinHeap* create_min_heap(int capacity) {
    if (capacity <= 0) capacity = 16;
    MinHeap* heap = (MinHeap*)malloc(sizeof(MinHeap));
    heap->data = (HeapNode*)malloc((size_t)capacity * sizeof(HeapNode));
    heap->size = 0;
    heap->capacity = capacity;
    return heap;
}

void free_min_heap(MinHeap* heap) {
    if (heap) {
        free(heap->data);
        free(heap);
    }
}

static void swap(HeapNode* a, HeapNode* b) {
    HeapNode temp = *a;
    *a = *b;
    *b = temp;
}

void push_min_heap(MinHeap* heap, int node_id, double cost) {
    if (heap->size >= heap->capacity) {
        heap->capacity *= 2;
        heap->data = (HeapNode*)realloc(heap->data, (size_t)heap->capacity * sizeof(HeapNode));
    }

    int i = heap->size++;
    heap->data[i].node_id = node_id;
    heap->data[i].cost = cost;

    while (i > 0 && heap->data[(i - 1) / 2].cost > heap->data[i].cost) {
        swap(&heap->data[i], &heap->data[(i - 1) / 2]);
        i = (i - 1) / 2;
    }
}

HeapNode pop_min_heap(MinHeap* heap) {
    HeapNode min_node = heap->data[0];
    heap->data[0] = heap->data[--heap->size];

    int i = 0;
    int left, right, smallest;

    while (1) {
        smallest = i;
        left = 2 * i + 1;
        right = 2 * i + 2;

        if (left < heap->size && heap->data[left].cost < heap->data[smallest].cost)
            smallest = left;
        if (right < heap->size && heap->data[right].cost < heap->data[smallest].cost)
            smallest = right;

        if (smallest != i) {
            swap(&heap->data[i], &heap->data[smallest]);
            i = smallest;
        } else {
            break;
        }
    }

    return min_node;
}

int is_min_heap_empty(MinHeap* heap) {
    return heap->size == 0;
}