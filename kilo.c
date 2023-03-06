/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termio.h>
#include <unistd.h>

/*** data ***/
struct termios orig_terminos;

/*** terminal ***/
void die(const char *s) {
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_terminos) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &orig_terminos) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = orig_terminos;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag &= (CS8);
  raw.c_lflag &= ~(ECHO | IEXTEN | ICANON | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 10;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

/*** init ***/
int main() {
  enableRawMode();

  while (1) {
    char c = '\0';
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
      die("READ");
    if (iscntrl(c))
      printf("%x\r\n", c);
    else
      printf("%x ('%c')\r\n", c, c);
    if (c == 'q')
      break;
  }
  return 0;
}
