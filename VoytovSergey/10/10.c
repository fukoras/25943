#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s command [arguments...]\n", argv[0]);
        return EXIT_FAILURE;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        // Дочерний процесс: команда
        execvp(argv[1], &argv[1]);

        // Выполняется только в случае ошибки execvp()
        perror("execvp");
        _exit(127);
    }

    // Родительский процесс: завершение дочернего
    int status;

    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return EXIT_FAILURE;
    }

    if (WIFEXITED(status)) {
        printf("Exit code: %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("Process terminated by signal: %d\n",
               WTERMSIG(status));   
    }

    return EXIT_SUCCESS;
}
