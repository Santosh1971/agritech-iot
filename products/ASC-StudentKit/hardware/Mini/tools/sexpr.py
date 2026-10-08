"""Minimal S-expression reader/writer for KiCad files."""
import re

class Sym(str):
    """Unquoted atom."""

_tok = re.compile(r'\s*(?:(\()|(\))|("(?:[^"\\]|\\.)*")|([^\s()"]+))')

def parse(text):
    stack, cur = [], []
    pos = 0
    while True:
        m = _tok.match(text, pos)
        if not m:
            break
        pos = m.end()
        o, c, s, a = m.groups()
        if o:
            stack.append(cur); cur = []
        elif c:
            done = cur; cur = stack.pop(); cur.append(done)
        elif s is not None:
            cur.append(bytes(s[1:-1], 'utf-8').decode('unicode_escape') if '\\' in s else s[1:-1])
        else:
            cur.append(Sym(a))
    return cur[0]

def q(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

def dump(x, ind=0):
    if isinstance(x, list):
        if not x:
            return '()'
        simple = all(not isinstance(e, list) for e in x)
        if simple:
            return '(' + ' '.join(dump(e) for e in x) + ')'
        out = '(' + dump(x[0])
        for e in x[1:]:
            if isinstance(e, list):
                out += '\n' + '\t' * (ind + 1) + dump(e, ind + 1)
            else:
                out += ' ' + dump(e)
        return out + '\n' + '\t' * ind + ')'
    if isinstance(x, Sym):
        return str(x)
    if isinstance(x, (int, float)):
        return ('%.4f' % x).rstrip('0').rstrip('.') if isinstance(x, float) else str(x)
    return q(x)

def find(node, key):
    return [e for e in node if isinstance(e, list) and e and e[0] == key]

def first(node, key):
    r = find(node, key)
    return r[0] if r else None
