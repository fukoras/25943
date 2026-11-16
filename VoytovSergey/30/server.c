#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCKET_PATH "/tmp/task30_unix.sock"

static int g_server_fd = -1;

static void cleanup_and_exit(int sig) {
    (void)sig;
    if (g_server_fd != -1) {
        close(g_server_fd);
    }
    unlink(SOCKET_PATH);
    _exit(130);
}

static int write_all(int fd, const void *data, size_t count) {
    const char *ptr = (const char *)data;
    size_t written = 0;
    while (written < count) {
        ssize_t n = write(fd, ptr + written, count - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        written += (size_t)n;
    }
    return 0;
}

int main(void) {
    struct sockaddr_un addr;
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = cleanup_and_exit;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    g_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    /* Удаляем файл сокета, если он остался от предыдущего запуска */
    if (unlink(SOCKET_PATH) < 0 && errno != ENOENT) {
        perror("unlink");
        close(g_server_fd);
        return EXIT_FAILURE;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(g_server_fd);
        return EXIT_FAILURE;
    }

    if (listen(g_server_fd, 1) < 0) {
        perror("listen");
        close(g_server_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Сервер слушает на %s...\n", SOCKET_PATH);

    int client_fd = accept(g_server_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept");
        close(g_server_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }

    char chunk[256];
    ssize_t n;
    while ((n = read(client_fd, chunk, sizeof(chunk))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            chunk[i] = (char)toupper((unsigned char)chunk[i]);
        }
        if (write_all(STDOUT_FILENO, chunk, (size_t)n) < 0) {
            perror("write stdout");
            break;
        }
    }

    if (n < 0) {
        perror("read socket");
    }

    /* Клиент закрыл соединение (n == 0) — закрываем дескрипторы и завершаемся */
    close(client_fd);
    close(g_server_fd);
    unlink(SOCKET_PATH);

    return EXIT_SUCCESS;
}