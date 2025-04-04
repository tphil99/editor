#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "structures.h"
#include "highlights.h"

/* =========================== Syntax highlights =========================
 *
 * add new syntax: define array of file-subnames/file-extensions (beginning with .), array of keywords
 *
 * keywords with trailing '|' highlighted in different color
 *
 * add a stanza in HLDB with two two arrays of strings, and a set of flags in order to enable highlighting of
 * comments and numbers.
 *
 * comment chars must come in pairs
 */

char *Python_HL_extensions[] = {".py",NULL};
char *Python_HL_keywords[] = {
  "def", "return", "lambda", 
};

char *C_HL_extensions[] = {".c",".h",".cpp",".hpp",".cc",NULL};
char *C_HL_keywords[] = {
	/* C Keywords */
	"auto","break","case","continue","default","do","else","enum",
	"extern","for","goto","if","register","return","sizeof","static",
	"struct","switch","typedef","union","volatile","while","NULL",

	/* C++ Keywords */
	"alignas","alignof","and","and_eq","asm","bitand","bitor","class",
	"compl","constexpr","const_cast","deltype","delete","dynamic_cast",
	"explicit","export","false","friend","inline","mutable","namespace",
	"new","noexcept","not","not_eq","nullptr","operator","or","or_eq",
	"private","protected","public","reinterpret_cast","static_assert",
	"static_cast","template","this","thread_local","throw","true","try",
	"typeid","typename","virtual","xor","xor_eq",

	/* C types */
        "int|","long|","double|","float|","char|","unsigned|","signed|",
        "void|","short|","auto|","const|","bool|",NULL
};

/* array of syntax highlights by extensions, keywords, comments, flags */
struct editorSyntax HLDB[] = {
  {
        C_HL_extensions,
        C_HL_keywords,
        "//","/*","*/",
	HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS
  },
  {
        Python_HL_extensions,
        Python_HL_keywords,
        "##",
        HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS
  }
};

#define HLDB_ENTRIES (sizeof(HLDB)/sizeof(HLDB[0]))

int is_separator(int c) {
    return c == '\0' || isspace(c) || strchr(",.()+-/*=~%[];",c) != NULL;
}

int editorRowHasOpenComment(struct line *row) {
    if (row->hl && row->rsize && row->hl[row->rsize-1] == HL_MLCOMMENT &&
        (row->rsize < 2 || (row->render[row->rsize-2] != '*' ||
                            row->render[row->rsize-1] != '/'))) return 1;
    return 0;
}

/* update line->hl in light of line->render */
void editorUpdateSyntax(struct line *row) {
    row->hl = realloc(row->hl,row->rsize);
    memset(row->hl,HL_NORMAL,row->rsize);
    if (E.buffer.syntax == NULL) return;

    int i, prev_sep, in_string, in_comment;
    char *p;
    char **keywords = E.buffer.syntax->keywords;
    char *scs = E.buffer.syntax->singleline_comment_start;
    char *mcs = E.buffer.syntax->multiline_comment_start;
    char *mce = E.buffer.syntax->multiline_comment_end;

    /* Point to first non-space char */
    p = row->render;
    i = 0; /* Current char offset */
    while(*p && isspace(*p)) {
        p++;
        i++;
    }
    prev_sep = 1; /* Tell parser if 'i' points to start of word */
    in_string = 0; /* inside "" or '' */
    in_comment = (row->idx > 0 && editorRowHasOpenComment(&E.buffer.lines[row->idx-1]));
    /* inside multi-line comment? */
    while(*p) {
        /* single-line comments */
        if (prev_sep && *p == scs[0] && *(p+1) == scs[1]) {
            memset(row->hl+i,HL_COMMENT,row->size-i);
            return;
        }

        /* multi-line comments */
        if (in_comment) {
            row->hl[i] = HL_MLCOMMENT;
            if (*p == mce[0] && *(p+1) == mce[1]) {
                row->hl[i+1] = HL_MLCOMMENT;
                p += 2; i += 2;
                in_comment = 0;
                prev_sep = 1;
                continue;
            } else {
                prev_sep = 0;
                p++; i++;
                continue;
            }
        } else if (*p == mcs[0] && *(p+1) == mcs[1]) {
            row->hl[i] = HL_MLCOMMENT;
            row->hl[i+1] = HL_MLCOMMENT;
            p += 2; i += 2;
            in_comment = 1;
            prev_sep = 0;
            continue;
        }

        /* Handle "" and '' */
        if (in_string) {
            row->hl[i] = HL_STRING;
            if (*p == '\\') {
                row->hl[i+1] = HL_STRING;
                p += 2; i += 2;
                prev_sep = 0;
                continue;
            }
            if (*p == in_string) in_string = 0;
            p++; i++;
            continue;
        } else {
            if (*p == '"' || *p == '\'') {
                in_string = *p;
                row->hl[i] = HL_STRING;
                p++; i++;
                prev_sep = 0;
                continue;
            }
        }

        if (!isprint(*p)) {
            row->hl[i] = HL_NONPRINT;
            p++; i++;
            prev_sep = 0;
            continue;
        }

        if ((isdigit(*p) && (prev_sep || row->hl[i-1] == HL_NUMBER)) ||
            (*p == '.' && i >0 && row->hl[i-1] == HL_NUMBER)) {
            row->hl[i] = HL_NUMBER;
            p++; i++;
            prev_sep = 0;
            continue;
        }

        /* keywords and lib calls */
        if (prev_sep) {
            int j;
            for (j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen-1] == '|';
                if (kw2) klen--;

                if (!memcmp(p,keywords[j],klen) &&
                    is_separator(*(p+klen)))
                {
                    /* Keyword */
                    memset(row->hl+i,kw2 ? HL_KEYWORD2 : HL_KEYWORD1,klen);
                    p += klen;
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) {
                prev_sep = 0;
                continue; /* We had a keyword match */
            }
        }

        prev_sep = is_separator(*p);
        p++; i++;
    }

    /* Propagate syntax change to next row if open comment state changed.
       may recursively affect all following rows */
    int oc = editorRowHasOpenComment(row);
    if (row->hl_oc != oc && row->idx+1 < E.buffer.numlines)
        editorUpdateSyntax(&E.buffer.lines[row->idx+1]);
    row->hl_oc = oc;
}

int editorSyntaxToColor(int hl) {
    switch(hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return 36;     /* point.rowan */
    case HL_KEYWORD1: return 33;      /* yellow */
    case HL_KEYWORD2: return 32;      /* green */
    case HL_STRING: return 35;        /* magenta */
    case HL_NUMBER: return 31;        /* red */
    case HL_MATCH: return 34;         /* blu */
    default: return 37;               /* white */
    }
}

void editorSelectSyntaxHighlight(char *filename) {
    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax *s = HLDB+j;
        unsigned int i = 0;
        while(s->filematch[i]) {
            char *p;
            int patlen = strlen(s->filematch[i]);
            if ((p = strstr(filename,s->filematch[i])) != NULL) {
                if (s->filematch[i][0] != '.' || p[patlen] == '\0') {
                    E.buffer.syntax = s;
                    return;
                }
            }
            i++;
        }
    }
}
