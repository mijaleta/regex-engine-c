/*
 * rx: runs one regular-expression operation with Python re semantics.
 *
 *   rx [-i] [-m] [-s] [-x] [-n COUNT] OP PATTERN [REPL] < subject
 */
#include "rx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ output buffer */

static char *out;
static size_t outlen, outcap;

static void put(const char *s, size_t n) {
    if (outlen + n > outcap) {
        outcap = (outlen + n) * 2 + 4096;
        out = xrealloc(out, outcap);
    }
    memcpy(out + outlen, s, n);
    outlen += n;
}

static void puts_(const char *s) { put(s, strlen(s)); }

static void put_codepoints(const u32 *s, int n) {
    for (int i = 0; i < n; i++) utf8_put(s[i], put);
}

/* ------------------------------------------------------------------ UTF-8 */

Str utf8_decode(const char *bytes, size_t len) {
    const unsigned char *b = (const unsigned char *)bytes;
    Str s;
    s.s = xmalloc((len + 1) * sizeof(u32));
    s.n = 0;
    size_t i = 0;
    while (i < len) {
        u32 c = b[i];
        int extra = 0;
        if (c >= 0xF0) {
            c &= 0x07;
            extra = 3;
        } else if (c >= 0xE0) {
            c &= 0x0F;
            extra = 2;
        } else if (c >= 0xC0) {
            c &= 0x1F;
            extra = 1;
        }
        i++;
        for (int k = 0; k < extra && i < len; k++, i++) c = (c << 6) | (b[i] & 0x3F);
        s.s[s.n++] = c;
    }
    return s;
}

void utf8_put(u32 c, void (*emit)(const char *, size_t)) {
    char buf[4];
    if (c < 0x80) {
        buf[0] = (char)c;
        emit(buf, 1);
    } else if (c < 0x800) {
        buf[0] = (char)(0xC0 | (c >> 6));
        buf[1] = (char)(0x80 | (c & 0x3F));
        emit(buf, 2);
    } else if (c < 0x10000) {
        buf[0] = (char)(0xE0 | (c >> 12));
        buf[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (c & 0x3F));
        emit(buf, 3);
    } else {
        buf[0] = (char)(0xF0 | (c >> 18));
        buf[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (c & 0x3F));
        emit(buf, 4);
    }
}

/* ------------------------------------------------------------------ errors */

static void usage(const char *msg) {
    fprintf(stderr, "rx: %s\nusage: rx [-i] [-m] [-s] [-x] [-n COUNT] OP PATTERN [REPL]\n", msg);
    exit(2);
}

static void error(const char *msg) {
    fprintf(stderr, "rx: error: %s\n", msg);
    exit(1);
}

/* ------------------------------------------------------------------ replacement templates */

typedef struct {
    int group; /* -1 for literal text */
    u32 *text;
    int len, cap;
} Part;

typedef struct {
    Part *parts;
    int n, cap;
} Template;

static Part *new_part(Template *t, int group) {
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 8;
        t->parts = xrealloc(t->parts, t->cap * sizeof(Part));
    }
    Part *p = &t->parts[t->n++];
    memset(p, 0, sizeof *p);
    p->group = group;
    return p;
}

static void lit(Template *t, u32 c) {
    Part *p = t->n && t->parts[t->n - 1].group < 0 ? &t->parts[t->n - 1] : new_part(t, -1);
    if (p->len == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 16;
        p->text = xrealloc(p->text, p->cap * sizeof(u32));
    }
    p->text[p->len++] = c;
}

