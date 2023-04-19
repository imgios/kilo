#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

#define VERSION "0.0.1"
#define TAB_STOP 8
#define QUIT_TIMES 3 // # of times required to quit without saving
#define CTRL_KEY(k) ((k) & 0x1f)

// By setting the first const to 1000, the rest
// get incrementing values of 1001/1002/1003 and so on.
enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
};

// Data type to store a row of text in our editor
typedef struct erow {
    int size;
    int rsize; // render size
    char *render;
    char *chars;
} erow;

struct editorConfig {
    struct termios orig_termios;
    int screenrows;
    int screencols;
    int cx, cy; // cursor position
    int rx;
    int numrows;
    // row/col offset to keep track of what row the user is currently scrolled to
    int rowoff;
    int coloff;
    erow *row;
    // dirty will tell us if the file has been modified since opening or saving
    int dirty;
    char *filename;
    char statusmsg[80]; // Status message
    time_t statusmsg_time; // Timestamp when status message was set
};

struct editorConfig E;

struct abuf {
    char *b;
    int len;
};

// Represents an empty buffer and acts as constructor
#define ABUF_INIT {NULL, 0}

// Prototypes
void editorSetStatusMessage(const char *fmt, ...);

void abAppend(struct abuf *ab, const char *s, int len) {
    // Allocate enough memory to hold the previous string
    // plus the new one.
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) {
        return;
    }
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    // Release memory
    free(ab->b);
}

void die(const char *s) {
    // Clear terminal and reposition the cursor
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("disableRawMode::tcsetattr");
    }
}

void enableRawMode() {
    // Get terminal attribute and store in orig_termios
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        die("enableRawMode::tcgetattr");
    }
    // Call disableRawMode automatically whn the program exits
    atexit(disableRawMode);
    
    // terminal attributes copy before updating them
    struct termios raw = E.orig_termios;

    // BRKINT turned on lead to a SIGINT signal to be sent to the
    // program when there's a break condition.
    // IXON turn off CTRL-S and CTRL-Q signals.
    // ICRNL fixes the CTRL-M behavior.
    // INPCK enables parity check.
    // ISTRIP strip the 8th bit of each input byte.
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP |IXON);

    // OPOST disables all output processing features
    raw.c_oflag &= ~(OPOST);

    // Set the char size to 8 bits per byte
    raw.c_cflag |= (CS8);

    // ICANON flag allows us to turn off canonical mode
    // and read the input byte-by-byte instead of
    // line-by-line.
    // IEXTEN turns off CTRL-V.
    // ISIG turns off CTRL-C and CTRL-Z signals.
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    
    // VMIN sets the minimum number of bytes of input needed
    // before read() can return.
    // VTIME sets the maxium amount of time (tenths of a second)
    // to wait before read() returns.
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    // Set terminal attribute
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("enableRawMode::tcsetattr");
    }
}

int getCursorPosition(int *rows, int *cols) {
    char buffer[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }

    while (i < sizeof(buffer) - 1) {
        if (read(STDIN_FILENO, &buffer[i], 1) != 1) {
            break;
        }
        if (buffer[i] == 'R') {
            break;
        }
        i++;
    }

    buffer[i] = '\0'; // string end
    
    if (buffer[0] != '\x1b' || buffer[1] != '[') {
        return -1;
    }

    if (sscanf(&buffer[2], "%d;%d", rows, cols) != 2) {
        return -1;
    }

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize size;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == -1 || size.ws_col == 0) {
        // Move the cursor to the right (using C command)
        // and then down (using B command).
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return -1;
        }
        return getCursorPosition(rows, cols);
    } else {
        *cols = size.ws_col;
        *rows = size.ws_row;
        return 0;
    }
}

int editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    int j = 0;

    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t') {
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        }
        rx++;
    }

    return rx;
}

void editorUpdateRow(erow *row) {
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            tabs++;
        }
    }

    free(row->render);
    row->render = malloc(row->size + tabs*(TAB_STOP - 1) + 1);

    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % TAB_STOP != 0) {
                row->render[idx++] = ' ';
            }
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    // idx now contains the # of chars we copied into row->render
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
    E.dirty++;
}

void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) {
        return;
    }
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    E.numrows--;
    E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    // Append a string to the end of the row
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size) {
        at = row->size;
    }
    // Make room for the new char + null byte
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
    if (at < 0 || at >= row->size) {
        return;
    }
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

void editorInsertChar(int c) {
    // Check if the cursor is on the tilde line
    if (E.cy == E.numrows) {
        editorAppendRow("", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorDelChar() {
    // Cursor past the end of the file
    if (E.cy == E.numrows) {
        return;
    }
    // Cursor at the beginning of the first line
    if (E.cx == 0 && E.cy == 0) {
        return;
    }

    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    } else { // Cursor at the beginning of a line
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

char * editorRowsToString(int *buflen) {
    // This functions convers an array of erow structs into
    // a single string.

    int totlen = 0;
    int j;
    for (j = 0; j < E.numrows; j++) {
        // Length of each row of text + 1 for the newline char.
        totlen += E.row[j].size + 1;
    }
    *buflen = totlen;
    
    char *buf = malloc(totlen);
    char *p = buf;
    for (j = 0; j < E.numrows; j++) {
        // Copy the content of each row to the end of the buffer
        // and then append a newline char.
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        die("editorOpen::fopen");
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r')) {
            linelen--;
        }
        editorAppendRow(line, linelen);
    }

    free(line);
    fclose(fp);
    E.dirty = 0;
}

void editorSave() {
    if (E.filename == NULL) {
        return;
    }

    int len;
    char *buf = editorRowsToString(&len);

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != 1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                E.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }

    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

int editorReadKey() {
    // Wait for one keypress and return it
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("editorReadKey::read");
        }
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            return '\x1b';
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                    return '\x1b';
                }
                // PAGE UP is sent as <esc>[5~
                // PAGE DOWN is sent as <esc>[6~
                // HOME is sent as <esc>[1~ / <esc>[7~ / <esc>[H / <esc>OH
                // END is sent as <esc>[4~ / <esc>[8~ / <esc>[F / <esc>OF
                // DEL is sent as <esc>[3~
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        
        return '\x1b';
    } else {
        return c;
    }
}

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) {
                E.cy++;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}

