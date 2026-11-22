#define _POSIX_C_SOURCE 200809L

#include "server.h"
#include "../engine/engine_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
  #define _WIN32_WINNT 0x0601
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define SOCK_INVALID INVALID_SOCKET
  #define SOCK_CLOSE(s) closesocket(s)
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  typedef int sock_t;
  #define SOCK_INVALID (-1)
  #define SOCK_CLOSE(s) close(s)
#endif

#define RECV_BUF_SIZE 16384
#define BODY_BUF_SIZE 65536

static char g_state_file[512] = "../db/bookings_state.json";

static void url_decode_inplace(char* str) {
    char* src = str;
    char* dst = str;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], '\0' };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static int get_query_param(const char* query, const char* key, char* out, size_t out_sz) {
    if (!query || !key || !out || out_sz == 0) return 0;
    size_t key_len = strlen(key);
    const char* p = query;

    while (*p) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char* val_start = p + key_len + 1;
            const char* val_end = strchr(val_start, '&');
            size_t len = val_end ? (size_t)(val_end - val_start) : strlen(val_start);
            if (len >= out_sz) len = out_sz - 1;
            memcpy(out, val_start, len);
            out[len] = '\0';
            url_decode_inplace(out);
            return 1;
        }
        const char* amp = strchr(p, '&');
        if (!amp) break;
        p = amp + 1;
    }
    return 0;
}

static int parse_bool_field(const char* body, const char* key, int default_val) {
    if (!body || !key) return default_val;
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* found = strstr(body, pattern);
    if (!found) return default_val;
    const char* colon = strchr(found + strlen(pattern), ':');
    if (!colon) return default_val;
    colon++;
    while (*colon && isspace((unsigned char)*colon)) colon++;
    if (strncmp(colon, "true", 4) == 0) return 1;
    if (strncmp(colon, "false", 5) == 0) return 0;
    if (*colon == '1') return 1;
    if (*colon == '0') return 0;
    return default_val;
}

static int parse_string_field(const char* body, const char* key, char* out, size_t out_sz) {
    if (!body || !key || !out || out_sz == 0) return 0;
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* found = strstr(body, pattern);
    if (!found) return 0;
    const char* colon = strchr(found + strlen(pattern), ':');
    if (!colon) return 0;
    const char* q1 = strchr(colon, '"');
    if (!q1) return 0;
    q1++;
    const char* q2 = strchr(q1, '"');
    if (!q2) return 0;
    size_t len = (size_t)(q2 - q1);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, q1, len);
    out[len] = '\0';
    return 1;
}

static void save_booking_state(void) {
    if (g_state_file[0] == '\0') return;
    FILE* fp = fopen(g_state_file, "wb");
    if (!fp) return;

    fprintf(fp, "{\n  \"booked_room_ids\": [");
    int total = engine_get_room_count();
    int first = 1;
    for (int i = 0; i < total; i++) {
        if (!engine_is_room_free(i)) {
            fprintf(fp, "%s\"%s\"", first ? "\n    " : ",\n    ", engine_get_room_id(i));
            first = 0;
        }
    }
    fprintf(fp, "%s]\n}\n", first ? "" : "\n  ");
    fclose(fp);
}

static void restore_booking_state(void) {
    if (g_state_file[0] == '\0') return;
    FILE* fp = fopen(g_state_file, "rb");
    if (!fp) return;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    if (sz <= 0 || sz > 1048576) {
        fclose(fp);
        return;
    }

    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return;
    }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    buf[n] = '\0';
    fclose(fp);

    const char* arr = strstr(buf, "\"booked_room_ids\"");
    if (arr) {
        const char* p = strchr(arr, '[');
        const char* end = p ? strchr(p, ']') : NULL;
        while (p && end && p < end) {
            const char* q1 = strchr(p, '"');
            if (!q1 || q1 >= end) break;
            const char* q2 = strchr(q1 + 1, '"');
            if (!q2 || q2 > end) break;
            char rid[64];
            size_t len = (size_t)(q2 - (q1 + 1));
            if (len >= sizeof(rid)) len = sizeof(rid) - 1;
            memcpy(rid, q1 + 1, len);
            rid[len] = '\0';
            engine_book_by_str_id(rid);
            p = q2 + 1;
        }
    }

    free(buf);
}

