# rx: Python-compatible regular expressions in C

Implement `rx`, a command-line program that runs one regular-expression
operation on its standard input. Patterns, replacement templates and results
must be exactly those of Python 3.13's `re` module used with the `re.ASCII`
flag, for everything described below, including which patterns and templates
are errors.

`/app` holds a `Makefile` and a stub `src/main.c` that only prints "not
implemented". Replace it with a full implementation.

## Constraints

- C, with only the C standard library. Do not use `<regex.h>` or any other
  regular-expression library, and your program must not run other programs.
  Python is not installed.
- Your code is graded by copying `/app` (without `.git/`, built binaries and
  `*.o` files) to a fresh directory and running `make rx` there. The result
  must be the executable `./rx`. You may change the `Makefile`.

## How you are scored

A hidden suite of a few hundred cases is run against the binary. Each case
gives the command-line arguments, the text on stdin, the expected stdout and
the expected exit status. A case passes only when **stdout matches byte for
byte and the exit status matches**. stderr is never compared. Each case has a
10 second limit. A case that expects an error also comes with a related valid
"control" command line, and passes only if the control is answered correctly
too. Your score is the fraction of cases that pass.

The hidden cases cover everything in this document: literals and escapes,
character classes, anchors, every kind of repetition, groups, backreferences,
lookaround, atomic groups, conditionals, flags and verbose mode, Unicode
text, all six operations, replacement templates, errors and the command line.
Some subjects are several hundred thousand characters long; for example a
fullmatch of `(a|b)*` against 200,000 characters must work, so the matcher
cannot recurse once per character on the C stack. Every expected output was
produced by Python 3.13.

`/app/examples/` holds 35 public cases, one directory each: `args` (every
argument followed by a NUL byte), `stdin`, `stdout`, `code`, and for error
cases a `control/` directory. `/app/run_examples.sh [-v]` builds `rx` and runs
them. The hidden cases are different and much more thorough.

## Command line

```
rx [-i] [-m] [-s] [-x] [-n COUNT] OP PATTERN [REPL]
```

The subject is all of stdin, decoded as UTF-8, including any final newline.
The pattern and the replacement are UTF-8 too. **All positions are counted in
Unicode code points**, not bytes.

Options come before `OP`, each as a separate argument. Everything after `OP`
is an argument, even if it starts with `-`, so `rx search -i` searches for
the text `-i`.

| Option | Python flag |
|---|---|
| `-i` | `re.IGNORECASE` |
| `-m` | `re.MULTILINE` |
| `-s` | `re.DOTALL` |
| `-x` | `re.VERBOSE` |
| `-n COUNT` | `count` for `sub`, `maxsplit` for `split`: a non-negative decimal integer; 0 means no limit |

`re.ASCII` is always on.

| OP | Arguments | Python equivalent |
|---|---|---|
| `search` | PATTERN | `re.search` |
| `match` | PATTERN | `re.match` |
| `fullmatch` | PATTERN | `re.fullmatch` |
| `finditer` | PATTERN | `re.finditer` |
| `sub` | PATTERN REPL | `re.sub` |
| `split` | PATTERN | `re.split` |

Exit status:

- **2**, with nothing on stdout, for a usage error: no `OP`, an unknown `OP`
  or option, `-n` without a valid count, `-n` with an operation other than
  `sub` and `split`, or the wrong number of arguments after `OP`.
- **1**, with nothing on stdout, if the pattern is invalid, or (for `sub`) the
  replacement template is invalid. A template is checked before any
  matching, so an invalid template is an error even when nothing matches.
- **0** otherwise, whether or not anything matched.

## Output

A span is written `start,end` (code-point offsets, end exclusive). A group
that did not take part in the match is written `-`.

- `search`, `match`, `fullmatch`: one line with the spans of group 0 (the
  whole match), group 1, group 2, … up to the last group, separated by single
  spaces. If there is no match, the line `none`.
  `rx search '(a)|(b)'` on `xb` prints `1,2 - 1,2`.
- `finditer`: one such line per match, in order. Nothing if there is none.
- `sub`: the resulting text, with nothing added.
- `split`: one line per piece of the result list. Every piece is either a
  slice of the subject, written as its span, or `-` for a group that did not
  take part. `rx split '(,)|;'` on `a,b;c` prints `0,1`, `1,2`, `2,3`, `-`,
  `4,5`.

