#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

struct termios orig_termios;

void die(const char *s) {
    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        die("tcsetattr");
    }
}

void enableRawMode() {
    // Get terminal attribute and store in orig_termios
    tcgetattr(STDIN_FILENO, &orig_termios);
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
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();
    char c;
    while (1) {
        char c = '\0';
        read(STDIN_FILENO, &c, 1);
        // Exit if q was pressed
        if (c == 'q') break;
        // Test if c is a control char (nonprintable)
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
    }
    return 0;
}