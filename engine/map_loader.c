#include "map_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

RoomRegistry* create_room_registry(int initial_cap) {
    if (initial_cap <= 0) initial_cap = 32;
    RoomRegistry* reg = (RoomRegistry*)malloc(sizeof(RoomRegistry));
    if (!reg) return NULL;
    reg->count = 0;
    reg->capacity = initial_cap;
    reg->rooms = (RoomSlot*)malloc((size_t)initial_cap * sizeof(RoomSlot));
    if (!reg->rooms) {
        free(reg);
        return NULL;
    }
    return reg;
}

void free_room_registry(RoomRegistry* reg) {
    if (!reg) return;
    free(reg->rooms);
    free(reg);
}

static void add_room_slot(RoomRegistry* reg, int node_idx) {
    if (!reg || node_idx < 0) return;
    if (reg->count >= reg->capacity) {
        reg->capacity *= 2;
        reg->rooms = (RoomSlot*)realloc(reg->rooms, (size_t)reg->capacity * sizeof(RoomSlot));
    }
    reg->rooms[reg->count++].node_idx = node_idx;
}

int is_bookable_room_type(const char* type) {
    if (!type) return 0;
    return (strcmp(type, "study_room") == 0 ||
            strcmp(type, "classroom") == 0 ||
            strcmp(type, "lab") == 0 ||
            strcmp(type, "seminar_hall") == 0);
}

/* Lightweight parser tailored for structured campus_map.json */
typedef struct {
    char id[64];
    char type[32];
    char name[64];
    char building_id[32];
    int floor;
    double x;
    double y;
    int node_idx;
    char conn_ids[16][64];
    int conn_count;
} ParsedNode;

static char* read_file_contents(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    rewind(fp);
    if (sz <= 0) {
        fclose(fp);
        return NULL;
    }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t nread = fread(buf, 1, (size_t)sz, fp);
    buf[nread] = '\0';
    fclose(fp);
    return buf;
}

static void skip_ws(const char** p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static int parse_string(const char** p, char* out, size_t out_sz) {
    skip_ws(p);
    if (**p != '"') return 0;
    (*p)++;
    size_t i = 0;
    while (**p && **p != '"') {
        if (**p == '\\' && *(*p + 1)) {
            (*p)++;
        }
        if (i + 1 < out_sz) {
            out[i++] = **p;
        }
        (*p)++;
    }
    out[i] = '\0';
    if (**p == '"') (*p)++;
    return 1;
}

static int parse_int(const char** p, int* out) {
    skip_ws(p);
    char* end = NULL;
    long val = strtol(*p, &end, 10);
    if (end == *p) return 0;
    *out = (int)val;
    *p = end;
    return 1;
}

static int parse_double(const char** p, double* out) {
    skip_ws(p);
    char* end = NULL;
    double val = strtod(*p, &end);
    if (end == *p) return 0;
    *out = val;
    *p = end;
    return 1;
}

static void skip_json_value(const char** p);

static void skip_json_array(const char** p) {
    (*p)++; // '['
    skip_ws(p);
    if (**p == ']') {
        (*p)++;
        return;
    }
    while (**p) {
        skip_json_value(p);
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
        } else if (**p == ']') {
            (*p)++;
            return;
        } else {
            return;
        }
    }
}

static void skip_json_object(const char** p) {
    (*p)++; // '{'
    skip_ws(p);
    if (**p == '}') {
        (*p)++;
        return;
    }
    while (**p) {
        char key[64];
        if (!parse_string(p, key, sizeof(key))) return;
        skip_ws(p);
        if (**p == ':') (*p)++;
        skip_json_value(p);
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
        } else if (**p == '}') {
            (*p)++;
            return;
        } else {
            return;
        }
    }
}

static void skip_json_value(const char** p) {
    skip_ws(p);
    if (**p == '"') {
        char tmp[128];
        parse_string(p, tmp, sizeof(tmp));
    } else if (**p == '{') {
        skip_json_object(p);
    } else if (**p == '[') {
        skip_json_array(p);
    } else {
        while (**p && **p != ',' && **p != '}' && **p != ']') (*p)++;
    }
}

static void parse_connections(const char** p, ParsedNode* pn) {
    (*p)++; // '['
    pn->conn_count = 0;
    skip_ws(p);
    while (**p && **p != ']') {
        if (**p == '"') {
            char cid[64];
            if (parse_string(p, cid, sizeof(cid)) && pn->conn_count < 16) {
                snprintf(pn->conn_ids[pn->conn_count++], sizeof(pn->conn_ids[0]), "%s", cid);
            }
        } else {
            (*p)++;
        }
        skip_ws(p);
        if (**p == ',') (*p)++;
        skip_ws(p);
    }
    if (**p == ']') (*p)++;
}

static void parse_node_obj(const char** p, const char* bldg_id, int floor_num, ParsedNode* pn) {
    memset(pn, 0, sizeof(*pn));
    pn->floor = floor_num;
    snprintf(pn->building_id, sizeof(pn->building_id), "%s", bldg_id);

    (*p)++; // '{'
    skip_ws(p);
    while (**p && **p != '}') {
        char key[64];
        if (!parse_string(p, key, sizeof(key))) break;
        skip_ws(p);
        if (**p == ':') (*p)++;
        skip_ws(p);

        if (strcmp(key, "id") == 0) {
            parse_string(p, pn->id, sizeof(pn->id));
        } else if (strcmp(key, "type") == 0) {
            parse_string(p, pn->type, sizeof(pn->type));
        } else if (strcmp(key, "name") == 0) {
            parse_string(p, pn->name, sizeof(pn->name));
        } else if (strcmp(key, "x") == 0) {
            parse_double(p, &pn->x);
        } else if (strcmp(key, "y") == 0) {
            parse_double(p, &pn->y);
        } else if (strcmp(key, "connections") == 0 && **p == '[') {
            parse_connections(p, pn);
        } else {
            skip_json_value(p);
        }

        skip_ws(p);
        if (**p == ',') (*p)++;
        skip_ws(p);
    }
    if (**p == '}') (*p)++;
    if (pn->name[0] == '\0') {
        snprintf(pn->name, sizeof(pn->name), "%s", pn->id);
    }
}

