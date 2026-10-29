#!/usr/bin/env python3
"""Builds the cases for the rx task. Not used by the verifier.

    tests/cases/<id>/                   hidden cases
    environment/workspace/examples/     public cases

Each case directory holds:
    args       the command-line arguments, each followed by a NUL byte
    stdin      the subject text (UTF-8)
    stdout     the expected output
    code       the expected exit status
    category   the case's category
    control/   (cases that expect an error) a valid command line that must
               also be answered correctly: args, stdin and stdout, exit 0

Every expected output comes from Python's re module with re.ASCII, except the
usage errors, which are written by hand.
"""
import os
import re
import shutil
import sys

from case_list import EXAMPLES, HIDDEN

HERE = os.path.dirname(os.path.abspath(__file__))
TASK = os.path.dirname(HERE)
HIDDEN_DIR = os.path.join(HERE, "cases")
EXAMPLE_DIR = os.path.join(TASK, "environment", "workspace", "examples")

FLAG_OPTS = {"-i": re.IGNORECASE, "-m": re.MULTILINE, "-s": re.DOTALL, "-x": re.VERBOSE}


def span_line(m, groups):
    out = []
    for g in range(groups + 1):
        a, b = m.span(g)
        out.append("-" if a < 0 else f"{a},{b}")
    return " ".join(out) + "\n"


def expected(args, subject):
    """What Python's re gives for an rx command line: (stdout, exit_code)."""
    flags = re.ASCII
    count = 0
    i = 0
    while i < len(args) and args[i].startswith("-"):
        if args[i] == "-n":
            count = int(args[i + 1])
            i += 2
            continue
        flags |= FLAG_OPTS[args[i]]
        i += 1
    op, pattern, rest = args[i], args[i + 1], args[i + 2:]
    try:
        rx = re.compile(pattern, flags)
    except re.error:
        return "", 1
    if op in ("search", "match", "fullmatch"):
        m = getattr(rx, op)(subject)
        return (span_line(m, rx.groups) if m else "none\n"), 0
    if op == "finditer":
        return "".join(span_line(m, rx.groups) for m in rx.finditer(subject)), 0
    if op == "sub":
        try:
            return rx.sub(rest[0], subject, count=count), 0
        except (re.error, IndexError):
            return "", 1
    if op == "split":
        lines, pieces, last, n = [], [], 0, 0
        for m in rx.finditer(subject):
            if count and n >= count:
                break
            lines.append(f"{last},{m.start()}\n")
            pieces.append(subject[last:m.start()])
            for g in range(1, rx.groups + 1):
                a, b = m.span(g)
                lines.append("-\n" if a < 0 else f"{a},{b}\n")
                pieces.append(None if a < 0 else subject[a:b])
            last = m.end()
            n += 1
        lines.append(f"{last},{len(subject)}\n")
        pieces.append(subject[last:])
        assert pieces == rx.split(subject, maxsplit=count), (pattern, subject)
        return "".join(lines), 0
    raise ValueError(op)


def write_run(directory, args, subject, stdout, code):
    os.makedirs(directory, exist_ok=True)
    with open(os.path.join(directory, "args"), "wb") as f:
        for a in args:
            f.write(a.encode("utf-8") + b"\0")
    with open(os.path.join(directory, "stdin"), "wb") as f:
        f.write(subject.encode("utf-8"))
    with open(os.path.join(directory, "stdout"), "wb") as f:
        f.write(stdout.encode("utf-8"))
    with open(os.path.join(directory, "code"), "w") as f:
        f.write(f"{code}\n")


def build(specs, root, prefix):
    if os.path.isdir(root):
        for name in os.listdir(root):
            path = os.path.join(root, name)
            if os.path.isdir(path) and name.startswith(prefix):
                shutil.rmtree(path)
    errors = 0
    for n, spec in enumerate(specs, 1):
        cid = f"{prefix}{n:03d}"
        d = os.path.join(root, cid)
        if "expect" in spec:
            stdout, code = spec["expect"]
        else:
            stdout, code = expected(spec["args"], spec["stdin"])
        write_run(d, spec["args"], spec["stdin"], stdout, code)
        with open(os.path.join(d, "category"), "w") as f:
            f.write(spec["cat"] + "\n")
        if code != 0:
            errors += 1
            if "control" not in spec:
                sys.exit(f"{cid}: a failing case needs a control: {spec}")
        if "control" in spec:
            cargs, cin = spec["control"]
            cout, ccode = expected(cargs, cin)
            if code == 0 or ccode != 0:
                sys.exit(f"{cid}: the case must fail and its control must succeed: {spec}")
            write_run(os.path.join(d, "control"), cargs, cin, cout, 0)
    return len(specs), errors


def main():
    h = build(HIDDEN, HIDDEN_DIR, "h")
    e = build(EXAMPLES, EXAMPLE_DIR, "e")
    print(f"hidden: {h[0]} cases ({h[1]} expect an error), examples: {e[0]} cases ({e[1]} expect an error)")


if __name__ == "__main__":
    main()
