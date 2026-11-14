"""
The ngspice netlist subset the network tool reads, flattened to linear elements.

Elements: R, C, L, V (the AC input; other sources are AC shorts), I (AC open), E (VCVS, or an ideal
op-amp when marked as a nullor), G (VCCS), X (subcircuit instances). Dot cards: .param, .subckt
with params:, .ends, .include, .end; .control blocks, .ac/.op/.tran and .option are ignored.
Values are SPICE numbers (scale suffixes) or {expressions} over parameters, translated to exact
sympy expressions (decimals become rationals). Directives live in comments, so ngspice runs the
same file unchanged:

    *@ input  <V source name>      the AC source (default: the only V source with an AC value)
    *@ output <node>               the output node, measured to ground
    *@ pot    <param> [default]    a parameter that is a pot position, 0 ... 1
    *@ switch <param> <positions> [default]   a parameter that is a switch, 0 ... positions-1
    *@ step   <param> <value> <value> ...     a component value on a rotary switch's detents (a
                                   stepped control: one topology, the value glides between detents)
    *@ nullor <E element>          an ideal op-amp (inside a .subckt it applies to every instance)
    *@ name   <C++ identifier>     the generated struct's name
    *@ generate <header> [vectors <header>]   where `netlist.py regenerate` writes (relative paths)
    *@ depends <netlist>           a netlist this one copies parts from (stamped with it; not read)
"""

import os
import re
from dataclasses import dataclass, field

import sympy as sp

SCALE = [("meg", 6), ("mil", None), ("t", 12), ("g", 9), ("k", 3), ("m", -3), ("u", -6),
         ("n", -9), ("p", -12), ("f", -15)]

NUMBER_RE = re.compile(r"^([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)([a-zA-Z]*)$")


class NetlistError(Exception):
    pass


def parse_number(text):
    """A SPICE number as an exact sympy Rational, or None when `text` is not one."""
    m = NUMBER_RE.match(text.strip())
    if not m:
        return None
    value = sp.Rational(m.group(1))
    suffix = m.group(2).lower()
    for name, exponent in SCALE:
        if suffix.startswith(name):
            if name == "mil":
                return value * sp.Rational(254, 10000000)
            return value * sp.Integer(10) ** exponent
    return value


# --- expressions ----------------------------------------------------------------------------------

TOKEN_RE = re.compile(r"\s*(?:(\d+\.?\d*(?:[eE][+-]?\d+)?[a-zA-Z]*|\.\d+(?:[eE][+-]?\d+)?[a-zA-Z]*)"
                      r"|([A-Za-z_][A-Za-z0-9_.]*)|(\*\*|<=|>=|==|!=|&&|\|\||[-+*/^(),?:<>!]))")

FUNCTIONS = {
    "sqrt": sp.sqrt, "exp": sp.exp, "log": sp.log, "ln": sp.log, "abs": sp.Abs,
    "min": sp.Min, "max": sp.Max, "pow": lambda a, b: a ** b, "pwr": lambda a, b: sp.Abs(a) ** b,
    "sin": sp.sin, "cos": sp.cos, "tan": sp.tan, "atan": sp.atan,
}


def tokenize(text):
    pos, out = 0, []
    text = text.strip()
    while pos < len(text):
        m = TOKEN_RE.match(text, pos)
        if not m or m.end() == pos:
            raise NetlistError(f"cannot read expression '{text}' at '{text[pos:]}'")
        out.append(m.group(1) or m.group(2) or m.group(3))
        pos = m.end()
    return out


