#include <locale.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#define MAX_CMD_LEN     512
#define MAX_LINE_LEN    1024
#define INITIAL_CAPACITY 256

#define CP_DIR     1
#define CP_EXEC    2
#define CP_FILTER  3

typedef struct {
    char **data;
    size_t size;
    size_t capacity;
} LineBuffer;

static WINDOW *win_list, *win_input;
static int term_height, term_width;
static char input_buf[MAX_CMD_LEN];
static char filter_buf[MAX_CMD_LEN];
static size_t offset = 0;

void init_buffer(LineBuffer *b) {
    b->size = 0;
    b->capacity = INITIAL_CAPACITY;
    b->data = malloc(b->capacity * sizeof(char*));
}

void free_buffer(LineBuffer *b) {
    for (size_t i = 0; i < b->size; ++i) free(b->data[i]);
    free(b->data);
    b->data = NULL;
    b->size = b->capacity = 0;
}

void add_line(LineBuffer *b, const char *line) {
    if (b->size >= b->capacity) {
        b->capacity *= 2;
        b->data = realloc(b->data, b->capacity * sizeof(char*));
    }
    b->data[b->size++] = strdup(line);
}

void cleanup(LineBuffer *b) {
    free_buffer(b);
    endwin();
}

void resize_handler(int sig) {
    (void)sig;
    endwin();
    refresh();
    clear();
    getmaxyx(stdscr, term_height, term_width);
    wresize(win_list, term_height - 3, term_width);
    wresize(win_input, 3, term_width);
    mvwin(win_input, term_height - 3, 0);
}

void init_ui() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    start_color();
    init_pair(CP_DIR,    COLOR_BLUE,   COLOR_BLACK);
    init_pair(CP_EXEC,   COLOR_GREEN,  COLOR_BLACK);
    init_pair(CP_FILTER, COLOR_YELLOW, COLOR_BLACK);
    getmaxyx(stdscr, term_height, term_width);
    win_list  = newwin(term_height - 3, term_width, 0, 0);
    win_input = newwin(3, term_width, term_height - 3, 0);
    box(win_list, 0, 0);
    box(win_input, 0, 0);
    signal(SIGWINCH, resize_handler);
    keypad(win_list, TRUE);
}

void show_help() {
    werase(win_list);
    box(win_list, 0, 0);
    const char *help_lines[] = {
        " Справка: ",
        " find>      : введите часть имени файла",
        " H          : показать эту справку",
        " /          : фильтр по подстроке (подсветка)",
        " n          : сбросить фильтр",
        " c          : новый поиск",
        " q          : выход",
        " ↑/↓        : прокрутка результатов",
        " Нажмите любую клавишу...",
        NULL
    };
    for (int i = 0; help_lines[i] != NULL; ++i) {
        mvwprintw(win_list, i + 1, 2, "%s", help_lines[i]);
    }
    wrefresh(win_list);
    wgetch(win_list);
}

int match_filter(const char *s) {
    if (!filter_buf[0]) return 1;
    return strstr(s, filter_buf) != NULL;
}

