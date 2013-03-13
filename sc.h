// SC is free software distributed under the MIT license

#define _GNU_SOURCE
#include "config.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <curses.h>
#include <sys/types.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#define	ATBL(tbl, row, col)	(*(tbl + row) + (col))

#define MINROWS		100 	// minimum size at startup
#define MINCOLS		30 
#define	ABSMAXCOLS	702	// absolute cols: ZZ (base 26)

#define CRROWS		1
#define CRCOLS		2
#define RESROW		3	// rows reserved for prompt, error, and column numbers

// formats for engformat()
#define REFMTFIX	0
#define REFMTFLT	1
#define REFMTENG	2
#define REFMTDATE	3
#define REFMTLDATE	4

#define DEFWIDTH	10	// Default column width and precision
#define DEFPREC		2
#define DEFREFMT	REFMTFIX// Make default format fixed point  THA 10/14/90

#define FKEYS		24	// Number of function keys available
#define HISTLEN		100	// Number of history entries for vi emulation
#define CPAIRS		8	// Number of color pairs available
#define COLFORMATS	10	// Number of custom column formats
#define DELBUFSIZE	40	// Number of named buffers + 4
#ifdef PSC
# define error(msg)	fprintf(stderr, msg);
#else
# define error(...)	do { if (isatty(STDOUT_FILENO)) { move(1,0); clrtoeol(); printw(__VA_ARGS__); } else fprintf (stderr, __VA_ARGS__); } while (0)
#endif
#define	FBUFLEN		1024	// buffer size for a single field
#define	PATHLEN		1024	// maximum path length

#ifndef DFLT_PAGER
#define	DFLT_PAGER	"more"	// more is probably more widespread than less
#endif

#define MAXCMD		160	// for ! command and commands that use the pager


#ifndef A_CHARTEXT		// Should be defined in curses.h
#define A_CHARTEXT	0xff
#endif

#ifndef color_set
#define color_set(c, o)		attron(COLOR_PAIR(c))
#endif

#if !defined(attr_get) || defined(NCURSES_VERSION) && NCURSES_VERSION_MAJOR < 5
#undef attr_get
#define attr_get(a, p, o)	(((a) != 0 && (*(a) = stdscr->_attrs)), \
				((p) != 0 && \
				(*(p) = PAIR_NUMBER(stdscr->_attrs))), OK)
#endif

#if (defined(BSD42) || defined(BSD43)) && !defined(strrchr)
#define strrchr rindex
#endif

#if (defined(BSD42) || defined(BSD43)) && !defined(strchr)
#define strchr index
#endif

#ifndef FALSE
# define	FALSE	0
# define	TRUE	1
#endif // !FALSE

//
// ent_ptr holds the row/col # and address type of a cell
//
// vf is the type of cell address, 0 non-fixed, or bitwise OR of FIX_ROW or
//	FIX_COL
// vp : we just use vp->row or vp->col, vp may be a new cell just for holding
//	row/col (say in gram.y) or a pointer to an existing cell
struct ent_ptr {
    int vf;
    struct ent *vp;
};

// holds the beginning/ending cells of a range
struct range_s {
    struct ent_ptr left, right;
};

//
// Some not too obvious things about the flags:
//    is_valid means there is a valid number in v.
//    is_locked means that the cell cannot be edited.
//    is_label set means it points to a valid constant string.
//    is_strexpr set means expr yields a string expression.
//    If is_strexpr is not set, and expr points to an expression tree, the
//        expression yields a numeric expression.
//    So, either v or label can be set to a constant. 
//        Either (but not both at the same time) can be set from an expression.

#define VALID_CELL(p, r, c) ((p = *ATBL(tbl, r, c)) && \
			     ((p->flags & is_valid) || p->label))

// info for each cell, only alloc'd when something is stored in a cell
struct ent {
    double v;			// v && label are set in EvalAll()
    char *label;
    struct enode *expr;		// cell's contents
    short flags;
    short row, col;
    short nrow, ncol;		// link to note
    short nlastrow, nlastcol;
    struct ent *next;		// next deleted ent (pulled, deleted cells)
    char *format;		// printf format for this cell
    char cellerror;		// error in a cell?
};

#define FIX_ROW 1
#define FIX_COL 2