static char* read_file_alloc(const char* path, size_t* out_len) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
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
    size_t n = fread(buf, 1, (size_t)sz, fp);
    buf[n] = '\0';
    fclose(fp);
    if (out_len) *out_len = n;
    return buf;
}

static void send_all(sock_t client, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int n = send(client, data + sent, (int)(len - sent), 0);
#else
        ssize_t n = send(client, data + sent, len - sent, 0);
#endif
        if (n <= 0) break;
        sent += (size_t)n;
    }
}

static void send_json_response(sock_t client, int status_code, const char* status_text, const char* json_body) {
    size_t body_len = json_body ? strlen(json_body) : 0;
    char header[512];
    int hlen = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n",
        status_code,
        status_text,
        body_len
    );
    send_all(client, header, (size_t)hlen);
    if (body_len > 0) {
        send_all(client, json_body, body_len);
    }
}

static void append_path_json_array(char* dst, size_t dst_sz, const char* pipe_path) {
    size_t cur = strlen(dst);
    cur += (size_t)snprintf(dst + cur, dst_sz - cur, "[");
    if (pipe_path && pipe_path[0] != '\0') {
        char copy[4096];
        snprintf(copy, sizeof(copy), "%s", pipe_path);
        char* tok = strtok(copy, "|");
        int first = 1;
        while (tok) {
            cur += (size_t)snprintf(dst + cur, dst_sz - cur, "%s\"%s\"", first ? "" : ",", tok);
            first = 0;
            tok = strtok(NULL, "|");
        }
    }
    snprintf(dst + cur, dst_sz - cur, "]");
}

