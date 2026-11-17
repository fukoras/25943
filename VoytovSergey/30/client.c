#ifdef __sun
#define __EXTENSIONS__
#else
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

#define SOCKET_PATH "/tmp/uds_30_s.voitov_upper.sock"
#define INPUT_BUF_SIZE 1024
#define FG_IDLE_DEMO_MS 4000 /* пауза без ввода в активном режиме (4 сек) */

/* Кастомный буфер: принимает исключительно однобайтовые символы */
typedef struct {
    char data[INPUT_BUF_SIZE];
    size_t len;
} SingleByteBuffer;

static struct termios orig_termios;
static bool termios_saved = false;
static bool raw_active = false;
static volatile sig_atomic_t got_sigcont = 0;

static bool is_foreground(void) {
    if (!isatty(STDIN_FILENO)) {
        return false;
    }
    pid_t fg_pgrp = tcgetpgrp(STDIN_FILENO);
    return (fg_pgrp != -1 && fg_pgrp == getpgrp());
}

static void restore_terminal(void) {
    if (termios_saved && raw_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        raw_active = false;
    }
}

static void handle_exit_signal(int sig) {
    (void)sig;
    restore_terminal();
    _exit(0);
}

static void handle_sigtstp(int sig);

static void handle_sigcont(int sig) {
    (void)sig;
    got_sigcont = 1;

    struct sigaction sa_tstp;
    memset(&sa_tstp, 0, sizeof(sa_tstp));
    sa_tstp.sa_handler = handle_sigtstp;
    sigaction(SIGTSTP, &sa_tstp, NULL);
}

static void handle_sigtstp(int sig) {
    (void)sig;
    restore_terminal();

    struct sigaction sa_dfl;
    memset(&sa_dfl, 0, sizeof(sa_dfl));
    sa_dfl.sa_handler = SIG_DFL;
    sigaction(SIGTSTP, &sa_dfl, NULL);

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTSTP);
    sigprocmask(SIG_UNBLOCK, &set, NULL);

    raise(SIGTSTP);
}

static void init_signals(void) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    struct sigaction sa_exit;
    memset(&sa_exit, 0, sizeof(sa_exit));
    sa_exit.sa_handler = handle_exit_signal;
    sigaction(SIGINT, &sa_exit, NULL);
    sigaction(SIGTERM, &sa_exit, NULL);

    struct sigaction sa_tstp;
    memset(&sa_tstp, 0, sizeof(sa_tstp));
    sa_tstp.sa_handler = handle_sigtstp;
    sigaction(SIGTSTP, &sa_tstp, NULL);

    struct sigaction sa_cont;
    memset(&sa_cont, 0, sizeof(sa_cont));
    sa_cont.sa_handler = handle_sigcont;
    sigaction(SIGCONT, &sa_cont, NULL);

    atexit(restore_terminal);
}

/* Включаем raw-режим только на переднем плане (после fg) */
static void sync_terminal_mode(void) {
    if (!isatty(STDIN_FILENO)) {
        return;
    }

    if (is_foreground()) {
        if (!termios_saved) {
            if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
                return;
            }
            termios_saved = true;
        }
        if (!raw_active) {
            struct termios raw = orig_termios;
            raw.c_lflag &= ~(ICANON | ECHO);
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;
            if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
                raw_active = true;
                const char *msg = "[client30 active: type text, wait 4s for demo, Ctrl+D to exit]\n";
                if (write(STDOUT_FILENO, msg, strlen(msg)) < 0) {
                    /* ignore */
                }
            }
        }
    } else {
        restore_terminal();
    }
}

/* Поглощаем хвост escape-последовательности (стрелки \033[A..D и др.) */
static void discard_escape_sequence(void) {
    if (!raw_active) {
        return;
    }

    struct termios tmp;
    if (tcgetattr(STDIN_FILENO, &tmp) == -1) {
        return;
    }

    struct termios nonblock_t = tmp;
    nonblock_t.c_cc[VMIN] = 0;
    nonblock_t.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSANOW, &nonblock_t);

    unsigned char seq;
    while (read(STDIN_FILENO, &seq, 1) == 1) {
        if ((seq >= 'A' && seq <= 'Z') || (seq >= 'a' && seq <= 'z') || seq == '~') {
            break;
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &tmp);
}

