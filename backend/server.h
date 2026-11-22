#ifndef BACKEND_SERVER_H
#define BACKEND_SERVER_H

typedef struct {
    int port;
    char map_path[512];
    char state_path[512];
} BackendConfig;

int backend_run(const BackendConfig* cfg);

#endif