#include <termios.h>
#include <unistd.h>

void enableRawMode() {
    struct termios raw;

    // Get terminal attribute
    tcgetattr(STDIN_FILENO, &raw);
    raw.c_lflag &= ~(ECHO);
    // Set terminal attribute
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();
    char c;
    while (read(STDIN_FILENO, &c, 1) ==1 && c != 'q');
    return 0;
}