/* Добавление строго однобайтового символа (ASCII 0x20..0x7E или '\n') */
static bool buf_append_single_byte(SingleByteBuffer *buf, unsigned char ch) {
    if (buf->len >= INPUT_BUF_SIZE - 1) {
        return false;
    }
    bool is_printable_ascii = (ch >= 0x20 && ch <= 0x7E);
    bool is_newline = (ch == '\n');

    if (!is_printable_ascii && !is_newline) {
        return false;
    }

    buf->data[buf->len++] = (char)ch;
    buf->data[buf->len] = '\0';
    return true;
}

/* Генерация строки демо-режима через тот же однобайтовый буфер */
static void fill_demo_message(SingleByteBuffer *buf, int counter) {
    char raw_msg[128];
    snprintf(raw_msg, sizeof(raw_msg), "[client30 demo pid=%ld] auto message #%d\n",
             (long)getpid(), counter);

    buf->len = 0;
    buf->data[0] = '\0';
    for (size_t i = 0; raw_msg[i] != '\0'; i++) {
        buf_append_single_byte(buf, (unsigned char)raw_msg[i]);
    }
}

/* В фоне тихо ждём fg; на переднем плане читаем ввод или запускаем демо при простое */
static ssize_t read_or_demo_line(SingleByteBuffer *buf, int *demo_counter) {
    buf->len = 0;
    buf->data[0] = '\0';

    while (1) {
        sync_terminal_mode();

        /* Если клиент в фоне — не спамим в терминал, ждём перевода на передний план (fg) */
        if (!is_foreground()) {
            poll(NULL, 0, 200);
            got_sigcont = 0;
            continue;
        }

        struct pollfd pfd;
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        pfd.revents = 0;

        /* Если буфер пуст и пользователь ничего не вводит 4 сек — срабатывает демо-режим */
        int timeout = (buf->len == 0) ? FG_IDLE_DEMO_MS : -1;
        int ready = poll(&pfd, 1, timeout);

        if (ready < 0) {
            if (errno == EINTR) {
                got_sigcont = 0;
                continue;
            }
            return -1;
        }

        if (ready == 0 && buf->len == 0) {
            (*demo_counter)++;
            fill_demo_message(buf, *demo_counter);
            return (ssize_t)buf->len;
        }

        unsigned char ch = 0;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            return (buf->len > 0) ? (ssize_t)buf->len : 0;
        }

        /* Ctrl+D на пустой строке -> выход */
        if (ch == 0x04) {
            if (buf->len == 0) {
                return 0;
            }
            continue;
        }

        /* Escape-последовательность (стрелки и функциональные клавиши) -> игнорируем */
        if (ch == 0x1B) {
            discard_escape_sequence();
            continue;
        }

        /* Backspace */
        if (ch == 0x7F || ch == 0x08) {
            if (buf->len > 0) {
                buf->len--;
                buf->data[buf->len] = '\0';
                if (write(STDOUT_FILENO, "\b \b", 3) < 0) {
                    /* ignore */
                }
            }
            continue;
        }

        /* Enter */
        if (ch == '\r' || ch == '\n') {
            buf_append_single_byte(buf, '\n');
            if (write(STDOUT_FILENO, "\n", 1) < 0) {
                /* ignore */
            }
            return (ssize_t)buf->len;
        }

        /* Однобайтовый печатный символ */
        if (buf_append_single_byte(buf, ch)) {
            if (write(STDOUT_FILENO, &ch, 1) < 0) {
                /* ignore */
            }
        }
    }
}

static bool send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, data + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

int main(void) {
    init_signals();

    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    bool connected = false;
    for (int attempt = 0; attempt < 20; attempt++) {
        if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            connected = true;
            break;
        }
        if (errno != ENOENT && errno != ECONNREFUSED) {
            break;
        }
        poll(NULL, 0, 50);
    }

    if (!connected) {
        perror("connect");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    SingleByteBuffer buf;
    int demo_counter = 0;

    while (1) {
        ssize_t len = read_or_demo_line(&buf, &demo_counter);
        if (len <= 0) {
            break;
        }
        if (!send_all(sock_fd, buf.data, (size_t)len)) {
            break;
        }
    }

    restore_terminal();
    close(sock_fd);
    return EXIT_SUCCESS;
}