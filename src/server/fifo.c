/*#include <dirent.h>
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
#include "api.h"
#include "fifo.h"

char *client_name(const char *client_fifo) {
    const char *last_slash = strrchr(client_fifo, '/');
    return (last_slash != NULL) ? (char *)(last_slash + 1) : (char *)client_fifo;
}

// Function to block SIGUSR1 in client threads
void block_sigusr1() {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
    perror("Failed to block SIGUSR1 in client thread");
    pthread_exit(NULL);
  }
}

void *fifo_reader (void *arg) {
  // First, block SIGUSR1
  block_sigusr1();
  //pthread_mutex_lock(&lock);
  char *fifo_registry = (char *)arg;
  char *name = client_name(fifo_registry);
  fflush(stdout);
  while (1) {
    int fd = open(fifo_registry, O_RDONLY);
    if (fd == -1) {
      write_str(STDERR_FILENO, "Failed to open request pipe\n");
    return 0;
    }
    char opcode;
    int interrupted = 0;
    if (read_all(fd, &opcode, sizeof(opcode), &interrupted) != 1) {
      if (interrupted) {
        write_str(STDERR_FILENO, "Read operation was interrupted\n");
      } 
      close(fd);
      continue;
    }
 
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
        if (unsubscribe(fd,name + 3)) {
          write_str(STDERR_FILENO, "Failed to unsubscribe\n");
        }
        break;
      default:
        write_str(STDERR_FILENO, "Unknown opcode\n");
        break;
    }

    close(fd);
  }
  return NULL;
}*/