"""Cases for the rx task. Expected outputs come from Python's re (see
build_cases.py); only usage errors carry a hand-written "expect"."""
import random


def run(cat, op, pattern, subject, opts=(), repl=None):
    args = list(opts) + [op, pattern] + ([repl] if repl is not None else [])
    return {"cat": cat, "args": args, "stdin": subject}


def S(cat, p, s, *o):
    return run(cat, "search", p, s, o)


def M(cat, p, s, *o):
    return run(cat, "match", p, s, o)


def F(cat, p, s, *o):
    return run(cat, "fullmatch", p, s, o)


def FI(cat, p, s, *o):
    return run(cat, "finditer", p, s, o)


def SUB(cat, p, r, s, *o):
    return run(cat, "sub", p, s, o, r)


def SP(cat, p, s, *o):
    return run(cat, "split", p, s, o)


def E(p, s, ok, op="search", opts=(), repl=None, ok_repl=None):
    """A pattern (or template) that must be rejected, with a related valid control."""
    case = run("errors", op, p, s, opts, repl)
    case["control"] = (list(opts) + [op, ok] + ([ok_repl] if ok_repl is not None else []), s)
    return case


def U(args, stdin, control):
    return {"cat": "cli", "args": args, "stdin": stdin, "expect": ("", 2), "control": (control, stdin)}


TEXT = "The quick brown fox jumps over the lazy dog.\nPack my box with 5 dozen liquor jugs!\n"
LOG = "2024-01-15 ERROR disk full\n2024-01-16 WARN  cpu 93%\n2024-02-01 INFO  ok\n"

# ---------------------------------------------------------------- larger subjects

_rng = random.Random(1029)
_words = ["alpha", "beta", "gamma", "delta", "omega", "kappa", "sigma", "tau", "rho", "phi"]


def _text(n):
    parts = []
    size = 0
    while size < n:
        w = _rng.choice(_words)
        if _rng.random() < 0.05:
            w = f"{_rng.randint(1000, 9999)}-{_rng.randint(1, 12):02d}-{_rng.randint(1, 28):02d}"
        if _rng.random() < 0.02:
            w = f"user{_rng.randint(1, 999)}@host{_rng.randint(1, 9)}.com"
        sep = _rng.choice([" ", " ", " ", ", ", "\n", "  "])
        parts.append(w + sep)
        size += len(w) + len(sep)
    return "".join(parts)


BIG_TEXT = _text(300_000)
BIG_AB = "".join(_rng.choice("ab") for _ in range(200_000))
BIG_A = "a" * 400_000
BIG_CSV = ",".join(str(_rng.randint(0, 99)) for _ in range(30_000))
BIG_PRICES = " ".join(f"item{i} ${_rng.randint(1, 500)}" for i in range(20_000))

