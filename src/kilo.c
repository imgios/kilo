#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
    // Get terminal attribute and store in orig_termios
    tcgetattr(STDIN_FILENO, &orig_termios);
    // Call disableRawMode automatically whn the program exits
    atexit(disableRawMode);
    
    // terminal attributes copy before updating them
    struct termios raw = orig_termios;

    // ICANON flag allows us to turn off canonical mode
    // and read the input byte-by-byte instead of
    // line-by-line.
    // ISIG turn off CTRL-C and CTRL-Z signals.
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    // Set terminal attribute
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();
    char c;
    while (read(STDIN_FILENO, &c, 1) ==1 && c != 'q') {
        // Test if c is a control char (nonprintable)
        if (iscntrl(c)) {
            printf("%d\n", c);
        } else {
            printf("%d ('%c')\n", c, c);
        }
    }
    return 0;
}