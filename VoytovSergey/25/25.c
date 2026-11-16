#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* Собственный динамический буфер только для однобайтовых символов */
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

/* Возвращает: 1 — символ добавлен, 0 — символ отклонён, -1 — ошибка памяти */
static int sb_push(SingleByteBuffer *buf, unsigned char byte) {
    /* Отсекаем любые байты многобайтовых кодировок (UTF-8 и т.п., >= 0x80) */
    if (byte >= 0x80) {
        return 0;
    }
    /* Разрешаем только однобайтовые печатные ASCII-символы и перевод строки */
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

/* Поглощает escape-последовательности (стрелки, Home/End, F1-F12), чтобы они не работали */
static void drain_escape_sequence(int fd, const struct termios *base_tio) {
    struct termios timeout_tio = *base_tio;
    unsigned char b;

    timeout_tio.c_cc[VMIN] = 0;
    timeout_tio.c_cc[VTIME] = 1; /* 100 мс на приход хвоста последовательности */
    tcsetattr(fd, TCSANOW, &timeout_tio);

    if (read(fd, &b, 1) == 1) {
        if (b == '[') {
            /* CSI-последовательность: читаем до финального байта 0x40..0x7E */
            while (read(fd, &b, 1) == 1) {
                if (b >= 0x40 && b <= 0x7E) {
                    break;
                }
            }
        } else if (b == 'O') {
            /* SS3-последовательность (например, \033OA..\033OD): пропускаем 1 байт */
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
            /* Игнорируем стрелки и любые Esc-последовательности */
            drain_escape_sequence(STDIN_FILENO, &raw_tio);
            continue;
        }
        if (ch == 0x04) {
            /* Ctrl+D — завершение ввода */
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
            /* Backspace */
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
    int pipefd[2];
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    if (pipe(pipefd) < 0) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        /* Дочерний процесс: читает из канала, переводит в верхний регистр, выводит в терминал */
        char chunk[256];
        ssize_t n;

        close(pipefd[1]);

        while ((n = read(pipefd[0], chunk, sizeof(chunk))) > 0) {
            for (ssize_t i = 0; i < n; i++) {
                chunk[i] = (char)toupper((unsigned char)chunk[i]);
            }
            if (write_all(STDOUT_FILENO, chunk, (size_t)n) < 0) {
                perror("write stdout");
                close(pipefd[0]);
                _exit(EXIT_FAILURE);
            }
        }

        if (n < 0) {
            perror("read pipe");
            close(pipefd[0]);
            _exit(EXIT_FAILURE);
        }

        close(pipefd[0]);
        _exit(EXIT_SUCCESS);
    }

    /* Родительский процесс: считывает ввод в однобайтовый буфер и пишет в канал */
    close(pipefd[0]);

    SingleByteBuffer input;
    if (sb_init(&input) < 0) {
        perror("malloc");
        close(pipefd[1]);
        waitpid(pid, NULL, 0);
        return EXIT_FAILURE;
    }

    const char *prompt = "Введите текст: ";
    (void)write_all(STDOUT_FILENO, prompt, strlen(prompt));

    if (read_single_byte_line(&input) < 0) {
        perror("read input");
        sb_free(&input);
        close(pipefd[1]);
        waitpid(pid, NULL, 0);
        return EXIT_FAILURE;
    }

    if (write_all(pipefd[1], input.data, input.len) < 0) {
        perror("write pipe");
    }

    sb_free(&input);
    close(pipefd[1]); /* Закрытие конца записи передаёт EOF дочернему процессу */

    int status = 0;
    waitpid(pid, &status, 0);
    return EXIT_SUCCESS;
}