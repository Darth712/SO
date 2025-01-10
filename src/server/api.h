#ifndef API_H
#define API_H

int connect (int fd_server);
int subscribe(int fd_req, const char *req_pipe_path);

#endif // API_H