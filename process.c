#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

#include "draw.h"
#include "term.h"
#include "structures.h"
#include "highlights.h"

struct editor E;

/* ========================== Helper Funs ========================= */

/* Update line->render, line->highlight */
void buffer_render_line(struct line *line) {
    unsigned int tabs = 0, nonprint = 0;
    int j, idx;

   /* re-render line: respect tabs, sub non printable with '?' */
    free(line->render);
    for (j = 0; j < line->size; j++)
        if (line->chars[j] == TAB) tabs++;

    unsigned long long allocsize =
        (unsigned long long) line->size + tabs*8 + nonprint*9 + 1;
    if (allocsize > 50) { /* UINT32_MAX */
        printf("file lines are too long ... quiting\n");
	sleep(3);
        exit(1);
    }

    line->render = malloc(line->size + tabs*8 + nonprint*9 + 1);
    idx = 0;
    for (j = 0; j < line->size; j++) {
        if (line->chars[j] == TAB) {
            line->render[idx++] = ' ';
            while((idx+1) % 8 != 0) line->render[idx++] = ' ';
        } else {
            line->render[idx++] = line->chars[j];
        }
    }
    line->rsize = idx;
    line->render[idx] = '\0';

    editorUpdateSyntax(line);
}

void buffer_insert_line(int at, char *s, size_t len) {
    if (at > E.buffer.numlines) return;
    E.buffer.lines = realloc(E.buffer.lines,sizeof(struct line)*(E.buffer.numlines+1));
    if (at != E.buffer.numlines) {
        memmove(E.buffer.lines+at+1,E.buffer.lines+at,sizeof(E.buffer.lines[0])*(E.buffer.numlines-at));
        for (int j = at+1; j <= E.buffer.numlines; j++) E.buffer.lines[j].idx++;
    }
    struct line *line = E.buffer.lines+at;
    line->size = len;
    line->chars = malloc(len+1);
    memcpy(line->chars,s,len+1);
    line->hl = NULL;
    line->hl_oc = 0;
    line->render = NULL;
    line->rsize = 0;
    line->idx = at;
    buffer_render_line(line);
    E.buffer.numlines++;
    E.buffer.dirty++;
}

void buffer_free_line(struct line *line) {
    free(line->render);
    free(line->chars);
    free(line->hl);
}
void buffer_clear(void) {
  E.buffer.point.col = E.buffer.point.row = 0;
  E.buffer.offset.col = E.buffer.offset.row = 0;
  /* E.buffer.syntax = NULL; */
  E.buffer.dirty = 0;
  for (int i=0; i<E.buffer.numlines; ++i) {
    buffer_free_line(E.buffer.lines+i);
  }
  E.buffer.numlines = 0;
}

static void editor_point_fix(void);
void buffer_kill_line(int at) {
    struct line *row;
    if (at >= E.buffer.numlines) return;
    row = E.buffer.lines+at;
    buffer_free_line(row);
    memmove(E.buffer.lines+at,E.buffer.lines+at+1,sizeof(E.buffer.lines[0])*(E.buffer.numlines-at-1));
    for (int j = at; j < E.buffer.numlines-1; j++) E.buffer.lines[j].idx--;
    E.buffer.numlines--;
    E.buffer.dirty++;
    editor_point_fix();
}
void buffer_kill_line_interactive(void) {
  buffer_kill_line(E.buffer.point.row + E.buffer.offset.row);
}

void editorRowInsertChar(struct line *row, int at, int c) {
    if (at > row->size) {
        /* Pad string with spaces if insert location outside current length by more than a single character. */
        int padlen = at-row->size;
        row->chars = realloc(row->chars,row->size+padlen+2);
        memset(row->chars+row->size,' ',padlen);
        row->chars[row->size+padlen+1] = '\0';
        row->size += padlen+1;
    } else {
        row->chars = realloc(row->chars,row->size+2);
        memmove(row->chars+at+1,row->chars+at,row->size-at+1);
        row->size++;
    }
    row->chars[at] = c;
    buffer_render_line(row);
    E.buffer.dirty++;
}

