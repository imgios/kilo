#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct editorConfig {
    struct termios orig_termios;
    int screenrows;
    int screencols;
};

struct editorConfig E;

struct abuf {
    char *b;
    int len;
};

// Represents an empty buffer and acts as constructor
#define ABUF_INIT {NULL, 0}

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

char editorReadKey() {
    // Wait for one keypress and return it
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("editorReadKey::read");
        }
    }

    return c;
}

void editorProcessKeypress() {
    // Wait for a keypress and then handle it
    char c = editorReadKey();

    // CTRL-Q will be used to quit from editor
    switch (c) {
        case CTRL_KEY('q'):
            // Clear terminal and reposition the cursor
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

void editorDrawRows(struct abuf *ab) {
    // Draw a column of tildes on the left hand side
    // of the screen, like vim does.
    int y;

    for (y = 0; y < E.screenrows; y++) {
        abAppend(ab, "~", 1);

        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
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
    abAppend(&ab, "\x1b[2J", 4);

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

    // Reposition the cursor at the top-left corner
    // write(STDOUT_FILENO, "\x1b[H", 3);
    abAppend(&ab, "\x1b[H", 3);

    // Show the cursor again
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void initEditor() {
    // This function initialize all the fields of our
    // editor configuration variable E.
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        die("init::getWindowSize");
    }
}

int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}