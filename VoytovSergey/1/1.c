#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

typedef struct optrec   // Структура узла списка опций с 1 опцией; 2 аргументом для U C V, иначе 0; и 3 указатель на следующий узел 
{
    int opt;
    char *arg;
    struct optrec *next;
} optrec;

static optrec *head = NULL;
static optrec *tail = NULL;

static void push(int opt, char *arg) // Закидывает опцию в конец списка
{
    optrec *n = malloc(sizeof(*n));
    n->opt = opt;
    n->arg = arg ? strdup(arg) : NULL;
    n->next = NULL;
    if (tail) 
    {
        tail->next = n;
    } else {
        head = n;
    }
    tail = n;
}

static void do_i(void) //  Печатает реальные и эффективные идентификаторы пользователя и группы
{
    printf("real uid = %d, effective uid = %d\n", getuid(), geteuid()); 
    printf("real gid = %d, effective gid = %d\n", getgid(), getegid());
}

static void do_s(void) // Процесс становится лидером группы
{
    if (setpgid(0, 0) == -1)
    {
        perror("setpgid");
    } else 
    {
        printf("Process has become group leader, pgid = %d\n", getpgrp());
    }
}

static void do_p(void)  // Печатает идентификаторы процесса, процесса-родителя и группы процессов
{
    printf("pid = %d\n", getpid());
    printf("ppid = %d\n", getppid());
    printf("pgid = %d\n", getpgrp());
}

static void do_u(void)  // Печатает значение ulimit
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_FSIZE, &rl) == 0)
    {
        printf("ulimit (RLIMIT_FSIZE): soft = ");
        if (rl.rlim_cur == RLIM_INFINITY)
            printf("unlimited");
        else
            printf("%ld", (long)rl.rlim_cur);

        printf(", hard = ");
        if (rl.rlim_max == RLIM_INFINITY)
            printf("unlimited");
        else
            printf("%ld", (long)rl.rlim_max);
        printf("\n");
    }
    else
    {
        perror("getrlimit");
    }
}

static void do_U(const char *arg) // Изменяет ulimit (RLIMIT_FSIZE)
{
    if (!arg)
    {
        fprintf(stderr, "-U requires argument\n");
        return;
    }
    char *end;

    long v = strtol(arg, &end, 10);
    if (*end != '\0' || v < 0)
    {
        fprintf(stderr, "Invalid ulimit value: %s\n", arg);
        return;
    }
    struct rlimit rl;
    if (getrlimit(RLIMIT_FSIZE, &rl) == -1)
    {
        perror("getrlimit");
        return;
    } 
    rl.rlim_cur = (rlim_t)v;
    if (setrlimit(RLIMIT_FSIZE, &rl) == -1)
    {
        perror("setrlimit");
        return;
    }
    printf("ulimit set to %ld\n", v);
}

static void do_c(void) // Выводит размер core-файла в байтах
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_CORE, &rl) == 0)
    {
        printf("core file size limit: soft=");
        if (rl.rlim_cur == RLIM_INFINITY)
            printf("unlimited");
        else
            printf("%ld", (long)rl.rlim_cur);

        printf(", hard=");
        if (rl.rlim_max == RLIM_INFINITY)
            printf("unlimited");
        else
            printf("%ld", (long)rl.rlim_max);
        printf("\n");
    }
    else
    {
        perror("getrlimit");
    }
}

static void do_C(const char *arg) // Изменяет размер core-файла
{
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_CORE, &rl) == 0)
    {
        printf("core file size limit: soft=");
        if (rl.rlim_cur == RLIM_INFINITY)
            printf("unlimited");
        else
            printf("%ld", (long)rl.rlim_cur);

        printf(", hard=");
        if (rl.rlim_max == RLIM_INFINITY)
            printf("unlimited");
        else
            printf("%ld", (long)rl.rlim_max);
        printf("\n");
    }
    else
    {
        perror("getrlimit");
    }
}    if (!arg)
    {
        fprintf(stderr, "-C requires argument\n");
        return;
    }
    char *end;
    long v = strtol(arg, &end, 10);
    if (*end != '\0' || v < 0)
    {
        fprintf(stderr, "invalid core size: %s\n", arg);
        return;
    }
    struct rlimit rl;
    if (getrlimit(RLIMIT_CORE, &rl) == -1)
    {
        perror("getrlimit");
        return;
    }
    rl.rlim_cur = (rlim_t)v;
    if (setrlimit(RLIMIT_CORE, &rl) == -1)
    {
        perror("setrlimit");
        return;
    }
    else
    {
        printf("core file size set to %ld\n", v);
    }
}


static void do_d(void) // Печатает текущей рабочей директории
{
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf)) == NULL)
    {
        perror("getcwd");
    } 
    else
    {
        printf("cwd = %s\n", buf);
    }
}

static void do_v(void) // Печатает все переменные среды
{
    extern char **environ;
    for (char **p = environ; *p; ++p)
    {
        printf("%s\n", *p);
    }
}

static void do_V(const char *arg) // Изменить/добавить переменную среду
{
    if (!arg)
    {
        fprintf(stderr, "-V requires name = value\n");
        return;
    }
    if (putenv(strdup(arg)) != 0)
    {
        perror("putenv");
    }
    else
    {
        printf("environment updated: %s\n", arg);
    }
}

int main(int argc, char *argv[])
{
    int opt;
    const char *optstring = "ispuU:cC:dvV:";
    
    while ((opt = getopt(argc, argv, optstring)) != -1)
    {
        int needs_arg = (opt == 'U' || opt == 'C' || opt == 'V');

        push(opt, needs_arg ? optarg : NULL);
    }

    optrec *prev = NULL;
    optrec *cur = head;
    optrec *next;
    while(cur)
    {
        next = cur->next;
        cur->next = prev;
        prev = cur;
        cur = next;
    }
    head = prev;

    for (optrec *p = head; p; p = p->next)
    {
        switch(p->opt)
        {
            case 'i': do_i(); break; // UID/GID
            case 's': do_s(); break; // setpgid
            case 'p': do_p(); break; // PID, PPID, PGID
            case 'u': do_u(); break; // print ulimit
            case 'U': do_U(p->arg); break; // set ulimit
            case 'c': do_c(); break; // print core size
            case 'C': do_C(p->arg); break; // set core size
            case 'd': do_d(); break; // pwd
            case 'v': do_v(); break; // var struct
            case 'V': do_V(p->arg); break; // set var struct
            case '?': fprintf(stderr, "unknown option or missing argument\n"); break; // unknown op or no arg

            default:
                fprintf(stderr, "unhandled option: %c\n", p->opt); // code of the option
        }
    }

    return 0;
} 