// stores type of operation this cell will perform
struct enode {
    int op;
    union {
	int gram_match;         // some compilers (hp9000ipc) need this
	double k;		// constant #
	struct ent_ptr v;	// ref. another cell
	struct range_s r;	// op is on a range
	char *s;		// string part of a cell
	struct {		// other cells use to eval()/seval()
	    struct enode *left, *right;
	    char *s;		// previous value of @ext function in case
	} o;			//	external functions are turned off
    } e;
};

// stores a range (left, right)
struct range {
    struct ent_ptr r_left, r_right;
    char *r_name;			// possible name for this range
    struct range *r_next, *r_prev;	// chained ranges
    int r_is_range;
};

// stores a framed range (left, right)
struct frange {
    struct ent *or_left, *or_right;	// outer range
    struct ent *ir_left, *ir_right;	// inner range
    struct frange *r_next, *r_prev;	// chained ranges
};

// stores a color range (left, right)
struct crange {
    struct ent *r_left, *r_right;
    int r_color;
    struct crange *r_next, *r_prev;	// chained ranges
};

struct colorpair {
    int fg;
    int bg;
    struct enode *expr;
};

// stores an abbreviation and its expansion
struct abbrev {
    char *abbr;
    char *exp;
    struct abbrev *a_next, *a_prev;
};

struct impexfilt {
    char ext[PATHLEN];
    char plugin[PATHLEN];
    char type;
    struct impexfilt *next;
};

// Use this structure to save the last 'g' command
struct go_save {
    int		g_type;
    double	g_n;
    char	*g_s;
    int		g_row;
    int		g_col;
    int		g_lastrow;
    int		g_lastcol;
    int		strow;
    int		stcol;
    int		stflag;
    int		errsearch;
};

// op values
#define O_VAR		'v'
#define O_CONST		'k'
#define O_ECONST	'E'	// constant cell w/ an error
#define O_SCONST	'$'
#define REDUCE		0200	// Or'ed into OP if operand is a range

#define OP_BASE		256
#define ACOS		(OP_BASE + 0)
#define ASIN		(OP_BASE + 1)
#define ATAN		(OP_BASE + 2)
#define CEIL		(OP_BASE + 3)
#define COS		(OP_BASE + 4)
#define EXP		(OP_BASE + 5)
#define FABS		(OP_BASE + 6)
#define FLOOR		(OP_BASE + 7)
#define HYPOT		(OP_BASE + 8)
#define LOG		(OP_BASE + 9)
#define LOG10		(OP_BASE + 10)
#define POW		(OP_BASE + 11)
#define SIN		(OP_BASE + 12)
#define SQRT		(OP_BASE + 13)
#define TAN		(OP_BASE + 14)
#define DTR		(OP_BASE + 15)
#define RTD		(OP_BASE + 16)
#define SUM		(OP_BASE + 17)
#define PROD		(OP_BASE + 18)
#define AVG		(OP_BASE + 19)
#define COUNT		(OP_BASE + 20)
#define STDDEV		(OP_BASE + 21)
#define MAX		(OP_BASE + 22)
#define MIN		(OP_BASE + 23)
#define RND		(OP_BASE + 24)
#define HOUR		(OP_BASE + 25)
#define MINUTE		(OP_BASE + 26)
#define SECOND		(OP_BASE + 27)
#define MONTH		(OP_BASE + 28)
#define DAY		(OP_BASE + 29)
#define YEAR		(OP_BASE + 30)
#define NOW		(OP_BASE + 31)
#define DATE		(OP_BASE + 32)
#define FMT		(OP_BASE + 33)
#define SUBSTR		(OP_BASE + 34)
#define STON		(OP_BASE + 35)
#define EQS		(OP_BASE + 36)
#define EXT		(OP_BASE + 37)
#define ELIST		(OP_BASE + 38)	// List of expressions
#define LMAX 		(OP_BASE + 39)
#define LMIN 		(OP_BASE + 40)
#define NVAL		(OP_BASE + 41)
#define SVAL		(OP_BASE + 42)
#define PV		(OP_BASE + 43)
#define FV		(OP_BASE + 44)
#define PMT		(OP_BASE + 45)
#define STINDEX		(OP_BASE + 46)
#define LOOKUP		(OP_BASE + 47)
#define ATAN2		(OP_BASE + 48)
#define INDEX		(OP_BASE + 49)
#define DTS		(OP_BASE + 50)
#define TTS		(OP_BASE + 51)
#define ABS		(OP_BASE + 52)
#define HLOOKUP		(OP_BASE + 53)
#define VLOOKUP		(OP_BASE + 54)
#define ROUND		(OP_BASE + 55)
#define IF		(OP_BASE + 56)
#define FILENAME	(OP_BASE + 57)
#define MYROW		(OP_BASE + 58)
#define MYCOL		(OP_BASE + 59)
#define LASTROW		(OP_BASE + 60)
#define LASTCOL		(OP_BASE + 61)
#define COLTOA		(OP_BASE + 62)
#define UPPER		(OP_BASE + 63)
#define LOWER		(OP_BASE + 64)
#define CAPITAL		(OP_BASE + 65)
#define NUMITER		(OP_BASE + 66)
#define ERR_		(OP_BASE + 67)
#define PI_		(OP_BASE + 68)
#define BLACK		(OP_BASE + 69)
#define RED		(OP_BASE + 70)
#define GREEN		(OP_BASE + 71)
#define YELLOW		(OP_BASE + 72)
#define BLUE		(OP_BASE + 73)
#define MAGENTA		(OP_BASE + 74)
#define CYAN		(OP_BASE + 75)
#define WHITE		(OP_BASE + 76)

