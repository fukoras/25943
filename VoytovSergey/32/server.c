#ifdef __sun
#define __EXTENSIONS__
#else
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef __sun
#include <stropts.h>
#endif

#ifndef SIGPOLL
#define SIGPOLL SIGIO
#endif

#define SOCKET_PATH "/tmp/uds_32_s.voitov_upper.sock"
#define MAX_CLIENTS 64
#define BUF_SIZE 1024

static int client_fds[MAX_CLIENTS];
static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t got_sigpoll = 0;

static void handle_term_sig(int sig) {
    (void)sig;
    running = 0;
}

static void handle_sigpoll(int sig) {
    (void)sig;
    got_sigpoll = 1;
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

/* Включаем неблокирующий режим и генерацию SIGPOLL/SIGIO для сокета */
static bool enable_async_io(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return false;
    }

#ifdef __sun
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl O_NONBLOCK");
        return false;
    }
    if (ioctl(fd, I_SETSIG, S_INPUT | S_RDNORM | S_HANGUP) == -1) {
        perror("ioctl I_SETSIG");
        return false;
    }
#else
    if (fcntl(fd, F_SETOWN, getpid()) == -1) {
        perror("fcntl F_SETOWN");
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK | O_ASYNC) == -1) {
        perror("fcntl O_ASYNC");
        return false;
    }
#endif
    return true;
}

static void drain_client(int idx) {
    char buf[BUF_SIZE];
    int fd = client_fds[idx];
    if (fd == -1) {
        return;
    }

    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            to_upper_and_print(buf, n);
        } else if (n == 0) {
            close(fd);
            client_fds[idx] = -1;
            break;
        } else {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            perror("read client");
            close(fd);
            client_fds[idx] = -1;
            break;
        }
    }
}

static void accept_new_clients(int listen_fd) {
    while (1) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            perror("accept");
            break;
        }

        int idx = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] == -1) {
                idx = i;
                break;
            }
        }

        if (idx == -1) {
            fprintf(stderr, "Max clients reached, rejecting connection\n");
            close(client_fd);
            continue;
        }

        if (!enable_async_io(client_fd)) {
            close(client_fd);
            continue;
        }

        client_fds[idx] = client_fd;
        drain_client(idx);
    }
}

int main(void) {
    signal(SIGTTOU, SIG_IGN);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_fds[i] = -1;
    }

    sigset_t block_mask, old_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGPOLL);
    sigaddset(&block_mask, SIGINT);
    sigaddset(&block_mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &block_mask, &old_mask) == -1) {
        perror("sigprocmask");
        return EXIT_FAILURE;
    }

    struct sigaction sa_term;
    memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_handler = handle_term_sig;
    sigaction(SIGINT, &sa_term, NULL);
    sigaction(SIGTERM, &sa_term, NULL);

    struct sigaction sa_poll;
    memset(&sa_poll, 0, sizeof(sa_poll));
    sa_poll.sa_handler = handle_sigpoll;
    sigaction(SIGPOLL, &sa_poll, NULL);

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

    if (!enable_async_io(listen_fd)) {
        close(listen_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, 16) == -1) {
        perror("listen");
        close(listen_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }

    printf("Server 32 (Signal-driven I/O) listening on %s...\n", SOCKET_PATH);
    fflush(stdout);

    accept_new_clients(listen_fd);

    while (running) {
        sigsuspend(&old_mask);

        if (!running) {
            break;
        }
        if (got_sigpoll) {
            got_sigpoll = 0;
            accept_new_clients(listen_fd);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_fds[i] != -1) {
                    drain_client(i);
                }
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] != -1) {
            close(client_fds[i]);
        }
    }
    close(listen_fd);
    unlink(SOCKET_PATH);
    return EXIT_SUCCESS;
}
