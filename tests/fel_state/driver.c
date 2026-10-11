/* fel_state_test: the sample core with two states; each sees its own state through every form */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void fst_bind(void *state);
uint64_t fst_state_size(void);
int fst_state_init(void *state);
const char *fst_row_name(int i);
unsigned fst_row_flags(int i);
int fst_row_has_act(int i);
char *fst_label(void);
char *fst_plain(void);
long fst_span(void);
int *fst_lo(void);
int *fst_hi(void);
static int fails;
#define CHECK(c, m) do { if (!(c)) { fails++; printf("FAIL %s\n", m); } } while (0)
static void *aligned16(size_t n, void **raw) {   /* (portable: no posix_memalign on Windows) */
    *raw = malloc(n + 16);
    return *raw ? (void *)(((uintptr_t)*raw + 15) & ~(uintptr_t)15) : NULL;
}
int main(void) {
    void *ra = NULL, *rb = NULL;
    const size_t n = (size_t)fst_state_size();
    void *a = aligned16(n, &ra), *b = aligned16(n, &rb);
    if (!a || !b) return 2;
    CHECK(fst_state_init(a) && fst_state_init(b), "state_init passes its layout check");
    fst_bind(a);
    char *la = fst_label();
    strcpy(la, "LANE A");
    CHECK(fst_row_name(1) == la && !strcmp(fst_row_name(1), "LANE A"), "a constant row's pointer to state is this state's");
    CHECK(!strcmp(fst_row_name(0), "PLAY") && !strcmp(fst_row_name(2), "CLEAR") && fst_row_flags(2) == 2 && fst_row_has_act(0) && !fst_row_has_act(1),
          "the row's other fields as initialized");
    CHECK(fst_plain() == la, "a plain pointer to state");
    const long spanA = fst_span();
    CHECK(spanA == (long)((char *)(fst_hi() + 2) - (char *)(fst_lo() + 1)), "a difference of two state addresses, in this state");
    fst_bind(b);
    char *lb = fst_label();
    strcpy(lb, "LANE B");
    CHECK(lb != la && fst_row_name(1) == lb && !strcmp(fst_row_name(1), "LANE B"), "another state: its own label, its own row");
    CHECK(fst_span() == spanA, "and the same layout");
    fst_bind(a);
    CHECK(!strcmp(fst_row_name(1), "LANE A"), "the first state's untouched");
    printf("%s (%d failed)\n", fails ? "fel_state_test: FAILED" : "fel_state_test: ok", fails);
    free(ra); free(rb);
    return fails != 0;
}
