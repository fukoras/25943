#ifdef __sun
#define __EXTENSIONS__
#else
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

#define SOCKET_PATH "/tmp/uds_31_s.voitov_upper.sock"
#define INPUT_BUF_SIZE 1024

/* Кастомный буфер, принимающий исключительно однобайтовые символы */
typedef struct {
    char data[INPUT_BUF_SIZE];
    size_t len;
} SingleByteBuffer;

static struct termios orig_termios;
static bool termios_saved = false;

static void restore_terminal(void) {
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        termios_saved = false;
    }
}

static void handle_exit_signal(int sig) {
    (void)sig;
    restore_terminal();
    _exit(0);
}

static void setup_raw_terminal(void) {
    if (!isatty(STDIN_FILENO)) {
        return;
    }
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    termios_saved = true;
    atexit(restore_terminal);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_exit_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
}

static void discard_escape_sequence(void) {
    if (!isatty(STDIN_FILENO)) {
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

static ssize_t read_single_byte_line(SingleByteBuffer *buf) {
    buf->len = 0;
    buf->data[0] = '\0';
    bool is_tty = isatty(STDIN_FILENO);

    while (1) {
        unsigned char ch = 0;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n <= 0) {
            return (buf->len > 0) ? (ssize_t)buf->len : 0;
        }

        /* Ctrl+D на пустом буфере -> EOF */
        if (is_tty && ch == 0x04) {
            if (buf->len == 0) {
                return 0;
            }
            continue;
        }

        if (ch == 0x1B) {
            discard_escape_sequence();
            continue;
        }

        if (is_tty && (ch == 0x7F || ch == 0x08)) {
            if (buf->len > 0) {
                buf->len--;
                buf->data[buf->len] = '\0';
                if (write(STDOUT_FILENO, "\b \b", 3) < 0) {
                }
            }
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            buf_append_single_byte(buf, '\n');
            if (is_tty) {
                if (write(STDOUT_FILENO, "\n", 1) < 0) {
                }
            }
            return (ssize_t)buf->len;
        }

        if (buf_append_single_byte(buf, ch)) {
            if (is_tty) {
                if (write(STDOUT_FILENO, &ch, 1) < 0) {
                }
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
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    setup_raw_terminal();
    if (isatty(STDIN_FILENO)) {
        printf("Connected. Enter single-byte ASCII text (arrows & multi-byte chars disabled, Ctrl+D to exit):\n");
        fflush(stdout);
    }

    SingleByteBuffer buf;
    while (1) {
        ssize_t len = read_single_byte_line(&buf);
        if (len <= 0) {
            break;
        }
        if (!send_all(sock_fd, buf.data, (size_t)len)) {
            perror("write");
            break;
        }
    }

    restore_terminal();
    close(sock_fd);
    return EXIT_SUCCESS;
}