void editorRowAppendString(struct line *row, char *s, size_t len) {
    row->chars = realloc(row->chars,row->size+len+1);
    memcpy(row->chars+row->size,s,len);
    row->size += len;
    row->chars[row->size] = '\0';
    buffer_render_line(row);
    E.buffer.dirty++;
}

void editorRowDelChar(struct line *line, int at) {
    if (line->size <= at) return;
    memmove(line->chars+at, line->chars+at+1, line->size-at);
    buffer_render_line(line);
    line->size--;
    E.buffer.dirty++;
}

/* ========================== Search Commands ========================= */

#define KILO_QUERY_LEN 256
/* static void editorFind() { */
/*     int fd = 1; */
/*     char query[KILO_QUERY_LEN+1] = {0}; */
/*     int qlen = 0; */
/*     int last_match = -1; /\* Last line where a match was found. -1 for none. *\/ */
/*     int find_next = 0; /\* if 1 search next, if -1 search prev. *\/ */
/*     int saved_hl_line = -1;  /\* No saved HL *\/ */
/*     char *saved_hl = NULL; */

/* #define FIND_RESTORE_HL do { \ */
/*     if (saved_hl) { \ */
/*         memcpy(E.buffer.lines[saved_hl_line].hl,saved_hl, E.buffer.lines[saved_hl_line].rsize); \ */
/*         free(saved_hl); \ */
/*         saved_hl = NULL; \ */
/*     } \ */
/* } while (0) */

/*     /\* save-excursion *\/ */
/*     int saved_point_col = E.buffer.point.col, saved_point_row = E.buffer.point.row; */
/*     int saved_offset_col = E.buffer.offset.col, saved_offset_row = E.buffer.offset.row; */

/*     while(1) { */
/*         editor_message("Search: %s (Use ESC/Arrows/Enter)", query); */
/*         editor_refresh(); */

/*         int c = term_read(fd); */
/*         if (c == CTRL_H || c == DEL) { */
/*             if (qlen != 0) query[--qlen] = '\0'; */
/*             last_match = -1; */
/*         } else if (c == ESC || c == CTRL_M) { */
/*             if (c == ESC) { */
/*                 E.buffer.point.col = saved_point_col; E.buffer.point.row = saved_point_row; */
/*                 E.buffer.offset.col = saved_offset_col; E.buffer.offset.row = saved_offset_row; */
/*             } */
/*             FIND_RESTORE_HL; */
/*             editor_message(""); */
/*             return; */
/*         } else if (c == CTRL_F || c == CTRL_N) { */
/*             find_next = 1; */
/*         } else if (c == CTRL_B || c == CTRL_P) { */
/*             find_next = -1; */
/*         } else if (isprint(c)) { */
/*             if (qlen < KILO_QUERY_LEN) { */
/*                 query[qlen++] = c; */
/*                 query[qlen] = '\0'; */
/*                 last_match = -1; */
/*             } */
/*         } */

/*         /\* Search occurrence. *\/ */
/*         if (last_match == -1) find_next = 1; */
/*         if (find_next) { */
/*             char *match = NULL; */
/*             int match_offset = 0; */
/*             int i, current = last_match; */

/*             for (i = 0; i < E.buffer.numlines; i++) { */
/*                 current += find_next; */
/*                 if (current == -1) current = E.buffer.numlines-1; */
/*                 else if (current == E.buffer.numlines) current = 0; */
/*                 match = strstr(E.buffer.lines[current].render,query); */
/*                 if (match) { */
/*                     match_offset = match-E.buffer.lines[current].render; */
/*                     break; */
/*                 } */
/*             } */
/*             find_next = 0; */

/*             /\* Highlight *\/ */
/*             FIND_RESTORE_HL; */

