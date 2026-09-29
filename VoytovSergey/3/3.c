#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

static void print_ids(const char *when)
{
    printf("--- %s ---\n", when);
    printf("real UID      = %d\n", getuid());
    printf("effective UID = %d\n", geteuid());
}

static void try_open(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
    {
        perror("fopen");
        return;
    }
    printf("fopen(\"%s\") - succesful\n", path);
    if (fclose(f) != 0)
    {
        perror("fclose");
    }
}

int main(int argc, char *argv[])
{
    const char *path = (argc > 1) ? argv[1] : "data.txt";

    print_ids("to setuid");
    try_open(path);

    if (setuid(getuid()) != 0){
        perror("setuid");
        return 1;
    }

    print_ids("after seuid");
    try_open(path);

    return 0;
}