void editorProcessKeypress() {
    static int quit_times = QUIT_TIMES;

    // Wait for a keypress and then handle it
    int c = editorReadKey();

    // CTRL-Q will be used to quit from editor
    // CTRL-S will be used to save the file
    switch (c) {
        case CTRL_KEY('q'):
            if (E.dirty && quit_times > 0) {
                editorSetStatusMessage("WARNING: file has unsaved changes. Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
            }
            // Clear terminal and reposition the cursor
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case CTRL_KEY('s'):
            editorSave();
            break;
        case '\r': // Enter key
            // TO-DO
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            if (E.cy < E.numrows) {
                E.cx = E.row[E.cy].size;
            }
            break;
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY) {
                editorMoveCursor(ARROW_RIGHT);
                editorDelChar();
            }
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            // Code block required to declare the times variable
            // otherwise we ain't able to delcare variables
            // inside a switch statement
            {
                if (c == PAGE_UP) {
                    E.cy = E.rowoff;
                } else if (c == PAGE_DOWN) {
                    E.cy = E.rowoff + E.screenrows - 1;
                    if (E.cy > E.numrows) {
                        E.cy = E.numrows;
                    }
                }
                
                int times = E.screenrows;
                while (times--) {
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
        case CTRL_KEY('l'):
        case '\x1b':
            break;
        default:
            editorInsertChar(c);
            break;
    }

    quit_times = QUIT_TIMES;
}

void editorScroll() {
    E.rx = 0;
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf *ab) {
    // Draw a column of tildes on the left hand side
    // of the screen, like vim does.
    // Or fill the screen with file lines
    int y;

    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "kilo editor -- version %s", VERSION);
                if (welcomelen > E.screencols) {
                    welcomelen = E.screencols;
                }
                int padding = (E.screencols - welcomelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) {
                    abAppend(ab, " ", 1);
                }
                abAppend(ab, welcome, welcomelen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) {
                len = 0;
            }
            if (len > E.screencols) {
                len = E.screencols;
            }
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }

        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab) {
    // Switch to inverted colors: \x1b[7m
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines", E.filename ? E.filename : "[No Name]", E.numrows, E.dirty ? "(modified)" : "");
    // Line number
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy +1, E.numrows);
    // Cut string if longer than screen size
    if (len > E.screencols) {
        len = E.screencols;
    }
    abAppend(ab, status, len);
    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }

    // Switch back to normal formatting
    abAppend(ab, "\x1b[m", 3);
    // Make room for the status message
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
    // <esc>[K clear the message bar
    abAppend(ab, "\x1b[K", 3);
    
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) {
        msglen = E.screencols;
    }
    // Show the message only if it's less than 5 seconds old
    if (msglen && time(NULL) - E.statusmsg_time < 5) {
        abAppend(ab, E.statusmsg, msglen);
    }
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;

    // Hide cursor before refreshing the screen
    abAppend(&ab, "\x1b[?25l", 6);

    // 4 means we are writing 4 bytes
    // \x1b is the escape character followed by [
    // the J command is used to clear the screen:
    // 0 clear the screen from the cursor up to
    // the end of the screen
    // 1 clear the screen up to where the cursor is
    // 2 clear the entire screen
    // write(STDOUT_FILENO, "\x1b[2J", 4);
    // abAppend(&ab, "\x1b[2J", 4);

    // Reposition the cursor at the top-left corner
    // H command takes two arguments:
    // row number and column number at which position
    // the cursor, default values are 1;1.
    // e.g. 80x24 terminal size and cursor at center
    // would be \x1b[12;40H
    // write(STDOUT_FILENO, "\x1b[H", 3);
    abAppend(&ab, "\x1b[H", 3);

    // Start drawing the "GUI"
    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    // Move the cursor to the position stored in E.cx / E.cy
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
    abAppend(&ab, buffer, strlen(buffer));

    // Show the cursor again
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
    // "..." makes it a variadic function, meaning it can take
    // any number of arguments. To handle those arguments in C
    // we can use va_start() and va_end() on a value of type va_list.
    // The last argument before the three dots (fmt in our case) must
    // be passed to va_start(). Between va_start() and va_end(), we
    // should call va_arg() and pass it the type of the next argument
    // and it would return the value of that argument.
    // In our case, we pass fmt and ap to vsnprintf(), it takes care
    // of reading the format string and calling va_arg() to get each
    // argument.

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);

    // Get current time
    E.statusmsg_time = time(NULL);
}

void initEditor() {
    // This function initialize all the fields of our
    // editor configuration variable E.
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.numrows = 0;
    E.row = NULL;
    E.rowoff = 0;
    E.coloff = 0;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        die("init::getWindowSize");
    }
    // We leave a line for the status bar and one for the
    // status message.
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();

    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-Q = Quit :: Ctrl-S = Save");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}