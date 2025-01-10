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



int connect (int fd_server) {
  // Initialize buffers
  char total_pipe_path[MAX_PIPE_PATH_LENGTH *3 + 1] = {0};
  char req_pipe_path[MAX_PIPE_PATH_LENGTH] = {0};
  char resp_pipe_path[MAX_PIPE_PATH_LENGTH] = {0};
  char notif_pipe_path[MAX_PIPE_PATH_LENGTH] = {0};

  // Read Request Pipe Path
  printf("waiting for req\n");
  if (read(fd_server, total_pipe_path, MAX_PIPE_PATH_LENGTH * 3 + 1) < 0) {
      write_str(STDERR_FILENO, "Failed to read req_pipe_path\n");
      close(fd_server);
      return 1;
  }
  printf("%s\n",total_pipe_path);
  strncpy(req_pipe_path, total_pipe_path, sizeof(req_pipe_path) - 1);
  strncpy(resp_pipe_path, total_pipe_path + MAX_PIPE_PATH_LENGTH, sizeof(resp_pipe_path) - 1);
  strncpy(notif_pipe_path, total_pipe_path + 2 * MAX_PIPE_PATH_LENGTH, sizeof(notif_pipe_path) - 1);
  pthread_t *thread = malloc(sizeof(pthread_t));
  if (thread == NULL) {
    fprintf(stderr, "Failed to allocate memory for threads\n");
    return 1;
  }

  
  register_client(req_pipe_path);
  pthread_create(&thread[0], NULL, fifo_reader, (void *)&req_pipe_path);
  // pthread_create(&thread[0], NULL, fifo_reader, (void *)&server_pipe_path);
  printf("waiting for thread\n");
  int fd_resp = open(resp_pipe_path, O_WRONLY);
  if (fd_resp == -1) {
    perror("Error opening response pipe");
    return 1;
  }
  char opcode = OP_CODE_CONNECT;
  write(fd_resp, &opcode, sizeof(opcode));
 
  // Close the server pipe after reading all data

  printf("all reads done\n");
  printf(" %s,%s,%s\n", req_pipe_path,resp_pipe_path,notif_pipe_path);
  printf("Connected to server\n");
  return 0;
}
int subscribe(int fd_req, char *name) {
  char key[MAX_STRING_SIZE];
  printf("crazy\n");
  if (read(fd_req, key, MAX_STRING_SIZE) < 0) {
    write_str(STDERR_FILENO, "Failed to read response\n");
    return 1;
  }
  printf("subscribing to %s\n", key);
  if (add_subscription(name, key)) {
    write_str(STDERR_FILENO, "Failed to subscribe\n");
    return 1;
  }
  char resp_pipe_path[256] = "/tmp/resp";
  strncpy(resp_pipe_path + 9, name, strlen(name) * sizeof(char));
  int fd_resp = open(resp_pipe_path, O_WRONLY);
  if (fd_resp == -1) {
    perror("Error opening response pipe");
    return 1;
  }
  char opcode = OP_CODE_SUBSCRIBE;
  write(fd_resp, &opcode, sizeof(opcode));

  return 0;
}