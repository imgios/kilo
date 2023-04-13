#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct termios orig_termios;

void die(const char *s) {
    // Clear terminal and reposition the cursor
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    
    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        die("disableRawMode::tcsetattr");
    }
}

void enableRawMode() {
    // Get terminal attribute and store in orig_termios
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        die("enableRawMode::tcgetattr");
    }
    // Call disableRawMode automatically whn the program exits
    atexit(disableRawMode);
    
    // terminal attributes copy before updating them
    struct termios raw = orig_termios;

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
            exit(0);
            break;
    }
}

void editorRefreshScreen() {
    // 4 means we are writing 4 bytes
    // \x1b is the escape character followed by [
    // the J command is used to clear the screen:
    // 0 clear the screen from the cursor up to
    // the end of the screen
    // 1 clear the screen up to where the cursor is
    // 2 clear the entire screen
    write(STDOUT_FILENO, "\x1b[2J", 4);

    // Reposition the cursor at the top-left corner
    // H command takes two arguments:
    // row number and column number at which position
    // the cursor, default values are 1;1.
    // e.g. 80x24 terminal size and cursor at center
    // would be \x1b[12;40H
    write(STDOUT_FILENO, "\x1b[H", 3);
}

int main() {
    enableRawMode();
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}