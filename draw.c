#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <limits.h>

#include "structures.h"
#include "highlights.h"
#include "draw.h"

#define TAB 9
#define min(a,b) ((a) < (b) ? (a) : (b))

struct str {
    char *data;
    unsigned int len;
    unsigned int capacity;
};

void str_Append(struct str *s, const char *e, int len) {
    int new_len = s->len+len;
    if (new_len > INT_MAX) {
      /* error */
    }
    if (new_len > s->capacity) {
      unsigned int new_capacity = s->capacity || 1;
      while (new_len > new_capacity) new_capacity = new_capacity << 1;
	char *new_data = realloc(s->data, new_capacity);
	if (new_data == NULL) {
	  /* error */
	}
	s->data = new_data;
	s->capacity = new_capacity;
    }
    memcpy(s->data+s->len, e, len);
    s->len = new_len;
}

void str_Free(struct str *s) {
    free(s->data);
}

void editor_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

void editor_refresh(void) {

    struct str str = {NULL, 0, 0};
    str_Append(&str, "\x1b[?25l", 6); 
    str_Append(&str, "\x1b[H", 3);
    for (struct line *line = E.buffer.lines+E.buffer.offset.row;
	 line < E.buffer.lines+E.buffer.offset.row+E.terminal.winsize.row;
	 line++) {
        if (line - E.buffer.lines >= E.buffer.numlines) {
	    str_Append(&str,"~\x1b[0K\r\n",7);
            continue;
        }
	
        int ncols = min(line->rsize - E.buffer.offset.col, E.terminal.winsize.col);
        int current_color = -1;
	unsigned char *hl = line->hl+E.buffer.offset.col-1;
	char *c = line->render+E.buffer.offset.col-1;
	while ((hl++, c++, ncols--)) {
                if (*hl == HL_NONPRINT) {
                    str_Append(&str, "\x1b[7m", 4);
		    char sym = *c<=26 ? '@'+*c : '?';
                    str_Append(&str, &sym, 1);
                    str_Append(&str, "\x1b[0m", 4);
                } else if (*hl == HL_NORMAL) {
                    if (current_color != -1) {
                        str_Append(&str,"\x1b[39m",5);
                        current_color = -1;
                    }
                    str_Append(&str, c, 1);
                } else {
                    int color = editorSyntaxToColor(*hl);
                    if (color != current_color) {
                        char buf[16];
                        int clen = snprintf(buf,sizeof(buf),"\x1b[%dm",color);
                        current_color = color;
                        str_Append(&str, buf, clen);
                    }
                    str_Append(&str, c, 1);
                }
	}
        str_Append(&str, "\x1b[39m", 5);
        str_Append(&str, "\x1b[0K", 4);
        str_Append(&str, "\r\n", 2);
    }

    /* mode-line */
    str_Append(&str, "\x1b[0K", 4);
    str_Append(&str, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
        E.buffer.filename, E.buffer.numlines, E.buffer.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus),
        "%d/%d",E.buffer.offset.row+E.buffer.point.row+1,E.buffer.numlines);
    if (len > E.terminal.winsize.col) len = E.terminal.winsize.col;
    str_Append(&str, status, len);
    while (len < E.terminal.winsize.col) {
        if (E.terminal.winsize.col - len == rlen) {
            str_Append(&str,rstatus,rlen);
            break;
        } else {
            str_Append(&str," ",1);
            len++;
        }
    }
    str_Append(&str,"\x1b[0m\r\n",6);

    /* echo area */
    str_Append(&str,"\x1b[0K",4);
    if (time(NULL) > E.statusmsg_time + 2) E.statusmsg[0] = '\0';
    str_Append(&str, E.statusmsg, min(strlen(E.statusmsg), E.terminal.winsize.col));

    /* flush point NB: col ≢ E.buffer.point.col (TABs) */
    int point_col = 1;
    int filerow = E.buffer.offset.row + E.buffer.point.row;
    struct line *row = (filerow >= E.buffer.numlines) ? NULL : &E.buffer.lines[filerow];
    if (row) {
        for (int j = E.buffer.offset.col; j < (E.buffer.point.col+E.buffer.offset.col); j++) {
            if (j < row->size && row->chars[j] == TAB) point_col += 7-((point_col)%8);
            point_col++;
        }
    }
    char buf[32];
    snprintf(buf,sizeof(buf),"\x1b[%d;%dH",E.buffer.point.row+1,point_col);
    str_Append(&str, buf, strlen(buf));
    str_Append(&str, "\x1b[?25h", 6);

    write(STDOUT_FILENO, str.data, str.len);
    str_Free(&str);
}

