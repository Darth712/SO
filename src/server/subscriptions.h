/*#ifndef SUBSCRIPTIONS_H
#define SUBSCRIPTIONS_H

#include <pthread.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>

#define MAX_CLIENT_FIFO_PATH 256
#define MAX_KEY_LENGTH 256
#define SUBSCRIPTION_TABLE_SIZE 128

typedef struct SubscriptionNode {
    char key[MAX_KEY_LENGTH];
    struct SubscriptionNode *next;
} SubscriptionNode;

typedef struct Client {
    char name[MAX_CLIENT_FIFO_PATH];      // Unique identifier for the client 
    SubscriptionNode *subscriptions;      // Linked list of subscribed keys
    struct Client *next;                  // Pointer to the next client in the chain
} Client;

// Hashtable parameters
typedef struct SubscriptionHashTable {
    Client *table[SUBSCRIPTION_TABLE_SIZE]; // Array of client chains
    pthread_mutex_t mutex;                  // Mutex for thread safety
} SubscriptionHashTable;

int sht_init();
SubscriptionHashTable* create_subscription_table();
char *client_name(const char *client_fifo);
unsigned long hash_subscription(const char *key);
int register_client(const char *client_fifo);
int add_subscription( char *name, const char *key);
int remove_subscription(const char *client_fifo, const char *key);
int notify_subscribers(const char *key);

#endif // SUBSCRIPTIONS_H*/