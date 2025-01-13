#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/stat.h>
#include <semaphore.h>
#include "constants.h"
#include "io.h"
#include "operations.h"
#include "parser.h"
#include "pthread.h"
#include "../common/protocol.h"
#include "../common/io.h"
#include "../common/constants.h"
#include "fifo.h"
#include "kvs.h"
#include "operations.h"
#include "api.h"
#include "errno.h"



const int s = MAX_SESSION_COUNT;
extern sem_t session_sem;



struct Client g_clients[MAX_SESSION_COUNT] = {0}; 
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static int write_response(const char *pipe_path, char op_code, char result) {
    int fd = open(pipe_path, O_WRONLY);
    if (fd == -1) return 1;
    
    char response[3] = {op_code, result + '0', '\0'};
    write(fd, response, sizeof(response));
    close(fd);
    return 0;
}



int handle_connection(int fd_server) {
    char total_pipe_path[MAX_PIPE_PATH_LENGTH * 3 + 1] = {0};

    if (sem_wait(&session_sem) != 0) {
        perror("sem_wait failed");
        close(fd_server);
        return 1;
    }
    
    if (read(fd_server, total_pipe_path, MAX_PIPE_PATH_LENGTH * 3 + 1) < 0) {
        fprintf(stderr, "[ERROR] Failed to read pipe paths\n");
        write_response(g_client.resp_pipe, OP_CODE_CONNECT, 1);
        return 1;
    }

    pthread_mutex_lock(&clients_mutex);
    if (g_client.active) {
        pthread_mutex_unlock(&clients_mutex);
        fprintf(stderr, "[ERROR] Client already connected\n");
        return 1;
    }

    strncpy(g_client.req_pipe, total_pipe_path, MAX_PIPE_PATH_LENGTH);
    strncpy(g_client.resp_pipe, total_pipe_path + MAX_PIPE_PATH_LENGTH, MAX_PIPE_PATH_LENGTH);
    strncpy(g_client.notif_pipe, total_pipe_path + (2 * MAX_PIPE_PATH_LENGTH), MAX_PIPE_PATH_LENGTH);
    g_client.active = 1;

    pthread_mutex_unlock(&clients_mutex);

    printf("[DEBUG] Client pipes configured\n");
    return write_response(g_client.resp_pipe, OP_CODE_CONNECT, 0);
}

void *client_handler(void *arg) {
    struct Client *client = (struct Client *)arg;
    printf("[DEBUG] Client handler started\n");
    
    while (client->active) {
        int fd = open(client->req_pipe, O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "[ERROR] Failed to open request pipe (errno=%d)\n", errno);
            continue;
        }

        char opcode;
        int interrupted = 0;
        if (read_all(fd, &opcode, 1, &interrupted) < 0) {
            fprintf(stderr, "[ERROR] Failed to read opcode (errno=%d)\n", errno);
            close(fd);
            continue;
        }

        printf("[DEBUG] Received opcode: %d\n", opcode);
        switch(opcode) {
            case OP_CODE_DISCONNECT:
                printf("[DEBUG] Processing disconnect\n");
                client_disconnect(client);
                close(fd);
                return NULL;
            
            case OP_CODE_SUBSCRIBE:
                printf("[DEBUG] Processing subscribe\n");
                handle_subscribe(fd, client);
                break;
                
            case OP_CODE_UNSUBSCRIBE:
                printf("[DEBUG] Processing unsubscribe\n");
                handle_unsubscribe(fd, client);
                break;
                
            default:
                fprintf(stderr, "[ERROR] Unknown opcode: %d\n", opcode);
        }
        close(fd);
    }
    printf("[DEBUG] Client handler exiting\n");
    return NULL;
}

int client_disconnect(struct Client *client) {
 
  if (write_response(client->resp_pipe, OP_CODE_DISCONNECT, 0) != 0) {
    return 1;
  }

  pthread_mutex_lock(&clients_mutex);
  client->active = 0;
  kvs_unsubscribe_all_keys(client->notif_pipe);
  pthread_mutex_unlock(&clients_mutex);
  
  return 0;
}




int handle_subscribe(int fd_req, struct Client *client) {
    char key[MAX_STRING_SIZE] = {0};
    printf("[DEBUG] Starting subscribe operation\n");

    int fd_resp = open(client->resp_pipe, O_WRONLY);
    if (fd_resp == -1) {
        fprintf(stderr, "[ERROR] Subscribe: Failed to open response pipe %s (errno=%d)\n", 
                client->resp_pipe, errno);
        return 1;
    }
    printf("[DEBUG] Opened response pipe: %s\n", client->resp_pipe);

    if (read(fd_req, key, MAX_STRING_SIZE) < 0) {
        fprintf(stderr, "[ERROR] Subscribe: Failed to read key from request (errno=%d)\n", 
                errno);
        write_response(client->resp_pipe, OP_CODE_SUBSCRIBE, 1);
        close(fd_resp);
        return 1;
    }
    printf("[DEBUG] Read key: %s\n", key);

    if (kvs_subscribe(key, client->notif_pipe)) {
        printf("[DEBUG] Successfully subscribed to key: %s\n", key);
        printf("[DEBUG] Using notification pipe: %s\n", client->notif_pipe);
        kvs_print_notif_pipes(key);
        
    write_response(client->resp_pipe, OP_CODE_SUBSCRIBE, 1);
    } else {
        fprintf(stderr, "[ERROR] Subscribe: KVS subscription failed for key: %s\n", key);
    write_response(client->resp_pipe, OP_CODE_SUBSCRIBE, 0);
    }

    printf("[DEBUG] Subscribe operation completed for key: %s\n", key);
    close(fd_resp);
    return 0;
}

int handle_unsubscribe(int fd_req, struct Client *client) {
    char key[MAX_STRING_SIZE] = {0};
    printf("[DEBUG] Starting unsubscribe operation\n");

    int fd_resp = open(client->resp_pipe, O_WRONLY);
    if (fd_resp == -1) {
        fprintf(stderr, "[ERROR] Unsubscribe: Failed to open response pipe %s (errno=%d)\n", 
                client->resp_pipe, errno);
        return 1;
    }
    printf("[DEBUG] Opened response pipe: %s\n", client->resp_pipe);

    if (read(fd_req, key, MAX_STRING_SIZE) < 0) {
        fprintf(stderr, "[ERROR] Unsubscribe: Failed to read key from request (errno=%d)\n", 
                errno);
        write_response(client->resp_pipe, OP_CODE_UNSUBSCRIBE, 1);
        close(fd_resp);
        return 1;
    }
    printf("[DEBUG] Read key: %s\n", key);

    if (kvs_unsubscribe(key, client->notif_pipe)) {
        printf("[DEBUG] Successfully unsubscribed from key: %s\n", key);
        printf("[DEBUG] Removed notification pipe: %s\n", client->notif_pipe);
        write_response(client->resp_pipe, OP_CODE_UNSUBSCRIBE, 0);
    } else {
        fprintf(stderr, "[ERROR] Unsubscribe: KVS unsubscription failed for key: %s\n", key);
        write_response(client->resp_pipe, OP_CODE_UNSUBSCRIBE, 1);
    }

    printf("[DEBUG] Unsubscribe operation completed for key: %s\n", key);
    close(fd_resp);
    return 0;
}