// flag values
#define is_valid    	0001
#define is_changed  	0002
#define is_strexpr  	0004
#define is_leftflush	0010
#define is_deleted  	0020
#define is_locked   	0040
#define is_label    	0100
#define is_cell_cleared 0200
#define may_sync	0400

// cell error (1st generation (ERROR) or 2nd+ (INVALID))
#define	CELLOK		0
#define	CELLERROR	1
#define	CELLINVALID	2

#define ctl(c)		((c)&037)
#define ESC		033
#define DEL		0177

// calculation order
#define BYCOLS		1
#define BYROWS		2

// values for showrange for ranges of rows or columns
#define SHOWROWS	2
#define SHOWCOLS	3

// tblprint style output for:
#define	TBL		1		// 'tbl'
#define	LATEX		2		// 'LaTeX'
#define	TEX		3		// 'TeX'
#define	SLATEX		4		// 'SLaTeX' (Scandinavian LaTeX)
#define	FRAME		5		// tblprint style output for FrameMaker

// Types for etype()
#define NUM		1
#define STR		2

#define	GROWAMT		30	// default minimum amount to grow

#define	GROWNEW		1	// first time table
#define	GROWROW		2	// add rows
#define	GROWCOL		3	// add columns
#define	GROWBOTH	4	// grow both

extern struct ent*** tbl;	// data table ref. in vmtbl.c and ATBL()
extern char curfile[];
extern int strow, stcol;
extern int currow, curcol;
extern int gmyrow, gmycol;	// globals used for @myrow, @mycol cmds
extern int rescol;		// columns reserved for row numbers
extern int savedrow[37], savedcol[37];
extern int savedstrow[37], savedstcol[37];
extern int FullUpdate;
extern int maxrow, maxcol;
extern int maxrows, maxcols;	// # cells currently allocated
extern int rowsinrange;	// Number of rows in target range of a goto
extern int colsinrange;	// Number of cols in target range of a goto
extern int *fwidth;
extern int *precision;
extern int *realfmt;
extern char *colformat[10];
extern char *col_hidden;
extern char *row_hidden;
extern char line[FBUFLEN];
extern int linelim;
extern int changed;
extern struct ent *delbuf[DELBUFSIZE];
extern char *delbuffmt[DELBUFSIZE];
extern int dbidx;
extern int qbuf;		// buffer no. specified by `"' command
extern int showsc, showsr;
extern int showrange;		// Causes ranges to be highlighted
extern int cellassign;
extern int macrofd;
extern int cslop;
extern int usecurses;
extern int brokenpipe;		// Set to true if SIGPIPE is received
extern char dpoint;	// country-dependent decimal point from locale
extern char thsep;	// country-dependent thousands separator from locale

