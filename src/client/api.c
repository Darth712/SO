#include "api.h"
#include "../common/constants.h"
#include "../common/protocol.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../common/io.h"
#include "../server/subscriptions.h"

static int fd_req = -1;
static int fd_resp = -1;
static int fd_notif = -1;
static int fd_server = -1;
char s_req_pipe_path[40] = {0};
char s_resp_pipe_path[40] = {0};
char s_notif_pipe_path[40] = {0};
char s_server_pipe_path[40];

int kvs_connect(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path,
                char const* notif_pipe_path, int* notif_pipe) {
  unlink(req_pipe_path);
  unlink(resp_pipe_path);
  unlink(notif_pipe_path);

  if (mkfifo(req_pipe_path, 0666) < 0){
    perror("Error creating client-to-server FIFO");
    return 1;
  }
  if (mkfifo(resp_pipe_path, 0666) < 0){
    perror("Error creating server-to-client FIFO");
    return 1;
  }
  if (mkfifo(notif_pipe_path, 0666) < 0){
    perror("Error creating notification FIFO");
    return 1;
  }
  strncpy(s_req_pipe_path, req_pipe_path, sizeof(s_req_pipe_path) - 1);
  strncpy(s_resp_pipe_path, resp_pipe_path, sizeof(s_resp_pipe_path) - 1);
  strncpy(s_notif_pipe_path, notif_pipe_path, sizeof(s_notif_pipe_path) - 1);
  // create pipes and connect
  int fd_server = open(server_pipe_path, O_WRONLY);
  if (fd_server == -1) {
    perror("Error opening server pipe");
    return 1;
  }
  // send connect message to request pipe and wait for response in response pipe
  char opcode = OP_CODE_CONNECT;
  int response;
  ssize_t bytes_written = write(fd_server, &opcode, sizeof(opcode));
  if (bytes_written == -1) {
    perror("Error writing to server pipe");
    return 1;
  }

  char total_pipe_path[121] = {0}; // 40 * 3 + 1

  // Concatenate the pipe paths
  strncpy(total_pipe_path, req_pipe_path, 40);
  strncpy(total_pipe_path + 40, resp_pipe_path, 40);
  strncpy(total_pipe_path + 80, notif_pipe_path, 40);

  ssize_t bytes_written_total = write(fd_server, total_pipe_path, sizeof(total_pipe_path));
  if (bytes_written_total == -1) {
    perror("Error writing concatenated pipe paths to server pipe");
    return 1;
  }
  printf("sent connect message\n");
  fd_resp = open(resp_pipe_path, O_RDONLY);
  if (fd_resp == -1) {
    perror("Error opening response pipe");
    return 1;
  }
  ssize_t bytes_read = read(fd_resp, &opcode, sizeof(opcode));
  if(bytes_read == -1) {
    perror("Error reading from response pipe");
    return 1;
  }
  if (opcode != OP_CODE_CONNECT) {
    return 1;
  }
  printf("Connected to server\n");
  return 0;
}
 
int kvs_disconnect(void) {
  char opcode = OP_CODE_DISCONNECT;
  fd_req = open(s_req_pipe_path, O_WRONLY);
  if (fd_req == -1) {
    perror("Error opening request pipe");
    return 1;
  }
  write(fd_req, &opcode, sizeof(opcode));

  // Optionally wait for a response
  char response;
  if (read(fd_resp, &response, sizeof(response)) > 0 && response == OP_CODE_DISCONNECT) {
    // Close and remove pipes
    close(fd_req);
    close(fd_resp);
    close(fd_notif);
    close(fd_server);
    unlink(s_req_pipe_path);
    unlink(s_resp_pipe_path);
    unlink(s_notif_pipe_path);
    unlink(s_server_pipe_path);
  }
  // close pipes and unlink pipe files
  return 0;
}

int kvs_subscribe(const char* key) {
  // send subscribe message to request pipe and wait for response in response pipe
  char opcode = OP_CODE_SUBSCRIBE;
  write(fd_req, &opcode, sizeof(opcode));
  write(fd_req, key, strlen(key) + 1);
  char response;
  read(fd_resp, &response, sizeof(response));
  printf("response: %d\n", response);
  if (response != OP_CODE_SUBSCRIBE) {
    return 1;
  }
  return 0;
}

int kvs_unsubscribe(const char* key) {
    // send unsubscribe message to request pipe and wait for response in response pipe
  return 0;
}


