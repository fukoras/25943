#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

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

static void print_file(const char *base, off_t size)
{
    if (!base || size <= 0)
        return;

    off_t off = 0;
    while (off < size)
    {
        ssize_t w = write(STDOUT_FILENO, base + off, size - off);
        if (w < 0)
        {
            perror("ERROR write");
            return;
        }
        off += w;
    }
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

    struct stat st;
    if (fstat(fd, &st) == -1)
    {
        perror("FATAL ERROR of fstat");
        close(fd);
        return 1;
    }
    if (!S_ISREG(st.st_mode))
    {
        printf("FATAL ERROR: not a regular file\n");
        close(fd);
        return 1;
    }

    off_t file_size = st.st_size;

    const char *base = NULL;
    if (file_size > 0)
    {
        void *m = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (m == MAP_FAILED)
        {
            perror("FATAL ERROR of mmap");
            close(fd);
            return 1;
        }
        base = (const char *)m;
    }

    struct line_info *table = NULL;
    size_t count = 0, capacity = 0;
    off_t line_start = 0;

    for (off_t i = 0; i < file_size; i++)
    {
        if (base[i] == '\n')
        {
            off_t line_end = i + 1;
            off_t length = line_end - line_start;

            if (count == capacity)
            {
                capacity = capacity ? capacity * 2 : 16;
                struct line_info *tmp =
                    realloc(table, capacity * sizeof(*table));
                if (!tmp)
                {
                    perror("FATAL ERROR of realloc");
                    munmap((void *)base, file_size);
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

    if (file_size > line_start)
    {
        if (count == capacity)
        {
            capacity = capacity ? capacity * 2 : 16;
            struct line_info *tmp = realloc(table, capacity * sizeof(*table));
            if (!tmp)
            {
                perror("FATAL ERROR of realloc");
                munmap((void *)base, file_size);
                close(fd);
                free(table);
                return 1;
            }
            table = tmp;
        }
        table[count].offset = line_start;
        table[count].length = file_size - line_start;
        count++;
    }

    printf("Table of strings (at all: %zu):\n", count);
    for (size_t i = 0; i < count; i++)
        printf("  string %2zu: offset = %lld, length = %lld\n",
               i, (long long)table[i].offset, (long long)table[i].length);

    if (install_alarm() == -1)
    {
        perror("FATAL ERROR of sigaction");
        munmap((void *)base, file_size);
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
                print_file(base, file_size);
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

        char prefix[64];
        int plen = snprintf(prefix, sizeof(prefix), "String %ld: ", num);
        write(STDOUT_FILENO, prefix, plen);

        off_t off = 0;
        while (off < li.length)
        {
            ssize_t w = write(STDOUT_FILENO, base + li.offset + off,
                              li.length - off);
            if (w < 0)
            {
                perror("ERROR write");
                break;
            }
            off += w;
        }

        if (li.length == 0 || base[li.offset + li.length - 1] != '\n')
            write(STDOUT_FILENO, "\n", 1);
    }

    if (base)
        munmap((void *)base, file_size);

    free(table);
    close(fd);
    return 0;
}