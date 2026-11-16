#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

#define SOCKET_PATH "/tmp/task30_unix.sock"

/* --- Собственный динамический буфер только для однобайтовых символов --- */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} SingleByteBuffer;

static struct termios g_orig_tio;
static volatile sig_atomic_t g_tio_saved = 0;

static void restore_terminal(void) {
    if (g_tio_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_tio);
        g_tio_saved = 0;
    }
}

static void handle_sigint(int sig) {
    (void)sig;
    restore_terminal();
    _exit(130);
}

static int sb_init(SingleByteBuffer *buf) {
    buf->cap = 64;
    buf->len = 0;
    buf->data = (char *)malloc(buf->cap);
    if (!buf->data) {
        return -1;
    }
    buf->data[0] = '\0';
    return 0;
}

static void sb_free(SingleByteBuffer *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static int sb_push(SingleByteBuffer *buf, unsigned char byte) {
    if (byte >= 0x80) {
        return 0;
    }
    if (byte != '\n' && (byte < 0x20 || byte > 0x7E)) {
        return 0;
    }

    if (buf->len + 2 > buf->cap) {
        size_t new_cap = buf->cap * 2;
        char *tmp = (char *)realloc(buf->data, new_cap);
        if (!tmp) {
            return -1;
        }
        buf->data = tmp;
        buf->cap = new_cap;
    }

    buf->data[buf->len++] = (char)byte;
    buf->data[buf->len] = '\0';
    return 1;
}

static int sb_pop(SingleByteBuffer *buf) {
    if (buf->len == 0) {
        return 0;
    }
    buf->len--;
    buf->data[buf->len] = '\0';
    return 1;
}

static void drain_escape_sequence(int fd, const struct termios *base_tio) {
    struct termios timeout_tio = *base_tio;
    unsigned char b;

    timeout_tio.c_cc[VMIN] = 0;
    timeout_tio.c_cc[VTIME] = 1;
    tcsetattr(fd, TCSANOW, &timeout_tio);

    if (read(fd, &b, 1) == 1) {
        if (b == '[') {
            while (read(fd, &b, 1) == 1) {
                if (b >= 0x40 && b <= 0x7E) {
                    break;
                }
            }
        } else if (b == 'O') {
            (void)read(fd, &b, 1);
        }
    }

    tcsetattr(fd, TCSANOW, base_tio);
}

static int read_single_byte_line(SingleByteBuffer *buf) {
    unsigned char ch;
    ssize_t n;

    if (!isatty(STDIN_FILENO)) {
        while ((n = read(STDIN_FILENO, &ch, 1)) > 0) {
            int res = sb_push(buf, ch);
            if (res < 0) return -1;
            if (ch == '\n') break;
        }
        return (n < 0) ? -1 : 0;
    }

    if (tcgetattr(STDIN_FILENO, &g_orig_tio) < 0) {
        return -1;
    }
    g_tio_saved = 1;

    struct termios raw_tio = g_orig_tio;
    raw_tio.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
    raw_tio.c_cc[VMIN] = 1;
    raw_tio.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_tio) < 0) {
        g_tio_saved = 0;
        return -1;
    }

    while ((n = read(STDIN_FILENO, &ch, 1)) > 0) {
        if (ch == 0x1B) {
            drain_escape_sequence(STDIN_FILENO, &raw_tio);
            continue;
        }
        if (ch == 0x04) {
            break;
        }
        if (ch == '\r' || ch == '\n') {
            if (sb_push(buf, '\n') < 0) {
                restore_terminal();
                return -1;
            }
            (void)write(STDOUT_FILENO, "\n", 1);
            break;
        }
        if (ch == 0x7F || ch == 0x08) {
            if (sb_pop(buf)) {
                (void)write(STDOUT_FILENO, "\b \b", 3);
            }
            continue;
        }

        int pushed = sb_push(buf, ch);
        if (pushed < 0) {
            restore_terminal();
            return -1;
        }
        if (pushed == 1) {
            (void)write(STDOUT_FILENO, &ch, 1);
        }
    }

    restore_terminal();
    return (n < 0) ? -1 : 0;
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
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    SingleByteBuffer input;
    if (sb_init(&input) < 0) {
        perror("malloc");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    const char *prompt = "Введите текст для отправки серверу: ";
    (void)write_all(STDOUT_FILENO, prompt, strlen(prompt));

    if (read_single_byte_line(&input) < 0) {
        perror("read input");
        sb_free(&input);
        close(sock_fd);
        return EXIT_FAILURE;
    }

    if (write_all(sock_fd, input.data, input.len) < 0) {
        perror("write socket");
    }

    sb_free(&input);
    /* Разрываем соединение: сервер получит EOF (read == 0) и оба процесса завершатся */
    close(sock_fd);

    return EXIT_SUCCESS;
}