HIDDEN = [
    # ------------------------------------------------------------ literals
    S("literal", "fox", TEXT),
    S("literal", "cat", TEXT),
    S("literal", "", "abc"),
    S("literal", "", ""),
    S("literal", "dog.", TEXT),
    M("literal", "The", TEXT),
    M("literal", "quick", TEXT),
    F("literal", "abc", "abc"),
    F("literal", "abc", "abcd"),
    S("literal", "a]b}c", "xa]b}c"),
    S("literal", "jugs!\n", TEXT),
    FI("literal", "o", TEXT),

    # ------------------------------------------------------------ escapes
    S("escapes", r"\.", "a.b"),
    S("escapes", r"\n\t", "a\n\tb"),
    S("escapes", r"\x41\x62", "zAb"),
    S("escapes", r"été", "café été"),
    S("escapes", r"\U0001F600", "smile \U0001F600!"),
    S("escapes", r"\101\0\01\012", "A\x00\x01\n"),
    S("escapes", r"\\", "a\\b"),
    S("escapes", r"\a\f\v\r", "x\a\f\v\ry"),
    S("escapes", r"\-\#\ \&\~\:", "-# &~:"),
    S("escapes", r"\$\^\*\+\?\(\)\[\]\{\}\|", "$^*+?()[]{}|"),
    S("escapes", r"\é", "café"),
    S("escapes", r"a\0b", "a\x00b"),
    S("escapes", r"[\101-\103]+", "xxABCD"),

    # ------------------------------------------------------------ classes
    S("classes", r"[aeiou]+", "rhythm and blues"),
    S("classes", r"[^aeiou ]+", "a quick test"),
    S("classes", r"[a-z0-9_]+", "--Hello_World42--"),
    S("classes", r"[]a]+", "x]a]b"),
    S("classes", r"[^]a]+", "]]xyz]"),
    S("classes", r"[a-]+", "x-a-b"),
    S("classes", r"[-a]+", "x-a-b"),
    S("classes", r"[]-a]+", "x^_`]a"),
    S("classes", r"[\d.]+", "pi=3.14159!"),
    S("classes", r"[\w\s]+", "!!a b_c 9!!"),
    S("classes", r"[^\W\d]+", "123abc456"),
    S("classes", r"[\D]+", "12ab34"),
    S("classes", r"[\S]+", "  xy  "),
    S("classes", r"[\]\\\-]+", "a]\\-b"),
    S("classes", r"[\b]", "a\bb"),
    S("classes", r"[.*+?(){}|$^]+", "x.*+?(){}|$^y"),
    S("classes", r"[\x41-\x43é]+", "zzBCAé!"),
    S("classes", r"[à-ÿ]+", "naïve café"),
    S("classes", r"[^\n]+", "first\nsecond"),
    FI("classes", r"\d+", "a1b22c333"),
    FI("classes", r"\w+", "foo-bar baz_9"),
    FI("classes", r"\s+", "a \t\nb\r\fc\vd"),
    FI("classes", r"\W+", "foo-bar baz_9!"),
    S("classes", r"\d", "١٢ 3"),
    S("classes", r"\w+", "été ok"),
    S("classes", r"\s", "a b c"),
    S("classes", r"[[]+", "a[[b"),

    # ------------------------------------------------------------ anchors
    S("anchors", r"^abc", "abc"),
    S("anchors", r"^abc", "xabc"),
    S("anchors", r"abc$", "xabc"),
    S("anchors", r"abc$", "xabc\n"),
    S("anchors", r"abc$", "xabc\n\n"),
    S("anchors", r"abc\Z", "xabc\n"),
    S("anchors", r"\Aabc", "abc\nabc"),
    FI("anchors", r"^\w+", "one\ntwo\nthree"),
    FI("anchors", r"^\w+", "one\ntwo\nthree", "-m"),
    FI("anchors", r"\w+$", "one\ntwo\nthree\n", "-m"),
    FI("anchors", r"\w+$", "one\ntwo\nthree\n"),
    FI("anchors", r"$", "a\nb\n", "-m"),
    FI("anchors", r"^", "a\nb\n", "-m"),
    FI("anchors", r"\b", "ab cd"),
    FI("anchors", r"\B", "ab cd"),
    S("anchors", r"\B", ""),
    S("anchors", r"\b", ""),
    S("anchors", r"^$", ""),
    FI("anchors", r"\bfoo\b", "foo foobar barfoo foo_ (foo)"),
    S("anchors", r"\Bo\B", "foo boot"),
    M("anchors", r"\Aa", "a"),
    FI("anchors", r"\A|\Z", "ab"),
    FI("anchors", r"$", "x\n"),

    # ------------------------------------------------------------ quantifiers
    S("quantifiers", r"ab*c", "ac abc abbbc"),
    S("quantifiers", r"ab+c", "ac abc abbbc"),
    S("quantifiers", r"ab?c", "abbc abc"),
    S("quantifiers", r"a{3}", "aa aaaa"),
    S("quantifiers", r"a{2,3}", "a aaaaa"),
    S("quantifiers", r"a{2,}", "a aaaaa"),
    S("quantifiers", r"a{,2}", "aaaa"),
    S("quantifiers", r"a{,}", "aaaa"),
    S("quantifiers", r"a{0}b", "aab"),
    S("quantifiers", r"a{}", "a{}"),
    S("quantifiers", r"a{x}", "a{x}"),
    S("quantifiers", r"a{1,x}", "a{1,x}"),
    S("quantifiers", r"a{ 1}", "a{ 1}"),
    S("quantifiers", r"(ab)*", "ababab"),
    S("quantifiers", r"(ab)+c", "xababc"),
    S("quantifiers", r"(a|b)*c", "abbac"),
    S("quantifiers", r"(a?){2}", "a"),
    S("quantifiers", r"(a|)*", "aa"),
    S("quantifiers", r"(a|)*", ""),
    S("quantifiers", r"(a*)+", "b"),
    S("quantifiers", r"(a*)*", "b"),
    S("quantifiers", r"(|a){2,3}", "aa"),
    S("quantifiers", r"(a){2,}", "aaaa"),
    S("quantifiers", r"(?:(a)|b)*", "ab"),
    S("quantifiers", r"(?:(a)|(b))+", "abba"),
    S("quantifiers", r"(\d{2}){2,3}", "1234567"),
    S("quantifiers", r".*x", "abcxdefx"),
    S("quantifiers", r"(.*)(\d+)", "abc 12345"),
    S("quantifiers", r"(x+x+)+y", "xxxxxxy"),
    S("quantifiers", r"(?:a+|b)*$", "aabab"),
    S("quantifiers", r"(a{2}){0,2}b", "aaaab"),
    S("quantifiers", r"(?:)*", "abc"),
    S("quantifiers", r"((a)|b)+", "ab"),
    S("quantifiers", r"(a?)+?b", "aab"),

    # ------------------------------------------------------------ lazy, possessive, atomic
    S("lazy", r"a.*?b", "a1b2b"),
    S("lazy", r"a.+?b", "ab a1b"),
    S("lazy", r"a??b", "aab"),
    S("lazy", r"<.+?>", "<a><b>"),
    S("lazy", r"(\d+?)(\d*)", "12345"),
    S("lazy", r"a{2,4}?", "aaaaa"),
    S("lazy", r"(a{2,4}?)a", "aaaaa"),
    S("lazy", r"(ab)*?c", "ababc"),
    S("lazy", r"(ab)+?", "ababab"),
    S("lazy", r"(?:a|ab)*?c", "abac"),
    FI("lazy", r"\w*?", "ab"),
    S("lazy", r"a*+a", "aaa"),
    S("lazy", r"a++b", "aaab"),
    S("lazy", r"a?+a", "a"),
    S("lazy", r"a{1,2}+a", "aaa"),
    S("lazy", r"(?>a*)a", "aaa"),
    S("lazy", r"(?>a|ab)c", "abc"),
    S("lazy", r"(?>(a+))b", "aaab"),
    S("lazy", r"(ab)*+ab", "ababab"),
    S("lazy", r"x(?>[^x]*)x", "x..x..x"),
    S("lazy", r"(?>.*)$", "anything"),
    S("lazy", r'"(?:[^"\\]++|\\.)*+"', r'say "hi \"there\"" ok'),

    # ------------------------------------------------------------ groups and alternation
    S("groups", r"(a)(b)(c)", "xabc"),
    S("groups", r"(a(b(c)))", "abc"),
    S("groups", r"(a)|(b)", "b"),
    S("groups", r"(?:ab)+", "ababx"),
    S("groups", r"(?P<word>\w+) (?P<num>\d+)", "item 42"),
    S("groups", r"()", "x"),
    S("groups", r"(?:)", "x"),
    S("groups", r"a(?#comment)b", "ab"),
    S("groups", r"(a)(?:b)(c)", "abc"),
    S("groups", r"((a)|(b))*", "ab"),
    S("groups", r"(a|ab)(c|bcd)(d*)", "abcd"),
    S("groups", r"cat|category", "category"),
    S("groups", r"category|cat", "category"),
    S("groups", r"a|b|c", "xyzc"),
    S("groups", r"|a", "a"),
    S("groups", r"a|", "b"),
    S("groups", r"(?:x|y|)z", "z"),
    FI("groups", r"red|green|blue", "green blue red"),
    S("groups", r"(?P<a>x)(?P<b>y)?(?P<c>z)", "xz"),
    S("groups", r"((((((((((a))))))))))", "a"),
    S("groups", r"(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)", "abcdefghijk"),

    # ------------------------------------------------------------ backreferences
    S("backrefs", r"(\w)\1", "abccd"),
    S("backrefs", r"(\w+) \1", "hello hello world"),
    S("backrefs", r"(?P<q>['\"]).*?(?P=q)", "say 'hi' \"there\""),
    S("backrefs", r"(a)|\1", "x"),
    S("backrefs", r"(a)?b\1", "b"),
    S("backrefs", r"(a*)b\1", "aabaa"),
    S("backrefs", r"(\w)\1", "aA", "-i"),
    S("backrefs", r"(?i)(ab)\1", "abAB"),
    S("backrefs", r"(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)\10", "abcdefghijj"),
    S("backrefs", r"(a)\1{2}", "aaaa"),
    S("backrefs", r"^(\w+)\s+\1$", "bye bye"),
    S("backrefs", r"(x)\1*", "xxxxy"),
    S("backrefs", r"(?:(a)|b)\1", "bb ab aa"),
    S("backrefs", r"(\d)(?P<x>\d)(?P=x)\1", "1221 1223"),

    # ------------------------------------------------------------ lookaround
    S("lookaround", r"foo(?=bar)", "foobaz foobar"),
    S("lookaround", r"foo(?!bar)", "foobar foobaz"),
    S("lookaround", r"(?<=\$)\d+", "cost: $42"),
    S("lookaround", r"(?<!\$)\b\d+", "$42 17"),
    S("lookaround", r"(?<=ab|cd)e", "cde"),
    S("lookaround", r"(?<=a.c)d", "abcd"),
    S("lookaround", r"(?<=(a)(b))c", "abc"),
    S("lookaround", r"(?=(\w+))\w", "abc"),
    S("lookaround", r"(?!(a))\w", "ab"),
    S("lookaround", r"^(?=.*\d)(?=.*[a-z]).{6,}$", "abc123"),
    S("lookaround", r"^(?=.*\d)(?=.*[a-z]).{6,}$", "abcdef"),
    FI("lookaround", r"(?<=,)", "a,b,c"),
    FI("lookaround", r"\d(?=(?:\d{3})+\b)", "1234567"),
    S("lookaround", r"(?<=^)a", "a"),
    S("lookaround", r"(?<!^)a", "aa"),
    S("lookaround", r"(?<=\b)x", "ax x"),
    S("lookaround", r"a(?=b)*c", "ac"),
    S("lookaround", r"(?<=a{3})b", "aaab"),
    S("lookaround", r"(?<!\d{2})\d", "12 3"),
    S("lookaround", r"(?<=(?P<p>a))(?P=p)", "aa"),
    S("lookaround", r"x(?=y(?=z))", "xyw xyz"),

    # ------------------------------------------------------------ flags
    S("flags", r"hello", "HeLLo", "-i"),
    S("flags", r"[a-z]+", "ABC def", "-i"),
    S("flags", r"[^a-z]+", "abcDEF123", "-i"),
    S("flags", r"\w+", "Été", "-i"),
    S("flags", r"é", "É", "-i"),
    S("flags", r"[Z-a]+", "z[\\]^_`A", "-i"),
    S("flags", r"a.c", "a\nc"),
    S("flags", r"a.c", "a\nc", "-s"),
    FI("flags", r".+", "ab\ncd"),
    FI("flags", r".+", "ab\ncd", "-s"),
    S("flags", r"(?i)ABC", "xabc"),
    S("flags", r"(?s)a.b", "a\nb"),
    S("flags", r"(?m)^b", "a\nb"),
    S("flags", r"(?i)a|B", "xb"),
    S("flags", r"(?#c)(?i)a", "A"),
    S("flags", r"(?im)^B$", "a\nb\nc"),
    S("flags", r"(?i:a)B", "AB Ab"),
    S("flags", r"(?i:a)B", "Ab"),
    S("flags", r"a(?i:b(?-i:c)d)e", "aBcDe aBCDe"),
    S("flags", r"(?-i:a)b", "Ab aB", "-i"),
    S("flags", r"(?s:.)\.", "\n."),
    S("flags", r"(?m:^b)|^c", "a\nb"),
    S("flags", r"(?a)\w+", "abc"),
    S("flags", r"x", "X", "-i", "-m", "-s"),

    # ------------------------------------------------------------ verbose
    S("verbose", r"a b c", "abc", "-x"),
    S("verbose", "a  # first\n b # second\n", "ab", "-x"),
    S("verbose", r"(?x) \d+ \s* \w+", "12   apples"),
    S("verbose", r"a\ b", "a b", "-x"),
    S("verbose", r"a[ ]b", "a b", "-x"),
    S("verbose", r"a\#b", "a#b", "-x"),
    S("verbose", r"a {2}", "aa", "-x"),
    S("verbose", r"(?x: a b ) c", "ab c"),
    S("verbose", r"(?x)a (?-x: b ) c", "a b c"),
    S("verbose", "(?x)\n  (?P<y> \\d{4} ) -  # year\n  (?P<m> \\d{2} )      # month\n", "on 2024-05"),

    # ------------------------------------------------------------ conditionals
    S("conditional", r"(a)?(?(1)b|c)", "ab"),
    S("conditional", r"(a)?(?(1)b|c)", "c"),
    S("conditional", r"(a)?(?(1)b)", "x"),
    S("conditional", r"(?P<q><)?\w+(?(q)>)", "<tag> word"),
    S("conditional", r"^(\()?\d+(?(1)\))$", "(123)"),
    S("conditional", r"^(\()?\d+(?(1)\))$", "(123"),
    S("conditional", r"(?(1)a|b)(x)?", "b"),
    S("conditional", r"(?:(a)|b)(?(1)c|d)", "bd ac"),
    S("conditional", r"(a)?(?(1)|z)", "z"),

    # ------------------------------------------------------------ unicode
    S("unicode", r"caf.", "un café"),
    S("unicode", r"é+", "ééé!"),
    FI("unicode", r".", "é中\U0001F600"),
    S("unicode", r"[一-鿿]+", "abc中文def"),
    S("unicode", r"\U0001F600+", "x\U0001F600\U0001F600"),
    SUB("unicode", r"é", "e", "café été"),
    S("unicode", r"(?<=é)t", "été"),
    SP("unicode", r"·", "a·b·c"),

    # ------------------------------------------------------------ finditer
    FI("finditer", r"x*", "axxb"),
    FI("finditer", r"a*?|b", "ab"),
    FI("finditer", r"(?:)", "ab"),
    FI("finditer", r"", ""),
    FI("finditer", r"\b|x", "ax"),
    FI("finditer", r"a|", "baa"),
    FI("finditer", r"(a)|(b)", "abc"),
    FI("finditer", r"(\w)(\w)?", "abcde"),
    FI("finditer", r"aa", "aaaaa"),
    FI("finditer", r"(?=a)", "aaa"),
    FI("finditer", r"\d*", "12ab3"),
    FI("finditer", r"^|a", "aa"),
    FI("finditer", r"$|b", "ab", "-m"),
    FI("finditer", r"(?<=a)|b", "ab"),
    FI("finditer", r".*", "ab\ncd\n"),

    # ------------------------------------------------------------ sub
    SUB("sub", r"\d+", "#", "a1b22c333"),
    SUB("sub", r"(\w+)@(\w+)", r"\2 at \1", "joe@site and ann@web"),
    SUB("sub", r"(?P<first>\w+) (?P<last>\w+)", r"\g<last>, \g<first>", "Ada Lovelace"),
    SUB("sub", r"(a)", r"[\g<0>|\g<1>|\1]", "banana"),
    SUB("sub", r"x*", "-", "abc"),
    SUB("sub", r"a|", "-", "baac"),
    SUB("sub", r"", "^", ""),
    SUB("sub", r"o", "0", "foo boo", "-n", "2"),
    SUB("sub", r"o", "0", "foo boo", "-n", "0"),
    SUB("sub", r"(a)|b", r"[\1]", "ab"),
    SUB("sub", r"\s+", " ", "a \t\n b   c"),
    SUB("sub", r"(\d)", r"\1\1", "a1b2"),
    SUB("sub", r"x", r"\n\t\\", "axb"),
    SUB("sub", r"x", r"\&\-\.\é", "axb"),
    SUB("sub", r"x", r"\a\b\f\v\r", "x"),
    SUB("sub", r"(x)", r"\101\0\012\1", "x"),
    SUB("sub", r"(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)", r"\10\9\g<1>0", "abcdefghij"),
    SUB("sub", r"(?i)HELLO", "bye", "hello Hello HELLO"),
    SUB("sub", r"^", "> ", "a\nb\nc", "-m"),
    SUB("sub", r"$", ";", "a\nb\n", "-m"),
    SUB("sub", r"\b", "|", "ab cd"),
    SUB("sub", r"(?<=\d)(?=(\d{3})+$)", ",", "1234567"),
    SUB("sub", r"zzz", "y", "abc"),
    SUB("sub", r"é", r"\g<0>\g<0>", "été"),
    SUB("sub", r"(a)|(b)", r"\2\1", "abab"),
    SUB("sub", r"a", "", "banana"),

    # ------------------------------------------------------------ split
    SP("split", r",", "a,b,,c"),
    SP("split", r"\s+", " a b  c "),
    SP("split", r"(,)", "a,b"),
    SP("split", r"(x)|y", "axbyc"),
    SP("split", r"x*", "axbc"),
    SP("split", r"", "abc"),
    SP("split", r"\b", "ab cd"),
    SP("split", r",", "a,b,c,d", "-n", "2"),
    SP("split", r"z", "abc"),
    SP("split", r",", ""),
    SP("split", r"(?=b)", "abab"),
    SP("split", r"(a)(b)?", "xaybaz"),
    SP("split", r"^", "a\nb", "-m"),

    # ------------------------------------------------------------ errors
    E(r"a**", "aa", r"a*"),
    E(r"a*?+", "aa", r"a*?"),
    E(r"a{2}*", "aa", r"a{2}"),
    E(r"*a", "aa", r"a*"),
    E(r"a|*b", "ab", r"a|b*"),
    E(r"(*)", "a", r"(a*)"),
    E(r"^*", "a", r"^a*"),
    E(r"\b+", "a", r"\b"),
    E(r"(a", "a", r"(a)"),
    E(r"a)", "a", r"(a)"),
    E(r"[a", "a", r"[a]"),
    E(r"[]", "]", r"[]]"),
    E(r"[z-a]", "a", r"[a-z]"),
    E(r"[\d-z]", "a", r"[\d\-z]"),
    E(r"[a-\w]", "a", r"[a\-\w]"),
    E(r"a{3,2}", "aaa", r"a{2,3}"),
    E(r"\q", "q", r"q"),
    E(r"[\q]", "q", r"[q]"),
    E(r"[\A]", "A", r"[A]"),
    E(r"\x4", "J", r"\x4a"),
    E(r"\u12", "J", r"J"),
    E("a\\", "a", r"a\\"),
    E(r"(a)\2", "aa", r"(a)\1"),
    E(r"(a\1)", "aa", r"(a)\1"),
    E(r"\1(a)", "aa", r"(a)\1"),
    E(r"(?P<n>a)(?P=m)", "aa", r"(?P<n>a)(?P=n)"),
    E(r"(?P<n>a)(?P<n>b)", "ab", r"(?P<n>a)(?P<m>b)"),
    E(r"(?P<1a>x)", "x", r"(?P<a1>x)"),
    E(r"(?P<>x)", "x", r"(?P<x>x)"),
    E(r"(?<=a+)b", "ab", r"(?<=a)b"),
    E(r"(?<=a|bc)d", "bcd", r"(?<=ab|bc)d"),
    E(r"(?Q)", "a", r"(?:a)"),
    E(r"a(?i)b", "ab", r"(?i)ab"),
    E(r"a|(?i)b", "ab", r"(?i)a|b"),
    E(r"(?i-i:a)", "a", r"(?-i:a)"),
    E(r"(?z)a", "a", r"(?i)a"),
    E(r"(?(1)a|b|c)(x)", "a", r"(?(1)a|b)(x)"),
    E(r"(?(2)a)(x)", "a", r"(?(1)a)(x)"),
    E(r"(?(x)a)", "a", r"(?P<x>b)?(?(x)a)"),
    E(r"(?#unterminated", "a", r"(?#done)a"),
    E(r"x", "x", r"x", op="sub", repl=r"\1", ok_repl=r"\g<0>"),
    E(r"(a)", "zzz", r"(a)", op="sub", repl=r"\g<x>", ok_repl=r"\g<1>"),
    E(r"(a)", "a", r"(a)", op="sub", repl=r"\q", ok_repl=r"\\q"),
    E(r"(a)", "a", r"(a)", op="sub", repl=r"\g<1", ok_repl=r"\g<1>"),
    E(r"(a)", "a", r"(a)", op="sub", repl="x\\", ok_repl=r"x\\"),
    E(r"(a)", "a", r"(a)", op="sub", repl=r"\g<a b>", ok_repl=r"\g<1>"),
    E(r"\Z*", "a", r"\Z"),
    E(r"{2}", "{2}", r"\{2}"),
    E(r"(?<=(a)\1)b", "aab", r"(a)(?<=\1)b"),
    E(r"(?<!(?P<x>a)(?P=x))b", "ab", r"(?P<x>a)(?<!(?P=x))b"),
    E(r"(?<=(a)(?(1)b|c))d", "abd", r"(a)(?<=(?(1)b|c))d"),
    E(r"x{1}{2}", "xx", r"x{1}x{1}"),

    # ------------------------------------------------------------ command line
    U([], "abc", ["search", "b"]),
    U(["search"], "abc", ["search", "b"]),
    U(["find", "b"], "abc", ["search", "b"]),
    U(["-q", "search", "b"], "abc", ["-i", "search", "B"]),
    U(["search", "a", "b"], "abc", ["sub", "a", "b"]),
    U(["sub", "a"], "abc", ["sub", "a", "x"]),
    U(["-n", "2", "search", "a"], "aaa", ["-n", "2", "sub", "a", "b"]),
    U(["-n", "x", "split", "a"], "aaa", ["-n", "1", "split", "a"]),
    U(["split", "a", "-n", "1"], "aaa", ["-n", "1", "split", "a"]),
    run("cli", "search", "-?\\d+", "x -12 y"),
    run("cli", "search", "-i", "I"),
    run("cli", "match", "a", ""),

    # ------------------------------------------------------------ larger inputs
    FI("perf", r"\b\d{4}-\d{2}-\d{2}\b", BIG_TEXT),
    FI("perf", r"\b[\w.]+@\w+\.com\b", BIG_TEXT),
    S("perf", r"(?:ab)+c", "ab" * 150_000 + "c"),
    F("perf", r"(?:a|b)*", BIG_AB),
    F("perf", r"(a|b)*", BIG_AB),
    S("perf", r"a*b", BIG_A + "b"),
    S("perf", r"(a+)+b", "a" * 30 + "b"),
    S("perf", r"\w+$", BIG_TEXT.replace("\n", " ") + "end"),
    SUB("perf", r"\s+", " ", BIG_TEXT),
    SP("perf", r",", BIG_CSV),
    FI("perf", r"(?<=\$)\d+(?=\D|$)", BIG_PRICES[:120_000]),
    S("perf", r".*sigma", BIG_TEXT.replace("\n", " ")),
    S("perf", r"(\w+) \1", BIG_TEXT.replace("\n", " ")),
    SUB("perf", r"(?i)ALPHA|beta", "X", BIG_TEXT[:100_000]),
]