static void handle_client(sock_t client, const char* map_path) {
    char req_buf[RECV_BUF_SIZE];
#ifdef _WIN32
    int received = recv(client, req_buf, sizeof(req_buf) - 1, 0);
#else
    ssize_t received = recv(client, req_buf, sizeof(req_buf) - 1, 0);
#endif
    if (received <= 0) {
        SOCK_CLOSE(client);
        return;
    }
    req_buf[received] = '\0';

    char method[16] = "";
    char raw_target[1024] = "";
    if (sscanf(req_buf, "%15s %1023s", method, raw_target) != 2) {
        send_json_response(client, 400, "Bad Request", "{\"error\":\"Malformed HTTP request\"}");
        SOCK_CLOSE(client);
        return;
    }

    if (strcmp(method, "OPTIONS") == 0) {
        send_json_response(client, 204, "No Content", "");
        SOCK_CLOSE(client);
        return;
    }

    const char* body = strstr(req_buf, "\r\n\r\n");
    body = body ? body + 4 : "";

    char path[512] = "";
    char query[512] = "";
    const char* qmark = strchr(raw_target, '?');
    if (qmark) {
        size_t plen = (size_t)(qmark - raw_target);
        if (plen >= sizeof(path)) plen = sizeof(path) - 1;
        memcpy(path, raw_target, plen);
        path[plen] = '\0';
        snprintf(query, sizeof(query), "%s", qmark + 1);
    } else {
        snprintf(path, sizeof(path), "%s", raw_target);
    }

    char resp[BODY_BUF_SIZE];

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/health") == 0) {
        int total = engine_get_room_count();
        int free_cnt = 0;
        for (int i = 0; i < total; i++) {
            if (engine_is_room_free(i)) free_cnt++;
        }
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"ok\",\"total_rooms\":%d,\"free_rooms\":%d,\"booked_rooms\":%d}",
                 total, free_cnt, total - free_cnt);
        send_json_response(client, 200, "OK", resp);

    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/map") == 0) {
        size_t mlen = 0;
        char* map_json = read_file_alloc(map_path, &mlen);
        if (!map_json) {
            send_json_response(client, 404, "Not Found", "{\"error\":\"campus_map.json not found\"}");
        } else {
            send_json_response(client, 200, "OK", map_json);
            free(map_json);
        }

    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/rooms") == 0) {
        char f_bldg[32] = "";
        char f_type[32] = "";
        char f_free[16] = "";
        get_query_param(query, "building_id", f_bldg, sizeof(f_bldg));
        get_query_param(query, "room_type", f_type, sizeof(f_type));
        get_query_param(query, "only_free", f_free, sizeof(f_free));
        int only_free = (strcmp(f_free, "true") == 0 || strcmp(f_free, "1") == 0);

        int total = engine_get_room_count();
        size_t pos = (size_t)snprintf(resp, sizeof(resp), "{\"rooms\":[");
        int first = 1;
        int matched = 0;

        for (int i = 0; i < total; i++) {
            const char* id = engine_get_room_id(i);
            const char* name = engine_get_room_name(i);
            const char* type = engine_get_room_type(i);
            const char* bldg = engine_get_room_building(i);
            int floor = engine_get_room_floor(i);
            int is_free = engine_is_room_free(i);

            if (f_bldg[0] != '\0' && strcmp(bldg, f_bldg) != 0) continue;
            if (f_type[0] != '\0' && strcmp(type, f_type) != 0) continue;
            if (only_free && !is_free) continue;

            if (pos + 256 < sizeof(resp)) {
                pos += (size_t)snprintf(
                    resp + pos,
                    sizeof(resp) - pos,
                    "%s{\"slot\":%d,\"id\":\"%s\",\"name\":\"%s\",\"type\":\"%s\","
                    "\"building_id\":\"%s\",\"floor\":%d,\"is_free\":%s}",
                    first ? "" : ",",
                    i, id, name, type, bldg, floor, is_free ? "true" : "false"
                );
                first = 0;
                matched++;
            }
        }

        snprintf(resp + pos, sizeof(resp) - pos, "],\"count\":%d}", matched);
        send_json_response(client, 200, "OK", resp);

    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/rooms/book") == 0) {
        char room_id[64] = "";
        if (!parse_string_field(body, "room_id", room_id, sizeof(room_id)) || !engine_book_by_str_id(room_id)) {
            send_json_response(client, 404, "Not Found", "{\"error\":\"Room not found or booking failed\"}");
        } else {
            save_booking_state();
            snprintf(resp, sizeof(resp), "{\"status\":\"booked\",\"room_id\":\"%s\"}", room_id);
            send_json_response(client, 200, "OK", resp);
        }

    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/rooms/release") == 0) {
        char room_id[64] = "";
        if (!parse_string_field(body, "room_id", room_id, sizeof(room_id)) || !engine_release_by_str_id(room_id)) {
            send_json_response(client, 404, "Not Found", "{\"error\":\"Room not found or release failed\"}");
        } else {
            save_booking_state();
            snprintf(resp, sizeof(resp), "{\"status\":\"released\",\"room_id\":\"%s\"}", room_id);
            send_json_response(client, 200, "OK", resp);
        }

    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/library/overflow") == 0) {
        int full = parse_bool_field(body, "library_full", 1);
        engine_set_library_full(full);
        save_booking_state();
        snprintf(resp, sizeof(resp), "{\"status\":\"updated\",\"library_full\":%s}", full ? "true" : "false");
        send_json_response(client, 200, "OK", resp);

    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/nearest-room") == 0) {
        char start_id[64] = "LIB_F1_HALL_01";
        char excl_bldg[32] = "";
        char auto_book_str[16] = "";
        get_query_param(query, "start", start_id, sizeof(start_id));
        get_query_param(query, "exclude_building", excl_bldg, sizeof(excl_bldg));
        get_query_param(query, "auto_book", auto_book_str, sizeof(auto_book_str));
        int auto_book = (strcmp(auto_book_str, "true") == 0 || strcmp(auto_book_str, "1") == 0);

        double cost = -1.0;
        char path_pipe[4096] = "";
        int slot = engine_find_nearest_free_room(
            start_id,
            excl_bldg,
            NULL,
            0,
            &cost,
            path_pipe,
            sizeof(path_pipe)
        );

        if (slot < 0) {
            send_json_response(client, 404, "Not Found", "{\"error\":\"No free study rooms available\"}");
        } else {
            if (auto_book) {
                engine_book_room(slot);
                save_booking_state();
            }
            size_t pos = (size_t)snprintf(
                resp,
                sizeof(resp),
                "{\"slot\":%d,\"room_id\":\"%s\",\"room_name\":\"%s\",\"room_type\":\"%s\","
                "\"building_id\":\"%s\",\"floor\":%d,\"cost\":%.2f,\"booked\":%s,\"path\":",
                slot,
                engine_get_room_id(slot),
                engine_get_room_name(slot),
                engine_get_room_type(slot),
                engine_get_room_building(slot),
                engine_get_room_floor(slot),
                cost,
                auto_book ? "true" : "false"
            );
            append_path_json_array(resp + pos, sizeof(resp) - pos, path_pipe);
            snprintf(resp + strlen(resp), sizeof(resp) - strlen(resp), "}");
            send_json_response(client, 200, "OK", resp);
        }

    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/route") == 0) {
        char start_id[64] = "";
        char target_id[64] = "";
        get_query_param(query, "start", start_id, sizeof(start_id));
        get_query_param(query, "target", target_id, sizeof(target_id));

        double cost = -1.0;
        char path_pipe[4096] = "";
        int steps = engine_shortest_path(start_id, target_id, &cost, path_pipe, sizeof(path_pipe));
        if (steps <= 0) {
            send_json_response(client, 404, "Not Found", "{\"error\":\"Route not found\"}");
        } else {
            size_t pos = (size_t)snprintf(
                resp,
                sizeof(resp),
                "{\"start\":\"%s\",\"target\":\"%s\",\"cost\":%.2f,\"steps\":%d,\"path\":",
                start_id, target_id, cost, steps
            );
            append_path_json_array(resp + pos, sizeof(resp) - pos, path_pipe);
            snprintf(resp + strlen(resp), sizeof(resp) - strlen(resp), "}");
            send_json_response(client, 200, "OK", resp);
        }

    } else {
        send_json_response(client, 404, "Not Found", "{\"error\":\"Endpoint not found\"}");
    }

    SOCK_CLOSE(client);
}

