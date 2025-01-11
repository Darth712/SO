/*#include "subscriptions.h"
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

SubscriptionHashTable *sht = NULL;

SubscriptionHashTable *create_subscription_table() {
    SubscriptionHashTable *sht = (SubscriptionHashTable *)malloc(sizeof(SubscriptionHashTable));
    if (!sht) {
        return NULL;
    }
    for (int i = 0; i < SUBSCRIPTION_TABLE_SIZE; i++) {
        sht->table[i] = NULL;
    }
    pthread_mutex_init(&sht->mutex, NULL);
    return sht;
}

int sht_init() {
    if (sht == NULL) {
        sht = create_subscription_table();
        if (sht == NULL) {
            return 1;
        }
    }
    return 0;
}

unsigned long hash_subscription(const char *key) {
    unsigned long hash = 5381;
    int c;
    while ((c = *key++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c 
    return hash % SUBSCRIPTION_TABLE_SIZE;
}

char *client_name(const char *client_fifo) {
    const char *last_slash = strrchr(client_fifo, '/');
    return (last_slash != NULL) ? (char *)(last_slash + 1) : (char *)client_fifo;
}

// Register a new client with no subscriptions
int register_client(const char *client_fifo) {
    pthread_mutex_lock(&sht->mutex);
    char* name = client_name(client_fifo);
    unsigned long index = hash_subscription(name);
    Client *current = sht->table[index];

    // Check if client already exists
    while (current) {
        if (strcmp(current->name, client_fifo) == 0) {
            pthread_mutex_unlock(&sht->mutex);
            return 1; // Client already registered
        }
        current = current->next;
    }

    // Create a new client
    Client *new_client = (Client *)malloc(sizeof(Client));
    if (!new_client) {
        pthread_mutex_unlock(&sht->mutex);
        return -1; // Memory allocation failed
    }
    strncpy(new_client->name, client_fifo, MAX_CLIENT_FIFO_PATH - 1);
    new_client->name[MAX_CLIENT_FIFO_PATH - 1] = '\0';
    new_client->subscriptions = NULL;
    new_client->next = sht->table[index];
    sht->table[index] = new_client;

    pthread_mutex_unlock(&sht->mutex);
    return 0; // Success
}

int unregister_client(const char *client_fifo) {
    pthread_mutex_lock(&sht->mutex);
    char* name = client_name(client_fifo);

    unsigned long index = hash_subscription(name);
    Client *current = sht->table[index];
    Client *prev = NULL;

    while (current) {
        if (strcmp(current->name, name) == 0) {
            // Remove client from the chain
            if (prev) {
                prev->next = current->next;
            } else {
                sht->table[index] = current->next;
            }

            // Free all subscriptions
            SubscriptionNode *sub = current->subscriptions;
            while (sub) {
                SubscriptionNode *temp = sub;
                sub = sub->next;
                free(temp);
            }

            free(current);
            pthread_mutex_unlock(&sht->mutex);
            return 0; // Success
        }
        prev = current;
        current = current->next;
    }

    pthread_mutex_unlock(&sht->mutex);
    return -1; // Client not found
}

int add_subscription(char *name, const char *key) {
    pthread_mutex_lock(&sht->mutex);
    unsigned long index = hash_subscription(name);
    Client *current = sht->table[index];

    // Find the client
    while (current) {
        if (strcmp(current->name, name) == 0) {
            break;
        }
        current = current->next;
    }

    if (!current) {
        pthread_mutex_unlock(&sht->mutex);
        return -1; // Client not found
    }

    // Check if already subscribed to the key
    SubscriptionNode *sub = current->subscriptions;
    while (sub) {
        if (strcmp(sub->key, key) == 0) {
            pthread_mutex_unlock(&sht->mutex);
            return 1; // Already subscribed
        }
        sub = sub->next;
    }

    // Add new subscription
    SubscriptionNode *new_sub = (SubscriptionNode *)malloc(sizeof(SubscriptionNode));
    if (!new_sub) {
        pthread_mutex_unlock(&sht->mutex);
        return -1; // Memory allocation failed
    }
    strncpy(new_sub->key, key, MAX_KEY_LENGTH - 1);
    new_sub->key[MAX_KEY_LENGTH - 1] = '\0';
    new_sub->next = current->subscriptions;
    current->subscriptions = new_sub;

    pthread_mutex_unlock(&sht->mutex);
    return 0; // Success
}

int remove_subscription(const char *client_fifo, const char *key) {
    pthread_mutex_lock(&sht->mutex);
    char* name = client_name(client_fifo);
    unsigned long index = hash_subscription(name);
    Client *client = sht->table[index];
    Client *prev_client = NULL;

    // Search for the client
    while (client != NULL && strcmp(client->name, name) != 0) {
        prev_client = client;
        client = client->next;
    }

    if (client == NULL) {
        pthread_mutex_unlock(&sht->mutex);
        return -1; // Client not found
    }

    // Search for the subscription
    SubscriptionNode *sub = client->subscriptions;
    SubscriptionNode *prev_sub = NULL;
    while (sub != NULL && strcmp(sub->key, key) != 0) {
        prev_sub = sub;
        sub = sub->next;
    }

    if (sub == NULL) {
        pthread_mutex_unlock(&sht->mutex);
        return -1; // Subscription not found
    }

    // Remove the subscription node
    if (prev_sub == NULL) {
        client->subscriptions = sub->next;
    } else {
        prev_sub->next = sub->next;
    }
    free(sub);

    // If the client has no more subscriptions, remove the client
    if (client->subscriptions == NULL) {
        if (prev_client == NULL) {
            sht->table[index] = client->next;
        } else {
            prev_client->next = client->next;
        }
        free(client);
    }

    pthread_mutex_unlock(&sht->mutex);
    return 0; // Success
}

int notify_subscribers(const char *key) {
    pthread_mutex_lock(&sht->mutex);

    for (int i = 0; i < SUBSCRIPTION_TABLE_SIZE; i++) {
        Client *current = sht->table[i];
        while (current) {
            SubscriptionNode *sub = current->subscriptions;
            while (sub) {
                if (strcmp(sub->key, key) == 0) {
                    // Open client's FIFO for writing
                    int fd = open(current->name, O_WRONLY | O_NONBLOCK);
                    if (fd != -1) {
                        char notification = '1'; // Indicates key exists
                        if (write(fd, &notification, sizeof(notification)) == -1) {
                            perror("Error writing notification to client FIFO");
                        }
                        close(fd);
                    } else {
                        perror("Error opening client FIFO for notification");
                    }
                }
                sub = sub->next;
            }
            current = current->next;
        }
    }

    pthread_mutex_unlock(&sht->mutex);
    return 0; // Success
} */