int load_campus_map(const char* json_path, Graph* graph, RoomRegistry* rooms) {
    if (!json_path || !graph) return 0;

    char* content = read_file_contents(json_path);
    if (!content) return 0;

    int parsed_cap = 256;
    int parsed_count = 0;
    ParsedNode* parsed = (ParsedNode*)malloc((size_t)parsed_cap * sizeof(ParsedNode));
    if (!parsed) {
        free(content);
        return 0;
    }

    const char* p = content;
    skip_ws(&p);
    if (*p == '{') {
        p++;
        while (*p && *p != '}') {
            char key[64];
            if (!parse_string(&p, key, sizeof(key))) break;
            skip_ws(&p);
            if (*p == ':') p++;
            skip_ws(&p);

            if (strcmp(key, "buildings") == 0 && *p == '[') {
                p++; // '['
                skip_ws(&p);
                while (*p && *p != ']') {
                    if (*p == '{') {
                        char bldg_id[32] = "";
                        p++;
                        while (*p && *p != '}') {
                            char bkey[64];
                            if (!parse_string(&p, bkey, sizeof(bkey))) break;
                            skip_ws(&p);
                            if (*p == ':') p++;
                            skip_ws(&p);

                            if (strcmp(bkey, "building_id") == 0) {
                                parse_string(&p, bldg_id, sizeof(bldg_id));
                            } else if (strcmp(bkey, "floors") == 0 && *p == '[') {
                                p++; // '['
                                skip_ws(&p);
                                while (*p && *p != ']') {
                                    if (*p == '{') {
                                        int floor_num = 1;
                                        p++;
                                        while (*p && *p != '}') {
                                            char fkey[64];
                                            if (!parse_string(&p, fkey, sizeof(fkey))) break;
                                            skip_ws(&p);
                                            if (*p == ':') p++;
                                            skip_ws(&p);

                                            if (strcmp(fkey, "floor_number") == 0) {
                                                parse_int(&p, &floor_num);
                                            } else if (strcmp(fkey, "nodes") == 0 && *p == '[') {
                                                p++; // '['
                                                skip_ws(&p);
                                                while (*p && *p != ']') {
                                                    if (*p == '{') {
                                                        if (parsed_count >= parsed_cap) {
                                                            parsed_cap *= 2;
                                                            parsed = (ParsedNode*)realloc(parsed, (size_t)parsed_cap * sizeof(ParsedNode));
                                                        }
                                                        parse_node_obj(&p, bldg_id, floor_num, &parsed[parsed_count]);
                                                        parsed_count++;
                                                    } else {
                                                        p++;
                                                    }
                                                    skip_ws(&p);
                                                    if (*p == ',') p++;
                                                    skip_ws(&p);
                                                }
                                                if (*p == ']') p++;
                                            } else {
                                                skip_json_value(&p);
                                            }
                                            skip_ws(&p);
                                            if (*p == ',') p++;
                                            skip_ws(&p);
                                        }
                                        if (*p == '}') p++;
                                    } else {
                                        p++;
                                    }
                                    skip_ws(&p);
                                    if (*p == ',') p++;
                                    skip_ws(&p);
                                }
                                if (*p == ']') p++;
                            } else {
                                skip_json_value(&p);
                            }
                            skip_ws(&p);
                            if (*p == ',') p++;
                            skip_ws(&p);
                        }
                        if (*p == '}') p++;
                    } else {
                        p++;
                    }
                    skip_ws(&p);
                    if (*p == ',') p++;
                    skip_ws(&p);
                }
                if (*p == ']') p++;
            } else {
                skip_json_value(&p);
            }
            skip_ws(&p);
            if (*p == ',') p++;
            skip_ws(&p);
        }
    }

    // Pass 1: Add all nodes and register bookable rooms
    for (int i = 0; i < parsed_count; i++) {
        int idx = add_node(graph, parsed[i].id, parsed[i].type, parsed[i].building_id,
                           parsed[i].floor, parsed[i].x, parsed[i].y, parsed[i].name);
        parsed[i].node_idx = idx;
        if (rooms && is_bookable_room_type(parsed[i].type)) {
            add_room_slot(rooms, idx);
        }
    }

    // Pass 2: Add edges from connections using planar distance as base weight
    for (int i = 0; i < parsed_count; i++) {
        int u_idx = parsed[i].node_idx;
        if (u_idx < 0) continue;
        for (int c = 0; c < parsed[i].conn_count; c++) {
            int v_idx = find_node_by_str_id(graph, parsed[i].conn_ids[c]);
            if (v_idx != -1) {
                double dx = graph->nodes[u_idx].x - graph->nodes[v_idx].x;
                double dy = graph->nodes[u_idx].y - graph->nodes[v_idx].y;
                double dist = sqrt(dx * dx + dy * dy);
                if (dist < 1.0) dist = 1.0;
                add_edge(graph, u_idx, v_idx, dist);
            }
        }
    }

    free(parsed);
    free(content);
    return parsed_count > 0;
}