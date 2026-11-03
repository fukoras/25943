#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#define CTRL_G "\x07" // Символ звукового сигнала (BEL / '\a', код 7)

/*
 * volatile запрещает компилятору кэшировать переменную в регистрах процессора.
 * sig_atomic_t гарантирует, что чтение и запись происходят атомарно.
 */
static volatile sig_atomic_t sigint_count = 0;
static volatile sig_atomic_t got_sigquit  = 0;

// Обработчик сигнала SIGINT (по умолчанию Ctrl-C) 
void handle_sigint(int sig) {
    (void)sig; // Подавляем предупреждение компилятора о неиспользуемом параметре 
    
    /* 
     * Системный вызов write() входит в список async-signal-safe функций POSIX,
     * поэтому безопасно вызываем прямо внутри обработчика сигнала.
     */
    write(STDOUT_FILENO, CTRL_G, 1);
    sigint_count++;
}

// Обработчик сигнала SIGQUIT (по умолчанию Ctrl-\) 
void handle_sigquit(int sig) {
    (void)sig;
    got_sigquit = 1;
}

int main(void) {
    struct sigaction sa_int, sa_quit;

    // Настраиваем обработчик для SIGINT 
    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("Ошибка sigaction (SIGINT)");
        return EXIT_FAILURE;
    }

    // Настраиваем обработчик для SIGQUIT 
    sa_quit.sa_handler = handle_sigquit;
    sigemptyset(&sa_quit.sa_mask);
    sa_quit.sa_flags = 0;
    if (sigaction(SIGQUIT, &sa_quit, NULL) == -1) {
        perror("Ошибка sigaction (SIGQUIT)");
        return EXIT_FAILURE;
    }

    printf("Программа запущена (PID: %d).\n", (int)getpid());
    printf("Нажимайте Ctrl-C для звукового сигнала, Ctrl-\\ для вывода результата и выхода.\n");

    // Бесконечный цикл ожидания сигналов 
    for (;;) {
        /* 
         * pause() переводит процесс в режим сна (S) до прихода любого сигнала,
         * чтобы бесконечный цикл не грузил ядро процессора на 100%.
         */
        pause();

        if (got_sigquit) {
            printf("\nПолучен сигнал SIGQUIT. Звуковой сигнал прозвучал %d раз(а).\n",
                   (int)sigint_count);
            break;
        }
    }

    return EXIT_SUCCESS;
}