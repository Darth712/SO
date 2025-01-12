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


static char s_req_pipe_path[MAX_PIPE_PATH_LENGTH];
static char s_resp_pipe_path[MAX_PIPE_PATH_LENGTH];
static char s_notif_pipe_path[MAX_PIPE_PATH_LENGTH];


int kvs_connect(char const *req_pipe_path, char const *resp_pipe_path,
                char const *server_pipe_path, char const *notif_pipe_path,
                int *notif_pipe) {
  
  strncpy(s_req_pipe_path, req_pipe_path, sizeof(s_req_pipe_path)-1);
  strncpy(s_resp_pipe_path, resp_pipe_path, sizeof(s_resp_pipe_path)-1); 
  strncpy(s_notif_pipe_path, notif_pipe_path, sizeof(s_notif_pipe_path)-1);

  unlink(req_pipe_path);
  unlink(resp_pipe_path);
  unlink(notif_pipe_path);

  if (mkfifo(req_pipe_path, 0666) < 0){
    perror("Error creating request FIFO file");
    return 1;
  }
  if (mkfifo(resp_pipe_path, 0666) < 0){
    perror("Error creating response FIFO");
    return 1;
  }
  if (mkfifo(notif_pipe_path, 0666) < 0){
    perror("Error creating notification FIFO");
    return 1;
  }

  // Open the server pipe
  int fd_server = open(server_pipe_path, O_WRONLY);
  if (fd_server == -1) {
    perror("Error opening server pipe from client");
    return 1;
  }
  // preparar mensagem de pedido
  char message[1 + 40 + 40 + 40];
  memset(message, 0, sizeof(message));
  message[0] = OP_CODE_CONNECT; // OP_CODE=1
  strncpy(message + 1, req_pipe_path, 40);
  strncpy(message + 1 + 40, resp_pipe_path, 40);
  strncpy(message + 1 + 80, notif_pipe_path, 40);

  // mandar mensagem ao serivor

  
  if (write(fd_server, message, sizeof(message)) < 0) {
      perror("Error writing to server FIFO");
      close(fd_server);
      return 1;
  }
  close(fd_server);

  // Open and read the response FIFO
  int resp_fd = open(resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
      perror("Error opening response FIFO");
      return 1;
  }

  // Read server response: OP_CODE(1) + result
  char resp_buf[3] = {0};
  if (read(resp_fd, resp_buf, sizeof(resp_buf)) < 0) {
      perror("read resp_pipe_path");
      close(resp_fd);
      return 1;
  }
  printf("resp_buf: %s\n", resp_buf);
  close(resp_fd);

  // Print response code
  printf("Server returned %c for operation: connect\n", resp_buf[1]);

  // Success if server returned 0
  return (resp_buf[1] == '0') ? 0 : 1;

}

int kvs_disconnect(char const *req_pipe_path, char const *resp_pipe_path) {
  // close pipes and unlink pipe files
  // Prepare message
  char request[1] = {OP_CODE_DISCONNECT};
  // Send to request pipe
  int req_fd = open(req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    perror("Error opening request FIFO for disconnect");
    return 1;
  }
  if (write(req_fd, request, sizeof(request)) < 0) {
    perror("Error writing disconnect request");
    close(req_fd);
    return 1;
  }

  close(req_fd);

  // Read response
  int resp_fd = open(resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    perror("Error opening response FIFO for disconnect");
    return 1;
  }
  // Read server response: OP_CODE(2) + result
  char resp_buf[3] = {0};
  printf("Reading\n");
  if (read(resp_fd, resp_buf, sizeof(resp_buf)) < 0) {
      perror("read resp_pipe_path");
      close(resp_fd);
      return 1;
  }
  printf("resp_buf: %s\n", resp_buf);
  close(resp_fd);

  // clean pipes

  unlink(s_req_pipe_path);
  unlink(s_resp_pipe_path); 
  unlink(s_notif_pipe_path);

  printf("Server returned %c for operation: disconnect\n", resp_buf[1]);
  return (resp_buf[1] == '0') ? 0 : 1;  
}

int kvs_subscribe(char const *req_pipe_path, char const *resp_pipe_path, const char *key) {
  // send subscribe message to request pipe and wait for response in response
  // pipe
  // Prepare message: OP_CODE=3 + 41-char buffer
  char request[1 + 41] = {0};
  request[0] = OP_CODE_SUBSCRIBE;
  strncpy(request + 1, key, 40);

  int req_fd = open(req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    perror("Error opening request FIFO for subscribe");
    return 1;
  }
  if (write(req_fd, request, sizeof(request)) < 0) {
    perror("Error writing subscribe request");
    close(req_fd);
    return 1;
  }
  close(req_fd);

  // Read response
  int resp_fd = open(resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    perror("Error opening response FIFO for subscribe");
    return 1;
  }
  char resp_buf[3] = {0};
  if (read(resp_fd, resp_buf, sizeof(resp_buf)) < 0) {
      perror("read resp_pipe_path");
      close(resp_fd);
      return 1;
  }
  printf("resp_buf: %s\n", resp_buf);
  close(resp_fd);

  printf ("%s\n", resp_buf);

  printf("Server returned %c for operation: subscribe\n", resp_buf[1]);
  return (resp_buf[1] == '0') ? 0 : 1;

}

int kvs_unsubscribe(char const *req_pipe_path, char const *resp_pipe_path, const char *key) {
  // send unsubscribe message to request pipe and wait for response in response
  // pipe
  // Prepare message: OP_CODE=4 + 41-char buffer
  char request[1 + 41] = {0};
  request[0] = OP_CODE_UNSUBSCRIBE;
  strncpy(request + 1, key, 40);

  int req_fd = open(req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    perror("Error opening request FIFO for unsubscribe");
    return 1;
  }
  if (write(req_fd, request, sizeof(request)) < 0) {
    perror("Error writing unsubscribe request");
    close(req_fd);
    return 1;
  }
  close(req_fd);

  // Read response
  int resp_fd = open(resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    perror("Error opening response FIFO for unsubscribe");
    return 1;
  }
  char resp_buf[3] = {0};
  if (read(resp_fd, resp_buf, sizeof(resp_buf)) < 0) {
      perror("read resp_pipe_path");
      close(resp_fd);
      return 1;
  }
  printf("resp_buf: %s\n", resp_buf);
  close(resp_fd);

  printf("Server returned %c for operation: unsubscribe\n", resp_buf[1]);
  return (resp_buf[1] == '0') ? 0 : 1;

}