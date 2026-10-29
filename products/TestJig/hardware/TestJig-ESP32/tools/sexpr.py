"""Minimal KiCad S-expression parser / writer (works on Python 3.9+)."""


class Q(str):
    """A quoted string token."""


def parse(text):
    pos = 0
    n = len(text)
    stack = [[]]
    while pos < n:
        c = text[pos]
        if c in ' \t\r\n':
            pos += 1
        elif c == '(':
            stack.append([])
            pos += 1
        elif c == ')':
            lst = stack.pop()
            stack[-1].append(lst)
            pos += 1
        elif c == '"':
            pos += 1
            buf = []
            while text[pos] != '"':
                if text[pos] == '\\':
                    pos += 1
                    buf.append({'n': '\n', 't': '\t'}.get(text[pos], text[pos]))
                else:
                    buf.append(text[pos])
                pos += 1
            pos += 1
            stack[-1].append(Q(''.join(buf)))
        else:
            start = pos
            while pos < n and text[pos] not in ' \t\r\n()"':
                pos += 1
            stack[-1].append(text[start:pos])
    return stack[0]


def _q(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'


def dump(node, indent=0):
    if isinstance(node, Q):
        return _q(node)
    if isinstance(node, str):
        return node
    if isinstance(node, (int, float)):
        return fmt(node)
    simple = all(not isinstance(x, list) for x in node)
    if simple:
        return '(' + ' '.join(dump(x) for x in node) + ')'
    out = '(' + dump(node[0])
    for x in node[1:]:
        if isinstance(x, list):
            out += '\n' + '\t' * (indent + 1) + dump(x, indent + 1)
        else:
            out += ' ' + dump(x)
    return out + '\n' + '\t' * indent + ')'


def fmt(v):
    if isinstance(v, float):
        s = ('%.4f' % v).rstrip('0').rstrip('.')
        return '0' if s in ('-0', '') else s
    return str(v)


def find(node, key):
    for x in node:
        if isinstance(x, list) and x and x[0] == key:
            return x
    return None


def findall(node, key):
    return [x for x in node if isinstance(x, list) and x and x[0] == key]