void add_abbr (char *string);
FILE *openfile (char *fname, int *rpid, int *rfd);
char *coltoa (int col);
char *findhome (char *path);
char *r_name (int r1, int c1, int r2, int c2);
char *scxmalloc (unsigned n);
char *scxrealloc (char *ptr, unsigned n);
char* seval (struct enode *se);
char *v_name (int row, int col);
double eval (struct enode *e);
int any_locked_cells (int r1, int c1, int r2, int c2);
int are_colors (void);
int are_frames (void);
int are_ranges (void);
int atocol (char *string, int len);
bool engformat (int fmt, int width, int lprecision, double val, char *buf, int buflen);
int etype (struct enode *e);
int find_range (char *name, int len, struct ent *lmatch, struct ent *rmatch, struct range **rng);
bool format (const char *fmt, int lprecision, double val, char *buf, unsigned buflen);
int get_rcqual (int ch);
int growtbl (int rowcol, int toprow, int topcol);
void list_frames (FILE *f);
int locked_cell (int r, int c);
int modcheck (char *endstr);
int nmgetch (void);
int plugin_exists (char *name, int len, char *path);
int readfile (char *fname, int eraseflg);
int writefile (char *fname, int r0, int c0, int rn, int cn);
int yn_ask (char *msg);
struct abbrev *find_abbr (char *abbrev, int len, struct abbrev **prev);
struct colorpair *cpairs[8];
struct enode *new (int op, struct enode *a1, struct enode *a2);
struct enode *new_const (int op, double a1);
struct enode *new_range (int op, struct range_s a1);
struct enode *new_str (char *s);
struct enode *new_var (int op, struct ent_ptr a1);
struct ent *lookat (int row, int col);
struct crange *find_crange (int row, int col);
struct frange *find_frange (int row, int col);
void EvalAll (void);
void add_crange (struct ent *r_left, struct ent *r_right, int pair);
void add_frange (struct ent *or_left, struct ent *or_right, struct ent *ir_left, struct ent *ir_right, int toprows, int bottomrows, int leftcols, int rightcols);
void add_range (char *name, struct ent_ptr left, struct ent_ptr right, int is_range);
void addplugin (char *ext, char *plugin, char type);
void backcol (int arg);
void backrow (int arg);
void change_color (int pair, struct enode *e);
void checkbounds (int *rowp, int *colp);
void clearent (struct ent *v);
void clean_crange (void);
void clean_frange (void);
void clean_range (void);
void closecol (int arg);
void closefile (FILE *f, int pid, int rfd);
void colshow_op (void);
void copy (struct ent *dv1, struct ent *dv2, struct ent *v1, struct ent *v2);
void copyent (struct ent *n, struct ent *p, int dr, int dc, int r1, int c1, int r2, int c2, int transpose);
void decompile (struct enode *e, int priority);
void deleterow (int arg);
void del_range (struct ent *left, struct ent *right);
void del_abbr (char *abbrev);
void deraw (int ClearLastLine);
void doend (int rowinc, int colinc);
void doformat (int c1, int c2, int w, int p, int r);
void dupcol (void);
void duprow (void);
void doquery (char *s, char *data, int fd);
void dostat (int fd);
void dotick (int tick);
void doeval (struct enode *e, char *fmt, int row, int col, int fd);
void doseval (struct enode *e, int row, int col, int fd);
void dogetkey (void);
void doshell (void);
void gohome (void);
void leftlimit (void);
void rightlimit (void);
void gototop (void);
void gotobottom (void);
void editexp (int row, int col);
void editfmt (int row, int col);
void edit_mode (void);
void edits (int row, int col);
void editv (int row, int col);
void efree (struct enode *e);
void erase_area (int sr, int sc, int er, int ec, int ignorelock);
void erasedb (void);
void eraser (struct ent *v1, struct ent *v2);
void fgetnum (int r0, int c0, int rn, int cn, int fd);
void fill (struct ent *v1, struct ent *v2, double start, double inc);
void fix_colors (int row1, int col1, int row2, int col2, int delta1, int delta2);
void fix_frames (int row1, int col1, int row2, int col2, int delta1, int delta2);
void fix_ranges (int row1, int col1, int row2, int col2, int delta1, int delta2);
void flush_saved (void);
void formatcol (int arg);
void format_cell (struct ent *v1, struct ent *v2, char *s);
void forwcol (int arg);
void forwrow (int arg);
void free_ent (struct ent *p, int unlock);
void getexp (int r0, int c0, int rn, int cn, int fd);
void getfmt (int r0, int c0, int rn, int cn, int fd);
void getformat (int col, int fd);
void getframe (int fd);
void getrange (char *name, int fd);
void getnum (int r0, int c0, int rn, int cn, int fd);
void getstring (int r0, int c0, int rn, int cn, int fd);
void go_last (void);
void goraw (void);
void gotonote (void);
void help (void);
void hide_col (int arg);
void hide_row (int arg);
void hidecol (int arg);
void hiderow (int arg);
void initcolor (int colornum);
void initkbd (void);
void ins_in_line (int c);
void ins_string (char *s);
void insert_mode (void);
void insertcol (int arg, int delta);
void insertrow (int arg, int delta);
void kbd_again (void);
void label (struct ent *v, char *s, int flushdir);
void let (struct ent *v, struct enode *e);
void list_colors (FILE *f);
void list_ranges (FILE *f);
void ljustify (int sr, int sc, int er, int ec);
void rjustify (int sr, int sc, int er, int ec);
void center (int sr, int sc, int er, int ec);
void lock_cells (struct ent *v1, struct ent *v2);
void markcell (void);
void move_area (int dr, int dc, int sr, int sc, int er, int ec);
void mover (struct ent *d, struct ent *v1, struct ent *v2);
void moveto (int row, int col, int lastrow, int lastcol, int cornrow, int corncol);
void toggle_navigate_mode (void);
void num_search (double n, int firstrow, int firstcol, int lastrow, int lastcol, int errsearch);
void printfile (char *fname, int r0, int c0, int rn, int cn);
void pullcells (int to_insert);
void query (char *s, char *data);
void read_hist (void);
void remember (int save);
void repaint (int x, int y, int len, int attron, int attroff);
void resetkbd (void);
void rowshow_op (void);
void scxfree (char *p);
void setauto (int i);
void setiterations (int i);
void setorder (int i);
void showcol (int c1, int c2);
void showdr (void);
void showrow (int r1, int r2);
void showstring (char *string, int dirflush, int hasvalue, int row, int col, int *nextcolp, int mxcol, int *fieldlenp, int r, int c, struct frange *fr, int frightcols, int flcols, int frcols);
void slet (struct ent *v, struct enode *se, int flushdir);
void sortrange (struct ent *left, struct ent *right, char *criteria);
void startshow (void);
void startdisp (void);
void stopdisp (void);
void str_search (char *s, int firstrow, int firstcol, int lastrow, int lastcol, int num);
void sync_cranges (void);
void sync_franges (void);
void sync_ranges (void);
void sync_refs (void);
void tblprintfile (char *fname, int r0, int c0, int rn, int cn);
void unlock_cells (struct ent *v1, struct ent *v2);
void update (int anychanged);
void valueize_area (int sr, int sc, int er, int ec);
void write_cells (FILE *f, int r0, int c0, int rn, int cn, int dr, int dc);
void write_colors (FILE *f, int indent);
void write_cranges (FILE *f);
void write_fd (FILE *f, int r0, int c0, int rn, int cn);
void write_franges (FILE *f);
void write_hist (void);
void write_line (int c);
void write_ranges (FILE *f);
void yank_area (int sr, int sc, int er, int ec);
void yankr (struct ent *v1, struct ent *v2);
void yankcol (int arg);
void yankrow (int arg);
void yyerror (const char *err);
int yylex (void);
int yyparse (void);

