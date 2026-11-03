#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <ctype.h>
#include <poll.h>

#define MAX_LINE 40
#define CTRL_W 0x17                 // CTRL-W
#define CTRL_D 0x04                 // CTRL-D
#define CTRL_G 0x07                 // CTRL-G
#define DEL_KEY 0x7F                // Esc и последовательности
#define ESC_KEY  0x1B               // Backspace

static struct termios orig_termios;
static int tty_fd = -1;

void restore_terminal(void) {
    if (tty_fd != -1) {
        tcsetattr(tty_fd, TCSAFLUSH, &orig_termios);
    }
}

void setup_terminal(void) {
    tty_fd = open("/dev/tty", O_RDWR);
    if (tty_fd == -1) {
        perror("Не удалось открыть /dev/tty");
        exit(EXIT_FAILURE);
    }

    if (tcgetattr(tty_fd, &orig_termios) == -1) {
        perror("Ошибка tcgetattr");
        exit(EXIT_FAILURE);
    }

    atexit(restore_terminal);

    struct termios raw = orig_termios;

    raw.c_lflag &= ~(ICANON | ECHO | IEXTEN); 
    /* 
    ICANON - не ждём ENTER, 
    ECHO - автоматическое отображение введённых символов, 
    IEXTEN - расширенная обработка ввода (не перехватывается CTRL + W).
    */

    raw.c_cc[VMIN] = 1;                       // 1 символ после нажатия
    raw.c_cc[VTIME] = 0;                      // без таймаута ожидания

    if (tcsetattr(tty_fd, TCSAFLUSH, &raw) == -1) {
        perror("Ошибка tcsetattr");
        exit(EXIT_FAILURE);
    }
}

// визуальное стираение 1 символа с экрана
void erase_one_char(void) {
      /*
      '\b' - 1 влево
      ' ' - старый символ затирается пробелом,
      '\b' - возвращение курсора на очищенную позицию.
      */
    write(tty_fd, "\b \b", 3);
}

// издать звуковой сигнал (Вывод символа CTRL+G)
void beep_terminal(void) {
    char bell = CTRL_G;
    write(tty_fd, &bell, 1);
}

/* 
 * Блокироватор Escape-последовательностей (стрелочки, F1-F12, Delete, Home, End).
 * Вычитывает и отбрасывает все байты, которые пришли в буфер вслед за ESC.
 */
void consume_escape_sequence(void) {
    struct pollfd pfd;
    pfd.fd = tty_fd;
    pfd.events = POLLIN;

    unsigned char dummy;
    /* Пока в течение 20 мс в буфере есть новые байты от этой же клавиши — съедаем их */
    while (poll(&pfd, 1, 20) > 0) {
        if (read(tty_fd, &dummy, 1) <= 0) {
            break;
        }
    }
}

int main(void) {
    setup_terminal();

    char line[MAX_LINE];
    int len = 0; // Текущее количество символов в строке (позиция курсора) 
    unsigned char ch;

    // Cимволы ERASE и KILL берем из исходных настроек терминала пользователя 
    unsigned char erase_char = orig_termios.c_cc[VERASE];
    unsigned char kill_char  = orig_termios.c_cc[VKILL];
    unsigned char werase_char = orig_termios.c_cc[VWERASE];
    unsigned char eof_char   = orig_termios.c_cc[VEOF];

    while (read(tty_fd, &ch, 1) == 1) {
        /* 
         * Перехват клавиши ESC, стрелочек и других спецклавиш (F1-F12, Home, End, Del).
         * Проглатываем весь хвост последовательности (например, "[A") и пищим 1 раз.
         */
        if (ch == ESC_KEY) {
            consume_escape_sequence();
            beep_terminal();
            continue;
        }

        // Правило 5: Завершение по CTRL-D в начале строки 
        if (ch == CTRL_D || ch == eof_char) {
            if (len == 0) {
                write(tty_fd, "\n", 1);
                break;
            } else {
                // Если курсор НЕ в начале строки, CTRL-D не срабатывает и пищит 
                beep_terminal();
                continue;
            }
        }

        // Правило 2: Стирание последнего символа (ERASE) 
        if (ch == erase_char || ch == '\b' || ch == DEL_KEY) {
            if (len > 0) {
                len--;
                erase_one_char();
            }
            continue;
        }

        // Правило 3: Стирание всей текущей строки (KILL, Ctrl-U) 
        if (ch == kill_char) {
            while (len > 0) {
                len--;
                erase_one_char();
            }
            continue;
        }

        // Правило 4: Стирание последнего слова и следующих за ним пробелов (CTRL-W) 
        if (ch == CTRL_W || ch == werase_char) {
            // Шаг 1: стираем все пробелы в конце строки (следующие за последним словом) 
            while (len > 0 && line[len - 1] == ' ') {
                len--;
                erase_one_char();
            }
            // Шаг 2: стираем последнее слово (все непробельные символы до пробела или начала) 
            while (len > 0 && line[len - 1] != ' ') {
                len--;
                erase_one_char();
            }
            continue;
        }

        // Перевод строки (Enter) сбрасывает текущую строку и переводит курсор 
        if (ch == '\n') {
            write(tty_fd, "\n", 1);
            len = 0;
            continue;
        }

        // Правила 1 и 7: Обработка печатаемых символов и перенос слов на 40-м столбце 
        if (isprint(ch)) {
            if (len < MAX_LINE) {
                // В строке еще есть место — сразу выводим символ 
                line[len++] = (char)ch;
                write(tty_fd, &ch, 1);
            } else {
                /* 
                 * Длина достигла 40 символов (мы на границе 40-го столбца).
                 * Если вводится пробел — просто переходим на новую строку и пишем его.
                 */
                if (ch == ' ') {
                    write(tty_fd, "\n", 1);
                    line[0] = ' ';
                    len = 1;
                    write(tty_fd, &ch, 1);
                } else {
                    /* 
                     * Вводится буква/символ слова, которое пересекает 40-й столбец.
                     * Ищем начало этого слова с конца текущей строки.
                     */
                    int word_len = 0;
                    while (word_len < MAX_LINE && line[MAX_LINE - 1 - word_len] != ' ') {
                        word_len++;
                    }

                    if (word_len > 0 && word_len < MAX_LINE) {
                        /* 
                         * Слово началось после пробела и пересекло 40-й столбец:
                         * 1. Стираем уже напечатанную часть этого слова с текущей строки.
                         * 2. Переводим строку.
                         * 3. Переносим буфер слова в начало новой строки и дописываем новый символ.
                         */
                        for (int i = 0; i < word_len; i++) {
                            erase_one_char();
                        }
                        write(tty_fd, "\n", 1);

                        memmove(line, &line[MAX_LINE - word_len], (size_t)word_len);
                        line[word_len] = (char)ch;
                        len = word_len + 1;

                        write(tty_fd, line, (size_t)len);
                    } else {
                        /* 
                         * Либо последний символ строки был пробелом (word_len == 0),
                         * либо вся строка из 40 символов — одно сплошное слово без пробелов.
                         * В обоих случаях просто переходим на новую строку и печатаем новый символ.
                         */
                        write(tty_fd, "\n", 1);
                        line[0] = (char)ch;
                        len = 1;
                        write(tty_fd, &ch, 1);
                    }
                }
            }
            continue;
        }

        // Правило 6: Все остальные непечатаемые символы издают звуковой сигнал CTRL-G 
        beep_terminal();
    }

    return 0;
}