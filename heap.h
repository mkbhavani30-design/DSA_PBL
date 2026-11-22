#ifndef HEAP_H
#define HEAP_H

typedef struct {
    int node_id;
    double cost;
} HeapNode;

typedef struct {
    HeapNode* data;
    int size;
    int capacity;
} MinHeap;

MinHeap* create_min_heap(int capacity);
void free_min_heap(MinHeap* heap);
void push_min_heap(MinHeap* heap, int node_id, double cost);
HeapNode pop_min_heap(MinHeap* heap);
int is_min_heap_empty(MinHeap* heap);

#endif