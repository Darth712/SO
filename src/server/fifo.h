#ifndef FIFO_H
#define FIFO_H

char *client_name(const char *client_fifo);
void *fifo_reader (void *arg);

#endif // FIFO_H