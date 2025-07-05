#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define MAX_LINES 1000
#define MAX_LINE_LEN 1000

struct {
    char lines[MAX_LINES][MAX_LINE_LEN];
    int line_count;
    int cursor_x, cursor_y;
    int offset_x, offset_y;
    int rows, cols;
    char filename[256];
    struct termios orig_termios;
} editor;

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &editor.orig_termios);
    write(STDOUT_FILENO, "\x1b[?25h", 6); // Mostrar cursor al salir
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &editor.orig_termios);
    atexit(disable_raw_mode);
    
    struct termios raw = editor.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    write(STDOUT_FILENO, "\x1b[?25l", 6); // Ocultar cursor
}

void get_window_size() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        editor.rows = 24;
        editor.cols = 80;
    } else {
        editor.rows = ws.ws_row;
        editor.cols = ws.ws_col;
    }
}

void clear_screen() {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void refresh_screen() {
    clear_screen();
    
    for (int i = 0; i < editor.rows; i++) {
        int line_idx = i + editor.offset_y;
        if (line_idx >= editor.line_count) {
            write(STDOUT_FILENO, "~\r\n", 3); // Indicador de línea vacía
            continue;
        }
        
        char *line = editor.lines[line_idx];
        int len = strlen(line);
        int start = editor.offset_x;
        int print_len = len - start;
        if (print_len < 0) print_len = 0;
        if (print_len > editor.cols) print_len = editor.cols;
        
        int cursor_here = (line_idx == editor.cursor_y);
        int cursor_x_in_screen = editor.cursor_x - editor.offset_x;
        int cursor_visible = cursor_here && (editor.cursor_x >= start) && (cursor_x_in_screen < editor.cols);
        
        if (cursor_visible) {
            // Dividir la línea en tres partes: antes, cursor, después
            int before = editor.cursor_x - start;
            int after = print_len - before;
            
            // Parte antes del cursor
            if (before > 0) {
                write(STDOUT_FILENO, line + start, before);
            }
            
            // Carácter en posición del cursor (invertido)
            write(STDOUT_FILENO, "\x1b[7m", 4); // Iniciar inversión
            if (editor.cursor_x < len) {
                write(STDOUT_FILENO, line + editor.cursor_x, 1);
            } else {
                write(STDOUT_FILENO, " ", 1);
            }
            write(STDOUT_FILENO, "\x1b[0m", 4); // Terminar inversión
            
            // Parte después del cursor
            if (after > 1) {
                write(STDOUT_FILENO, line + editor.cursor_x + 1, after - 1);
            }
        } else {
            // Imprimir línea normalmente
            if (print_len > 0) {
                write(STDOUT_FILENO, line + start, print_len);
            }
        }
        
        // Limpiar el resto de la línea
        write(STDOUT_FILENO, "\x1b[K", 3);
        write(STDOUT_FILENO, "\r\n", 2);
    }
    
    // Posicionar cursor
    char buf[32];
    int screen_y = editor.cursor_y - editor.offset_y + 1;
    int screen_x = editor.cursor_x - editor.offset_x + 1;
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", screen_y, screen_x);
    write(STDOUT_FILENO, buf, strlen(buf));
}

void load_file(char *filename) {
    strcpy(editor.filename, filename);
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        editor.line_count = 1;
        editor.lines[0][0] = '\0';
        return;
    }
    
    editor.line_count = 0;
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp) && editor.line_count < MAX_LINES) {
        int len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        strcpy(editor.lines[editor.line_count], line);
        editor.line_count++;
    }
    
    fclose(fp);
    if (editor.line_count == 0) {
        editor.line_count = 1;
        editor.lines[0][0] = '\0';
    }
}

void save_file() {
    FILE *fp = fopen(editor.filename, "w");
    if (!fp) {
        const char *msg = "\x1b[31mError guardando\x1b[0m";
        write(STDOUT_FILENO, msg, strlen(msg));
        usleep(500000);
        return;
    }
    
    for (int i = 0; i < editor.line_count; i++) {
        fprintf(fp, "%s\n", editor.lines[i]);
    }
    fclose(fp);
}

void adjust_scroll() {
    if (editor.cursor_y < editor.offset_y) {
        editor.offset_y = editor.cursor_y;
    }
    if (editor.cursor_y >= editor.offset_y + editor.rows) {
        editor.offset_y = editor.cursor_y - editor.rows + 1;
    }
    if (editor.cursor_x < editor.offset_x) {
        editor.offset_x = editor.cursor_x;
    }
    if (editor.cursor_x >= editor.offset_x + editor.cols) {
        editor.offset_x = editor.cursor_x - editor.cols + 1;
    }
}

