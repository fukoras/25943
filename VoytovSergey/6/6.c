#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#define READ_BLOCK 4096
#define TIMEOUT_SEC 5

struct line_info {
    off_t offset;
    off_t length;
};

static volatile sig_atomic_t timed_out = 0;

static void on_alarm(int sig)
{
    (void)sig;
    timed_out = 1;
}

static int install_alarm(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_alarm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    return sigaction(SIGALRM, &sa, NULL);
}

static void print_file(int fd)
{
    if (lseek(fd, 0, SEEK_SET) == (off_t)-1)
    {
        perror("FATAL ERROR lseek");
        return;
    }
    char buf[READ_BLOCK];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
        ssize_t off = 0;
        while (off < n)
        {
            ssize_t w = write(STDOUT_FILENO, buf + off, n - off);
            if (w < 0)
            {
                perror("ERROR write");
                return;
            }
            off += w;
        }
    }
    if (n < 0)
        perror("ERROR read");
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
    off_t line_start = 0, pos = 0;
    char buf[READ_BLOCK];
    ssize_t n;

    while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
        for (ssize_t i = 0; i < n; i++)
        {
            if (buf[i] == '\n')
            {
                off_t line_end = pos + i + 1;
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
        pos += n;
    }
    if (n < 0)
    {
        perror("FATAL ERROR of read");
        close(fd);
        free(table);
        return 1;
    }
    if (pos > line_start)
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
        table[count].length = pos - line_start;
        count++;
    }

    printf("Table of strings (at all: %zu):\n", count);
    for (size_t i = 0; i < count; i++)
        printf("  string %2zu: offset = %lld, length = %lld\n",
               i, (long long)table[i].offset, (long long)table[i].length);

    if (install_alarm() == -1)
    {
        perror("FATAL ERROR of sigaction");
        close(fd);
        free(table);
        return 1;
    }

    int timer_active = 1;
    char input[64];

    while (1)
    {
        printf("\nWrite the number of string (0 - exit): ");
        fflush(stdout);

        if (timer_active)
        {
            errno = 0;
            timed_out = 0;
            alarm(TIMEOUT_SEC);
        }

        if (!fgets(input, sizeof(input), stdin))
        {
            if (timer_active)
                alarm(0);

            if (timer_active && errno == EINTR && timed_out)
            {
                printf("\nTime is up! Printing whole file:\n");
                print_file(fd);
                printf("\n");
                break;
            }
            if (feof(stdin))
            {
                printf("\nEOF - exit\n");
                break;
            }
            perror("FATAL ERROR of fgets");
            break;
        }

        if (timer_active)
        {
            alarm(0);
            timer_active = 0;
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

        ssize_t got = read(fd, out, li.length);
        if (got < 0)
        {
            perror("ERROR of read");
            free(out);
            continue;
        }

        out[got] = '\0';
        char prefix[64];
        int plen = snprintf(prefix, sizeof(prefix), "String %ld: ", num);
        write(STDOUT_FILENO, prefix, plen);
        write(STDOUT_FILENO, out, got);
        if (got == 0 || out[got - 1] != '\n')
            write(STDOUT_FILENO, "\n", 1);

        free(out);
    }

    free(table);
    close(fd);
    return 0;
}