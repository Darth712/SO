#ifndef API_H
#define API_H

int connect (int fd_server);
int disconnect(const char *name);
int subscribe(int fd_req, char *name);

#endif // API_H