class ExpressionParser:
    """Recursive descent over ngspice's expression grammar (ternary, logic, comparison, ^/**)."""

    def __init__(self, text, scope):
        self.tokens = tokenize(text)
        self.i = 0
        self.scope = scope

    def peek(self):
        return self.tokens[self.i] if self.i < len(self.tokens) else None

    def take(self, expected=None):
        tok = self.peek()
        if tok is None or (expected is not None and tok != expected):
            raise NetlistError(f"expected '{expected}' in expression {' '.join(self.tokens)}")
        self.i += 1
        return tok

    def parse(self):
        e = self.ternary()
        if self.peek() is not None:
            raise NetlistError(f"unexpected '{self.peek()}' in expression {' '.join(self.tokens)}")
        return e

    def ternary(self):
        cond = self.logic_or()
        if self.peek() == "?":
            self.take("?")
            a = self.ternary()
            self.take(":")
            b = self.ternary()
            return sp.Piecewise((a, cond), (b, True))
        return cond

    def logic_or(self):
        e = self.logic_and()
        while self.peek() == "||":
            self.take()
            e = sp.Or(e, self.logic_and())
        return e

    def logic_and(self):
        e = self.comparison()
        while self.peek() == "&&":
            self.take()
            e = sp.And(e, self.comparison())
        return e

    def comparison(self):
        e = self.additive()
        ops = {"<": sp.Lt, ">": sp.Gt, "<=": sp.Le, ">=": sp.Ge, "==": sp.Eq, "!=": sp.Ne}
        while self.peek() in ops:
            op = ops[self.take()]
            e = op(e, self.additive())
        return e

    def additive(self):
        e = self.multiplicative()
        while self.peek() in ("+", "-"):
            if self.take() == "+":
                e = e + self.multiplicative()
            else:
                e = e - self.multiplicative()
        return e

    def multiplicative(self):
        e = self.unary()
        while self.peek() in ("*", "/"):
            if self.take() == "*":
                e = e * self.unary()
            else:
                e = e / self.unary()
        return e

    def unary(self):
        if self.peek() == "-":
            self.take()
            return -self.unary()
        if self.peek() == "+":
            self.take()
            return self.unary()
        if self.peek() == "!":
            self.take()
            return sp.Not(self.unary())
        return self.power()

    def power(self):
        base = self.atom()
        if self.peek() in ("^", "**"):
            self.take()
            return base ** self.unary()
        return base

    def atom(self):
        tok = self.take()
        if tok == "(":
            e = self.ternary()
            self.take(")")
            return e
        number = parse_number(tok)
        if number is not None:
            return number
        name = tok.lower()
        if self.peek() == "(":
            if name not in FUNCTIONS:
                raise NetlistError(f"unknown function '{tok}'")
            self.take("(")
            args = [self.ternary()]
            while self.peek() == ",":
                self.take()
                args.append(self.ternary())
            self.take(")")
            return FUNCTIONS[name](*args)
        if name == "pi":
            return sp.pi
        if name not in self.scope:
            raise NetlistError(f"unknown parameter '{tok}'")
        return self.scope[name]


def evaluate(text, scope):
    """A value field (number, parameter name or {expression}) as a sympy expression."""
    text = text.strip()
    if text.startswith("{") and text.endswith("}"):
        text = text[1:-1]
    elif text.startswith("'") and text.endswith("'"):
        text = text[1:-1]
    number = parse_number(text)
    if number is not None:
        return number
    return ExpressionParser(text, scope).parse()


# --- netlist --------------------------------------------------------------------------------------

@dataclass
class Element:
    kind: str           # R C L V I E G
    name: str           # flattened, lower case
    nodes: list
    value: object = None    # sympy expression (R C L E G) or None
    ac: bool = False        # V: carries an AC value
    nullor: bool = False    # E: ideal op-amp
    condition: object = sp.true     # .if / .elseif / .else around it, over switches and pots


@dataclass
class Control:
    name: str
    kind: str           # "pot", "switch" or "step"
    positions: int      # switch and step positions (pots: 0)
    default: float      # a pot's position; a switch's or a step's default position
    symbol: object = None
    values: list = None     # a step's component values, exact, one per position

    @property
    def continuous(self):
        """A pot, or a step: its symbol is the coordinate 0 ... 1 the analysis runs over (a step's
        component value is values' lowest + coordinate * their span)."""
        return self.kind in ("pot", "step")

    def coordinate(self, value):
        """A step's component value as its coordinate 0 ... 1."""
        low, high = min(self.values), max(self.values)
        return (value - low) / (high - low)

    @property
    def default_coordinate(self):
        return self.coordinate(self.values[int(self.default)]) if self.kind == "step" else sp.Rational(str(self.default))

    def spice_value(self, x):
        """The .param value ngspice takes for a coordinate x (a pot's position is its own)."""
        if self.kind != "step":
            return x
        low, high = min(self.values), max(self.values)
        return float(low) + x * float(high - low)


