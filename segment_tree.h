#ifndef SEGMENT_TREE_H
#define SEGMENT_TREE_H

typedef struct {
    int len;   // Length of the segment/interval
    int pref;  // Maximum consecutive free units starting from the left
    int suf;   // Maximum consecutive free units ending at the right
    int best;  // Maximum consecutive free units anywhere in the segment
    int lazy;  // Lazy tag: -1 = no pending update, 0 = release (free), 1 = book (occupied)
} SegmentTreeNode;

typedef struct {
    int n;                 // Total number of elements (rooms)
    SegmentTreeNode* tree; // Array-based segment tree storage
} SegmentTree;

// Function prototypes
SegmentTree* create_segment_tree(int n);
void free_segment_tree(SegmentTree* st);
void st_book(SegmentTree* st, int l, int r);
void st_release(SegmentTree* st, int l, int r);
int st_find_first_free(SegmentTree* st, int req_len, int from);

#endif