Every line ends with `\n`.

## Operations

- `search`: try a match at position 0, then 1, 2, … up to and including the
  end of the subject; report the first that succeeds.
- `match`: try only at position 0. `fullmatch`: only at position 0, and the
  match must end at the end of the subject (the matcher backtracks until it
  does, or fails).
- `finditer`: search from position 0. After each match `[s, e)`, the next
  search starts at `e`. If the previous match was empty (`s == e`), a match
  at that starting position must not be empty itself (the matcher backtracks
  into other alternatives instead), while matches starting later may be
  empty. Stop when a search finds nothing. So `x*` on `axxb` finds
  `0,0`, `1,3`, `3,3`, `4,4`, and `a*?|b` on `ab` finds `0,0`, `0,1`,
  `1,1`, `1,2`, `2,2`.
- `sub`: walk the matches exactly as `finditer` does (stopping after
  `COUNT` of them when a count is given) and replace each with its expanded
  template. `rx sub 'x*' - ` on `abc` gives `-a-b-c-`.
- `split`: walk the matches the same way (at most `COUNT` when given). The
  pieces are: the text before the first match, then for each match the spans
  of all its groups (1, 2, …) followed by the text up to the next match, and
  finally the text after the last match.

## Pattern syntax

These are Python's rules. Anything that is not listed is out of scope.

### Literals and escapes

Characters other than `. ^ $ * + ? { [ \ | (` and `)` match themselves (`]`
and `}` are ordinary). Escapes outside a class:

