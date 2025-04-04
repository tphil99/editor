
struct editorSyntax {
    char **filematch;
    char **keywords;
    char singleline_comment_start[2];
    char multiline_comment_start[3];
    char multiline_comment_end[3];
    int flags;
};
typedef struct hlcolor {
    int r,g,b;
} hlcolor;

struct line {
    int idx;            /* index of line in file */
    int size;           /* line length, excl \0 */
    int rsize;          /* sizeof rendered line */
    char *chars;        /* contents */
    char *render;       /* rendered contents eg. TABs expanded */
    unsigned char *hl;  /* Syntactic type of corresponding char in render: uses DEFINES */
    int hl_oc;          /* line ends with open comment */
};			/* line of file */

struct point {
  int row;
  int col;
};

struct buffer {
    struct point point;    
    struct line *lines;      /* row renamed to lines */
    int numlines;	     /* numrows renamed to numlines */
    int dirty;      /* file modified */
    char *filename;
    struct editorSyntax *syntax;    /* Current syntax highlight, or NULL. */
    struct point offset; /* imagine entire buffer displayed, but top-left of screen is at offest */
};
struct terminal {
    struct point winsize;
    int rawmode;    /* terminal in raw mode? */
};
struct editor {
    struct buffer buffer;
    struct terminal terminal;
    char statusmsg[80];
    time_t statusmsg_time;
};
extern struct editor E;

enum SPECIAL_KEY {
        KEY_NULL = 0,   
        CTRL_B = 2,        
        CTRL_C = 3,    
        CTRL_D = 4,    
        CTRL_F = 6,    
        CTRL_H = 8,    
        TAB = 9,       
        CTRL_L = 12,   
        CTRL_M = 13,   
        CTRL_N = 14,   
        CTRL_Y = 25,   
        CTRL_K = 11,   
        CTRL_P = 16,        
        META_F = 230,        
        CTRL_Q = 17,   
        CTRL_S = 19,   
        CTRL_U = 21,   
        ESC = 27,      
        DEL =  127,
        /* soft codes, not really reported by terminal */
        HOME_KEY = 1005,
        END_KEY,
        PAGE_UP,
        PAGE_DOWN
};
