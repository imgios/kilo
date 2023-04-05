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

    // ICANON flag allows us to torun off canonical mode
    // and read the input byte-by-byte instead of
    // line-by-line.
    raw.c_lflag &= ~(ECHO | ICANON);
    // Set terminal attribute
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();
    char c;
    while (read(STDIN_FILENO, &c, 1) ==1 && c != 'q');
    return 0;
}