void draw_list(LineBuffer *b, size_t *p_shown, size_t *p_total_matching) {
    werase(win_list);
    box(win_list, 0, 0);
    size_t shown = 0;
    size_t total_matching = 0;
    int max_lines = term_height - 5;
    size_t skipped = 0;
    for (size_t i = 0; i < b->size; ++i) {
        if (match_filter(b->data[i])) {
            total_matching++;
            if (skipped < offset) {
                skipped++;
            } else if (shown < (size_t)max_lines) {
                const char *line = b->data[i];
                int y = shown + 1, x = 2;
                struct stat st;
                int color = 0;
                if (stat(line, &st) == 0) {
                    if (S_ISDIR(st.st_mode)) color = CP_DIR;
                    else if (st.st_mode & S_IXUSR) color = CP_EXEC;
                }
                const char *p = line;
                if (!filter_buf[0]) {
                    if (color) wattron(win_list, COLOR_PAIR(color));
                    mvwprintw(win_list, y, x, "%s", p);
                    if (color) wattroff(win_list, COLOR_PAIR(color));
                } else {
                    while (*p) {
                        char *f = strstr(p, filter_buf);
                        if (!f) {
                            if (color) wattron(win_list, COLOR_PAIR(color));
                            mvwprintw(win_list, y, x, "%s", p);
                            if (color) wattroff(win_list, COLOR_PAIR(color));
                            break;
                        }
                        int len = f - p;
                        if (len > 0) {
                            if (color) wattron(win_list, COLOR_PAIR(color));
                            mvwprintw(win_list, y, x, "%.*s", len, p);
                            if (color) wattroff(win_list, COLOR_PAIR(color));
                            x += len;
                        }
                        wattron(win_list, COLOR_PAIR(CP_FILTER));
                        mvwprintw(win_list, y, x, "%s", filter_buf);
                        wattroff(win_list, COLOR_PAIR(CP_FILTER));
                        x += strlen(filter_buf);
                        p = f + strlen(filter_buf);
                    }
                }
                shown++;
            }
        }
    }
    *p_shown = shown;
    *p_total_matching = total_matching;
    mvwprintw(win_list, term_height - 5, 2,
        "[%zu/%zu] /:filter n:clear H:help c:new q:quit ↑/↓:scroll (offset=%zu)",
        shown, total_matching, offset);
    wrefresh(win_list);
}

void prompt_input(const char *prompt, char *buf_in) {
    memset(buf_in, 0, MAX_CMD_LEN);
    int pos = 0;
    werase(win_input);
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "%s", prompt);
    wrefresh(win_input);
    int ch;
    while ((ch = wgetch(win_input)) != '\n') {
        if ((ch == KEY_BACKSPACE || ch == 127) && pos > 0)
            buf_in[--pos] = 0;
        else if (isprint(ch) && pos < MAX_CMD_LEN-1)
            buf_in[pos++] = ch;
        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 2, "%s%s", prompt, buf_in);
        wrefresh(win_input);
    }
}

void run_find(LineBuffer *b, const char *pattern) {
    char cmd[MAX_CMD_LEN + 64];
    snprintf(cmd, sizeof(cmd),
        "find / \\( -type f -o -type d \\) -iname \"*%s*\" 2>/dev/null",
        pattern);
    FILE *fp = popen(cmd, "r");
    free_buffer(b);
    init_buffer(b);
    if (!fp) {
        add_line(b, "Error: unable to run find");
    } else {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\n")] = 0;
            char *clean_line = malloc(MAX_LINE_LEN);
            size_t j = 0;
            for (size_t i = 0; line[i]; i++) {
                if (isprint((unsigned char)line[i]) || line[i] == '/' || line[i] == '.') {
                    clean_line[j++] = line[i];
                }
            }
            clean_line[j] = 0;
            if (j > 0) {
                add_line(b, clean_line);
            }
            free(clean_line);
        }
        pclose(fp);
    }
    offset = 0;
}

int main() {
    LineBuffer buf;

    setlocale(LC_ALL, "ru_RU.UTF-8");
    if (!initscr()) {
        fprintf(stderr, "Error initializing ncurses\n");
        return 1;
    }
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    init_ui();
    init_buffer(&buf);
    show_help();

    while (1) {
        prompt_input("find> ", input_buf);
        if (strcmp(input_buf, "q") == 0) break;

        run_find(&buf, input_buf);
        memset(filter_buf, 0, sizeof(filter_buf));
        offset = 0;

        while (1) {
            size_t shown, total_matching;
            draw_list(&buf, &shown, &total_matching);
            int ch = wgetch(win_list);
            wrefresh(win_input);

            if (ch == 'h' || ch == 'H') {
                show_help();
            } else if (ch == '/') {
                prompt_input("filter> ", filter_buf);
            } else if (ch == 'n') {
                memset(filter_buf, 0, sizeof(filter_buf));
            } else if (ch == 'c') {
                break;
            } else if (ch == 'q') {
                cleanup(&buf);
                return 0;
            } else if (ch == KEY_DOWN) {
                if (offset + shown < total_matching) {
                    offset++;
                }
            } else if (ch == KEY_UP) {
                if (offset > 0) {
                    offset--;
                }
            }
        }
    }

    cleanup(&buf);
    return 0;
}