/*             if (match) { */
/*                 struct line *row = &E.buffer.lines[current]; */
/*                 last_match = current; */
/*                 if (row->hl) { */
/*                     saved_hl_line = current; */
/*                     saved_hl = malloc(row->rsize); */
/*                     memcpy(saved_hl,row->hl,row->rsize); */
/*                     memset(row->hl+match_offset,HL_MATCH,qlen); */
/*                 } */
/*                 E.buffer.point.row = 0; */
/*                 E.buffer.point.col = match_offset; */
/*                 E.buffer.offset.row = current; */
/*                 E.buffer.offset.col = 0; */
/*                 /\* Scroll horizontally as needed. *\/ */
/*                 if (E.buffer.point.col > E.terminal.winsize.col) { */
/*                     int diff = E.buffer.point.col - E.terminal.winsize.col; */
/*                     E.buffer.point.col -= diff; */
/*                     E.buffer.offset.col += diff; */
/*                 } */
/*             } */
/*         } */
/*     } */
/* } */

/* ========================== Editing Commands ========================= */

/* at point */
void editorInsertChar(int c) {
    int filerow = E.buffer.offset.row+E.buffer.point.row;
    int filecol = E.buffer.offset.col+E.buffer.point.col;
    struct line *row = (filerow >= E.buffer.numlines) ? NULL : &E.buffer.lines[filerow];

    /* If point on "line" that does not exist in our represented file, add empty rows */
    if (!row) {
        while(E.buffer.numlines <= filerow)
            buffer_insert_line(E.buffer.numlines,"",0);
    }
    row = &E.buffer.lines[filerow];
    editorRowInsertChar(row,filecol,c);
    if (E.buffer.point.row == E.terminal.winsize.col-1)
        E.buffer.offset.col++;
    else
        E.buffer.point.col++;
    E.buffer.dirty++;
}