extern int modflg;
extern char *mdir;
extern char *autorun;
extern int skipautorun;
extern char *fkey[FKEYS];
extern char *scext;
extern char *ascext;
extern char *tbl0ext;
extern char *tblext;
extern char *latexext;
extern char *slatexext;
extern char *texext;
extern int scrc;
extern double prescale;
extern int extfunc;
extern int propagation;
extern int repct;
extern int calc_order;
extern int autocalc;
extern int autolabel;
extern int autoinsert;
extern int autowrap;
extern int optimize;
extern int numeric;
extern int showcell;
extern int showtop;
extern int color;
extern int colorneg;
extern int colorerr;
extern int braille;
extern int braillealt;
extern int loading;
extern int getrcqual;
extern int tbl_style;
extern int rndtoeven;
extern char *progname;
extern int numeric_field;
extern int craction;
extern int pagesize;	// If nonzero, use instead of 1/2 screen height
extern int rowlimit;
extern int collimit;

#if BSD42 || SYSIII

#ifndef cbreak
#define	cbreak		crmode
#define	nocbreak	nocrmode
#endif

#endif

#if defined(BSD42) || defined(BSD43) && !defined(ultrix)
#define	memcpy(dest, source, len)	bcopy(source, dest, (unsigned int)len);
#define	memset(dest, zero, len)		bzero((dest), (unsigned int)(len));
#else
#include <memory.h>
#endif
