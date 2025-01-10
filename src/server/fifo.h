#ifndef FIFO_H
#define FIFO_H

typedef struct {
    char *fifo_path;
    char *data;
} WriterArgs;

void *fifo_reader (void *arg);
void *fifo_writer (void *arg);


#endif // FIFO_H