/* handle inserting newline in middle of line, splitting line */
void editorInsertNewline(void) {
    int filerow = E.buffer.offset.row+E.buffer.point.row;
    int filecol = E.buffer.offset.col+E.buffer.point.col;
    struct line *row = (filerow >= E.buffer.numlines) ? NULL : &E.buffer.lines[filerow];

    if (!row) {
        if (filerow == E.buffer.numlines) {
            buffer_insert_line(filerow,"",0);
            goto fixcursor;
        }
        return;
    }
    /* If the cursor is over the current line size, we want to conceptually
     * think it's just over the last character. */
    if (filecol >= row->size) filecol = row->size;
    if (filecol == 0) {
        buffer_insert_line(filerow,"",0);
    } else {
        /* We are in the middle of a line. Split it between two rows. */
        buffer_insert_line(filerow+1,row->chars+filecol,row->size-filecol);
        row = &E.buffer.lines[filerow];
        row->chars[filecol] = '\0';
        row->size = filecol;
        buffer_render_line(row);
    }
fixcursor:
    if (E.buffer.point.row == E.terminal.winsize.row-1) {
        E.buffer.offset.row++;
    } else {
        E.buffer.point.row++;
    }
    E.buffer.point.col = 0;
    E.buffer.offset.col = 0;
}
/* Fix point.col passed end-of-line */
static void editor_point_fix(void) {
    int filerow = E.buffer.offset.row+E.buffer.point.row;
    int filecol = E.buffer.offset.col+E.buffer.point.col;
    struct line *row = (filerow >= E.buffer.numlines) ? NULL : E.buffer.lines+filerow;
    int rowlen = row ? row->size : 0;
    if (filecol > rowlen) {
        E.buffer.point.col -= filecol-rowlen;
        if (E.buffer.point.col < 0) {
            E.buffer.offset.col += E.buffer.point.col;
            E.buffer.point.col = 0;
        }
    }
}
static void editor_point_next_line(void) {
    int filerow = E.buffer.offset.row+E.buffer.point.row;
    if (filerow < E.buffer.numlines) {
      if (E.buffer.point.row == E.terminal.winsize.row-1) {
	E.buffer.offset.row++;
      } else {
	E.buffer.point.row += 1;
      }
    }
    editor_point_fix();
}
static void editor_point_backward_char(void) {
    int filerow = E.buffer.offset.row+E.buffer.point.row;
    if (E.buffer.point.col == 0) {
      if (E.buffer.offset.col) {
	E.buffer.offset.col--;
      } else {
	if (filerow > 0) {
	  E.buffer.point.row--;
	  E.buffer.point.col = E.buffer.lines[filerow-1].size;
	  if (E.buffer.point.col > E.terminal.winsize.col-1) {
	    E.buffer.offset.col = E.buffer.point.col-E.terminal.winsize.col+1;
	    E.buffer.point.col = E.terminal.winsize.col-1;
	  }
	}
      }
    } else {
      E.buffer.point.col -= 1;
    }
    editor_point_fix();
}
static void editor_point_forward_char(void) {
    int filerow = E.buffer.offset.row+E.buffer.point.row;
    int filecol = E.buffer.offset.col+E.buffer.point.col;
    struct line *row = (filerow >= E.buffer.numlines) ? NULL : &E.buffer.lines[filerow];
    if (row && filecol < row->size) {
      if (E.buffer.point.col == E.terminal.winsize.col-1) {
	E.buffer.offset.col++;
      } else {
	E.buffer.point.col += 1;
      }
    } else if (row && filecol == row->size) {
      E.buffer.point.col = 0;
      E.buffer.offset.col = 0;
      if (E.buffer.point.row == E.terminal.winsize.row-1) {
	E.buffer.offset.row++;
      } else {
	E.buffer.point.row += 1;
      }
    }
    editor_point_fix();
}
static void editor_point_prev_line(void) {
    if (E.buffer.point.row == 0) {
      if (E.buffer.offset.row) E.buffer.offset.row--;
    } else {
      E.buffer.point.row -= 1;
    }
    editor_point_fix();
}
static void editorDelChar(void) {
    int filerow = E.buffer.offset.row+E.buffer.point.row;
    int filecol = E.buffer.offset.col+E.buffer.point.col;
    struct line *row = (filerow >= E.buffer.numlines) ? NULL : &E.buffer.lines[filerow];

    if (!row || (filecol == 0 && filerow == 0)) return;
    if (filecol == 0) {
        /* col 0, move current line on the right of the previous one. */
        filecol = E.buffer.lines[filerow-1].size;
        editorRowAppendString(&E.buffer.lines[filerow-1],row->chars,row->size);
        buffer_kill_line(filerow);
        row = NULL;
        if (E.buffer.point.row == 0)
            E.buffer.offset.row--;
        else
            E.buffer.point.row--;
        E.buffer.point.col = filecol;
        if (E.buffer.point.col >= E.terminal.winsize.col) {
            int shift = (E.terminal.winsize.col-E.buffer.point.col)+1;
            E.buffer.point.col -= shift;
            E.buffer.offset.col += shift;
        }
    } else {
        editorRowDelChar(row,filecol-1);
        if (E.buffer.point.col == 0 && E.buffer.offset.col)
            E.buffer.offset.col--;
        else
            E.buffer.point.col--;
    }
    if (row) buffer_render_line(row);
    E.buffer.dirty++;
}
static void editorDelForwardChar(void) {
  editor_point_forward_char();
  editorDelChar();
}

/* void editor_page_up(void){ */
    /* case PAGE_UP: */
    /* case PAGE_DOWN: */
    /*     if (c == PAGE_UP && E.buffer.point.row != 0) */
    /*         E.buffer.point.row = 0; */
    /*     else if (c == PAGE_DOWN && E.buffer.point.row != E.terminal.winsize.row-1) */
    /*         E.buffer.point.row = E.terminal.winsize.row-1; */
    /*     { */
    /*     int times = E.terminal.winsize.row; */
    /*     while(times--) */
    /* 	    eventHandler[c == PAGE_UP ? CTRL_P: CTRL_N](); */
    /*     } */
    /*     break; */
    /*   }; */
/* } */

/* ==================== Buffer Commands ========================== */

