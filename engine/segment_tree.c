#include "segment_tree.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void pull(SegmentTreeNode* node, SegmentTreeNode* left, SegmentTreeNode* right) {
    node->len = left->len + right->len;

    node->pref = left->pref;
    if (left->pref == left->len) {
        node->pref += right->pref;
    }

    node->suf = right->suf;
    if (right->suf == right->len) {
        node->suf += left->suf;
    }

    int cross = left->suf + right->pref;
    node->best = left->best > right->best ? left->best : right->best;
    if (cross > node->best) {
        node->best = cross;
    }
}

static void apply(SegmentTreeNode* node, int val, int len) {
    node->lazy = val;
    if (val == 1) {
        node->pref = 0;
        node->suf = 0;
        node->best = 0;
    } else {
        node->pref = len;
        node->suf = len;
        node->best = len;
    }
}

static void push(int node_idx, SegmentTree* st) {
    SegmentTreeNode* node = &st->tree[node_idx];
    if (node->lazy != -1) {
        int left_idx = 2 * node_idx;
        int right_idx = 2 * node_idx + 1;

        apply(&st->tree[left_idx], node->lazy, st->tree[left_idx].len);
        apply(&st->tree[right_idx], node->lazy, st->tree[right_idx].len);

        node->lazy = -1;
    }
}

static void build(int node_idx, int l, int r, SegmentTree* st) {
    st->tree[node_idx].lazy = -1;
    if (l == r) {
        st->tree[node_idx].len = 1;
        st->tree[node_idx].pref = 1;
        st->tree[node_idx].suf = 1;
        st->tree[node_idx].best = 1;
        return;
    }
    int mid = l + (r - l) / 2;
    build(2 * node_idx, l, mid, st);
    build(2 * node_idx + 1, mid + 1, r, st);
    pull(&st->tree[node_idx], &st->tree[2 * node_idx], &st->tree[2 * node_idx + 1]);
}

SegmentTree* create_segment_tree(int n) {
    if (n <= 0) return NULL;
    SegmentTree* st = (SegmentTree*)malloc(sizeof(SegmentTree));
    st->n = n;
    st->tree = (SegmentTreeNode*)malloc((size_t)(4 * n) * sizeof(SegmentTreeNode));
    build(1, 0, n - 1, st);
    return st;
}

void free_segment_tree(SegmentTree* st) {
    if (st) {
        free(st->tree);
        free(st);
    }
}

static void update_range(int node_idx, int l, int r, int ql, int qr, int val, SegmentTree* st) {
    if (ql > r || qr < l) return;
    if (ql <= l && r <= qr) {
        apply(&st->tree[node_idx], val, st->tree[node_idx].len);
        return;
    }
    push(node_idx, st);
    int mid = l + (r - l) / 2;
    update_range(2 * node_idx, l, mid, ql, qr, val, st);
    update_range(2 * node_idx + 1, mid + 1, r, ql, qr, val, st);
    pull(&st->tree[node_idx], &st->tree[2 * node_idx], &st->tree[2 * node_idx + 1]);
}

void st_book(SegmentTree* st, int l, int r) {
    if (!st || st->n <= 0) return;
    if (l < 0) l = 0;
    if (r >= st->n) r = st->n - 1;
    if (l > r) return;
    update_range(1, 0, st->n - 1, l, r, 1, st);
}

void st_release(SegmentTree* st, int l, int r) {
    if (!st || st->n <= 0) return;
    if (l < 0) l = 0;
    if (r >= st->n) r = st->n - 1;
    if (l > r) return;
    update_range(1, 0, st->n - 1, l, r, 0, st);
}

static int query_find(int node_idx, int l, int r, int req_len, int from, SegmentTree* st) {
    if (st->tree[node_idx].best < req_len || r < from) return -1;

    if (l == r) {
        return (l >= from) ? l : -1;
    }

    push(node_idx, st);
    int mid = l + (r - l) / 2;

    if (st->tree[2 * node_idx].best >= req_len && mid >= from) {
        int res = query_find(2 * node_idx, l, mid, req_len, from, st);
        if (res != -1) return res;
    }

    int cross_start = mid - st->tree[2 * node_idx].suf + 1;
    int cross_end   = mid + st->tree[2 * node_idx + 1].pref;
    int start = cross_start > from ? cross_start : from;
    if (cross_end - start + 1 >= req_len) {
        return start;
    }

    return query_find(2 * node_idx + 1, mid + 1, r, req_len, from, st);
}

int st_find_first_free(SegmentTree* st, int req_len, int from) {
    if (!st || st->n <= 0 || req_len <= 0) return -1;
    if (from < 0) from = 0;
    if (from >= st->n) return -1;
    return query_find(1, 0, st->n - 1, req_len, from, st);
}