@dataclass
class Netlist:
    title: str
    elements: list
    controls: list
    input: str
    output: str
    name: str
    source: str
    params: dict = field(default_factory=dict)
    generate: tuple = None      # (header, vectors or None), absolute paths


@dataclass
class Subcircuit:
    name: str
    ports: list
    params: list            # [(name, default text)]
    lines: list
    nullors: set


def _logical_lines(path, seen=None):
    """Lines with '+' continuations joined, .include expanded, .control blocks dropped."""
    seen = seen or set()
    path = os.path.abspath(path)
    if path in seen:
        raise NetlistError(f"recursive .include of {path}")
    seen = seen | {path}
    with open(path, encoding="utf-8") as fh:
        raw = fh.read().splitlines()
    out, in_control = [], False
    for n, line in enumerate(raw):
        stripped = line.strip()
        low = stripped.lower()
        if low.startswith(".control"):
            in_control = True
            continue
        if low.startswith(".endc"):
            in_control = False
            continue
        if in_control:
            continue
        if stripped.startswith("+") and out and not out[-1][0].startswith("*"):
            out[-1] = (out[-1][0] + " " + stripped[1:].strip(), out[-1][1])
            continue
        if low.startswith(".include") or low.startswith(".inc "):
            target = stripped.split(None, 1)[1].strip().strip('"')
            target = os.path.join(os.path.dirname(path), target)
            out.extend(_logical_lines(target, seen))     # an included file has no title line
            continue
        out.append((stripped, f"{os.path.basename(path)}:{n + 1}"))
    return out


def _strip_inline_comment(line):
    for marker in (" ;", "\t;", " $ ", "\t$ "):
        k = line.find(marker)
        if k >= 0:
            line = line[:k]
    return line.strip()


def _split_fields(line):
    """Whitespace split that keeps {...} and '...' together and splits name=value pairs."""
    fields, cur, depth, quote = [], "", 0, False
    for ch in line:
        if ch == "'" and depth == 0:
            quote = not quote
        if ch == "{" and not quote:
            depth += 1
        if ch == "}" and not quote:
            depth -= 1
        if ch.isspace() and depth == 0 and not quote:
            if cur:
                fields.append(cur)
            cur = ""
        else:
            cur += ch
    if cur:
        fields.append(cur)
    merged = []
    for f in fields:
        if merged and (f.startswith("=") or merged[-1].endswith("=")):
            merged[-1] += f
        else:
            merged.append(f)
    return merged


def _param_assignments(fields):
    out = []
    for f in fields:
        if "=" not in f:
            raise NetlistError(f"expected name=value, got '{f}'")
        k, v = f.split("=", 1)
        out.append((k.strip().lower(), v.strip()))
    return out


