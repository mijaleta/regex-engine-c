#ifndef RX_H
#define RX_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t u32;

/* A string of Unicode code points. */
typedef struct {
    u32 *s;
    int n;
} Str;

enum { FL_I = 1, FL_M = 2, FL_S = 4, FL_X = 8 };

/* Compiled program. */
typedef struct Regex Regex;

/* Compiles pattern with the given flags. On error returns NULL and sets *err. */
Regex *rx_compile(Str pattern, int flags, const char **err);
int rx_groups(const Regex *re);
/* Group index for a name, or -1. */
int rx_group_index(const Regex *re, const u32 *name, int len);

/*
 * Tries to match starting exactly at position start. caps must hold
 * 2 * (groups + 1) ints and receives the spans (-1 when unset).
 * match_all requires the match to end at the end of the subject.
 * must_advance rejects an empty match at start.
 * Returns 1 on a match, 0 otherwise.
 */
int rx_match_at(Regex *re, Str subject, int start, int match_all, int must_advance, int *caps);

/* Searches from start onwards. Same conventions as rx_match_at. */
int rx_search(Regex *re, Str subject, int start, int must_advance, int *caps);

/* UTF-8 helpers. */
Str utf8_decode(const char *bytes, size_t len);
void utf8_put(u32 c, void (*emit)(const char *, size_t));

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);

#endif
