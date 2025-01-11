#include "operations.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "constants.h"
#include "io.h"
#include "kvs.h"

static struct HashTable *kvs_table = NULL;

/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

int kvs_init() {
  if (kvs_table != NULL) {
    fprintf(stderr, "KVS state has already been initialized\n");
    return 1;
  }

  kvs_table = create_hash_table();
  return kvs_table == NULL;
}

int kvs_terminate() {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  free_table(kvs_table);
  kvs_table = NULL;
  return 0;
}

int kvs_write(size_t num_pairs, char keys[][MAX_STRING_SIZE],
              char values[][MAX_STRING_SIZE]) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  pthread_rwlock_wrlock(&kvs_table->tablelock);

  for (size_t i = 0; i < num_pairs; i++) {
    if (write_pair(kvs_table, keys[i], values[i]) != 0) {
      fprintf(stderr, "Failed to write key pair (%s,%s)\n", keys[i], values[i]);
    }
  }

  pthread_rwlock_unlock(&kvs_table->tablelock);
  return 0;
}

int kvs_read(size_t num_pairs, char keys[][MAX_STRING_SIZE], int fd) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  pthread_rwlock_rdlock(&kvs_table->tablelock);

  write_str(fd, "[");
  for (size_t i = 0; i < num_pairs; i++) {
    char *result = read_pair(kvs_table, keys[i]);
    char aux[MAX_STRING_SIZE];
    if (result == NULL) {
      snprintf(aux, MAX_STRING_SIZE, "(%s,KVSERROR)", keys[i]);
    } else {
      snprintf(aux, MAX_STRING_SIZE, "(%s,%s)", keys[i], result);
    }
    write_str(fd, aux);
    free(result);
  }
  write_str(fd, "]\n");

  pthread_rwlock_unlock(&kvs_table->tablelock);
  return 0;
}

int kvs_delete(size_t num_pairs, char keys[][MAX_STRING_SIZE], int fd) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  pthread_rwlock_wrlock(&kvs_table->tablelock);

  int aux = 0;
  for (size_t i = 0; i < num_pairs; i++) {
    if (delete_pair(kvs_table, keys[i]) != 0) {
      if (!aux) {
        write_str(fd, "[");
        aux = 1;
      }
      char str[MAX_STRING_SIZE];
      snprintf(str, MAX_STRING_SIZE, "(%s,KVSMISSING)", keys[i]);
      write_str(fd, str);
    }
  }
  if (aux) {
    write_str(fd, "]\n");
  }

  pthread_rwlock_unlock(&kvs_table->tablelock);
  return 0;
}

void kvs_show(int fd) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return;
  }

  pthread_rwlock_rdlock(&kvs_table->tablelock);
  char aux[MAX_STRING_SIZE];

  for (int i = 0; i < TABLE_SIZE; i++) {
    KeyNode *keyNode = kvs_table->table[i]; // Get the next list head
    while (keyNode != NULL) {
      snprintf(aux, MAX_STRING_SIZE, "(%s, %s)\n", keyNode->key,
               keyNode->value);
      write_str(fd, aux);
      keyNode = keyNode->next; // Move to the next node of the list
    }
  }

  pthread_rwlock_unlock(&kvs_table->tablelock);
}