| Escape | Meaning |
|---|---|
| `\a \f \n \r \t \v` | the control characters 7, 12, 10, 13, 9, 11 |
| `\\` | backslash |
| `\xhh` | exactly 2 hex digits, else an error |
| `\uhhhh` | exactly 4 hex digits, else an error |
| `\Uhhhhhhhh` | exactly 8 hex digits, at most 10FFFF, else an error |
| `\0`, `\0o`, `\0oo` | octal: `\0` followed by up to two more octal digits |
| `\d \D \w \W \s \S` | the classes below |
| `\A \Z \b \B` | the anchors below |
| `\1` … `\99` | backreference, see below |
| `\` + other ASCII letter | error ("bad escape") |
| `\` + any other character | that character literally (`\.`, `\-`, `\é`, `\ `) |
| `\` at the end of the pattern | error |

A backslash followed by digits 1-9 is read like this: take the first digit;
if another digit follows, take it too; if both are octal digits (0-7) and a
third octal digit follows, the three form an octal character code (an error
above 0o377). Otherwise the one or two digits are a group number. That group
must already be closed at that point in the pattern: a reference to a group
that has not been opened yet is an error ("invalid group reference"), and to
a group that is still open is an error too.

### Character classes

`[…]` matches one character from the set, `[^…]` one character not in it.

- A `]` that comes first (right after `[` or `[^`) is a literal; later, `]`
  ends the class. A class that never ends is an error.
- `a-z` is a range of code points. A `-` that is first, or last before `]`,
  is literal. A range whose end is below its start is an error, as is a range
  with a class escape at either end (`[\d-z]`).
- Inside a class, `\b` is the backspace character (8), `\d \D \w \W \s \S`
  add those classes, `\a \f \n \r \t \v \\ \x \u \U` work as above, `\` with
  one to three octal digits is an octal code (at most 0o377), and `\` with
  any other ASCII letter or digit (including `\A \Z \B`) is an error. Any
  other escaped character is literal.
- Every other character inside the class is literal, including `[`, `.`,
  `*`, `$`, `^` (when not first) and `|`.

Class escapes (always ASCII, because `re.ASCII` is on):

| Escape | Matches |
|---|---|
| `\d` | `0`-`9` |
| `\w` | `a`-`z`, `A`-`Z`, `0`-`9`, `_` |
| `\s` | space, `\t`, `\n`, `\v`, `\f`, `\r` |
| `\D \W \S` | every character not matched by the lowercase one |

`.` matches any character except `\n`; with DOTALL, any character.

### Anchors

| Anchor | Matches at position p when |
|---|---|
| `^` | p = 0; with MULTILINE also when the previous character is `\n` |
| `$` | p is the end, or p is just before a `\n` that is the last character; with MULTILINE, p is the end or the character at p is `\n` |
| `\A` | p = 0 |
| `\Z` | p is the end |
| `\b` | exactly one of the characters before and after p is a `\w` character (outside the subject counts as non-word) |
| `\B` | both or neither are |

On an empty subject `\b` and `\B` never match.

### Repetition

A quantifier applies to the item just before it:

| Quantifier | Count | | Lazy | Possessive |
|---|---|---|---|---|
| `*` | 0 or more | | `*?` | `*+` |
| `+` | 1 or more | | `+?` | `++` |
| `?` | 0 or 1 | | `??` | `?+` |
| `{n}` | exactly n | | `{n}?` | `{n}+` |
| `{n,m}` | n to m | | `{n,m}?` | `{n,m}+` |
| `{n,}` | at least n | | `{n,}?` | `{n,}+` |
| `{,m}` | 0 to m | | `{,m}?` | `{,m}+` |
| `{,}` | 0 or more | | `{,}?` | `{,}+` |

- A `{` only starts a quantifier when it is followed by optional digits, an
  optional `,` with optional digits, and `}`, with nothing else in between
  (no spaces). Otherwise the `{` is a literal character: `a{x}`, `a{ 1}` and
  `a{}` match those characters literally.
- `{n,m}` with m < n is an error. Counts never exceed 1,000,000 in the tests.
- A quantifier with nothing before it in its branch, or after an anchor
  (`^ $ \A \Z \b \B`), is an error ("nothing to repeat"). A quantifier right
  after another quantifier (including its `?` or `+` suffix) is an error
  ("multiple repeat"): `a**`, `a{2}*`, `a*?+`. A literal `{` is not a
  quantifier, so `x{1}{2}` is an error but `x{1}{` is fine.
- `X*+` (possessive) behaves exactly like `(?>X*)`, and so on for the others.

### Groups and other constructs

| Syntax | Meaning |
|---|---|
| `(…)` | capturing group, numbered by the position of its `(` from 1 |
| `(?P<name>…)` | named capturing group; `name` is an identifier (`[A-Za-z_][A-Za-z0-9_]*`); reusing a name is an error |
| `(?P=name)` | backreference to a named group (unknown name: error) |
| `(?:…)` | non-capturing group |
| `(?#…)` | comment, up to the next `)`; contributes nothing |
| `(?=…)` `(?!…)` | lookahead, negative lookahead |
| `(?<=…)` `(?<!…)` | lookbehind, negative lookbehind |
| `(?>…)` | atomic group |
| `(?(id)yes\|no)`, `(?(id)yes)` | conditional; `id` is a group number or name |
| `(?flags)` | global flags |
| `(?flags-flags:…)`, `(?flags:…)`, `(?-flags:…)` | flags for one group |

`|` separates alternatives and has the lowest precedence. An empty
alternative or group matches the empty string. A `)` with no matching `(` is
an error, as is a `(` that is never closed, an empty group name, an
unterminated name or comment, and `(?` followed by anything not listed.

**Flags.** The flag letters are `i` (IGNORECASE), `m` (MULTILINE),
`s` (DOTALL) and `x` (VERBOSE); `a` is accepted and changes nothing. Other
letters are out of scope. Global flags `(?ims)` must come at the very start
of the pattern, before anything except other global flags and comments,
otherwise it is an error ("global flags not at the start of the expression").
They then apply to the whole pattern, as if given on the command line. A
scoped group `(?i-s:…)` turns flags on and off inside it only; a letter
turned both on and off is an error, and `(?-i)` without `:` is an error.

**Conditionals.** `(?(1)yes|no)` matches `yes` if group 1 has a span at that
point, otherwise `no` (or the empty string). A third alternative is an error.
A numeric id of 0 is an error, and a numeric id must name a group that
exists somewhere in the pattern (it may come later), otherwise it is an
error. A name must be already defined.

**Lookbehind** must have a fixed width: every way of matching its contents
has the same length, otherwise it is an error ("look-behind requires
fixed-width pattern"). Width: a character, class or `.` is 1; anchors and
lookarounds are 0; a sequence adds up; alternatives must all agree;
repetitions multiply, and an unbounded repetition of something non-empty is
not fixed; a backreference has the width of its group; a conditional takes
the widths of both branches (a missing `no` counts as 0). Inside a
lookbehind, a backreference or conditional may only refer to a group that was
closed before the (outermost) lookbehind started; referring to a group
defined inside it is an error.

### Verbose mode

With VERBOSE (`-x` or `(?x)`), whitespace (space, `\t`, `\n`, `\r`, `\v`,
`\f`) between items is ignored and `#` starts a comment to the end of the
line. This does not apply inside a class, after a backslash, or inside a
quantifier's braces, and a quantifier's `?`/`+` suffix must follow it
directly: `a {2}` is `a{2}`, but `a* ?` is an error.

## Matching

Matching is backtracking and leftmost-first, exactly like Python:

- Alternatives are tried left to right, and the first one that lets the whole
  match succeed wins. `cat|category` on `category` matches `cat`.
- Greedy quantifiers try as many iterations as possible first, lazy ones as
  few as possible, backing off or extending one iteration at a time.
- A group's span is set when the group finishes. When the matcher backtracks
  out of a group, the group gets back the span it had before. After a
  repetition, a group inside it keeps the span from the last iteration in
  which it took part: `(?:(a)|b)*` on `ab` gives group 1 = `0,1`.
- An atomic group `(?>X)` matches `X` once, taking the first way `X` can
  match there, and is never re-entered by backtracking: `(?>a*)a` never
  matches.
- Lookarounds match their contents at the current position (lookbehind: the
  fixed width before it) without consuming anything and are never re-entered
  by backtracking. A positive lookaround keeps the spans of groups set inside
  it; a negative one leaves all spans as they were.
- A backreference matches the exact text of its group (ignoring ASCII case
  with IGNORECASE). If the group has no span, the backreference fails.
- With IGNORECASE, only ASCII letters match regardless of case: a literal or
  class matches a character if it would match the character or its ASCII
  case-swapped form. `é` and `É` are different characters.

**Repetition and empty iterations.** A repetition `X{min,max}` (with `*`,
`+` and `?` being `{0,}`, `{1,}` and `{0,1}`) works like this. It counts
completed iterations `n` (starting at 0) and remembers `p`, the position
where its last *optional* iteration started (initially none). Before the
first iteration and after each one:

1. If `n < min`, run another iteration (mandatory: `p` is not changed).
2. Greedy: if `n < max` and the current position is not `p`, set `p` to the
   current position and try another iteration; if that fails, continue with
   the rest of the pattern. Otherwise continue with the rest of the pattern.
3. Lazy: try the rest of the pattern first. If that fails and `n < max` and
   the current position is not `p`, set `p` to the current position and try
   another iteration. Otherwise fail.

So an optional iteration that matched the empty string ends the repetition,
but whatever groups it set keep their spans: `(a|)*` on `aa` gives group 1 =
`2,2`, `(a?){2}` on `a` gives `1,1`, and `(|a){2,3}` on `aa` matches
`0,0` with group 1 = `0,0`. When the matcher backtracks, `n` and `p` are
restored along with everything else.

## Replacement templates (sub)

The replacement is read left to right:

| Text | Meaning |
|---|---|
| any character but `\` | itself |
| `\g<name>` | the named group; an unknown name is an error |
| `\g<number>` | that group (`\g<0>` is the whole match) |
| `\1` … `\99` | that group, read like in patterns: one or two digits, unless they are three octal digits (an octal character code, at most 0o377) |
| `\0`, `\0o`, `\0oo` | octal character code |
| `\a \b \f \n \r \t \v` | the control characters 7, 8, 12, 10, 13, 9, 11 |
| `\\` | one backslash |
| `\` + any other ASCII letter | error |
| `\` + any other character | both characters kept: `\&` gives `\&` |
| `\` at the end | error |

A group number above the pattern's number of groups is an error. In
`\g<…>`, an empty name, a missing `<` or `>`, or a name that is neither an
identifier nor all digits is an error. A group that did not take part in the
match is replaced by the empty string.

## Out of scope

The hidden cases never use: flags `u` and `L`; `\N{…}`; group names with
non-ASCII characters; counts above 1,000,000; invalid UTF-8; patterns that
make Python take more than a second.
