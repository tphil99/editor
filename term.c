#include <termios.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>

#include "structures.h"
#include "draw.h"

static struct termios orig_termios;

static void term__exit_hook(void);
static int term__switch_raw_mode(int fd, int on) {

    if (!on && E.terminal.rawmode) {
        tcsetattr(fd,TCSAFLUSH,&orig_termios);
        E.terminal.rawmode = 0;
	return 0;
    }

    if (E.terminal.rawmode) return 0;
    if (!isatty(STDIN_FILENO)) goto fatal;
    atexit(term__exit_hook);
    if (tcgetattr(fd,&orig_termios) == -1) goto fatal;

    struct termios raw = orig_termios; 
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 0; 
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(fd,TCSAFLUSH,&raw) < 0) goto fatal;
    E.terminal.rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}
static void term__exit_hook(void) {
  term__switch_raw_mode(STDIN_FILENO, 0);
  write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
}
static int term__get_point(int ifd, int ofd, struct point *point) {
    char buf[32];
    unsigned int i = 0;
    if (write(ofd, "\x1b[6n", 4) != 4) return -1;

    /* ESC[<r>;<c>R */
    while (i < sizeof(buf)-1) {
        if (read(ifd,buf+i,1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] == ESC &&
	buf[1] == '[' &&
	sscanf(buf+2,"%d;%d",&(point->row),&(point->col)) == 2) return 0;
    else return -1;
}
static void term__handleSIGWINCH(int unused __attribute__((unused))) {
    struct winsize ws;
    /* try to get via ioctl() */
    int got_winsize = !ioctl(1, TIOCGWINSZ, &ws) && ws.ws_col != 0;
    /* try to get via point excursion */
    if (!got_winsize) {
        struct point orig_point, bottom_right_point;
        if (term__get_point(STDIN_FILENO,STDOUT_FILENO, &orig_point) != -1) {
	  if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B",12) == 12) {
	    if (term__get_point(STDIN_FILENO,STDOUT_FILENO, &bottom_right_point) != -1) {
	        ws.ws_col = bottom_right_point.col;
                ws.ws_row = bottom_right_point.row;
	        got_winsize = 1;
	    }
	    char seq[32];
	    snprintf(seq,32,"\x1b[%d;%dH",orig_point.row,orig_point.col);
	    if (write(STDOUT_FILENO,seq,strlen(seq)) == -1) {
	      perror("Tried to query screen size via a point excursion, unable to restore point");
	      exit(1);
	    }
	  }
	}
    }
    if (got_winsize) {
        E.terminal.winsize.col = ws.ws_col;
        E.terminal.winsize.row = ws.ws_row;
    } else {
        perror("Unable to query the screen for size (columns / rows)");
        exit(1);
    }
    E.terminal.winsize.row -= 2; /* room for status bar. */
    if (E.buffer.point.row > E.terminal.winsize.row) E.buffer.point.row = E.terminal.winsize.row - 1;
    if (E.buffer.point.col > E.terminal.winsize.col) E.buffer.point.col = E.terminal.winsize.col - 1;
    editor_refresh();
}

int term_read(int fd) {
    assert(E.terminal.rawmode);
    int nread;
    char c, seq[3];
    while ((nread = read(fd,&c,1)) == 0);
    if (nread == -1) exit(1);

    /* normal character */
    if (c != ESC) return c;

    /* just an ESC */
    if (!read(fd,seq,1) || !read(fd,seq+1,1)) return ESC; 

    /* esc seq */
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
	/* DEL, PgUP, PgDn */
	if (read(fd,seq+2,1) == 0) return ESC;
	if (seq[2] == '~') {
	  switch(seq[1]) {
	  case '3': return CTRL_D;
	  case '5': return PAGE_UP;
	  case '6': return PAGE_DOWN;
	  }
	}
      } else {
	/* arrows, home, end */
	switch(seq[1]) {
	case 'A': return CTRL_P;
	case 'B': return CTRL_N;
	case 'C': return CTRL_F;
	case 'D': return CTRL_B;
	case 'H': return HOME_KEY;
	case 'F': return END_KEY;
	}
      }
    }
    /* ESC O sequences. */
    else if (seq[0] == 'O') {
      switch(seq[1]) {
      case 'H': return HOME_KEY;
      case 'F': return END_KEY;
      }
    }

    /* bad esc seq */
    return ESC;
}

void term_setup(void) {
    term__handleSIGWINCH(0);
    signal(SIGWINCH, term__handleSIGWINCH);
    term__switch_raw_mode(STDIN_FILENO, 1);
}
