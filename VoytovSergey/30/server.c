#ifdef __sun
#define __EXTENSIONS__
#else
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCKET_PATH "/tmp/uds_30_s.voitov_upper.sock"
#define BUF_SIZE 1024

static volatile sig_atomic_t running = 1;

static void handle_term_sig(int sig) {
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
    /* Не даём фоновому процессу остановиться при выводе в терминал */
    signal(SIGTTOU, SIG_IGN);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_term_sig;
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

    if (listen(listen_fd, 1) == -1) {
        perror("listen");
        close(listen_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }

    int client_fd = -1;
    while (running) {
        client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd >= 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        perror("accept");
        close(listen_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }

    close(listen_fd);

    if (client_fd >= 0) {
        char buf[BUF_SIZE];
        while (running) {
            ssize_t n = read(client_fd, buf, sizeof(buf));
            if (n > 0) {
                to_upper_and_print(buf, n);
            } else if (n == 0) {
                /* Клиент закрыл соединение -> завершаем сервер */
                break;
            } else {
                if (errno == EINTR) {
                    continue;
                }
                perror("read");
                break;
            }
        }
        close(client_fd);
    }

    unlink(SOCKET_PATH);
    return EXIT_SUCCESS;
}