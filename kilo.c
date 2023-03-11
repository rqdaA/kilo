/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termio.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k)&0x1f)
#define KILO_VERSION "0.0.1"
#define ABUF_INIT                                                              \
  { NULL, 0 }

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

/*** append buffer ***/
struct abuf {
  char *b;
  int len;
};

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->b); }

/*** output ***/
void editorDrawRows(struct abuf *ab) {
  int y;
  char buf[5] = {'\0'};
  for (y = 0; y < E.screenRows; y++) {
    if (y == E.screenRows / 3) {
      char welcome[0x50];
      int welcomeLen = snprintf(welcome, sizeof(welcome),
                                "Kilo Editor -- version %s", KILO_VERSION);
      if (welcomeLen > E.screenCols)
        welcomeLen = E.screenCols;

      int padding = (E.screenCols - welcomeLen) / 2;
      while (padding--)
        abAppend(ab, " ", 1);
      abAppend(ab, welcome, welcomeLen);
    } else {
      sprintf(buf, "%d", y);
      abAppend(ab, buf, strlen(buf));
    }

    abAppend(ab, "\x1b[K", 3);
    if (y != E.screenRows - 1)
      abAppend(ab, "\r\n", 2);
  }
}

void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6); // Hide Cursor
  abAppend(&ab, "\x1b[H", 3);    // Set Mouse Pos 1:1

  editorDrawRows(&ab);
  abAppend(&ab, "\x1b[H", 3);    // Set Mouse Pos 1:1
  abAppend(&ab, "\x1b[?25h", 6); // Show Cursor

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
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
