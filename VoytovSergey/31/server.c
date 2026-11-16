#ifdef __sun
#define __EXTENSIONS__
#else
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCKET_PATH "/tmp/uds_31_s.voitov_upper.sock"
#define MAX_CLIENTS 64
#define BUF_SIZE 1024

static volatile sig_atomic_t running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}

static void to_upper_and_print(char *buf, ssize_t len) {
    for (ssize_t i = 0; i < len; i++) {
        buf[i] = (char)toupper((unsigned char)buf[i]);
    }
    ssize_t written = 0;
    while (written < len) {
        ssize_t n = write(STDOUT_FILENO, buf + written, (size_t)(len - written));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("write stdout");
            break;
        }
        written += n;
    }
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    unlink(SOCKET_PATH);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, 16) == -1) {
        perror("listen");
        close(listen_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }

    struct pollfd fds[MAX_CLIENTS + 1];
    fds[0].fd = listen_fd;
    fds[0].events = POLLIN;
    for (int i = 1; i <= MAX_CLIENTS; i++) {
        fds[i].fd = -1;
        fds[i].events = POLLIN;
    }

    printf("Server (poll) listening on %s...\n", SOCKET_PATH);
    fflush(stdout);

    char buf[BUF_SIZE];

    while (running) {
        int ready = poll(fds, MAX_CLIENTS + 1, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }

        // Проверяем новое подключение на слушающем сокете 
        if (fds[0].revents & POLLIN) {
            int client_fd = accept(listen_fd, NULL, NULL);
            if (client_fd >= 0) {
                bool placed = false;
                for (int i = 1; i <= MAX_CLIENTS; i++) {
                    if (fds[i].fd == -1) {
                        fds[i].fd = client_fd;
                        fds[i].events = POLLIN;
                        placed = true;
                        break;
                    }
                }
                if (!placed) {
                    fprintf(stderr, "Max clients reached, rejecting connection\n");
                    close(client_fd);
                }
            } else if (errno != EINTR) {
                perror("accept");
            }
        }

        // Проверяем активность на всех клиентских дескрипторах 
        for (int i = 1; i <= MAX_CLIENTS; i++) {
            if (fds[i].fd == -1) {
                continue;
            }
            if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                ssize_t n = read(fds[i].fd, buf, sizeof(buf));
                if (n > 0) {
                    to_upper_and_print(buf, n);
                } else {
                    if (n < 0 && errno == EINTR) {
                        continue;
                    }
                    close(fds[i].fd);
                    fds[i].fd = -1;
                }
            }
        }
    }

    for (int i = 0; i <= MAX_CLIENTS; i++) {
        if (fds[i].fd != -1) {
            close(fds[i].fd);
        }
    }
    unlink(SOCKET_PATH);
    return EXIT_SUCCESS;
}