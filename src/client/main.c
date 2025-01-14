#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "parser.h"
#include "api.h"
#include "../common/constants.h"
#include "../common/io.h"

// unfinished
static void* notification_handler(void* arg) {
    printf("[DEBUG] Notification handler started\n");
    char* notif_path = (char*)arg;
  

    while (1) {
        char key[41] = {0};
        char value[41] = {0};

        int notif_fd = open(notif_path, O_RDONLY);
        if (notif_fd < 0) {
          perror("Failed to open notification FIFO");
          return NULL;
        }        
        // Read fixed-size fields
        if (read_all(notif_fd, key, 41, NULL) <= 0 ||
            read_all(notif_fd, value, 41, NULL) <= 0) {
            close(notif_fd);
            continue;
        }

        // Trim spaces
        for (int i = 39; i >= 0; i--) {
            if (key[i] == ' ') key[i] = '\0'; else break;
        }
        for (int i = 39; i >= 0; i--) {
            if (value[i] == ' ') value[i] = '\0'; else break;
        }

        printf("(%s,%s)\n", key, value);
        close(notif_fd);
    }
    

    return NULL;
}




int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <client_unique_id> <register_pipe_path>\n",
            argv[0]);
    return 1;
  }

  char req_pipe_path[256] = "/tmp/req";
  char resp_pipe_path[256] = "/tmp/resp";
  char notif_pipe_path[256] = "/tmp/notif";
  char server_pipe_path[256] = "../server/";
  char keys[MAX_NUMBER_SUB][MAX_STRING_SIZE] = {0};
  unsigned int delay_ms;
  size_t num;

  strncat(req_pipe_path, argv[1], strlen(argv[1]) * sizeof(char));
  strncat(resp_pipe_path, argv[1], strlen(argv[1]) * sizeof(char));
  strncat(notif_pipe_path, argv[1], strlen(argv[1]) * sizeof(char));
  strncat(server_pipe_path, argv[2], strlen(argv[2]) * sizeof(char));


  if(strlen(argv[2]) > MAX_PIPE_PATH_LENGTH) {
    fprintf(stderr, "Invalid register pipe path\n");
    return 1;
  }
 // int notif_pipe;
  if (kvs_connect(req_pipe_path, resp_pipe_path, server_pipe_path, notif_pipe_path) != 0) {
    fprintf(stderr, "Failed to connect to the server\n");
    return 1;
  }  //se a conexão for successful entao avançamos
  printf("Connected to server\n");

  pthread_t notif_thread;
  if (pthread_create(&notif_thread, NULL, notification_handler, (void*)notif_pipe_path) != 0) {
      perror("Failed to create notification thread");
      return 1;
  }

  
  while (1) {
    switch (get_next(STDIN_FILENO)) {
    case CMD_DISCONNECT:
      if (kvs_disconnect(req_pipe_path, resp_pipe_path)) {
        fprintf(stderr, "Failed to disconnect to the server\n");
        return 1;
      }
      //close(notif_pipe);
      pthread_cancel(notif_thread);
      pthread_join(notif_thread, NULL);
      printf("Disconnected from server\n");
      return 0;

    case CMD_SUBSCRIBE:
      num = parse_list(STDIN_FILENO, keys, 1, MAX_STRING_SIZE);
      if (num == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_subscribe(req_pipe_path, resp_pipe_path, keys[0])) {
        fprintf(stderr, "Command subscribe failed\n");
      }

      break;

    case CMD_UNSUBSCRIBE:
      num = parse_list(STDIN_FILENO, keys, 1, MAX_STRING_SIZE);
      if (num == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_unsubscribe(req_pipe_path, resp_pipe_path, keys[0])) {
        fprintf(stderr, "Command subscribe failed\n");
      }

      break;

    case CMD_DELAY:
      if (parse_delay(STDIN_FILENO, &delay_ms) == -1) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (delay_ms > 0) {
        printf("Waiting...\n");
        delay(delay_ms);
      }
      break;

    case CMD_INVALID:
      fprintf(stderr, "Invalid command. See HELP for usage\n");
      break;

    case CMD_EMPTY:
      break;

    case EOC:
      // input should end in a disconnect, or it will loop here forever
      break;
    }
  }
}