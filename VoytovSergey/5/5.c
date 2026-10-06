#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>

#define BUF_SIZE 1

struct line_info {
    off_t offset;
    off_t length;
};

static struct termios saved_termios;
static int termios_saved = 0;

static int set_canonical_terminal(void)
{
    struct termios t;

    if (tcgetattr(STDIN_FILENO, &t) == -1)
    {
        perror("FATAL ERROR of tcgetattr");
        return -1;
    }

    saved_termios = t;
    termios_saved = 1;

    t.c_lflag |= ICANON | ECHO | ISIG | IEXTEN | ECHOCTL;
    t.c_iflag |= ICRNL;
    t.c_iflag &= ~(IXON | IXOFF | IXANY);

#ifdef TCSETS
    if (tcsetattr(STDIN_FILENO, TCSETS, &t) == -1)
#else
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) == -1)
#endif
    {
        perror("FATAL ERROR of tcsetattr");
        return -1;
    }
    return 0;
}

static void restore_terminal(void)
{
    if (termios_saved)
    {
#ifdef TCSETS
        tcsetattr(STDIN_FILENO, TCSETS, &saved_termios);
#else
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
#endif
        termios_saved = 0;
    }
}

static void on_fatal_signal(int sig)
{
    (void)sig;
    restore_terminal();
    _exit(1);
}

static void install_fatal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_fatal_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
}

static ssize_t read_line(int fd, char *buf, size_t size)
{
    size_t i = 0;
    while (i + 1 < size)
    {
        unsigned char c;
        ssize_t n = read(fd, &c, 1);
        if (n == 0)
            break;
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (c == '\n')
        {
            buf[i++] = '\n';
            break;
        }

        if (c == 0x1B)
        {
            unsigned char next;
            ssize_t m = read(fd, &next, 1);
            if (m != 1)
                continue;
            if (next == '[' || next == 'O')
            {
                while (read(fd, &next, 1) == 1)
                {
                    if ((next >= 'A' && next <= 'Z') ||
                        (next >= 'a' && next <= 'z') ||
                        next == '~')
                        break;
                }
            }
            continue;
        }

        if (c == 0x7F || c == 0x08)
        {
            if (i > 0)
                i--;
            continue;
        }

        if (c >= 0x20 && c < 0x7F)
            buf[i++] = (char)c;
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Using: %s <file>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd == -1)
    {
        perror("FATAL ERROR opening");
        return 1;
    }

    struct line_info *table = NULL;
    size_t count = 0, capacity = 0;
    off_t line_start = 0;
    char buf[BUF_SIZE];
    ssize_t n;

    while ((n = read(fd, buf, BUF_SIZE)) > 0)
    {
        off_t pos = lseek(fd, 0L, SEEK_CUR);
        if (buf[0] == '\n')
        {
            off_t line_end = pos;
            off_t length = line_end - line_start;

            if (count == capacity)
            {
                capacity = capacity ? capacity * 2 : 16;
                struct line_info *tmp =
                    realloc(table, capacity * sizeof(*table));
                if (!tmp)
                {
                    perror("FATAL ERROR of realloc");
                    close(fd);
                    free(table);
                    return 1;
                }
                table = tmp;
            }
            table[count].offset = line_start;
            table[count].length = length;
            count++;
            line_start = line_end;
        }
    }
    if (n < 0)
    {
        perror("FATAL ERROR of read");
        close(fd);
        free(table);
        return 1;
    }

    off_t file_end = lseek(fd, 0L, SEEK_CUR);
    if (file_end > line_start)
    {
        if (count == capacity)
        {
            capacity = capacity ? capacity * 2 : 16;
            struct line_info *tmp = realloc(table, capacity * sizeof(*table));
            if (!tmp)
            {
                perror("FATAL ERROR of realloc");
                close(fd);
                free(table);
                return 1;
            }
            table = tmp;
        }
        table[count].offset = line_start;
        table[count].length = file_end - line_start;
        count++;
    }

    printf("Table of strings (at all: %zu):\n", count);
    for (size_t i = 0; i < count; i++)
        printf("  string %2zu: offset = %lld, length = %lld\n",
               i, (long long)table[i].offset, (long long)table[i].length);

    install_fatal_handlers();

    if (set_canonical_terminal() == -1)
    {
        close(fd);
        free(table);
        return 1;
    }

    char input[64];
    while (1)
    {
        printf("\nWrite the number of string (0 - exit): ");
        fflush(stdout);

        memset(input, 0, sizeof(input));

        ssize_t got = read_line(STDIN_FILENO, input, sizeof(input));
        if (got <= 0)
        {
            if (feof(stdin))
                printf("\nEOF - exit\n");
            else
                perror("FATAL ERROR of read_line");
            break;
        }

        char *end;
        long num = strtol(input, &end, 10);

        if (end == input)
        {
            printf("ERROR, enter an integer!\n");
            continue;
        }
        while (*end == ' ' || *end == '\t') end++;
        if (*end != '\n' && *end != '\0')
        {
            printf("ERROR, enter an integer!\n");
            continue;
        }

        if (num == 0) break;
        if (num < 1 || (size_t)num > count)
        {
            printf("There is no string like that. allowed: 1..%zu\n", count);
            continue;
        }

        size_t idx = (size_t)num - 1;
        struct line_info li = table[idx];

        if (lseek(fd, li.offset, SEEK_SET) == (off_t)-1)
        {
            perror("ERROR of lseek");
            continue;
        }

        char *out = malloc(li.length + 1);
        if (!out)
        {
            perror("ERROR of malloc");
            continue;
        }

        ssize_t r = read(fd, out, li.length);
        if (r < 0)
        {
            perror("ERROR of read");
            free(out);
            continue;
        }

        out[r] = '\0';
        char prefix[64];
        int plen = snprintf(prefix, sizeof(prefix), "String %ld: ", num);
        write(STDOUT_FILENO, prefix, plen);
        write(STDOUT_FILENO, out, r);
        if (r == 0 || out[r - 1] != '\n')
            write(STDOUT_FILENO, "\n", 1);

        free(out);
    }

    restore_terminal();
    free(table);
    close(fd);
    return 0;
}