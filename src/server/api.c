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
#include "constants.h"
#include "io.h"
#include "operations.h"
#include "parser.h"
#include "pthread.h"
#include "../common/protocol.h"
#include "../common/io.h"
#include "../common/constants.h"
#include "subscriptions.h"
#include "fifo.h"
#include "kvs.h"
#include "operations.h"



int connect (int fd_server) {
  // Initialize buffers
  char total_pipe_path[MAX_PIPE_PATH_LENGTH *3 + 1] = {0};
  char req_pipe_path[MAX_PIPE_PATH_LENGTH] = {0};
  char resp_pipe_path[MAX_PIPE_PATH_LENGTH] = {0};
  char notif_pipe_path[MAX_PIPE_PATH_LENGTH] = {0};

  // Read Request Pipe Path
  if (read(fd_server, total_pipe_path, MAX_PIPE_PATH_LENGTH * 3 + 1) < 0) {
      write_str(STDERR_FILENO, "Failed to read req_pipe_path\n");
      close(fd_server);
      return 1;
  }

  strncpy(req_pipe_path, total_pipe_path, sizeof(req_pipe_path) - 1);
  strncpy(resp_pipe_path, total_pipe_path + MAX_PIPE_PATH_LENGTH, sizeof(resp_pipe_path) - 1);
  strncpy(notif_pipe_path, total_pipe_path + 2 * MAX_PIPE_PATH_LENGTH, sizeof(notif_pipe_path) - 1);
  pthread_t *thread = malloc(sizeof(pthread_t));
  if (thread == NULL) {
    fprintf(stderr, "Failed to allocate memory for threads\n");
    return 1;
  }

  pthread_create(&thread[0], NULL, fifo_reader, (void *)&req_pipe_path);
  int fd_resp = open(resp_pipe_path, O_WRONLY);
  if (fd_resp == -1) {
    perror("Error opening response pipe");
    return 1;
  }

  // Response to the client
  char response [3] = {0};
  response[0] = '1';
  response[1] = '0';
  response[2] = '\0';
  write(fd_resp, response, sizeof(response));
  char *name = client_name(req_pipe_path);

  // Close the server pipe after reading all data
  return 0;
}

int disconnect(const char *name) {
  char opcode = OP_CODE_DISCONNECT;
  char resp_pipe_path[256] = "/tmp/resp";
  strncpy(resp_pipe_path + 9, name, strlen(name) * sizeof(char));
  int fd_resp = open(resp_pipe_path, O_WRONLY);
  if (fd_resp == -1) {
    perror("Error opening response pipe");
    return 1;
  }
  char response [3] = {0};
  response[0] = '2';
  response[1] = '0';
  response[2] = '\0';
  write(fd_resp, response, sizeof(response));
  close(fd_resp);
}

int subscribe(int fd_req, char *name) {
  char key[MAX_STRING_SIZE];
  char result[2];
  char resp_pipe_path[256] = "/tmp/resp";
  char notif_pipe_path[256] = "/tmp/notif";
  strncpy(resp_pipe_path + 9, name, strlen(name) * sizeof(char));
  strncpy(notif_pipe_path + 9, name, strlen(name) * sizeof(char));

  int fd_resp = open(resp_pipe_path, O_WRONLY);
  if (fd_resp == -1) {
    perror("Error opening response pipe");
    strncpy(result, "1", sizeof(result));
    write(fd_resp, &result, sizeof(result));
    close(fd_resp);
    return 1;
  }

  if (read(fd_req, key, MAX_STRING_SIZE) < 0) {
    write_str(STDERR_FILENO, "Failed to read response\n");
    strncpy(result, "1", sizeof(result));
    write(fd_resp, &result, sizeof(result));
    close(fd_resp);
    return 1;
  }

  if (kvs_subscribe(key,notif_pipe_path,name)) {
    strncpy(result, "0", sizeof(result));
  } else {
    strncpy(result, "1", sizeof(result));
  }
  char response [3] = {0};
  response[0] = '3';
  response[1] = result[0];
  response[2] = '\0';
  write(fd_resp, response, sizeof(response));
  close(fd_resp);

  return 0;
}