static void buffer_write(void) {

    /* join rows into char* buf */
    int len = 0;
    char *buf = NULL;
    {
      char *p;
      int j;
      /* count bytes */
      for (j = 0; j < E.buffer.numlines; j++)
        len += E.buffer.lines[j].size+1; /* for \n */
      len++; /* for \0 */

      p = buf = malloc(len);
      for (j = 0; j < E.buffer.numlines; j++) {
        memcpy(p,E.buffer.lines[j].chars,E.buffer.lines[j].size);
        p += E.buffer.lines[j].size;
        *p = '\n';
        p++;
      }
      *p = '\0';
      --len;      /* exclude \0 */
    }

    int fd = open(E.buffer.filename,O_RDWR|O_CREAT,0644);
    if (fd == -1) goto writeerr;

    /* Use truncate + single write(2) call in order to make saving safer */
    if (ftruncate(fd,len) == -1) goto writeerr;
    if (write(fd,buf,len) != len) goto writeerr;

    close(fd);
    free(buf);
    E.buffer.dirty = 0;
    editor_message("%d bytes written on disk", len);
    return;

writeerr:
    free(buf);
    if (fd != -1) close(fd);
    editor_message("Can't save! I/O error: %s",strerror(errno));
}

/* 0 ⇒ success */
int buffer_find_file(char *filename) {
    FILE *fp;

    buffer_clear();
    editorSelectSyntaxHighlight(filename);
    free(E.buffer.filename);
    size_t fnlen = strlen(filename)+1;
    E.buffer.filename = malloc(fnlen);
    memcpy(E.buffer.filename,filename,fnlen);

    fp = fopen(filename,"r");
    if (!fp) {
        if (errno != ENOENT) {
            perror("Opening file");
            exit(1);
        }
        return 1;
    }

    char *line = NULL;
    size_t linecap=0, linelen;
    while((linelen = getline(&line,&linecap,fp)) != (size_t)-1) {
        if (linelen && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
            line[--linelen] = '\0';
        buffer_insert_line(E.buffer.numlines,line,linelen);
    }
    free(line);
    fclose(fp);
    E.buffer.dirty = 0;
    return 0;
}
void buffer_find_file_interactive(void) {
  int fd = 1;
  if (E.buffer.dirty) {
    editor_message("You must first write or discard the current changes");
    return;
  }
    char query[KILO_QUERY_LEN+1] = {0};
    int qlen = 0;
    while(1) {
        editor_message("File name: %s (Use ESC/Enter)", query);
        editor_refresh();

        int c = term_read(fd);
        if (c == CTRL_H || c == DEL) {
            if (qlen != 0) query[--qlen] = '\0';
        } else if (c == ESC) {
            editor_message("");
	    return;
	} else if (c == CTRL_M) {
	    editor_message("Opening %s", query);
	    buffer_find_file(query);
            return;
        } else if (isprint(c)) {
            if (qlen < KILO_QUERY_LEN) {
                query[qlen++] = c;
                query[qlen] = '\0';
            }
        }
    }
}

#define KILO_QUIT_TIMES 2
static int quit_times = KILO_QUIT_TIMES;

/* When modified, require C-q ... C-q (KILO_QUIT_TIMES) */
static void editor_quit(void) {
  if (!E.buffer.dirty || !quit_times) exit(0);
  editor_message(
      "WARNING!!! unsaved changes. Press C-q %d more times to quit.",
      quit_times--);
}

/* ========================= Processor =========================== */

static void (*eventHandler[256])(void) = {
  [CTRL_N] = editor_point_next_line,
  [CTRL_P] = editor_point_prev_line,
  [CTRL_F] = editor_point_forward_char,
  [CTRL_B] = editor_point_backward_char,
  [CTRL_D] = editorDelForwardChar,
  [CTRL_L] = buffer_find_file_interactive,
  /* [CTRL_Y] = editorFind, */
  [CTRL_M] = editorInsertNewline,
  [CTRL_H] = editorDelChar,
  [DEL]    = editorDelChar, 
  [CTRL_S] = buffer_write,
  [CTRL_K] = buffer_kill_line_interactive,
  [CTRL_Q] = editor_quit,
};

void editor_process(int c) {
    if (isprint(c)) editorInsertChar(c);
    else if (eventHandler[c] != NULL) eventHandler[c]();
    else editor_message("unknown command. HELP: C-s: save | C-q: quit | C-f: find");
    if (c != CTRL_Q) quit_times = KILO_QUIT_TIMES; /* reset static var */
}
