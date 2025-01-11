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
#include "api.h"
#include "fifo.h"

void *fifo_reader (void *arg) {
  //pthread_mutex_lock(&lock);
  char *fifo_registry = (char *)arg;
  char *name = client_name(fifo_registry);
  printf("reading from %s\n", fifo_registry);
  fflush(stdout);
  while (1) {
    int fd = open(fifo_registry, O_RDONLY);
    if (fd == -1) {
      write_str(STDERR_FILENO, "Failed to open request pipe\n");
    return 0;
    }
    char opcode;
    int interrupted = 0;
    printf("reading\n");
    if (read_all(fd, &opcode, sizeof(opcode), &interrupted) != 1) {
      if (interrupted) {
        write_str(STDERR_FILENO, "Read operation was interrupted\n");
      } 
      close(fd);
      continue;
    }
    printf("opcode: %d\n", opcode);
 
    switch (opcode) {
      case OP_CODE_CONNECT:
        if(connect(fd)){
          write_str(STDERR_FILENO, "Failed to connect to server\n");
        }
        break;
      case OP_CODE_DISCONNECT:
        if (disconnect(name + 3)) {
          write_str(STDERR_FILENO, "Failed to disconnect\n");
        }
        break;
      case OP_CODE_SUBSCRIBE:
        if (subscribe(fd,name + 3)) {
          write_str(STDERR_FILENO, "Failed to subscribe\n");
        }
        break;
      case OP_CODE_UNSUBSCRIBE:
        // Handle command operation
        break;
      default:
        write_str(STDERR_FILENO, "Unknown opcode\n");
        break;
    }

    close(fd);
  }
  return NULL;
}

void *fifo_writer (void *arg) {
    //pthread_mutex_lock(&lock);  
    WriterArgs *args = (WriterArgs *)arg;
    char *fifo_registry = args->fifo_path;
    char *message = args->data;
    printf("writing to %s\n", args->fifo_path);
    printf("message: %s\n", args->data);

    int fd = open(fifo_registry, O_WRONLY);
    if (fd == -1) {
        write_str(STDERR_FILENO, "Failed to open FIFO for writing\n");
        return NULL;
    }

    write(fd, message, strlen(message));
    close(fd);

    return NULL;
}