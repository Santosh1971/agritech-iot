"""
Terminal UI helpers -- colors and a clean end-of-unit summary block.
Deliberately just this, not a TUI/curses redraw or a separate GUI --
matches the "guided CLI, keep it simple" decision. colorama makes ANSI
codes work on Windows cmd/PowerShell too, not just Mac/Linux terminals,
so this looks the same on both your and Avinash's machines.
"""
from __future__ import annotations

import colorama
from colorama import Fore, Style

colorama.init(autoreset=True)


def green(s: str) -> str:
    return f"{Fore.GREEN}{s}{Style.RESET_ALL}"


def red(s: str) -> str:
    return f"{Fore.RED}{s}{Style.RESET_ALL}"


def yellow(s: str) -> str:
    return f"{Fore.YELLOW}{s}{Style.RESET_ALL}"


def cyan(s: str) -> str:
    return f"{Fore.CYAN}{s}{Style.RESET_ALL}"


def bold(s: str) -> str:
    return f"{Style.BRIGHT}{s}{Style.RESET_ALL}"


def dim(s: str) -> str:
    return f"{Style.DIM}{s}{Style.RESET_ALL}"


def header(s: str) -> str:
    return bold(cyan(s))


def step_line(label: str, passed: "bool | None", detail: str = "") -> str:
    if passed is None:
        marker = dim("⏳")
        return dim(f"{marker} {label}")
    if passed:
        line = green(f"✅ {label}")
    else:
        line = red(f"❌ {label}")
    if detail:
        line += (dim(f" — {detail}") if passed else f" — {detail}")
    return line


def print_summary(steps: dict, overall_passed: bool) -> None:
    from .report import PRODUCTION_TEST_STEPS, STEP_LABELS

    width = 56
    print()
    print(header("=" * width))
    print(header("  RESULT SUMMARY"))
    print(header("=" * width))
    for name in PRODUCTION_TEST_STEPS:
        step = steps.get(name)
        label = STEP_LABELS.get(name, name)
        passed = step.passed if step else None
        detail = step.detail if step else ""
        print(step_line(label, passed, detail))
    print(header("=" * width))
    print(bold(green("  PASS")) if overall_passed else bold(red("  FAIL")))
    print(header("=" * width))
