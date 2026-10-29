/*
 * Regular expressions with the semantics of Python's re module in ASCII mode.
 *
 * The parser follows the rules of CPython's sre_parse, including which
 * patterns are errors. The pattern compiles to a small program that runs on a
 * backtracking machine with an explicit stack, so long subjects never exhaust
 * the C stack. Repetition follows sre's REPEAT/MAX_UNTIL/MIN_UNTIL logic,
 * including its protection against empty iterations.
 */
#include "rx.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ AST */

enum { N_EMPTY, N_CHAR, N_ANY, N_CLASS, N_AT, N_SEQ, N_ALT, N_GROUP, N_REPEAT, N_REF, N_LOOK, N_ATOMIC, N_COND };

enum { AT_BEG, AT_BEG_LINE, AT_BEG_STR, AT_END, AT_END_LINE, AT_END_STR, AT_BOUND, AT_NONBOUND };

enum { REP_GREEDY, REP_LAZY, REP_POSSESSIVE };
enum { LOOK_NEG = 1, LOOK_BEHIND = 2 };

typedef struct Node Node;
struct Node {
    int type;
    int icase;
    int dotall;
    u32 ch;
    int cls;
    int at;
    int group;
    long min, max; /* max < 0: unbounded */
    int mode;
    Node **kids;
    int nkids, cap;
};

/* Character class: literals and ranges plus category escapes. */
enum { CAT_D = 1, CAT_ND = 2, CAT_W = 4, CAT_NW = 8, CAT_S = 16, CAT_NS = 32 };

typedef struct {
    u32 *lo, *hi;
    int n, cap;
    int cats;
    int neg;
} Class;

/* ------------------------------------------------------------------ program */

enum {
    I_CHAR, I_CHARI, I_ANY, I_ANYALL, I_CLASS, I_ASSERT, I_SAVE, I_SPLIT, I_JMP, I_BACKREF,
    I_STAR1, I_REPINIT, I_REPUNTIL, I_REPLAZY, I_LOOK, I_ATOMIC, I_COND, I_SUCCEED, I_MATCH
};

typedef struct {
    int op;
    int icase;
    long a, b, c;
    u32 ch;
} Inst;

typedef struct {
    long min, max;
    int lazy;
    int body, exit, lazyiter;
} Rep;

typedef struct {
    u32 *name;
    int len;
    int index;
} Name;

struct Regex {
    Inst *prog;
    int ninst, icap;
    Class *classes;
    int nclasses, ccap;
    Rep *reps;
    int nreps, rcap;
    int ngroups;
    Name *names;
    int nnames;
};

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) abort();
    return p;
}

void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) abort();
    return p;
}

/* ------------------------------------------------------------------ character tests */

