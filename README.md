# rx: Python-compatible regular expressions in C

This is a Harbor task in the FrontierSWE format. The agent starts with a stub
C program and has to build `rx`, a regular-expression engine that matches
Python 3.13's `re` module (in ASCII mode) exactly. That covers Python's
pattern syntax and its exact error rules, backtracking with groups, lazy,
possessive and atomic repetition, lookaround, backreferences, conditionals,
inline and verbose flags, and the `search`, `match`, `fullmatch`, `finditer`,
`sub` and `split` operations with Python's empty-match rules and replacement
templates. Only the C standard library is allowed, `<regex.h>` is off limits,
the container has no network, and Python is not installed.

## How it's scored

The verifier rebuilds the agent's code with `make` as an unprivileged user and
runs the binary on 370 hidden cases, each with its own subject on stdin. Every
expected output was produced by Python's `re`. A case passes only if stdout
matches byte for byte and the exit status matches. Cases that expect an error
also carry a valid control command that must succeed, so a program that
rejects everything scores 0. The score is the fraction of cases that pass.

## What's in here

- `instruction.md`: what the agent is told
- `task.toml`: time limits and resources (the agent gets 20 hours)
- `environment/`: the Dockerfile and the starting code, with 35 public example cases
- `tests/`: the hidden verifier (`test.sh`, plain bash) and its cases, plus
  `build_cases.py` and `case_list.py`, which regenerate the cases with Python
- `solution/`: the reference implementation

## Running it

```
harbor run -p . -a oracle   # reference solution, scores 1.0
harbor run -p . -a nop      # does nothing, scores 0.0
```
