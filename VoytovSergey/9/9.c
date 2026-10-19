#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {

    const char *filename = (argc > 1) ? argv[1] : "/etc/services";

    printf("[Parent, PID = %d] Open underprocess to read file: %s\n", getpid(), filename);
    fflush(stdout);

    pid_t pid = fork();

    if (pid < 0) {

        perror("Error fork");
        return EXIT_FAILURE;
    }
    else if (pid == 0) 
    {
        // Дочерний процесс
        execlp("cat", "cat", filename, NULL);

        // Ошибка запуском execlp cat
        perror("Error execlp");
        _exit(EXIT_FAILURE);
    }
    else {
        // Родительский процесс
       int status;

        // Ждем завершения конкретного дочернего процесса с идентификатором pid
        pid_t waited_pid = waitpid(pid, &status, 0);

        if (waited_pid == -1) {
            perror("Error waitpid");
            return EXIT_FAILURE;
        }

        // Эта строка гарантированно выведется ПОСЛЕ всего текста из cat
        if (WIFEXITED(status)) {
            printf("\n[Parent, PID=%d] Underprocess (PID=%d) ended with code %d. End.\n",
                   getpid(), pid, WEXITSTATUS(status));
        } else {
            printf("\n[Родитель, PID=%d] Underprocess (PID=%d) Has been interrupted.\n",
                   getpid(), pid);


        }
    }
    return EXIT_SUCCESS;
}