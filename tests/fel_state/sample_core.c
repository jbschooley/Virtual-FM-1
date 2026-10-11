/* fel_state's own check (CMakeLists.txt fel_state_test): the forms a firmware update brought, each
 * through the rewrite as a core is, then run with two states (driver.c). Not a firmware. */
#define FEL_PRAGMA(x) _Pragma(#x)
#define FEL_SECTIONS(b, d) FEL_PRAGMA(clang section bss = b data = d)
FEL_SECTIONS(FEL_BSS_SECTION, FEL_DATA_SECTION)

typedef struct row { const char *name; void (*act)(void); unsigned char flags; } row_t;
static void act(void) {}
static char label[16];                         /* state: written per instance */
/* a constant table whose rows point at state (Melodee 1.0's sheets: a row's label a buffer) */
static const row_t TABLE[3] = {{"PLAY", act, 0}, {label, 0, 1}, {"CLEAR", act, 2}};
/* an inline name with a comma and brackets beside a pointer to state (c"A,(B)" is one element) */
typedef struct named { char tag[8]; char *buf; } named_t;
static const named_t NAMED[2] = {{"A,(B)", label}, {"[C]{D}", 0}};
static int lo[4], hi[4];                       /* two state arrays */
static char *plain = label;                    /* (the forms handled before: a pointer to state) */

#pragma clang section bss = "" data = ""

const char *fst_row_name(int i) { return TABLE[i].name; }
unsigned fst_row_flags(int i) { return TABLE[i].flags; }
int fst_row_has_act(int i) { return TABLE[i].act != 0; }
char *fst_label(void) { return label; }
const char *fst_named_tag(int i) { return NAMED[i].tag; }
char *fst_named_buf(int i) { return NAMED[i].buf; }
char *fst_plain(void) { return plain; }
/* a difference of two state addresses, which clang folds into a constant expression (Melodee 1.0's
 * phi operand: sub (trunc (sub (ptrtoint ..., ptrtoint ...)))) */
long fst_span(void) { return (long)((char *)&hi[2] - (char *)&lo[1]); }
int *fst_lo(void) { return lo; }
int *fst_hi(void) { return hi; }