static int is_dig(u32 c) { return c >= '0' && c <= '9'; }
static int is_octd(u32 c) { return c >= '0' && c <= '7'; }
static int is_letter(u32 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static void add_group(Template *t, Regex *re, long g) {
    if (g > rx_groups(re)) error("invalid group reference");
    new_part(t, (int)g);
}

/* Parses a replacement the way Python's re does before any matching. */
static Template parse_template(Str s, Regex *re) {
    Template t = {0};
    int i = 0;
    while (i < s.n) {
        u32 c = s.s[i++];
        if (c != '\\') {
            lit(&t, c);
            continue;
        }
        if (i >= s.n) error("bad escape (end of pattern)");
        c = s.s[i++];
        if (c == 'g') {
            if (i >= s.n || s.s[i] != '<') error("missing <");
            i++;
            int start = i;
            while (i < s.n && s.s[i] != '>') i++;
            if (i >= s.n) error(i == start ? "missing group name" : "missing >, unterminated name");
            int len = i - start;
            i++;
            if (len == 0) error("missing group name");
            const u32 *name = s.s + start;
            int ident = 1;
            for (int k = 0; k < len; k++) {
                u32 x = name[k];
                if (!(x >= 128 || is_letter(x) || x == '_' || (k > 0 && is_dig(x)))) ident = 0;
            }
            if (ident) {
                int g = rx_group_index(re, name, len);
                if (g < 0) error("unknown group name");
                add_group(&t, re, g);
            } else {
                long g = 0;
                for (int k = 0; k < len; k++) {
                    if (!is_dig(name[k])) error("bad character in group name");
                    if (g < 100000000) g = g * 10 + (name[k] - '0');
                }
                add_group(&t, re, g);
            }
        } else if (c == '0') {
            long v = 0;
            for (int k = 0; k < 2 && i < s.n && is_octd(s.s[i]); k++) v = v * 8 + (s.s[i++] - '0');
            lit(&t, (u32)(v & 0xff));
        } else if (is_dig(c)) {
            long g = c - '0';
            if (i < s.n && is_dig(s.s[i])) {
                u32 d = s.s[i++];
                if (is_octd(c) && is_octd(d) && i < s.n && is_octd(s.s[i])) {
                    long v = (c - '0') * 64 + (d - '0') * 8 + (s.s[i++] - '0');
                    if (v > 0377) error("octal escape value outside of range 0-0o377");
                    lit(&t, (u32)v);
                    continue;
                }
                g = g * 10 + (d - '0');
            }
            add_group(&t, re, g);
        } else {
            switch (c) {
            case 'a': lit(&t, 7); break;
            case 'b': lit(&t, 8); break;
            case 'f': lit(&t, 12); break;
            case 'n': lit(&t, 10); break;
            case 'r': lit(&t, 13); break;
            case 't': lit(&t, 9); break;
            case 'v': lit(&t, 11); break;
            case '\\': lit(&t, '\\'); break;
            default:
                if (is_letter(c)) error("bad escape");
                lit(&t, '\\');
                lit(&t, c);
            }
        }
    }
    return t;
}

/* ------------------------------------------------------------------ operations */

static void put_spans(const int *caps, int groups) {
    char buf[64];
    for (int g = 0; g <= groups; g++) {
        if (g) puts_(" ");
        if (caps[2 * g] < 0 || caps[2 * g + 1] < 0) {
            puts_("-");
        } else {
            snprintf(buf, sizeof buf, "%d,%d", caps[2 * g], caps[2 * g + 1]);
            puts_(buf);
        }
    }
    puts_("\n");
}

static char *read_stdin(size_t *len) {
    size_t cap = 1 << 16, n = 0;
    char *buf = xmalloc(cap);
    for (;;) {
        if (n == cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        size_t r = fread(buf + n, 1, cap - n, stdin);
        if (r == 0) break;
        n += r;
    }
    *len = n;
    return buf;
}

int main(int argc, char **argv) {
    int flags = 0;
    long count = 0;
    int have_count = 0;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-i")) flags |= FL_I;
        else if (!strcmp(a, "-m")) flags |= FL_M;
        else if (!strcmp(a, "-s")) flags |= FL_S;
        else if (!strcmp(a, "-x")) flags |= FL_X;
        else if (!strcmp(a, "-n")) {
            if (i + 1 >= argc) usage("-n needs a count");
            const char *v = argv[++i];
            if (!*v) usage("bad count");
            count = 0;
            for (const char *q = v; *q; q++) {
                if (*q < '0' || *q > '9') usage("bad count");
                if (count < 1000000000L) count = count * 10 + (*q - '0');
            }
            have_count = 1;
        } else {
            usage("unknown option");
        }
    }
    if (i >= argc) usage("missing operation");
    const char *op = argv[i++];
    int is_sub = !strcmp(op, "sub");
    int is_split = !strcmp(op, "split");
    if (!is_sub && !is_split && strcmp(op, "search") && strcmp(op, "match") && strcmp(op, "fullmatch") &&
        strcmp(op, "finditer"))
        usage("unknown operation");
    if (have_count && !is_sub && !is_split) usage("-n only applies to sub and split");
    int want = is_sub ? 2 : 1;
    if (argc - i != want) usage(argc - i < want ? "missing argument" : "too many arguments");

    Str pattern = utf8_decode(argv[i], strlen(argv[i]));
    size_t inlen;
    char *input = read_stdin(&inlen);
    Str subject = utf8_decode(input, inlen);

    const char *err = NULL;
    Regex *re = rx_compile(pattern, flags, &err);
    if (!re) error(err);
    int groups = rx_groups(re);
    int *caps = xmalloc(2 * (groups + 1) * sizeof(int));

    Template tmpl = {0};
    if (is_sub) {
        Str r = utf8_decode(argv[i + 1], strlen(argv[i + 1]));
        tmpl = parse_template(r, re);
    }

    if (!strcmp(op, "search") || !strcmp(op, "match") || !strcmp(op, "fullmatch")) {
        int ok = op[0] == 's' ? rx_search(re, subject, 0, 0, caps)
                              : rx_match_at(re, subject, 0, op[0] == 'f', 0, caps);
        if (ok) put_spans(caps, groups);
        else puts_("none\n");
    } else {
        int pos = 0, must_advance = 0, last = 0;
        long done = 0;
        while (pos <= subject.n) {
            if ((is_sub || is_split) && count && done >= count) break;
            if (!rx_search(re, subject, pos, must_advance, caps)) break;
            int s = caps[0], e = caps[1];
            if (is_sub) {
                put_codepoints(subject.s + last, s - last);
                for (int k = 0; k < tmpl.n; k++) {
                    Part *p = &tmpl.parts[k];
                    if (p->group < 0) {
                        put_codepoints(p->text, p->len);
                    } else if (caps[2 * p->group] >= 0 && caps[2 * p->group + 1] >= 0) {
                        put_codepoints(subject.s + caps[2 * p->group], caps[2 * p->group + 1] - caps[2 * p->group]);
                    }
                }
            } else if (is_split) {
                char buf[64];
                snprintf(buf, sizeof buf, "%d,%d\n", last, s);
                puts_(buf);
                for (int g = 1; g <= groups; g++) {
                    if (caps[2 * g] < 0 || caps[2 * g + 1] < 0) {
                        puts_("-\n");
                    } else {
                        snprintf(buf, sizeof buf, "%d,%d\n", caps[2 * g], caps[2 * g + 1]);
                        puts_(buf);
                    }
                }
            } else {
                put_spans(caps, groups);
            }
            last = e;
            done++;
            must_advance = e == s;
            pos = e;
        }
        if (is_sub) {
            put_codepoints(subject.s + last, subject.n - last);
        } else if (is_split) {
            char buf[64];
            snprintf(buf, sizeof buf, "%d,%d\n", last, subject.n);
            puts_(buf);
        }
    }
    fwrite(out, 1, outlen, stdout);
    return 0;
}