def read(path):
    lines = _logical_lines(path)
    if not lines:
        raise NetlistError(f"{path} is empty")
    title = lines[0][0]
    body = lines[1:]

    directives = {"input": None, "output": None, "name": None, "generate": None}
    controls, top_nullors = {}, set()
    subckts, top_lines, param_lines = {}, [], []
    current = None

    for line, where in body:
        if not line:
            continue
        if line.startswith("*@"):
            words = line[2:].split()
            if not words:
                continue
            key = words[0].lower()
            try:
                if key in ("input", "output", "name"):
                    directives[key] = words[1] if key == "name" else words[1].lower()
                elif key == "pot":
                    default = float(words[2]) if len(words) > 2 else 0.5
                    controls[words[1].lower()] = Control(words[1].lower(), "pot", 0, default)
                elif key == "switch":
                    positions = int(words[2])
                    default = float(words[3]) if len(words) > 3 else 0.0
                    if positions < 2:
                        raise NetlistError("a switch needs at least two positions")
                    controls[words[1].lower()] = Control(words[1].lower(), "switch", positions, default)
                elif key == "step":
                    values = [parse_number(w) for w in words[2:]]
                    if len(values) < 2 or any(v is None or v <= 0 for v in values) or len(set(values)) != len(values):
                        raise NetlistError("a step needs two or more distinct positive component values")
                    controls[words[1].lower()] = Control(words[1].lower(), "step", len(values), 0.0, values=values)
                elif key == "generate":
                    base = os.path.dirname(os.path.abspath(path))
                    vectors = None
                    if len(words) > 2:
                        if words[2].lower() != "vectors" or len(words) != 4:
                            raise NetlistError("expected 'generate <header> [vectors <header>]'")
                        vectors = os.path.normpath(os.path.join(base, words[3]))
                    directives["generate"] = (os.path.normpath(os.path.join(base, words[1])), vectors)
                elif key == "nullor":
                    (current.nullors if current else top_nullors).add(words[1].lower())
                elif key == "depends":
                    target = os.path.join(os.path.dirname(os.path.abspath(path)), words[1])
                    if not os.path.exists(target):
                        raise NetlistError(f"'{words[1]}' does not exist")
                else:
                    raise NetlistError(f"unknown directive '{key}'")
            except (IndexError, ValueError) as e:
                raise NetlistError(f"{where}: bad directive '{line}': {e}")
            continue
        if line.startswith("*"):
            continue
        line = _strip_inline_comment(line)
        if not line:
            continue
        low = line.lower()
        if low.startswith(".subckt"):
            fields = _split_fields(line)
            name = fields[1].lower()
            ports, params, k = [], [], 2
            while k < len(fields) and "=" not in fields[k] and fields[k].lower() != "params:":
                ports.append(fields[k].lower())
                k += 1
            if k < len(fields) and fields[k].lower() == "params:":
                k += 1
            params = _param_assignments(fields[k:])
            current = Subcircuit(name, ports, params, [], set())
            subckts[name] = current
            continue
        if low.startswith(".ends"):
            current = None
            continue
        if low.startswith(".param"):
            if current:
                current.lines.append((line, where))
            else:
                param_lines.append((line, where))
            continue
        if low == ".end" or low.startswith(".end "):
            break
        if re.match(r"^\.(if|elseif|else|endif)\b", low):
            (current.lines if current else top_lines).append((line, where))
            continue
        if low.startswith("."):
            continue        # analyses, options, models: not part of the linear network
        (current.lines if current else top_lines).append((line, where))

    # Global parameters: controls are symbols, everything else exact values in order of definition.
    scope = {}
    for c in controls.values():
        c.symbol = sp.Symbol(c.name)
    for line, where in param_lines:
        for k, v in _param_assignments(_split_fields(line)[1:]):
            if k in controls and controls[k].kind == "step":
                c = controls[k]
                default = evaluate(v, scope)
                if default not in c.values:
                    raise NetlistError(f"{where}: the .param value of step '{k}' is not one of its values")
                c.default = float(c.values.index(default))
                low, high = min(c.values), max(c.values)
                scope[k] = low + c.symbol * (high - low)
            elif k in controls:
                scope[k] = controls[k].symbol
            else:
                try:
                    scope[k] = evaluate(v, scope)
                except NetlistError as e:
                    raise NetlistError(f"{where}: {e}")
    for c in controls.values():
        if c.name not in scope:
            raise NetlistError(f"control '{c.name}' has no .param line (ngspice needs one)")

    elements = []
    _flatten(top_lines, scope, {}, "", subckts, top_nullors, elements, depth=0)

    sources = [e for e in elements if e.kind == "v"]
    input_name = directives["input"]
    if input_name is None:
        ac = [e for e in sources if e.ac]
        if len(ac) != 1:
            raise NetlistError("name the input with '*@ input <V source>' (need exactly one AC source)")
        input_name = ac[0].name
    if not any(e.name == input_name for e in sources):
        raise NetlistError(f"input source '{input_name}' not found")
    if directives["output"] is None:
        raise NetlistError("name the output node with '*@ output <node>'")

    name = directives["name"] or re.sub(r"[^A-Za-z0-9]", "", os.path.splitext(os.path.basename(path))[0].title())
    return Netlist(title, elements, list(controls.values()), input_name, directives["output"], name,
                   os.path.abspath(path), scope, directives["generate"])


