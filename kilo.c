/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/types.h>
#include <termio.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k)&0x1f)
#define KILO_VERSION "0.0.1"
#define ABUF_INIT                                                              \
  { NULL, 0 }
enum editorKey {
  ARROW_LEFT = 0x100,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN,
  HOME_KEY,
  END_KEY,
  DEL_KEY
};

/*** data ***/
typedef struct erow {
  int length;
  char *chars;
} erow;

struct editorConfig {
  int cx, cy;
  int rowOff;
  int colOff;
  int screenRows;
  int screenCols;
  int numRows;
  erow *row;
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

int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("READ");
  }

  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[') {
      if ('0' <= seq[1] && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '1':
            return HOME_KEY;
          case '3':
            return DEL_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
      return '\x1b';
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }
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

/*** row operations ***/

void editorAppendRow(char *s, size_t len) {
  E.row = realloc(E.row, sizeof(erow) * (E.numRows + 1));

  int at = E.numRows;
  E.row[at].length = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.numRows++;
}
/*** file i/o ***/

void editorOpen(char *fileName) {
  FILE *fp = fopen(fileName, "r");
  if (!fp)
    die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
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
void editorScroll() {
  if (E.cy < E.rowOff) {
    E.rowOff -= E.screenRows;
  }

  if (E.rowOff + E.screenRows <= E.cy) {
    E.rowOff += E.screenRows;
  }

  if (E.cx < E.colOff)
    E.colOff -= E.screenCols;

  if (E.colOff + E.screenCols <= E.cx)
    E.colOff += E.screenCols;
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenRows; y++) {
    int fileRow = y + E.rowOff;
    if (fileRow >= E.numRows) {
      if (E.numRows == 0 && y == E.screenRows / 3) {
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
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[fileRow].length - E.colOff;
      if (len < 0)
        len = 0;
      if (len >= E.screenCols)
        len = E.screenCols;
      abAppend(ab, &E.row[fileRow].chars[E.colOff], len);
    }

    abAppend(ab, "\x1b[K", 3);
    if (y != E.screenRows - 1)
      abAppend(ab, "\r\n", 2);
  }
}

void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6); // Hide Cursor
  abAppend(&ab, "\x1b[H", 3);    // Set Mouse Pos 1:1

  editorDrawRows(&ab);

  char buf[0x20];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowOff + 1),
           (E.cx - E.colOff + 1));
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6); // Show Cursor

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** input ***/
void editorMoveCursor(int key) {
  erow *row = (E.cy > E.numRows - 1) ? NULL : &E.row[E.cy];
  switch (key) {
  case ARROW_LEFT:
    E.cx = E.cx == 0 ? 0 : E.cx - 1;
    break;
  case ARROW_DOWN:
    E.cy = E.cy >= E.numRows - 1 ? E.cy : E.cy + 1;
    break;
  case ARROW_UP:
    E.cy = E.cy == 0 ? 0 : E.cy - 1;
    break;
  case ARROW_RIGHT:
    E.cx = row && row->length - 1 >= E.cx + 1 ? E.cx + 1 : E.cx;
    break;
  }

  row = (E.cy > E.numRows - 1) ? NULL : &E.row[E.cy];
  int rowLen = row ? row->length - 1 : 0;
  if (rowLen <= E.cx)
    E.cx = MAX(row->length - 1, 0);
}
void editorProcessKeyPress() {
  int c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  case PAGE_UP:
  case PAGE_DOWN: {
    int times = E.screenRows;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;
  case HOME_KEY:
  case END_KEY: {
    int times = E.screenCols;
    while (times--)
      editorMoveCursor(c == HOME_KEY ? ARROW_LEFT : ARROW_RIGHT);
  } break;
  case ARROW_RIGHT:
  case ARROW_LEFT:
  case ARROW_UP:
  case ARROW_DOWN:
    editorMoveCursor(c);
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
  E.cx = 0;
  E.cy = 0;
  E.rowOff = 0;
  E.colOff = 0;
  E.numRows = 0;
  E.row = NULL;

  if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
    die("GET_WINDOWS_SIZE");
}

int main(int argc, char **argv) {
  enableRawMode();
  initEditor();
  if (argc >= 2)
    editorOpen(argv[1]);

  while (1) {
    editorRefreshScreen();
    editorProcessKeyPress();
  }
  return 0;
}
