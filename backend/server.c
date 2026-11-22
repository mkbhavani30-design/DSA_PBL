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
#define BODY_BUF_SIZE 131072

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
        char day[16] = "MON", shift[16] = "MORNING";
        get_query_param(query, "day", day, sizeof(day));
        get_query_param(query, "shift", shift, sizeof(shift));

        int total = engine_get_room_count();
        int free_cnt = 0;
        for (int i = 0; i < total; i++) {
            if (engine_is_room_free_at(i, day, shift)) free_cnt++;
        }
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"ok\",\"day\":\"%s\",\"shift\":\"%s\","
                 "\"total_rooms\":%d,\"free_rooms\":%d,\"booked_rooms\":%d}",
                 day, shift, total, free_cnt, total - free_cnt);
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
        char day[16] = "MON", shift[16] = "MORNING";
        char f_bldg[32] = "", f_type[32] = "", f_free[16] = "";
        get_query_param(query, "day", day, sizeof(day));
        get_query_param(query, "shift", shift, sizeof(shift));
        get_query_param(query, "building_id", f_bldg, sizeof(f_bldg));
        get_query_param(query, "room_type", f_type, sizeof(f_type));
        get_query_param(query, "only_free", f_free, sizeof(f_free));
        int only_free = (strcmp(f_free, "true") == 0 || strcmp(f_free, "1") == 0);

        engine_list_rooms_json(day, shift, f_bldg, f_type, only_free, resp, sizeof(resp));
        send_json_response(client, 200, "OK", resp);

    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/top-rooms") == 0) {
        char start_id[64] = "LIB_F1_HALL_01";
        char day[16] = "MON", shift[16] = "MORNING";
        char prefer_bldg[32] = "";
        char stairs_str[16] = "", floor_str[16] = "";

        get_query_param(query, "start", start_id, sizeof(start_id));
        get_query_param(query, "day", day, sizeof(day));
        get_query_param(query, "shift", shift, sizeof(shift));
        get_query_param(query, "prefer_building", prefer_bldg, sizeof(prefer_bldg));
        get_query_param(query, "prefer_stairs", stairs_str, sizeof(stairs_str));
        get_query_param(query, "prefer_lower_floor", floor_str, sizeof(floor_str));

        int prefer_stairs = (strcmp(stairs_str, "true") == 0 || strcmp(stairs_str, "1") == 0);
        int prefer_lower_floor = (strcmp(floor_str, "true") == 0 || strcmp(floor_str, "1") == 0);

        if (!engine_top3_overflow_json(start_id, day, shift, prefer_bldg,
                                       prefer_stairs, prefer_lower_floor, resp, sizeof(resp))) {
            send_json_response(client, 404, "Not Found", "{\"error\":\"No free rooms available\"}");
        } else {
            send_json_response(client, 200, "OK", resp);
        }

    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/rooms/book") == 0) {
        char room_id[64] = "", day[16] = "MON", shift[16] = "MORNING";
        parse_string_field(body, "day", day, sizeof(day));
        parse_string_field(body, "shift", shift, sizeof(shift));
        if (!parse_string_field(body, "room_id", room_id, sizeof(room_id)) ||
            !engine_book_room_at(room_id, day, shift)) {
            send_json_response(client, 404, "Not Found", "{\"error\":\"Room not found or booking failed\"}");
        } else {
            engine_save_state(g_state_file);
            snprintf(resp, sizeof(resp),
                     "{\"status\":\"booked\",\"room_id\":\"%s\",\"day\":\"%s\",\"shift\":\"%s\"}",
                     room_id, day, shift);
            send_json_response(client, 200, "OK", resp);
        }

    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/rooms/release") == 0) {
        char room_id[64] = "", day[16] = "MON", shift[16] = "MORNING";
        parse_string_field(body, "day", day, sizeof(day));
        parse_string_field(body, "shift", shift, sizeof(shift));
        if (!parse_string_field(body, "room_id", room_id, sizeof(room_id)) ||
            !engine_release_room_at(room_id, day, shift)) {
            send_json_response(client, 404, "Not Found", "{\"error\":\"Room not found or release failed\"}");
        } else {
            engine_save_state(g_state_file);
            snprintf(resp, sizeof(resp),
                     "{\"status\":\"released\",\"room_id\":\"%s\",\"day\":\"%s\",\"shift\":\"%s\"}",
                     room_id, day, shift);
            send_json_response(client, 200, "OK", resp);
        }

    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/library/overflow") == 0) {
        char day[16] = "MON", shift[16] = "MORNING";
        parse_string_field(body, "day", day, sizeof(day));
        parse_string_field(body, "shift", shift, sizeof(shift));
        int full = parse_bool_field(body, "library_full", 1);
        engine_set_library_full_at(day, shift, full);
        engine_save_state(g_state_file);
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"updated\",\"day\":\"%s\",\"shift\":\"%s\",\"library_full\":%s}",
                 day, shift, full ? "true" : "false");
        send_json_response(client, 200, "OK", resp);

    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/nearest-room") == 0) {
        char start_id[64] = "LIB_F1_HALL_01";
        char day[16] = "MON", shift[16] = "MORNING";
        char prefer_bldg[32] = "";
        char stairs_str[16] = "", floor_str[16] = "", book_str[16] = "";

        get_query_param(query, "start", start_id, sizeof(start_id));
        get_query_param(query, "day", day, sizeof(day));
        get_query_param(query, "shift", shift, sizeof(shift));
        get_query_param(query, "prefer_building", prefer_bldg, sizeof(prefer_bldg));
        get_query_param(query, "prefer_stairs", stairs_str, sizeof(stairs_str));
        get_query_param(query, "prefer_lower_floor", floor_str, sizeof(floor_str));
        get_query_param(query, "auto_book", book_str, sizeof(book_str));

        int prefer_stairs = (strcmp(stairs_str, "true") == 0 || strcmp(stairs_str, "1") == 0);
        int prefer_lower_floor = (strcmp(floor_str, "true") == 0 || strcmp(floor_str, "1") == 0);
        int auto_book = (strcmp(book_str, "true") == 0 || strcmp(book_str, "1") == 0);

        int ok = engine_find_overflow_json(
            start_id, day, shift, prefer_bldg,
            prefer_stairs, prefer_lower_floor, auto_book,
            resp, sizeof(resp)
        );

        if (!ok) {
            send_json_response(client, 404, "Not Found", "{\"error\":\"No free rooms available for given criteria\"}");
        } else {
            if (auto_book) engine_save_state(g_state_file);
            send_json_response(client, 200, "OK", resp);
        }

    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/route") == 0) {
        char start_id[64] = "", target_id[64] = "";
        get_query_param(query, "start", start_id, sizeof(start_id));
        get_query_param(query, "target", target_id, sizeof(target_id));

        double cost = -1.0;
        char path_pipe[4096] = "";
        int steps = engine_shortest_path(start_id, target_id, &cost, path_pipe, sizeof(path_pipe));
        if (steps <= 0) {
            send_json_response(client, 404, "Not Found", "{\"error\":\"Route not found\"}");
        } else {
            size_t pos = (size_t)snprintf(
                resp, sizeof(resp),
                "{\"start\":\"%s\",\"target\":\"%s\",\"cost\":%.2f,\"steps\":%d,\"path\":[",
                start_id, target_id, cost, steps
            );
            char copy[4096];
            snprintf(copy, sizeof(copy), "%s", path_pipe);
            char* tok = strtok(copy, "|");
            int first = 1;
            while (tok) {
                pos += (size_t)snprintf(resp + pos, sizeof(resp) - pos, "%s\"%s\"", first ? "" : ",", tok);
                first = 0;
                tok = strtok(NULL, "|");
            }
            snprintf(resp + pos, sizeof(resp) - pos, "]}");
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
    engine_load_state(g_state_file);

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
    printf("Loaded map: %s (%d rooms with 5-day Morning/Afternoon schedules)\n",
           cfg->map_path, engine_get_room_count());

    while (1) {
        sock_t client = accept(srv, NULL, NULL);
        if (client == SOCK_INVALID) continue;
        handle_client(client, cfg->map_path);
    }

    engine_save_state(g_state_file);
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