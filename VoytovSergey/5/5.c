#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

#define BUF_SIZE 1

struct line_info {
    off_t offset;
    off_t length;
};

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
        perror("FATAL ERROR of opening");
        return 1;
    }

    struct line_info *table = NULL;
    size_t count = 0;
    size_t capacity = 0;

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
                table = realloc(table, capacity * sizeof(*table));
                if (!table) 
                {
                    perror("FATAL ERROR of realloc");
                    close(fd); 
                    return 1;
                }
            }

            table[count].offset = line_start;
            table[count].length = length;
            count++;

            line_start = line_end;
        }
    }
    if (n == -1) 
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
            table = realloc(table, capacity * sizeof(*table));
            if (!table)
            {
                perror("FATAL ERROR of realloc"); 
                close(fd);
                return 1;
            }
        }
        table[count].offset = line_start;
        table[count].length = file_end - line_start;
        count++;
    }
    printf("Table of strings: (at all: %zu):\n", count);
    for (size_t i = 0; i < count; i++)
    {
        printf(" string %2zu: offset = %lld, length = %lld\n", i, 
            (long long)table[i].offset, (long long)table[i].length);
    }
    char input[64];
    while (1)
    {
        printf("\nWrite the number of string (0 - exit): ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin))
        {
            if (feof(stdin))
                printf("\nEOF - exit\n");
            else
                perror("FATAL ERROR fgets");
            break;
        }

        char *end;
        long num = strtol(input, &end, 10);

        if (end == input || (*end != '\n' && *end != '\0'))
        {
            printf("ERROR, you MUST write a NUMBER, enter an INT!\n");
            continue;
        }

        while (*end == ' ' || *end == '\t') end++;

        if (*end != '\n' && *end != '\0')
        {
            printf("ERROR, you MUST write a NUMBER, enter an INT!\n");
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

        if (lseek(fd, li.offset, SEEK_SET) == (off_t) - 1)
        {
            perror("lseek");
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
            perror("read");
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