void insert_char(char c) {
    if (editor.cursor_y >= editor.line_count) {
        if (editor.line_count < MAX_LINES) {
            editor.line_count = editor.cursor_y + 1;
            editor.lines[editor.cursor_y][0] = '\0';
        } else return;
    }
    
    char *line = editor.lines[editor.cursor_y];
    int len = strlen(line);
    if (len >= MAX_LINE_LEN - 1) return;
    
    if (editor.cursor_x <= len) {
        memmove(line + editor.cursor_x + 1, line + editor.cursor_x, len - editor.cursor_x + 1);
        line[editor.cursor_x] = c;
        editor.cursor_x++;
    }
}

void delete_char() {
    if (editor.cursor_x > 0) {
        char *line = editor.lines[editor.cursor_y];
        int len = strlen(line);
        memmove(line + editor.cursor_x - 1, line + editor.cursor_x, len - editor.cursor_x + 1);
        editor.cursor_x--;
    } else if (editor.cursor_y > 0) {
        int prev_len = strlen(editor.lines[editor.cursor_y - 1]);
        int curr_len = strlen(editor.lines[editor.cursor_y]);
        
        if (prev_len + curr_len < MAX_LINE_LEN - 1) {
            strcat(editor.lines[editor.cursor_y - 1], editor.lines[editor.cursor_y]);
            for (int i = editor.cursor_y; i < editor.line_count - 1; i++) {
                strcpy(editor.lines[i], editor.lines[i + 1]);
            }
            editor.line_count--;
            editor.cursor_y--;
            editor.cursor_x = prev_len;
        }
    }
}

void new_line() {
    if (editor.line_count >= MAX_LINES) return;
    
    char *line = editor.lines[editor.cursor_y];
    int len = strlen(line);
    
    for (int i = editor.line_count; i > editor.cursor_y; i--) {
        strcpy(editor.lines[i], editor.lines[i - 1]);
    }
    
    if (editor.cursor_x < len) {
        strcpy(editor.lines[editor.cursor_y + 1], line + editor.cursor_x);
        line[editor.cursor_x] = '\0';
    } else {
        editor.lines[editor.cursor_y + 1][0] = '\0';
    }
    
    editor.line_count++;
    editor.cursor_y++;
    editor.cursor_x = 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s <archivo>\n", argv[0]);
        return 1;
    }
    
    enable_raw_mode();
    get_window_size();
    load_file(argv[1]);
    
    editor.cursor_x = 0;
    editor.cursor_y = 0;
    editor.offset_x = 0;
    editor.offset_y = 0;
    
    while (1) {
        adjust_scroll();
        refresh_screen();
        
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            if (c == 17) break;          // Ctrl+Q
            else if (c == 19) save_file(); // Ctrl+S
            else if (c == 27) {           // Escape
                char seq[2];
                if (read(STDIN_FILENO, &seq[0], 1) != 1) continue;
                if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;
                
                if (seq[0] == '[') {
                    int current_line_len = strlen(editor.lines[editor.cursor_y]);
                    switch (seq[1]) {
                        case 'A': // Up
                            if (editor.cursor_y > 0) {
                                editor.cursor_y--;
                                if (editor.cursor_x > strlen(editor.lines[editor.cursor_y]))
                                    editor.cursor_x = strlen(editor.lines[editor.cursor_y]);
                            }
                            break;
                        case 'B': // Down
                            if (editor.cursor_y < editor.line_count - 1) {
                                editor.cursor_y++;
                                if (editor.cursor_x > strlen(editor.lines[editor.cursor_y]))
                                    editor.cursor_x = strlen(editor.lines[editor.cursor_y]);
                            }
                            break;
                        case 'C': // Right
                            if (editor.cursor_x < current_line_len) editor.cursor_x++;
                            break;
                        case 'D': // Left
                            if (editor.cursor_x > 0) editor.cursor_x--;
                            break;
                    }
                }
            }
            else if (c == 127) delete_char(); // Backspace
            else if (c == 13) new_line();     // Enter
            else if (c >= 32 && c <= 126) insert_char(c); // Caracteres normales
        }
    }
    
    clear_screen();
    return 0;
}