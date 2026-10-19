import sys, os, copy; sys.path.insert(0, os.path.dirname(__file__))
import sx
from sx import Sym
STOCK = os.environ.get('KICAD_SYMBOL_DIR', '/usr/share/kicad/symbols')
_cache = {}
def _lib(path):
    if path not in _cache:
        _cache[path] = sx.parse(open(path).read())
    return _cache[path]

def get_symbol(libname, name, path=None):
    """Return a flattened lib symbol named 'libname:name' ready for lib_symbols."""
    path = path or os.path.join(STOCK, libname + '.kicad_sym')
    lib = _lib(path)
    syms = {e[1]: e for e in sx.findall(lib, 'symbol')}
    s = copy.deepcopy(syms[name])
    ext = sx.find(s, 'extends')
    if ext:
        parent = get_symbol(libname, ext[1], path)
        pname = ext[1]
        flat = [x for x in parent if not (isinstance(x, list) and x and x[0] in ('property',))]
        # rename units
        out = [Sym('symbol'), libname + ':' + name]
        for x in flat[2:]:
            if isinstance(x, list) and x[0] == 'symbol':
                x = copy.deepcopy(x)
                x[1] = x[1].replace(pname, name, 1)
            out.append(x)
        # insert derived properties after the header flags
        props = sx.findall(s, 'property')
        idx = next(i for i, x in enumerate(out) if isinstance(x, list) and x[0] == 'symbol')
        out[idx:idx] = props
        return out
    s[1] = libname + ':' + name
    return s

def from_sch(sch_tree, full_name):
    ls = sx.find(sch_tree, 'lib_symbols')
    for e in sx.findall(ls, 'symbol'):
        if e[1] == full_name:
            return copy.deepcopy(e)
    raise KeyError(full_name)

def pins(sym):
    """{number: (x, y, angle, name, type)} in lib coordinates (y up)."""
    out = {}
    for u in sx.findall(sym, 'symbol'):
        for p in sx.findall(u, 'pin'):
            at = sx.find(p, 'at'); num = sx.find(p, 'number')[1]; nm = sx.find(p, 'name')[1]
            out[num] = (at[1], at[2], at[3], nm, str(p[1]))
    return out
