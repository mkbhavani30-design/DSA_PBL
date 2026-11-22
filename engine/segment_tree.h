#ifndef SEGMENT_TREE_H
#define SEGMENT_TREE_H

typedef struct {
    int len;
    int pref;
    int suf;
    int best;
    int lazy; // -1 = none, 0 = free/release, 1 = occupied/book
} SegmentTreeNode;

typedef struct {
    int n;
    SegmentTreeNode* tree;
} SegmentTree;

SegmentTree* create_segment_tree(int n);
void free_segment_tree(SegmentTree* st);
void st_book(SegmentTree* st, int l, int r);
void st_release(SegmentTree* st, int l, int r);
int st_find_first_free(SegmentTree* st, int req_len, int from);

#endif