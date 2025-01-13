#include "operations.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <semaphore.h>

#include "constants.h"
#include "io.h"
#include "kvs.h"
#include "fifo.h"
#include "api.h"

static struct HashTable *kvs_table = NULL;
extern sem_t session_sem;

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
  // Destroy the semaphore
  if (sem_destroy(&session_sem) != 0) {
    perror("Failed to destroy semaphore");
    return 1;
  }
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
    char *old_value = read_pair(kvs_table, keys[i]);
    if (write_pair(kvs_table, keys[i], values[i]) != 0) {
      fprintf(stderr, "Failed to write key pair (%s,%s)\n", keys[i], values[i]);
    }
    else {
      if (!old_value || strcmp(old_value, values[i]) != 0) {
        char message[MAX_STRING_SIZE]; // Single buffer for the message
        char *new_value = read_pair(kvs_table, keys[i]); // Assume this returns a valid string
        if (new_value) { // Check if read_pair returned a valid value
            snprintf(message, MAX_STRING_SIZE, "(<%s>,<%s>)", keys[i], new_value);
        } else {
            snprintf(message, MAX_STRING_SIZE, "(<%s>,<null>)", keys[i]); // Handle null case
        }
        kvs_notify(keys[i], message);
        free(new_value);

      }
    }
    free(old_value);
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

int kvs_subscribe(char key[MAX_STRING_SIZE], const char* notif_pipe_path) {
    pthread_rwlock_wrlock(&kvs_table->tablelock);
    KeyNode *keyNode = kvs_table->table[hash(key)];
    while (keyNode != NULL) {
        if (strcmp(keyNode->key, key) == 0) {
            
            // Check if notif_pipe_path already exists
            for (size_t i = 0; i < keyNode->notif_pipe_count; i++) {
                if (strcmp(keyNode->notif_pipe_paths[i], notif_pipe_path) == 0) {
                    // Already subscribed
                    pthread_rwlock_unlock(&kvs_table->tablelock);
                    return 0;
                }
            }

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
            for (size_t i = 0; i < keyNode->notif_pipe_count; i++) {
                keyNode->notif_pipe_paths[i] = old_paths[i];
            }

            // Free the old pointer array (not the strings)
            if (old_paths) {
                free(old_paths);
            }

            // Add the new path
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

int kvs_unsubscribe(char key[MAX_STRING_SIZE], const char* notif_pipe_path) {
    pthread_rwlock_wrlock(&kvs_table->tablelock);
    KeyNode *keyNode = kvs_table->table[hash(key)];
    while (keyNode != NULL) {
        if (strcmp(keyNode->key, key) == 0) {
            // Find the index of the pipe to remove
            int remove_index = -1;
            for (size_t i = 0; i < keyNode->notif_pipe_count; i++) {
                if (strcmp(keyNode->notif_pipe_paths[i], notif_pipe_path) == 0) {
                    remove_index = (int)i;
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
            for (size_t i = 0; i < keyNode->notif_pipe_count + 1; i++) {
                if ((int)i != remove_index) {
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

// Consider calling this in your disconnect() function
void kvs_unsubscribe_all_keys(const char *client_name) {
    // Acquire a write lock to modify the table
    pthread_rwlock_wrlock(&kvs_table->tablelock);
    
    for (int i = 0; i < TABLE_SIZE; i++) {
        KeyNode *keyNode = kvs_table->table[i];

        while (keyNode) {
            size_t j = 0;
            // Iterate all pipes in this keyNode
            while (j < keyNode->notif_pipe_count) {
                // If pipe matches client_name (substring check or exact match)
                if (strstr(keyNode->notif_pipe_paths[j], client_name)) {
                    // Free the string
                    free(keyNode->notif_pipe_paths[j]);
                    
                    // Shift the rest left
                    for (size_t k = j; k < keyNode->notif_pipe_count - 1; k++) {
                        keyNode->notif_pipe_paths[k] = keyNode->notif_pipe_paths[k + 1];
                    }
                    keyNode->notif_pipe_count--;
                } else {
                    j++;
                }
            }
            // If the list is empty, free the array
            if (keyNode->notif_pipe_count == 0) {
                free(keyNode->notif_pipe_paths);
                keyNode->notif_pipe_paths = NULL;
            }
            keyNode = keyNode->next;
        }
    }
    
    pthread_rwlock_unlock(&kvs_table->tablelock);
}

// Function to unsubscribe all keys from the KVS
void kvs_subscription_terminate() {
    // Acquire a write lock to modify the hashtable
    pthread_rwlock_wrlock(&kvs_table->tablelock);
    
    // Iterate over each bucket in the hashtable
    for (int i = 0; i < TABLE_SIZE; i++) {
        KeyNode *keyNode = kvs_table->table[i];
        
        // Traverse the linked list of KeyNodes in the current bucket
        while (keyNode) {
            printf("Unsubscribing from all keys\n");
            // Iterate through all notification pipes for the current key
            for (size_t j = 0; j < keyNode->notif_pipe_count; j++) {
                // Attempt to open the notification pipe in write-only mode
                int fd = open(keyNode->notif_pipe_paths[j], O_WRONLY);
                if (fd != -1) {
                    // Close the pipe to signal the client
                    close(fd);
                    char *name = client_name(keyNode->notif_pipe_paths[j]);
                    printf("Unsubscribed from %s\n", name + 5);
                    disconnect(name + 5);
                    
                } else {
                    // If opening fails, log the error (pipe might already be closed)
                    perror("Error closing notification pipe");
                }

                // Free the memory allocated for the notification pipe path
                free(keyNode->notif_pipe_paths[j]);
                keyNode->notif_pipe_paths[j] = NULL;
            }

            // Free the array of notification pipe paths
            free(keyNode->notif_pipe_paths);
            keyNode->notif_pipe_paths = NULL;
            // Reset the notification pipe count
            keyNode->notif_pipe_count = 0;

            // Move to the next KeyNode in the linked list
            keyNode = keyNode->next;
        }
    }
    
    // Release the write lock
    pthread_rwlock_unlock(&kvs_table->tablelock);
}

// Function to close all FIFOs (response and notification)
void close_all_fifos() {
    pthread_rwlock_rdlock(&kvs_table->tablelock);

    for (int i = 0; i < TABLE_SIZE; i++) {
        KeyNode *keyNode = kvs_table->table[i];
        while (keyNode) {
            // Close all notification FIFOs
            for (size_t j = 0; j < keyNode->notif_pipe_count; j++) {
                close(open(keyNode->notif_pipe_paths[j], O_WRONLY));
            }

            // Assuming you store resp_pipe_paths similarly
            // If not, implement accordingly
            // Example:
            // close(open(keyNode->resp_pipe_path, O_WRONLY));

            keyNode = keyNode->next;
        }
    }

    pthread_rwlock_unlock(&kvs_table->tablelock);
}

int kvs_notify(const char *key, char message[MAX_STRING_SIZE]) {
    KeyNode *keyNode = kvs_table->table[hash(key)];
    while (keyNode) {
      if (strcmp(keyNode->key, key) == 0) {
        for (size_t i = 0; i < keyNode->notif_pipe_count; i++) {
          if (keyNode->notif_pipe_paths[i] != NULL) {
            int fd = open(keyNode->notif_pipe_paths[i], O_WRONLY);
            if (fd != -1) {
              printf("Sending notification to %s: %s\n", keyNode->notif_pipe_paths[i], message);
              write(fd, message, strlen(message));
              close(fd);
            }
          }
        }
        break;
      }
      keyNode = keyNode->next;
    }
    return 0;
}