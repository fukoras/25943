#ifdef __sun
#define __EXTENSIONS__
#else
#define _GNU_SOURCE
#endif

#include <aio.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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

typedef struct {
    bool active;
    int fd;
    struct aiocb cb;
    char buf[BUF_SIZE];
    volatile sig_atomic_t completed;
} ClientSlot;

static ClientSlot clients[MAX_CLIENTS];
static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t got_accept_sig = 0;
static volatile sig_atomic_t got_aio_sig = 0;

static void handle_term_sig(int sig) {
    (void)sig;
    running = 0;
}

static void handle_accept_sig(int sig) {
    (void)sig;
    got_accept_sig = 1;
}

static void handle_aio_sig(int sig, siginfo_t *info, void *ucontext) {
    (void)sig;
    (void)ucontext;
    got_aio_sig = 1;
    if (info != NULL && info->si_value.sival_ptr != NULL) {
        ClientSlot *slot = (ClientSlot *)info->si_value.sival_ptr;
        slot->completed = 1;
    }
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

static bool start_async_read(ClientSlot *slot) {
    slot->completed = 0;
    memset(&slot->cb, 0, sizeof(struct aiocb));
    slot->cb.aio_fildes = slot->fd;
    slot->cb.aio_buf = slot->buf;
    slot->cb.aio_nbytes = BUF_SIZE;
    slot->cb.aio_offset = 0;
    slot->cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    slot->cb.aio_sigevent.sigev_signo = SIGUSR1;
    slot->cb.aio_sigevent.sigev_value.sival_ptr = slot;

    if (aio_read(&slot->cb) == -1) {
        perror("aio_read");
        return false;
    }
    return true;
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
            if (!clients[i].active) {
                idx = i;
                break;
            }
        }

        if (idx == -1) {
            fprintf(stderr, "Max clients reached, rejecting connection\n");
            close(client_fd);
            continue;
        }

        clients[idx].active = true;
        clients[idx].fd = client_fd;
        if (!start_async_read(&clients[idx])) {
            close(client_fd);
            clients[idx].active = false;
            clients[idx].fd = -1;
        }
    }
}

static void process_completed_aio(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            continue;
        }

        int err = aio_error(&clients[i].cb);
        if (err == EINPROGRESS) {
            continue;
        }

        ssize_t n = aio_return(&clients[i].cb);
        if (err == 0 && n > 0) {
            to_upper_and_print(clients[i].buf, n);
            if (!start_async_read(&clients[i])) {
                close(clients[i].fd);
                clients[i].active = false;
                clients[i].fd = -1;
            }
        } else {
            /* EOF (n == 0) или ошибка чтения -> закрываем клиента */
            close(clients[i].fd);
            clients[i].active = false;
            clients[i].fd = -1;
        }
    }
}

int main(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].active = false;
        clients[i].fd = -1;
    }

    // Блокируем сигналы перед настройкой, чтобы избежать гонок с sigsuspend
    sigset_t block_mask, old_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGUSR1);
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

    struct sigaction sa_acc;
    memset(&sa_acc, 0, sizeof(sa_acc));
    sa_acc.sa_handler = handle_accept_sig;
    sigaction(SIGPOLL, &sa_acc, NULL);

    struct sigaction sa_aio;
    memset(&sa_aio, 0, sizeof(sa_aio));
    sa_aio.sa_sigaction = handle_aio_sig;
    sa_aio.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR1, &sa_aio, NULL);

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

    // Переводим слушающий сокет в неблокирующий + сигнальный режим
    int flags = fcntl(listen_fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        close(listen_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }

#ifdef __sun
    if (fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl O_NONBLOCK");
        close(listen_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }
    if (ioctl(listen_fd, I_SETSIG, S_INPUT | S_RDNORM) == -1) {
        perror("ioctl I_SETSIG");
        close(listen_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }
#else
    if (fcntl(listen_fd, F_SETOWN, getpid()) == -1 ||
        fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK | O_ASYNC) == -1) {
        perror("fcntl O_ASYNC");
        close(listen_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }
#endif

    if (listen(listen_fd, 16) == -1) {
        perror("listen");
        close(listen_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }

    printf("Server (POSIX AIO + Signals) listening on %s...\n", SOCKET_PATH);
    fflush(stdout);

    // На случай, если клиент успел подключиться до входа в sigsuspend
    accept_new_clients(listen_fd);

    while (running) {
        sigsuspend(&old_mask);

        if (!running) {
            break;
        }
        if (got_accept_sig) {
            got_accept_sig = 0;
            accept_new_clients(listen_fd);
        }
        if (got_aio_sig) {
            got_aio_sig = 0;
            process_completed_aio();
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            close(clients[i].fd);
        }
    }
    close(listen_fd);
    unlink(SOCKET_PATH);
    return EXIT_SUCCESS;
}