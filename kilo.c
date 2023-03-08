/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termio.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k)&0x1f)

/*** data ***/
struct editorConfig {
  int screenRows;
  int screenCols;
  struct termios orig_terminos;
};

struct editorConfig E;

/*** terminal ***/
void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_terminos) == -1)
    die("TCSETATTR");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_terminos) == -1)
    die("TCGETATTR");
  atexit(disableRawMode);

  struct termios raw = E.orig_terminos;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag &= (CS8);
  raw.c_lflag &= ~(ECHO | IEXTEN | ICANON | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 10;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("READ");
  }
  return c;
}

int getCursorPos(int *row, int *col) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R') {
      buf[i] = '\0';
      break;
    }
    i++;
  }

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", row, col) != 2)
    return -1;

  return 0;
}

/*** output ***/
void editorDrawRows() {
  int y;
  char buf[] = {'0', '\r', '\n'};
  for (y = 0; y < E.screenRows; y++) {
    buf[0] = y + '0';
    write(STDOUT_FILENO, buf, 3);
  }
}

void editorRefreshScreen() {
  write(STDOUT_FILENO, "\x1b[2J", 4); // Clear Screen
  write(STDOUT_FILENO, "\x1b[H", 3);  // Set Mouse Pos 1:1

  editorDrawRows();
  write(STDOUT_FILENO, "\x1b[H", 3); // Set Mouse Pos 1:1
}

/*** input ***/
void editorProcessKeyPress() {
  char c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  }
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPos(rows, cols);
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
  }
  return 0;
}

/*** init ***/
void initEditor() {
  if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
    die("GET_WINDOWS_SIZE");
}
int main() {
  enableRawMode();
  initEditor();

  while (1) {
    editorRefreshScreen();
    editorProcessKeyPress();
  }
  return 0;
}
