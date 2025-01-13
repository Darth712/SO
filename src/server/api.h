#ifndef API_H
#define API_H

#include <pthread.h>
#include "../common/constants.h"

struct Client {
    char req_pipe[40];
    char resp_pipe[40];
    char notif_pipe[40];
    pthread_t thread;
    int active;
};

extern struct Client g_client;  // Single client instance
extern pthread_mutex_t clients_mutex;

int handle_connection(int fd_server);
void *client_handler(void *arg);
int client_disconnect(struct Client *client);
int handle_disconnect(const char *name);
int handle_subscribe(int fd_req, struct Client *client);
int handle_unsubscribe(int fd_req, struct Client *client);

#endif // API_H