#ifndef API_H
#define API_H

int connect (int fd_server);
int subscribe(int fd_req, char *name);

#endif // API_H