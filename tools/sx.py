"""Minimal KiCad S-expression reader/writer."""
import re

class Sym(str):
    """An unquoted atom."""
    pass

_tok = re.compile(r'\s*(?:(\()|(\))|"((?:[^"\\]|\\.)*)"|(\|[^|]*\|)|([^\s()"]+))', re.S)

_num = re.compile(r'-?\d+(\.\d+)?')

def parse(text):
    stack = [[]]
    pos = 0
    n = len(text)
    while pos < n:
        m = _tok.match(text, pos)
        if not m:
            if text[pos:].strip() == '':
                break
            raise ValueError('bad token at %d: %r' % (pos, text[pos:pos+40]))
        pos = m.end()
        if m.group(1):
            stack.append([])
        elif m.group(2):
            e = stack.pop()
            stack[-1].append(e)
        elif m.group(3) is not None:
            stack[-1].append(bytes(m.group(3), 'utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8') if '\\' in m.group(3) else m.group(3))
        elif m.group(4):
            stack[-1].append(Sym(m.group(4)))
        else:
            a = m.group(5)
            if _num.fullmatch(a):
                stack[-1].append(float(a))
            else:
                stack[-1].append(Sym(a))
    assert len(stack) == 1
    return stack[0][0]

def q(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'

def dump(e, ind=0):
    if not isinstance(e, list):
        return str(e) if isinstance(e, Sym) else (q(e) if isinstance(e, str) else fmtnum(e))
    head = dump(e[0]) if e else ''
    simple = all(not isinstance(x, list) for x in e)
    if simple and len(e) < 12:
        return '(' + ' '.join(dump(x) for x in e) + ')'
    # xy lists on one line
    if e and e[0] == 'pts':
        return '(pts\n' + '\t'*(ind+1) + ' '.join(dump(x) for x in e[1:]) + '\n' + '\t'*ind + ')'
    out = ['(' + head]
    i = 1
    while i < len(e) and not isinstance(e[i], list):
        out[0] += ' ' + dump(e[i]); i += 1
    for x in e[i:]:
        out.append('\t'*(ind+1) + dump(x, ind+1))
    out.append('\t'*ind + ')')
    return '\n'.join(out)

def fmtnum(v):
    if isinstance(v, bool):
        return 'yes' if v else 'no'
    if isinstance(v, int):
        return str(v)
    s = ('%.4f' % v).rstrip('0').rstrip('.')
    return '0' if s == '-0' else s

def S(*a):
    """Build a list; strings that look like keywords become Sym when first."""
    return [Sym(a[0])] + list(a[1:])

def find(e, key):
    for x in e:
        if isinstance(x, list) and x and x[0] == key:
            return x
    return None

def findall(e, key):
    return [x for x in e if isinstance(x, list) and x and x[0] == key]

def prop(e, name):
    for x in findall(e, 'property'):
        if x[1] == name:
            return x
    return None