EXAMPLES = [
    S("literal", "fox", TEXT),
    M("literal", "quick", TEXT),
    F("literal", "abc", "abc"),
    S("escapes", r"\x41\n", "zA\n"),
    S("classes", r"[a-c]+", "xxabcab!"),
    S("classes", r"[]^-]+", "a^]-b"),
    FI("classes", r"\w+", "hi, there!"),
    S("anchors", r"world$", "hello world\n"),
    FI("anchors", r"^\w", "a\nb", "-m"),
    S("quantifiers", r"(ab)+", "xababy"),
    S("quantifiers", r"a{2,}?", "aaaa"),
    S("quantifiers", r"(a|)+", "aa"),
    S("lazy", r"a.*?b", "aXbYb"),
    S("lazy", r"(?>a+)ab", "aaab"),
    S("groups", r"(?P<k>\w+)=(?P<v>\w*)", "key=value"),
    S("groups", r"(a)|(b)", "b"),
    S("backrefs", r"(\w)\1", "abbc"),
    S("lookaround", r"(?<=\$)\d+", "$5"),
    S("lookaround", r"\w+(?!\w|\()", "call(x) y"),
    S("flags", r"(?i)abc", "ABC"),
    S("flags", r"a.b", "a\nb", "-s"),
    S("verbose", r"a b # comment", "ab", "-x"),
    S("conditional", r"(<)?a(?(1)>)", "<a>"),
    FI("finditer", r"x*", "axb"),
    SUB("sub", r"(\w+) (\w+)", r"\2 \1", "hello world"),
    SUB("sub", r"o", "0", "foo", "-n", "1"),
    SUB("sub", r"", "-", "ab"),
    SP("split", r"(,)|;", "a,b;c"),
    SP("split", r"", "ab"),
    S("unicode", r"é.", "café!"),
    E(r"a{2,1}", "aa", r"a{1,2}"),
    E(r"(?<=a*)b", "ab", r"(?<=a)b"),
    E(r"(a)", "a", r"(a)", op="sub", repl=r"\2", ok_repl=r"\1"),
    U(["search"], "abc", ["search", "b"]),
    FI("perf", r"\d+", " ".join(str(i) for i in range(2000))),
]