int backend_run(const BackendConfig* cfg) {
    if (!cfg) return 1;

    snprintf(g_state_file, sizeof(g_state_file), "%s", cfg->state_path);

    if (!engine_init(cfg->map_path)) {
        fprintf(stderr, "Failed to initialize engine with map: %s\n", cfg->map_path);
        return 1;
    }
    restore_booking_state();

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        engine_shutdown();
        return 1;
    }
#endif

    sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == SOCK_INVALID) {
        fprintf(stderr, "Failed to create socket.\n");
#ifdef _WIN32
        WSACleanup();
#endif
        engine_shutdown();
        return 1;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)cfg->port);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to bind port %d.\n", cfg->port);
        SOCK_CLOSE(srv);
#ifdef _WIN32
        WSACleanup();
#endif
        engine_shutdown();
        return 1;
    }

    if (listen(srv, 16) < 0) {
        fprintf(stderr, "Failed to listen on port %d.\n", cfg->port);
        SOCK_CLOSE(srv);
#ifdef _WIN32
        WSACleanup();
#endif
        engine_shutdown();
        return 1;
    }

    printf("C Backend listening on http://localhost:%d\n", cfg->port);
    printf("Loaded map: %s (%d rooms)\n", cfg->map_path, engine_get_room_count());

    while (1) {
        sock_t client = accept(srv, NULL, NULL);
        if (client == SOCK_INVALID) continue;
        handle_client(client, cfg->map_path);
    }

    save_booking_state();
    SOCK_CLOSE(srv);
#ifdef _WIN32
    WSACleanup();
#endif
    engine_shutdown();
    return 0;
}

int main(int argc, char** argv) {
    BackendConfig cfg;
    cfg.port = 8000;
    snprintf(cfg.map_path, sizeof(cfg.map_path), "../db/campus_map.json");
    snprintf(cfg.state_path, sizeof(cfg.state_path), "../db/bookings_state.json");

    if (argc > 1) cfg.port = atoi(argv[1]);
    if (argc > 2) snprintf(cfg.map_path, sizeof(cfg.map_path), "%s", argv[2]);
    if (argc > 3) snprintf(cfg.state_path, sizeof(cfg.state_path), "%s", argv[3]);

    return backend_run(&cfg);
}