static int is_digit(u32 c) { return c >= '0' && c <= '9'; }
static int is_alpha(u32 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_word(u32 c) { return is_alpha(c) || is_digit(c) || c == '_'; }
static int is_space(u32 c) { return c == ' ' || (c >= 9 && c <= 13); }
static int is_oct(u32 c) { return c >= '0' && c <= '7'; }
static int is_hex(u32 c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static u32 lower(u32 c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static u32 swapcase(u32 c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}
static int hexval(u32 c) {
    if (is_digit(c)) return c - '0';
    return (int)lower(c) - 'a' + 10;
}

static int class_has(const Class *cl, u32 c) {
    for (int i = 0; i < cl->n; i++)
        if (c >= cl->lo[i] && c <= cl->hi[i]) return 1;
    if (cl->cats) {
        if ((cl->cats & CAT_D) && is_digit(c)) return 1;
        if ((cl->cats & CAT_ND) && !is_digit(c)) return 1;
        if ((cl->cats & CAT_W) && is_word(c)) return 1;
        if ((cl->cats & CAT_NW) && !is_word(c)) return 1;
        if ((cl->cats & CAT_S) && is_space(c)) return 1;
        if ((cl->cats & CAT_NS) && !is_space(c)) return 1;
    }
    return 0;
}

static int class_match(const Class *cl, u32 c, int icase) {
    int in = class_has(cl, c);
    if (!in && icase && is_alpha(c)) in = class_has(cl, swapcase(c));
    return in != cl->neg;
}

/* ------------------------------------------------------------------ parser */

typedef struct {
    const u32 *p;
    int n, pos;
    int groups; /* groups opened so far, plus one (as in sre) */
    char *closed;
    long *gw_lo, *gw_hi;
    int gcap;
    Name *names;
    int nnames, ncap;
    long *condrefs;
    int ncond, condcap;
    int gflags; /* global flags from (?ims) at the start */
    int lookbehind; /* groups opened before the outermost lookbehind being parsed, or -1 */
    Regex *re;
    jmp_buf jb;
    const char *err;
} P;

static void fail(P *p, const char *msg) {
    p->err = msg;
    longjmp(p->jb, 1);
}

static Node *node(int type) {
    Node *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->type = type;
    return n;
}

static void add_kid(Node *n, Node *k) {
    if (n->nkids == n->cap) {
        n->cap = n->cap ? n->cap * 2 : 4;
        n->kids = xrealloc(n->kids, n->cap * sizeof *n->kids);
    }
    n->kids[n->nkids++] = k;
}

static int at_end(P *p) { return p->pos >= p->n; }
static u32 peek(P *p) { return p->p[p->pos]; }
static int match_ch(P *p, u32 c) {
    if (!at_end(p) && peek(p) == c) {
        p->pos++;
        return 1;
    }
    return 0;
}

static int new_class(Regex *re) {
    if (re->nclasses == re->ccap) {
        re->ccap = re->ccap ? re->ccap * 2 : 8;
        re->classes = xrealloc(re->classes, re->ccap * sizeof *re->classes);
    }
    memset(&re->classes[re->nclasses], 0, sizeof(Class));
    return re->nclasses++;
}

static void class_add(Class *cl, u32 lo, u32 hi) {
    if (cl->n == cl->cap) {
        cl->cap = cl->cap ? cl->cap * 2 : 4;
        cl->lo = xrealloc(cl->lo, cl->cap * sizeof(u32));
        cl->hi = xrealloc(cl->hi, cl->cap * sizeof(u32));
    }
    cl->lo[cl->n] = lo;
    cl->hi[cl->n] = hi;
    cl->n++;
}

static int cat_of(u32 c) {
    switch (c) {
    case 'd': return CAT_D;
    case 'D': return CAT_ND;
    case 'w': return CAT_W;
    case 'W': return CAT_NW;
    case 's': return CAT_S;
    case 'S': return CAT_NS;
    }
    return 0;
}

static Node *char_node(u32 c, int flags) {
    Node *n = node(N_CHAR);
    n->ch = c;
    n->icase = (flags & FL_I) != 0;
    return n;
}

static Node *cat_node(P *p, int cat, int flags) {
    Node *n = node(N_CLASS);
    n->cls = new_class(p->re);
    p->re->classes[n->cls].cats = cat;
    n->icase = (flags & FL_I) != 0;
    return n;
}

static long read_fixed_hex(P *p, int count) {
    long v = 0;
    for (int i = 0; i < count; i++) {
        if (at_end(p) || !is_hex(peek(p))) fail(p, "incomplete escape");
        v = v * 16 + hexval(p->p[p->pos++]);
    }
    return v;
}

/* Escapes shared by both contexts: \a \f \n \r \t \v \\ \x \u \U. Returns -1 if c is none of them. */
static long simple_escape(P *p, u32 c) {
    switch (c) {
    case 'a': return 7;
    case 'f': return 12;
    case 'n': return 10;
    case 'r': return 13;
    case 't': return 9;
    case 'v': return 11;
    case '\\': return '\\';
    case 'x': return read_fixed_hex(p, 2);
    case 'u': return read_fixed_hex(p, 4);
    case 'U': {
        long v = read_fixed_hex(p, 8);
        if (v > 0x10FFFF) fail(p, "bad escape");
        return v;
    }
    case 'N': fail(p, "named character escapes are not supported");
    }
    return -1;
}

typedef struct {
    int kind; /* 0 literal, 1 category */
    u32 ch;
    int cat;
} ClassItem;

static ClassItem class_escape(P *p) {
    ClassItem it = {0, 0, 0};
    if (at_end(p)) fail(p, "bad escape (end of pattern)");
    u32 c = p->p[p->pos++];
    if (c == 'b') {
        it.ch = 8;
        return it;
    }
    if (cat_of(c)) {
        it.kind = 1;
        it.cat = cat_of(c);
        return it;
    }
    long v = simple_escape(p, c);
    if (v >= 0) {
        it.ch = (u32)v;
        return it;
    }
    if (is_oct(c)) {
        long o = c - '0';
        for (int i = 0; i < 2 && !at_end(p) && is_oct(peek(p)); i++) o = o * 8 + (p->p[p->pos++] - '0');
        if (o > 0377) fail(p, "octal escape value outside of range 0-0o377");
        it.ch = (u32)o;
        return it;
    }
    if (is_digit(c) || is_alpha(c)) fail(p, "bad escape");
    it.ch = c;
    return it;
}

static Node *parse_class(P *p, int flags) {
    int ci = new_class(p->re);
    int empty = 1;
    int neg = match_ch(p, '^');
    for (;;) {
        if (at_end(p)) fail(p, "unterminated character set");
        u32 c = p->p[p->pos++];
        if (c == ']' && !empty) break;
        ClassItem a;
        if (c == '\\') {
            a = class_escape(p);
        } else {
            a.kind = 0;
            a.ch = c;
            a.cat = 0;
        }
        Class *cl = &p->re->classes[ci];
        if (match_ch(p, '-')) {
            if (at_end(p)) fail(p, "unterminated character set");
            u32 d = p->p[p->pos++];
            if (d == ']') {
                if (a.kind) cl->cats |= a.cat;
                else class_add(cl, a.ch, a.ch);
                class_add(cl, '-', '-');
                break;
            }
            ClassItem b;
            if (d == '\\') {
                b = class_escape(p);
            } else {
                b.kind = 0;
                b.ch = d;
                b.cat = 0;
            }
            cl = &p->re->classes[ci];
            if (a.kind || b.kind || b.ch < a.ch) fail(p, "bad character range");
            class_add(cl, a.ch, b.ch);
        } else if (a.kind) {
            cl->cats |= a.cat;
        } else {
            class_add(cl, a.ch, a.ch);
        }
        empty = 0;
    }
    p->re->classes[ci].neg = neg;
    Node *n = node(N_CLASS);
    n->cls = ci;
    n->icase = (flags & FL_I) != 0;
    return n;
}

static void ensure_groups(P *p, int g) {
    if (g < p->gcap) return;
    int nc = p->gcap ? p->gcap : 16;
    while (nc <= g) nc *= 2;
    p->closed = xrealloc(p->closed, nc);
    p->gw_lo = xrealloc(p->gw_lo, nc * sizeof(long));
    p->gw_hi = xrealloc(p->gw_hi, nc * sizeof(long));
    memset(p->closed + p->gcap, 0, nc - p->gcap);
    p->gcap = nc;
}

/* sre's checklookbehindgroup: inside a lookbehind, a reference may only use
 * groups that were complete before the lookbehind started. */
static void check_lookbehind_ref(P *p, long g) {
    if (p->lookbehind < 0) return;
    if (g >= p->groups || !p->closed[g]) fail(p, "cannot refer to an open group");
    if (g >= p->lookbehind) fail(p, "cannot refer to group defined in the same lookbehind subpattern");
}

static Node *ref_node(P *p, long g, int flags) {
    if (g >= p->groups) fail(p, "invalid group reference");
    if (!p->closed[g]) fail(p, "cannot refer to an open group");
    check_lookbehind_ref(p, g);
    Node *n = node(N_REF);
    n->group = (int)g;
    n->icase = (flags & FL_I) != 0;
    return n;
}

static Node *at_node(int at) {
    Node *n = node(N_AT);
    n->at = at;
    return n;
}

static Node *parse_escape(P *p, int flags) {
    if (at_end(p)) fail(p, "bad escape (end of pattern)");
    u32 c = p->p[p->pos++];
    switch (c) {
    case 'A': return at_node(AT_BEG_STR);
    case 'Z': return at_node(AT_END_STR);
    case 'b': return at_node(AT_BOUND);
    case 'B': return at_node(AT_NONBOUND);
    }
    if (cat_of(c)) return cat_node(p, cat_of(c), flags);
    long v = simple_escape(p, c);
    if (v >= 0) return char_node((u32)v, flags);
    if (c == '0') {
        long o = 0;
        for (int i = 0; i < 2 && !at_end(p) && is_oct(peek(p)); i++) o = o * 8 + (p->p[p->pos++] - '0');
        return char_node((u32)o, flags);
    }
    if (is_digit(c)) {
        long g = c - '0';
        if (!at_end(p) && is_digit(peek(p))) {
            u32 d = p->p[p->pos++];
            if (is_oct(c) && is_oct(d) && !at_end(p) && is_oct(peek(p))) {
                long o = (c - '0') * 64 + (d - '0') * 8 + (p->p[p->pos++] - '0');
                if (o > 0377) fail(p, "octal escape value outside of range 0-0o377");
                return char_node((u32)o, flags);
            }
            g = g * 10 + (d - '0');
        }
        return ref_node(p, g, flags);
    }
    if (is_alpha(c)) fail(p, "bad escape");
    return char_node(c, flags);
}

/* Reads a name up to the terminator, as sre's Tokenizer.getuntil. */
static void get_until(P *p, u32 term, int *start, int *len) {
    int s = p->pos;
    for (;;) {
        if (at_end(p)) {
            if (p->pos == s) fail(p, "missing group name");
            fail(p, "missing terminator, unterminated name");
        }
        u32 c = p->p[p->pos++];
        if (c == term) {
            if (p->pos - 1 == s) fail(p, "missing group name");
            break;
        }
    }
    *start = s;
    *len = p->pos - 1 - s;
}

static int is_identifier(const u32 *s, int n) {
    if (n == 0) return 0;
    for (int i = 0; i < n; i++) {
        u32 c = s[i];
        if (c >= 128 || is_alpha(c) || c == '_') continue;
        if (i > 0 && is_digit(c)) continue;
        return 0;
    }
    return 1;
}

static int all_digits(const u32 *s, int n) {
    if (n == 0) return 0;
    for (int i = 0; i < n; i++)
        if (!is_digit(s[i])) return 0;
    return 1;
}

static int find_name(Name *names, int nnames, const u32 *s, int n) {
    for (int i = 0; i < nnames; i++)
        if (names[i].len == n && memcmp(names[i].name, s, n * sizeof(u32)) == 0) return names[i].index;
    return -1;
}

static void width(P *p, Node *n, long *lo, long *hi);

#define INF_WIDTH 4294967295L

static long add_w(long a, long b) { return (a >= INF_WIDTH || b >= INF_WIDTH || a + b >= INF_WIDTH) ? INF_WIDTH : a + b; }

static long mul_w(long a, long b) {
    if (a == 0 || b == 0) return 0;
    if (a >= INF_WIDTH || b >= INF_WIDTH || a > INF_WIDTH / b) return INF_WIDTH;
    return a * b;
}

static void width(P *p, Node *n, long *lo, long *hi) {
    long l = 0, h = 0, a, b;
    switch (n->type) {
    case N_CHAR:
    case N_ANY:
    case N_CLASS:
        l = h = 1;
        break;
    case N_SEQ:
        for (int i = 0; i < n->nkids; i++) {
            width(p, n->kids[i], &a, &b);
            l = add_w(l, a);
            h = add_w(h, b);
        }
        break;
    case N_ALT:
        l = INF_WIDTH;
        h = 0;
        for (int i = 0; i < n->nkids; i++) {
            width(p, n->kids[i], &a, &b);
            if (a < l) l = a;
            if (b > h) h = b;
        }
        break;
    case N_GROUP:
    case N_ATOMIC:
        width(p, n->kids[0], &l, &h);
        break;
    case N_REPEAT:
        width(p, n->kids[0], &a, &b);
        l = mul_w(a, n->min);
        h = n->max < 0 ? (b ? INF_WIDTH : 0) : mul_w(b, n->max);
        break;
    case N_REF:
        l = p->gw_lo[n->group];
        h = p->gw_hi[n->group];
        break;
    case N_COND:
        width(p, n->kids[0], &l, &h);
        if (n->kids[1]) {
            width(p, n->kids[1], &a, &b);
            if (a < l) l = a;
            if (b > h) h = b;
        } else {
            l = 0;
        }
        break;
    }
    *lo = l;
    *hi = h;
}

static Node *parse_alt(P *p, int flags, int nested, int first);

/* Parses inline flags after "(?". Returns 1 for global flags "(?ims)", 0 for scoped "(?i-s:". */
static int parse_flags(P *p, u32 c, int *add, int *del) {
    *add = *del = 0;
    if (c != '-') {
        for (;;) {
            switch (c) {
            case 'i': *add |= FL_I; break;
            case 'm': *add |= FL_M; break;
            case 's': *add |= FL_S; break;
            case 'x': *add |= FL_X; break;
            case 'a': break;
            default: fail(p, "unsupported inline flag");
            }
            if (at_end(p)) fail(p, "missing -, : or )");
            c = p->p[p->pos++];
            if (c == ')' || c == '-' || c == ':') break;
            if (c == 0 || c >= 128 || !strchr("imsxauL", (int)c)) fail(p, is_alpha(c) ? "unknown flag" : "missing -, : or )");
        }
    }
    if (c == ')') return 1;
    if (c == '-') {
        if (at_end(p)) fail(p, "missing flag");
        c = p->p[p->pos++];
        if (c == 0 || c >= 128 || !strchr("imsxauL", (int)c)) fail(p, is_alpha(c) ? "unknown flag" : "missing flag");
        for (;;) {
            switch (c) {
            case 'i': *del |= FL_I; break;
            case 'm': *del |= FL_M; break;
            case 's': *del |= FL_S; break;
            case 'x': *del |= FL_X; break;
            default: fail(p, "bad inline flags: cannot turn off flags 'a', 'u' and 'L'");
            }
            if (at_end(p)) fail(p, "missing :");
            c = p->p[p->pos++];
            if (c == ':') break;
            if (c == 0 || c >= 128 || !strchr("imsxauL", (int)c)) fail(p, is_alpha(c) ? "unknown flag" : "missing :");
        }
    }
    if (*add & *del) fail(p, "bad inline flags: flag turned on and off");
    return 0;
}

static void open_group_name(P *p, int g, int s, int len) {
    if (find_name(p->names, p->nnames, p->p + s, len) >= 0) fail(p, "redefinition of group name");
    if (p->nnames == p->ncap) {
        p->ncap = p->ncap ? p->ncap * 2 : 8;
        p->names = xrealloc(p->names, p->ncap * sizeof(Name));
    }
    Name *nm = &p->names[p->nnames++];
    nm->name = xmalloc(len * sizeof(u32));
    memcpy(nm->name, p->p + s, len * sizeof(u32));
    nm->len = len;
    nm->index = g;
}

/* Parses "(" after the opening parenthesis. Returns NULL for items that add
 * nothing (comments and global flags). */
static Node *parse_group(P *p, int *flags, int nested, int first, int have_items) {
    int capture = 1, atomic = 0, add = 0, del = 0;
    int name_s = 0, name_len = -1;
    if (match_ch(p, '?')) {
        if (at_end(p)) fail(p, "unexpected end of pattern");
        u32 c = p->p[p->pos++];
        if (c == 'P') {
            if (match_ch(p, '<')) {
                get_until(p, '>', &name_s, &name_len);
                if (!is_identifier(p->p + name_s, name_len)) fail(p, "bad character in group name");
            } else if (match_ch(p, '=')) {
                int s, len;
                get_until(p, ')', &s, &len);
                if (!is_identifier(p->p + s, len)) fail(p, "bad character in group name");
                int g = find_name(p->names, p->nnames, p->p + s, len);
                if (g < 0) fail(p, "unknown group name");
                return ref_node(p, g, *flags);
            } else {
                if (at_end(p)) fail(p, "unexpected end of pattern");
                fail(p, "unknown extension ?P");
            }
        } else if (c == ':') {
            capture = 0;
        } else if (c == '#') {
            for (;;) {
                if (at_end(p)) fail(p, "missing ), unterminated comment");
                if (p->p[p->pos++] == ')') break;
            }
            return NULL;
        } else if (c == '=' || c == '!' || c == '<') {
            int mode = 0;
            if (c == '<') {
                if (at_end(p)) fail(p, "unexpected end of pattern");
                c = p->p[p->pos++];
                if (c != '=' && c != '!') fail(p, "unknown extension ?<");
                mode |= LOOK_BEHIND;
            }
            if (c == '!') mode |= LOOK_NEG;
            int outer = p->lookbehind;
            if ((mode & LOOK_BEHIND) && outer < 0) p->lookbehind = p->groups;
            Node *body = parse_alt(p, *flags, nested + 1, 0);
            if ((mode & LOOK_BEHIND) && outer < 0) p->lookbehind = -1;
            if (!match_ch(p, ')')) fail(p, "missing ), unterminated subpattern");
            Node *n = node(N_LOOK);
            n->mode = mode;
            add_kid(n, body);
            if (mode & LOOK_BEHIND) {
                long lo, hi;
                width(p, body, &lo, &hi);
                if (lo != hi) fail(p, "look-behind requires fixed-width pattern");
                n->min = lo;
            }
            return n;
        } else if (c == '(') {
            int s, len;
            long g;
            get_until(p, ')', &s, &len);
            if (all_digits(p->p + s, len)) {
                g = 0;
                for (int i = 0; i < len; i++) {
                    g = g * 10 + (p->p[s + i] - '0');
                    if (g > 1000000) g = 1000000;
                }
                if (g == 0) fail(p, "bad group number");
                if (p->ncond == p->condcap) {
                    p->condcap = p->condcap ? p->condcap * 2 : 8;
                    p->condrefs = xrealloc(p->condrefs, p->condcap * sizeof(long));
                }
                p->condrefs[p->ncond++] = g;
            } else {
                if (!is_identifier(p->p + s, len)) fail(p, "bad character in group name");
                g = find_name(p->names, p->nnames, p->p + s, len);
                if (g < 0) fail(p, "unknown group name");
            }
            check_lookbehind_ref(p, g);
            Node *n = node(N_COND);
            n->group = (int)g;
            Node *yes = parse_alt(p, *flags, -1 - nested, 0);
            Node *no = NULL;
            if (match_ch(p, '|')) {
                no = parse_alt(p, *flags, -1 - nested, 0);
                if (!at_end(p) && peek(p) == '|') fail(p, "conditional backref with more than two branches");
            }
            if (!match_ch(p, ')')) fail(p, "missing ), unterminated subpattern");
            add_kid(n, yes);
            add_kid(n, no);
            return n;
        } else if (c == '>') {
            capture = 0;
            atomic = 1;
        } else if (c != 0 && c < 128 && (strchr("imsxauL", (int)c) || c == '-')) {
            if (parse_flags(p, c, &add, &del)) {
                if (!first || have_items) fail(p, "global flags not at the start of the expression");
                *flags |= add;
                p->gflags |= add;
                return NULL;
            }
            capture = 0;
        } else {
            fail(p, "unknown extension");
        }
    }
    int g = 0;
    if (capture) {
        g = p->groups++;
        ensure_groups(p, g);
        if (name_len >= 0) open_group_name(p, g, name_s, name_len);
    }
    int sub = (*flags | add) & ~del;
    Node *body = parse_alt(p, sub, nested + 1, 0);
    if (!match_ch(p, ')')) fail(p, "missing ), unterminated subpattern");
    if (capture) {
        p->closed[g] = 1;
        width(p, body, &p->gw_lo[g], &p->gw_hi[g]);
    }
    Node *n = node(atomic ? N_ATOMIC : N_GROUP);
    n->group = g;
    add_kid(n, body);
    return n;
}

static int read_count(P *p, long *v) {
    int digits = 0;
    *v = 0;
    while (!at_end(p) && is_digit(peek(p))) {
        if (*v < 10000000000L) *v = *v * 10 + (p->p[p->pos] - '0');
        p->pos++;
        digits++;
    }
    return digits;
}

/* One branch: a sequence of items up to "|", ")" or the end (sre's _parse). */
static Node *parse_seq(P *p, int flags, int nested, int first) {
    Node *seq = node(N_SEQ);
    for (;;) {
        if (at_end(p)) break;
        u32 c = peek(p);
        if (c == '|' || c == ')') break;
        p->pos++;
        if (flags & FL_X) {
            if (c == ' ' || (c >= 9 && c <= 13)) continue;
            if (c == '#') {
                while (!at_end(p) && p->p[p->pos++] != '\n') {}
                continue;
            }
        }
        Node *item = NULL;
        switch (c) {
        case '\\':
            item = parse_escape(p, flags);
            break;
        case '[':
            item = parse_class(p, flags);
            break;
        case '.':
            item = node(N_ANY);
            item->dotall = (flags & FL_S) != 0;
            break;
        case '^':
            item = at_node((flags & FL_M) ? AT_BEG_LINE : AT_BEG);
            break;
        case '$':
            item = at_node((flags & FL_M) ? AT_END_LINE : AT_END);
            break;
        case '(':
            item = parse_group(p, &flags, nested, first, seq->nkids > 0);
            if (!item) continue;
            break;
        case '*':
        case '+':
        case '?':
        case '{': {
            long min, max;
            int here = p->pos;
            if (c == '?') {
                min = 0;
                max = 1;
            } else if (c == '*') {
                min = 0;
                max = -1;
            } else if (c == '+') {
                min = 1;
                max = -1;
            } else {
                if (!at_end(p) && peek(p) == '}') {
                    item = char_node('{', flags);
                    break;
                }
                long lo, hi;
                int nlo = read_count(p, &lo), nhi;
                if (match_ch(p, ',')) {
                    nhi = read_count(p, &hi);
                } else {
                    hi = lo;
                    nhi = nlo;
                }
                if (!match_ch(p, '}')) {
                    p->pos = here;
                    item = char_node('{', flags);
                    break;
                }
                min = nlo ? lo : 0;
                max = nhi ? hi : -1;
                if (min >= 4294967295L || max >= 4294967295L) fail(p, "the repetition number is too large");
                if (max >= 0 && max < min) fail(p, "min repeat greater than max repeat");
            }
            if (seq->nkids == 0) fail(p, "nothing to repeat");
            Node *last = seq->kids[seq->nkids - 1];
            if (last->type == N_AT) fail(p, "nothing to repeat");
            if (last->type == N_REPEAT) fail(p, "multiple repeat");
            Node *r = node(N_REPEAT);
            r->min = min;
            r->max = max;
            r->mode = REP_GREEDY;
            if (match_ch(p, '?')) r->mode = REP_LAZY;
            else if (match_ch(p, '+')) r->mode = REP_POSSESSIVE;
            add_kid(r, last);
            seq->kids[seq->nkids - 1] = r;
            continue;
        }
        default:
            item = char_node(c, flags);
        }
        add_kid(seq, item);
    }
    return seq;
}

/* Alternation (sre's _parse_sub). A negative nested value parses a single
 * branch for a conditional. */
static Node *parse_alt(P *p, int flags, int nested, int first) {
    if (nested < 0) return parse_seq(p, flags, -nested, 0);
    Node *alt = node(N_ALT);
    for (;;) {
        /* Global flags appear at the start of the first top-level branch and
         * apply to the later top-level branches too. Nested branches inherit
         * them through flags. */
        int f = nested == 0 ? flags | p->gflags : flags;
        add_kid(alt, parse_seq(p, f, nested + 1, first && nested == 0 && alt->nkids == 0));
        if (!match_ch(p, '|')) break;
    }
    if (alt->nkids == 1) return alt->kids[0];
    return alt;
}

/* ------------------------------------------------------------------ code generation */

static int emit(Regex *re, int op) {
    if (re->ninst == re->icap) {
        re->icap = re->icap ? re->icap * 2 : 64;
        re->prog = xrealloc(re->prog, re->icap * sizeof(Inst));
    }
    memset(&re->prog[re->ninst], 0, sizeof(Inst));
    re->prog[re->ninst].op = op;
    return re->ninst++;
}

static int single_char(const Node *n) { return n->type == N_CHAR || n->type == N_ANY || n->type == N_CLASS; }

static void gen(Regex *re, Node *n);

static void gen_test(Regex *re, Node *n) {
    int pc;
    switch (n->type) {
    case N_CHAR:
        if (n->icase && is_alpha(n->ch)) {
            pc = emit(re, I_CHARI);
            re->prog[pc].ch = lower(n->ch);
        } else {
            pc = emit(re, I_CHAR);
            re->prog[pc].ch = n->ch;
        }
        break;
    case N_ANY:
        emit(re, n->dotall ? I_ANYALL : I_ANY);
        break;
    case N_CLASS:
        pc = emit(re, I_CLASS);
        re->prog[pc].a = n->cls;
        re->prog[pc].icase = n->icase;
        break;
    }
}

static void gen_repeat(Regex *re, Node *n) {
    Node *body = n->kids[0];
    if (n->mode == REP_POSSESSIVE) {
        int at = emit(re, I_ATOMIC);
        n->mode = REP_GREEDY;
        gen_repeat(re, n);
        n->mode = REP_POSSESSIVE;
        emit(re, I_SUCCEED);
        re->prog[at].c = re->ninst;
        return;
    }
    if (single_char(body)) {
        int pc = emit(re, I_STAR1);
        re->prog[pc].a = n->min;
        re->prog[pc].b = n->max;
        re->prog[pc].c = n->mode == REP_LAZY;
        gen_test(re, body);
        return;
    }
    if (re->nreps == re->rcap) {
        re->rcap = re->rcap ? re->rcap * 2 : 8;
        re->reps = xrealloc(re->reps, re->rcap * sizeof(Rep));
    }
    int r = re->nreps++;
    re->reps[r].min = n->min;
    re->reps[r].max = n->max;
    re->reps[r].lazy = n->mode == REP_LAZY;
    int init = emit(re, I_REPINIT);
    re->prog[init].a = r;
    re->reps[r].body = re->ninst;
    gen(re, body);
    int jmp = emit(re, I_JMP);
    int until = emit(re, I_REPUNTIL);
    re->prog[until].a = r;
    re->prog[jmp].a = until;
    re->prog[init].b = until;
    re->reps[r].lazyiter = emit(re, I_REPLAZY);
    re->prog[re->reps[r].lazyiter].a = r;
    re->reps[r].exit = re->ninst;
}

static void gen(Regex *re, Node *n) {
    int pc;
    switch (n->type) {
    case N_EMPTY:
        break;
    case N_CHAR:
    case N_ANY:
    case N_CLASS:
        gen_test(re, n);
        break;
    case N_AT:
        pc = emit(re, I_ASSERT);
        re->prog[pc].a = n->at;
        break;
    case N_SEQ:
        for (int i = 0; i < n->nkids; i++) gen(re, n->kids[i]);
        break;
    case N_ALT: {
        int *jumps = xmalloc(n->nkids * sizeof(int));
        for (int i = 0; i < n->nkids; i++) {
            if (i < n->nkids - 1) {
                int split = emit(re, I_SPLIT);
                re->prog[split].a = split + 1;
                gen(re, n->kids[i]);
                jumps[i] = emit(re, I_JMP);
                re->prog[split].b = re->ninst;
            } else {
                gen(re, n->kids[i]);
            }
        }
        for (int i = 0; i < n->nkids - 1; i++) re->prog[jumps[i]].a = re->ninst;
        free(jumps);
        break;
    }
    case N_GROUP:
        if (n->group) {
            pc = emit(re, I_SAVE);
            re->prog[pc].a = 2 * n->group;
        }
        gen(re, n->kids[0]);
        if (n->group) {
            pc = emit(re, I_SAVE);
            re->prog[pc].a = 2 * n->group + 1;
        }
        break;
    case N_REPEAT:
        gen_repeat(re, n);
        break;
    case N_REF:
        pc = emit(re, I_BACKREF);
        re->prog[pc].a = n->group;
        re->prog[pc].icase = n->icase;
        break;
    case N_LOOK:
        pc = emit(re, I_LOOK);
        re->prog[pc].a = n->mode;
        re->prog[pc].b = n->min;
        gen(re, n->kids[0]);
        emit(re, I_SUCCEED);
        re->prog[pc].c = re->ninst;
        break;
    case N_ATOMIC:
        pc = emit(re, I_ATOMIC);
        gen(re, n->kids[0]);
        emit(re, I_SUCCEED);
        re->prog[pc].c = re->ninst;
        break;
    case N_COND: {
        pc = emit(re, I_COND);
        re->prog[pc].a = n->group;
        gen(re, n->kids[0]);
        int jmp = emit(re, I_JMP);
        re->prog[pc].b = re->ninst;
        if (n->kids[1]) gen(re, n->kids[1]);
        re->prog[jmp].a = re->ninst;
        break;
    }
    }
}

/* Parses and generates code; errors longjmp back to rx_compile. */
static Regex *build(P *p, int flags) {
    Regex *re = p->re;
    Node *root = parse_alt(p, flags, 0, 1);
    if (!at_end(p)) fail(p, "unbalanced parenthesis");
    for (int i = 0; i < p->ncond; i++)
        if (p->condrefs[i] >= p->groups) fail(p, "invalid group reference");
    gen(re, root);
    emit(re, I_MATCH);
    re->ngroups = p->groups - 1;
    re->names = p->names;
    re->nnames = p->nnames;
    return re;
}

static P *parser;

Regex *rx_compile(Str pattern, int flags, const char **err) {
    parser = xmalloc(sizeof *parser);
    memset(parser, 0, sizeof *parser);
    parser->p = pattern.s;
    parser->n = pattern.n;
    parser->groups = 1;
    parser->lookbehind = -1;
    parser->re = xmalloc(sizeof(Regex));
    memset(parser->re, 0, sizeof(Regex));
    ensure_groups(parser, 1);
    if (setjmp(parser->jb)) {
        *err = parser->err;
        return NULL;
    }
    return build(parser, flags);
}

int rx_groups(const Regex *re) { return re->ngroups; }

int rx_group_index(const Regex *re, const u32 *name, int len) { return find_name(re->names, re->nnames, name, len); }

/* ------------------------------------------------------------------ matcher */

enum { F_BRANCH, F_CAP, F_REG, F_STAR_G, F_STAR_L };

typedef struct {
    int kind;
    long pc, sp, a, b;
} Frame;

typedef struct {
    const u32 *s;
    long n;
    long start;
    int match_all, must_advance;
    int *caps;
    int ncaps;
    long *rcnt, *rlast;
    Frame *stk;
    long top, cap;
    int *snaps;
    long snaptop, snapcap;
} M;

static M mstate;

static void push(M *m, int kind, long pc, long sp, long a, long b) {
    if (m->top == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 1024;
        m->stk = xrealloc(m->stk, m->cap * sizeof(Frame));
    }
    Frame *f = &m->stk[m->top++];
    f->kind = kind;
    f->pc = pc;
    f->sp = sp;
    f->a = a;
    f->b = b;
}

static int test1(const Regex *re, const Inst *t, const M *m, long sp) {
    if (sp >= m->n) return 0;
    u32 c = m->s[sp];
    switch (t->op) {
    case I_CHAR: return c == t->ch;
    case I_CHARI: return lower(c) == t->ch;
    case I_ANY: return c != '\n';
    case I_ANYALL: return 1;
    case I_CLASS: return class_match(&re->classes[t->a], c, t->icase);
    }
    return 0;
}

static int at_match(const M *m, int at, long sp) {
    int before, after;
    switch (at) {
    case AT_BEG:
    case AT_BEG_STR: return sp == 0;
    case AT_BEG_LINE: return sp == 0 || m->s[sp - 1] == '\n';
    case AT_END: return sp == m->n || (sp == m->n - 1 && m->s[sp] == '\n');
    case AT_END_LINE: return sp == m->n || m->s[sp] == '\n';
    case AT_END_STR: return sp == m->n;
    case AT_BOUND:
    case AT_NONBOUND:
        if (m->n == 0) return 0;
        before = sp > 0 && is_word(m->s[sp - 1]);
        after = sp < m->n && is_word(m->s[sp]);
        return (before != after) == (at == AT_BOUND);
    }
    return 0;
}

static int *snapshot(M *m) {
    if (m->snaptop + m->ncaps > m->snapcap) {
        m->snapcap = (m->snapcap + m->ncaps) * 2;
        m->snaps = xrealloc(m->snaps, m->snapcap * sizeof(int));
    }
    int *s = m->snaps + m->snaptop;
    memcpy(s, m->caps, m->ncaps * sizeof(int));
    m->snaptop += m->ncaps;
    return s;
}

static long run(Regex *re, M *m, long pc, long sp, int toplevel) {
    long base = m->top;
    const Inst *in;
    for (;;) {
    dispatch:
        in = &re->prog[pc];
        switch (in->op) {
        case I_CHAR:
        case I_CHARI:
        case I_ANY:
        case I_ANYALL:
        case I_CLASS:
            if (!test1(re, in, m, sp)) goto fail;
            sp++;
            pc++;
            goto dispatch;
        case I_ASSERT:
            if (!at_match(m, (int)in->a, sp)) goto fail;
            pc++;
            goto dispatch;
        case I_SAVE:
            push(m, F_CAP, 0, 0, in->a, m->caps[in->a]);
            m->caps[in->a] = (int)sp;
            pc++;
            goto dispatch;
        case I_SPLIT:
            push(m, F_BRANCH, in->b, sp, 0, 0);
            pc = in->a;
            goto dispatch;
        case I_JMP:
            pc = in->a;
            goto dispatch;
        case I_BACKREF: {
            long s0 = m->caps[2 * in->a], e0 = m->caps[2 * in->a + 1];
            if (s0 < 0 || e0 < 0) goto fail;
            long len = e0 - s0;
            if (sp + len > m->n) goto fail;
            for (long i = 0; i < len; i++) {
                u32 x = m->s[s0 + i], y = m->s[sp + i];
                if (in->icase ? lower(x) != lower(y) : x != y) goto fail;
            }
            sp += len;
            pc++;
            goto dispatch;
        }
        case I_STAR1: {
            const Inst *t = &re->prog[pc + 1];
            long min = in->a, max = in->b;
            long room = m->n - sp;
            long limit = (max < 0 || max > room) ? room : max;
            if (!in->c) {
                long count = 0;
                while (count < limit && test1(re, t, m, sp + count)) count++;
                if (count < min) goto fail;
                if (count > min) push(m, F_STAR_G, pc + 2, sp + count, sp + min, 0);
                sp += count;
            } else {
                if (min > room) goto fail;
                for (long i = 0; i < min; i++)
                    if (!test1(re, t, m, sp + i)) goto fail;
                sp += min;
                if (max < 0 || min < max) push(m, F_STAR_L, pc + 2, sp, min, pc);
            }
            pc += 2;
            goto dispatch;
        }
        case I_REPINIT:
            push(m, F_REG, 0, m->rlast[in->a], in->a, m->rcnt[in->a]);
            m->rcnt[in->a] = -1;
            m->rlast[in->a] = -1;
            pc = in->b;
            goto dispatch;
        case I_REPUNTIL: {
            long r = in->a;
            const Rep *rp = &re->reps[r];
            long count = m->rcnt[r] + 1;
            if (count < rp->min) {
                push(m, F_REG, 0, m->rlast[r], r, m->rcnt[r]);
                m->rcnt[r] = count;
                pc = rp->body;
                goto dispatch;
            }
            int more = (rp->max < 0 || count < rp->max) && sp != m->rlast[r];
            if (!rp->lazy) {
                if (more) {
                    push(m, F_BRANCH, rp->exit, sp, 0, 0);
                    push(m, F_REG, 0, m->rlast[r], r, m->rcnt[r]);
                    m->rcnt[r] = count;
                    m->rlast[r] = sp;
                    pc = rp->body;
                    goto dispatch;
                }
                pc = rp->exit;
                goto dispatch;
            }
            if (more) push(m, F_BRANCH, rp->lazyiter, sp, 0, 0);
            pc = rp->exit;
            goto dispatch;
        }
        case I_REPLAZY: {
            long r = in->a;
            push(m, F_REG, 0, m->rlast[r], r, m->rcnt[r]);
            m->rcnt[r] = m->rcnt[r] + 1;
            m->rlast[r] = sp;
            pc = re->reps[r].body;
            goto dispatch;
        }
        case I_LOOK:
        case I_ATOMIC: {
            int neg = in->op == I_LOOK && (in->a & LOOK_NEG);
            long from = sp;
            if (in->op == I_LOOK && (in->a & LOOK_BEHIND)) {
                if (sp < in->b) {
                    if (neg) {
                        pc = in->c;
                        goto dispatch;
                    }
                    goto fail;
                }
                from = sp - in->b;
            }
            long next = in->c;
            int *snap = snapshot(m);
            long snapoff = snap - m->snaps;
            long r = run(re, m, pc + 1, from, 0);
            snap = m->snaps + snapoff;
            m->snaptop = snapoff;
            if (neg) {
                if (r >= 0) {
                    memcpy(m->caps, snap, m->ncaps * sizeof(int));
                    goto fail;
                }
                pc = next;
                goto dispatch;
            }
            if (r < 0) goto fail;
            for (int i = 0; i < m->ncaps; i++)
                if (m->caps[i] != snap[i]) push(m, F_CAP, 0, 0, i, snap[i]);
            if (in->op == I_ATOMIC) sp = r;
            pc = next;
            goto dispatch;
        }
        case I_COND:
            if (m->caps[2 * in->a] >= 0 && m->caps[2 * in->a + 1] >= 0) pc++;
            else pc = in->b;
            goto dispatch;
        case I_SUCCEED:
            m->top = base;
            return sp;
        case I_MATCH:
            if (toplevel && ((m->match_all && sp != m->n) || (m->must_advance && sp == m->start))) goto fail;
            m->top = base;
            return sp;
        }
    fail:
        for (;;) {
            if (m->top == base) return -1;
            Frame *f = &m->stk[--m->top];
            switch (f->kind) {
            case F_CAP:
                m->caps[f->a] = (int)f->b;
                continue;
            case F_REG:
                m->rcnt[f->a] = f->b;
                m->rlast[f->a] = f->sp;
                continue;
            case F_BRANCH:
                pc = f->pc;
                sp = f->sp;
                goto dispatch;
            case F_STAR_G: {
                long cur = f->sp - 1;
                pc = f->pc;
                if (cur > f->a) {
                    f->sp = cur;
                    m->top++;
                }
                sp = cur;
                goto dispatch;
            }
            case F_STAR_L: {
                const Inst *st = &re->prog[f->b];
                long count = f->a, cur = f->sp;
                if (st->b >= 0 && count >= st->b) continue;
                if (!test1(re, &re->prog[f->b + 1], m, cur)) continue;
                cur++;
                count++;
                pc = f->pc;
                if (st->b < 0 || count < st->b) {
                    f->sp = cur;
                    f->a = count;
                    m->top++;
                }
                sp = cur;
                goto dispatch;
            }
            }
        }
    }
}

static void prepare(Regex *re, Str subject) {
    M *m = &mstate;
    m->s = subject.s;
    m->n = subject.n;
    m->ncaps = 2 * (re->ngroups + 1);
    m->caps = xrealloc(m->caps, m->ncaps * sizeof(int));
    m->rcnt = xrealloc(m->rcnt, (re->nreps + 1) * sizeof(long));
    m->rlast = xrealloc(m->rlast, (re->nreps + 1) * sizeof(long));
    m->top = 0;
    m->snaptop = 0;
}

static int attempt(Regex *re, long start, int match_all, int must_advance, int *caps) {
    M *m = &mstate;
    for (int i = 0; i < m->ncaps; i++) m->caps[i] = -1;
    m->start = start;
    m->match_all = match_all;
    m->must_advance = must_advance;
    long end = run(re, m, 0, start, 1);
    if (end < 0) return 0;
    memcpy(caps, m->caps, m->ncaps * sizeof(int));
    caps[0] = (int)start;
    caps[1] = (int)end;
    return 1;
}

int rx_match_at(Regex *re, Str subject, int start, int match_all, int must_advance, int *caps) {
    prepare(re, subject);
    return attempt(re, start, match_all, must_advance, caps);
}

int rx_search(Regex *re, Str subject, int start, int must_advance, int *caps) {
    prepare(re, subject);
    const Inst *first = &re->prog[0];
    int anchored = first->op == I_ASSERT && (first->a == AT_BEG || first->a == AT_BEG_STR);
    for (long pos = start; pos <= subject.n; pos++) {
        if (first->op == I_CHAR) {
            while (pos < subject.n && subject.s[pos] != first->ch) pos++;
            if (pos >= subject.n) return 0;
        }
        if (attempt(re, pos, 0, pos == start && must_advance, caps)) return 1;
        if (anchored) return 0;
    }
    return 0;
}