def _flatten(lines, scope, nodemap, prefix, subckts, nullors, out, depth):
    if depth > 20:
        raise NetlistError("subcircuits nested too deep")

    def node(n):
        n = n.lower()
        if n in ("0", "gnd"):
            return "0"
        if n in nodemap:
            return nodemap[n]
        return prefix + n

    # .if stack: each level holds (conditions of the branches before, this branch's condition).
    branches = []

    def active():
        c = sp.true
        for before, this in branches:
            c = sp.And(c, sp.Not(before), this)
        return c

    def condition_of(text):
        text = text.strip()
        if text.startswith("(") and text.endswith(")"):
            text = text[1:-1]
        return sp.sympify(evaluate("{" + text + "}", scope))

    for line, where in lines:
        low = line.lower()
        try:
            if re.match(r"^\.if\b", low):
                branches.append((sp.false, condition_of(line[3:])))
                continue
            if re.match(r"^\.elseif\b", low):
                before, this = branches.pop()
                branches.append((sp.Or(before, this), condition_of(line[7:])))
                continue
            if re.match(r"^\.else\b", low):
                before, this = branches.pop()
                branches.append((sp.Or(before, this), sp.true))
                continue
            if re.match(r"^\.endif\b", low):
                branches.pop()
                continue
        except IndexError:
            raise NetlistError(f"{where}: {line.split()[0]} without .if")
        except NetlistError as e:
            raise NetlistError(f"{where}: {e}")
        fields = _split_fields(line)
        if line.lower().startswith(".param"):
            if branches:
                raise NetlistError(f"{where}: .param inside .if is not supported")
            for k, v in _param_assignments(fields[1:]):
                scope = dict(scope)
                scope[k] = evaluate(v, scope)
            continue
        name = fields[0].lower()
        kind = name[0]
        full = prefix + name
        start = len(out)
        try:
            if kind in "rcl":
                if len(fields) < 4:
                    raise NetlistError("needs two nodes and a value")
                value = evaluate(fields[3], scope)
                out.append(Element(kind, full, [node(fields[1]), node(fields[2])], value))
            elif kind == "v":
                rest = [f.lower() for f in fields[3:]]
                has_ac = "ac" in rest or any(f.startswith("ac=") for f in rest)
                out.append(Element("v", full, [node(fields[1]), node(fields[2])], None, ac=has_ac))
            elif kind == "i":
                rest = [f.lower() for f in fields[3:]]
                if "ac" in rest:
                    raise NetlistError("AC current sources are not supported")
                out.append(Element("i", full, [node(fields[1]), node(fields[2])]))
            elif kind in "eg":
                if len(fields) < 6:
                    raise NetlistError("needs four nodes and a gain (linear form only)")
                value = evaluate(fields[5], scope)
                e = Element(kind, full, [node(f) for f in fields[1:5]], value)
                e.nullor = kind == "e" and name in nullors
                out.append(e)
            elif kind == "x":
                k = 1
                while k < len(fields) and "=" not in fields[k] and fields[k].lower() != "params:":
                    k += 1
                sub = subckts.get(fields[k - 1].lower())
                if sub is None:
                    raise NetlistError(f"unknown subcircuit '{fields[k - 1]}'")
                actual = [node(n) for n in fields[1:k - 1]]
                if len(actual) != len(sub.ports):
                    raise NetlistError(f"subcircuit '{sub.name}' has {len(sub.ports)} ports")
                rest = fields[k:]
                if rest and rest[0].lower() == "params:":
                    rest = rest[1:]
                overrides = dict(_param_assignments(rest))
                inner = dict(scope)
                for pname, ptext in sub.params:
                    # An instance value reads the caller's parameters, a default the subcircuit's.
                    if pname in overrides:
                        inner[pname] = evaluate(overrides.pop(pname), scope)
                    else:
                        inner[pname] = evaluate(ptext, inner)
                if overrides:
                    raise NetlistError(f"unknown parameters {sorted(overrides)} for '{sub.name}'")
                _flatten(sub.lines, inner, dict(zip(sub.ports, actual)), full + ".", subckts,
                         sub.nullors, out, depth + 1)
            else:
                raise NetlistError(f"element type '{kind.upper()}' is not supported")
        except NetlistError as e:
            raise NetlistError(f"{where}: {name}: {e}")
        if branches:
            cond = active()
            for e in out[start:]:
                e.condition = sp.And(e.condition, cond)
