#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <libgen.h>

// Формирование строки из 10 символов: тип файла + 9 бит прав доступа
void format_mode(mode_t mode, char str[11]) {
    // 1. Тип файла: d — каталог, - — обычный файл, ? — все остальные 
    if (S_ISDIR(mode)) {
        str[0] = 'd';
    } else if (S_ISREG(mode)) {
        str[0] = '-';
    } else {
        str[0] = '?';
    }

    // 2. Права владельца (User) 
    str[1] = (mode & S_IRUSR) ? 'r' : '-';
    str[2] = (mode & S_IWUSR) ? 'w' : '-';
    str[3] = (mode & S_IXUSR) ? 'x' : '-';

    // 3. Права группы (Group) 
    str[4] = (mode & S_IRGRP) ? 'r' : '-';
    str[5] = (mode & S_IWGRP) ? 'w' : '-';
    str[6] = (mode & S_IXGRP) ? 'x' : '-';

    // 4. Права всех остальных (Others) 
    str[7] = (mode & S_IROTH) ? 'r' : '-';
    str[8] = (mode & S_IWOTH) ? 'w' : '-';
    str[9] = (mode & S_IXOTH) ? 'x' : '-';

    str[10] = '\0';
}

// Вывод информации об одном файле в виде строки таблицы 
int print_file_info(const char *filepath) {
    struct stat sb;

    /* 
     * Используем lstat вместо stat, чтобы символические ссылки не разыменовывались,
     * а распознавались как особый тип файла ('?' по условию задачи).
     */
    if (lstat(filepath, &sb) == -1) {
        perror(filepath);
        return -1;
    }

    // Строка типа файла и прав доступа 
    char mode_str[11];
    format_mode(sb.st_mode, mode_str);

    // 2. Имя собственника (UID) и группы (GID) 
    char user_buf[32];
    char group_buf[32];
    struct passwd *pw = getpwuid(sb.st_uid);
    struct group  *gr = getgrgid(sb.st_gid);

    const char *user_name  = (pw != NULL) ? pw->pw_name : NULL;
    const char *group_name = (gr != NULL) ? gr->gr_name : NULL;

    // Если в /etc/passwd или /etc/group нет имени для ID, выводим сам числовой ID 
    if (user_name == NULL) {
        snprintf(user_buf, sizeof(user_buf), "%u", (unsigned int)sb.st_uid);
        user_name = user_buf;
    }
    if (group_name == NULL) {
        snprintf(group_buf, sizeof(group_buf), "%u", (unsigned int)sb.st_gid);
        group_name = group_buf;
    }

    /* 
     * 3. Размер файла: выводим только для обычного файла.
     * Для каталогов и прочих файлов оставляем пустую строку "",
     * которая при выводе через %10s превратится в 10 ровных пробелов.
     */
    char size_str[32];
    if (S_ISREG(sb.st_mode)) {
        snprintf(size_str, sizeof(size_str), "%lld", (long long)sb.st_size);
    } else {
        size_str[0] = '\0';
    }

    // 4. Дата модификации (mtime) в фиксированном формате "Мес ДД ЧЧ:ММ" 
    char time_str[32];
    struct tm *tm_info = localtime(&sb.st_mtime);
    if (tm_info != NULL) {
        strftime(time_str, sizeof(time_str), "%b %d %H:%M", tm_info);
    } else {
        strncpy(time_str, "??? ?? ??:??", sizeof(time_str));
        time_str[sizeof(time_str) - 1] = '\0';
    }

    /* 
     * 5. Извлечение только имени файла без пути.
     * Функция basename() может модифицировать переданную строку,
     * поэтому работаем с копией пути.
     */
    char *path_copy = strdup(filepath);
    if (path_copy == NULL) {
        perror("strdup");
        return -1;
    }
    char *base_name = basename(path_copy);

    /* 
     * Табличный вывод с фиксированной шириной полей:
     * %-10s — права (10 символов)
     * %3lu  — кол-во жестких связей (ширина 3, выравнивание вправо)
     * %-8s  — владелец (ширина 8, выравнивание влево)
     * %-8s  — группа (ширина 8, выравнивание влево)
     * %10s  — размер или пустое поле (ширина 10, выравнивание вправо)
     * %-12s — дата модификации (12 символов)
     * %s    — короткое имя файла
     */
    printf("%-10s %3lu %-8s %-8s %10s %-12s %s\n",
           mode_str,
           (unsigned long)sb.st_nlink,
           user_name,
           group_name,
           size_str,
           time_str,
           base_name);

    free(path_copy);
    return 0;
}

int main(int argc, char *argv[]) {
    int exit_status = EXIT_SUCCESS;

    // Если аргументы не переданы, как и стандартная ls -ld, проверяем текущий каталог "." 
    if (argc == 1) {
        if (print_file_info(".") == -1) {
            exit_status = EXIT_FAILURE;
        }
        return exit_status;
    }

    // Обрабатываем каждый переданный аргумент по очереди 
    for (int i = 1; i < argc; i++) {
        if (print_file_info(argv[i]) == -1) {
            exit_status = EXIT_FAILURE;
        }
    }

    return exit_status;
}