int kvs_backup(size_t num_backup, char *job_filename, char *directory) {
  pid_t pid;
  char bck_name[50];
  snprintf(bck_name, sizeof(bck_name), "%s/%s-%ld.bck", directory,
           strtok(job_filename, "."), num_backup);

  pthread_rwlock_rdlock(&kvs_table->tablelock);
  pid = fork();
  pthread_rwlock_unlock(&kvs_table->tablelock);
  if (pid == 0) {
    // functions used here have to be async signal safe, since this
    // fork happens in a multi thread context (see man fork)
    int fd = open(bck_name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    for (int i = 0; i < TABLE_SIZE; i++) {
      KeyNode *keyNode = kvs_table->table[i]; // Get the next list head
      while (keyNode != NULL) {
        char aux[MAX_STRING_SIZE];
        aux[0] = '(';
        size_t num_bytes_copied = 1; // the "("
        // the - 1 are all to leave space for the '/0'
        num_bytes_copied += strn_memcpy(aux + num_bytes_copied, keyNode->key,
                                        MAX_STRING_SIZE - num_bytes_copied - 1);
        num_bytes_copied += strn_memcpy(aux + num_bytes_copied, ", ",
                                        MAX_STRING_SIZE - num_bytes_copied - 1);
        num_bytes_copied += strn_memcpy(aux + num_bytes_copied, keyNode->value,
                                        MAX_STRING_SIZE - num_bytes_copied - 1);
        num_bytes_copied += strn_memcpy(aux + num_bytes_copied, ")\n",
                                        MAX_STRING_SIZE - num_bytes_copied - 1);
        aux[num_bytes_copied] = '\0';
        write_str(fd, aux);
        keyNode = keyNode->next; // Move to the next node of the list
      }
    }
    exit(1);
  } else if (pid < 0) {
    return -1;
  }
  return 0;
}

void kvs_wait(unsigned int delay_ms) {
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}

int kvs_subscribe(char key[MAX_STRING_SIZE], const char* notif_pipe_path, char *name) {
    pthread_rwlock_wrlock(&kvs_table->tablelock);
    KeyNode *keyNode = kvs_table->table[hash(key)];
    while (keyNode != NULL) {
        if (strcmp(keyNode->key, key) == 0) {
            // Duplicate notif_pipe_path
            char *notif_pipe = strdup(notif_pipe_path);
            if (!notif_pipe) {
                pthread_rwlock_unlock(&kvs_table->tablelock);
                return 0;
            }

            // Save old array of pointers
            char **old_paths = keyNode->notif_pipe_paths;

            // Allocate new array with one extra slot
            keyNode->notif_pipe_paths = malloc((keyNode->notif_pipe_count + 1) * sizeof(char *));
            if (!keyNode->notif_pipe_paths) {
                // Restore old pointer and free new string
                keyNode->notif_pipe_paths = old_paths;
                free(notif_pipe);
                pthread_rwlock_unlock(&kvs_table->tablelock);
                return 0;
            }

            // Copy old pointers to new array
            for (int i = 0; i < keyNode->notif_pipe_count; i++) {
                keyNode->notif_pipe_paths[i] = old_paths[i];
            }

            // Free the old pointer array (not the strings)
            if (old_paths) {
                free(old_paths);
            }

            // Add the new path at the end
            keyNode->notif_pipe_paths[keyNode->notif_pipe_count] = notif_pipe;
            keyNode->notif_pipe_count++;

            pthread_rwlock_unlock(&kvs_table->tablelock);
            return 1;
        }
        keyNode = keyNode->next;
    }
    pthread_rwlock_unlock(&kvs_table->tablelock);
    return 0; // Key not found
}

int kvs_unsubscribe(char key[MAX_STRING_SIZE], const char* notif_pipe_path, char *name) {
    pthread_rwlock_wrlock(&kvs_table->tablelock);
    KeyNode *keyNode = kvs_table->table[hash(key)];
    while (keyNode != NULL) {
        if (strcmp(keyNode->key, key) == 0) {
            // Find the index of the pipe to remove
            int remove_index = -1;
            for (int i = 0; i < keyNode->notif_pipe_count; i++) {
                if (strcmp(keyNode->notif_pipe_paths[i], notif_pipe_path) == 0) {
                    remove_index = i;
                    break;
                }
            }
            if (remove_index == -1) {
                // Pipe not found
                pthread_rwlock_unlock(&kvs_table->tablelock);
                return 0;
            }

            // Free the string for the pipe to be removed
            free(keyNode->notif_pipe_paths[remove_index]);

            // If only one pipe, free array entirely
            if (keyNode->notif_pipe_count == 1) {
                free(keyNode->notif_pipe_paths);
                keyNode->notif_pipe_paths = NULL;
                keyNode->notif_pipe_count = 0;
                pthread_rwlock_unlock(&kvs_table->tablelock);
                return 1;
            }

            // Rebuild array without removed element
            char **old_paths = keyNode->notif_pipe_paths;
            keyNode->notif_pipe_count--;
            keyNode->notif_pipe_paths = malloc(keyNode->notif_pipe_count * sizeof(char*));
            if (!keyNode->notif_pipe_paths) {
                // Restore old array in case of failure
                keyNode->notif_pipe_paths = old_paths;
                keyNode->notif_pipe_count++;
                pthread_rwlock_unlock(&kvs_table->tablelock);
                return 0;
            }
            int j = 0;
            for (int i = 0; i < keyNode->notif_pipe_count + 1; i++) {
                if (i != remove_index) {
                    keyNode->notif_pipe_paths[j++] = old_paths[i];
                }
            }
            free(old_paths);
            pthread_rwlock_unlock(&kvs_table->tablelock);
            return 1;
        }
        keyNode = keyNode->next;
    }
    pthread_rwlock_unlock(&kvs_table->tablelock);
    return 0; // Key not found
}

void kvs_print_notif_pipes(const char *key) {
    pthread_rwlock_rdlock(&kvs_table->tablelock);
    KeyNode *keyNode = kvs_table->table[hash(key)];
    while (keyNode) {
        if (strcmp(keyNode->key, key) == 0) {
            printf("Notification pipes for key: %s\n", key);
            for (int i = 0; i < keyNode->notif_pipe_count; i++) {
                printf("  [%d] %s\n", i, keyNode->notif_pipe_paths[i]);
            }
            pthread_rwlock_unlock(&kvs_table->tablelock);
            return;
        }
        keyNode = keyNode->next;
    }
    pthread_rwlock_unlock(&kvs_table->tablelock);
    printf("No notification pipes found for key: %s\n", key);
}