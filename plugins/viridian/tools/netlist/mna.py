"""
Symbolic modified nodal analysis: H(s) = V(output) / V(input) of a flattened netlist, exactly, as a
ratio of coprime polynomials in s and the pot positions (switches substituted beforehand).

The input source fixes its node at 1 V; every other voltage source is an AC short (its nodes are
merged), current sources are open. A nullor (ideal op-amp) replaces the KCL row of its output node
by V(+) = V(-); any other VCVS adds its branch current. H = det(A_out) / det(A) by Cramer's rule,
with each row cleared of denominators first (the row factors cancel in the ratio), computed
fraction-free over QQ[s, pots] with sympy's DomainMatrix.
"""

import sympy as sp

from spice import NetlistError

S = sp.Symbol("s")


class _Nodes:
    def __init__(self):
        self.parent = {}

    def find(self, n):
        self.parent.setdefault(n, n)
        while self.parent[n] != n:
            self.parent[n] = self.parent[self.parent[n]]
            n = self.parent[n]
        return n

    def union(self, a, b):
        a, b = self.find(a), self.find(b)
        if a == b:
            return
        if b == "0":
            a, b = b, a
        self.parent[b] = a      # ground stays the representative


def transfer(netlist, substitutions):
    """(N, D) as sympy Polys in (s, *pots) with no common factor, for one state: `substitutions`
    gives every switch its position and every pot condition (a region) its truth value."""
    N, D = None, None
    for n, d in transfer_pieces(netlist, substitutions):
        N = n if N is None else N * n
        D = d if D is None else D * d
    return N, D


def transfer_pieces(netlist, substitutions):
    """H(s) as a chain of pieces [(N, D), ...] whose product is exact: the network is cut at every
    op-amp output that alone joins the input's side to the output's (an ideal source: what follows
    cannot load it). Each piece is solved on its own, so a buffered filter in front of a large
    network costs its own size, not the product's."""
    pots = [c.symbol for c in netlist.controls if c.continuous]
    gens = [S] + pots

    def value(e):
        v = sp.sympify(e.value).xreplace(substitutions)
        v = sp.piecewise_fold(v)
        if isinstance(v, sp.Piecewise) or v.has(sp.Piecewise):
            v = sp.simplify(v)
        if v.has(sp.Piecewise):
            raise NetlistError(f"{e.name}: its value depends on a pot through a condition")
        free = v.free_symbols - set(pots)
        if free:
            raise NetlistError(f"{e.name}: unresolved symbols {sorted(map(str, free))}")
        return v

    elements = []
    for e in netlist.elements:
        present = sp.sympify(e.condition).xreplace(substitutions)
        present = sp.simplify(present) if present not in (sp.true, sp.false) else present
        if present not in (sp.true, sp.false):
            raise NetlistError(f"{e.name}: its .if condition does not resolve ({present})")
        if present == sp.true:
            elements.append(e)

    nodes = _Nodes()
    nodes.find("0")
    input_src = None
    for e in elements:
        for n in e.nodes:
            nodes.find(n)
        if e.kind == "v":
            if e.name == netlist.input:
                input_src = e
            else:
                nodes.union(e.nodes[0], e.nodes[1])
    if input_src is None:
        raise NetlistError(f"input source '{netlist.input}' is switched out")
    a, b = nodes.find(input_src.nodes[0]), nodes.find(input_src.nodes[1])
    if b != "0" and a == "0":
        a, b = b, a
        sign = -1
    else:
        sign = 1
    if b != "0" or a == "0":
        raise NetlistError(f"input source '{input_src.name}' must drive a node against ground")
    input_node = a
    output_node = nodes.find(netlist.output)
    if output_node == "0":
        return [(sp.Poly(0, *gens), sp.Poly(1, *gens))]
    if output_node == input_node:
        return [(sp.Poly(sign, *gens), sp.Poly(1, *gens))]

    network = [e for e in elements if e.kind != "v" and e.kind != "i"]
    pieces = []
    for k, (part, start, end) in enumerate(_chain(network, nodes, input_node, output_node)):
        n, d = _solve(part, nodes, start, end, gens, value)
        pieces.append((n * sign if k == 0 else n, d))
    return pieces


def _chain(elements, nodes, input_node, output_node):
    """[(elements, input node, output node), ...]: the network cut at op-amp outputs that separate
    the input from the output, in signal order."""
    def touching(e):
        return {nodes.find(n) for n in e.nodes} - {"0"}

    for e in elements:
        if e.kind != "e" or not e.nullor:
            continue
        x = nodes.find(e.nodes[0])
        if x in (input_node, "0"):
            continue
        # The input's side once x is removed: everything reachable without passing through x.
        side, frontier = {input_node}, [input_node]
        while frontier:
            n = frontier.pop()
            for other in elements:
                t = touching(other)
                if n in t:
                    for m in t - {x} - side:
                        side.add(m)
                        frontier.append(m)
        if not (touching(e) & side):
            continue
        if output_node in side or output_node == x:
            # The output is on the input's side, or is x: whatever hangs behind x (an ideal
            # source) cannot reach it. Drop that and look again.
            kept = [o for o in elements if touching(o) & side]
            if len(kept) < len(elements):
                return _chain(kept, nodes, input_node, output_node)
            continue
        upstream = [o for o in elements if touching(o) & side]
        downstream = [o for o in elements if not touching(o) & side]
        return _chain(upstream, nodes, input_node, x) + _chain(downstream, nodes, x, output_node)
    return [(elements, input_node, output_node)]


def _solve(elements, nodes, input_node, output_node, gens, value):
    """(N, D) of V(output) / V(input) for one piece, the input node held at 1 V."""
    if output_node == input_node:
        return sp.Poly(1, *gens), sp.Poly(1, *gens)
    unknown = sorted({nodes.find(n) for e in elements for n in e.nodes} - {"0", input_node})
    if output_node not in unknown:
        return sp.Poly(0, *gens), sp.Poly(1, *gens)
    index = {n: k for k, n in enumerate(unknown)}
    branches = [e for e in elements if e.kind == "e" and not e.nullor]
    size = len(unknown) + len(branches)
    A = [[sp.Integer(0)] * size for _ in range(size)]
    rhs = [sp.Integer(0)] * size

    def add(row_node, col_node, v):
        r = index.get(row_node)
        if r is None:
            return
        if col_node == input_node:
            rhs[r] -= v
        elif col_node in index:
            A[r][index[col_node]] += v

    def admittance(n1, n2, y):
        n1, n2 = nodes.find(n1), nodes.find(n2)
        add(n1, n1, y)
        add(n2, n2, y)
        add(n1, n2, -y)
        add(n2, n1, -y)

    constraint_rows = {}
    for e in elements:
        if e.kind == "r":
            admittance(e.nodes[0], e.nodes[1], 1 / value(e))
        elif e.kind == "c":
            admittance(e.nodes[0], e.nodes[1], S * value(e))
        elif e.kind == "l":
            admittance(e.nodes[0], e.nodes[1], 1 / (S * value(e)))
        elif e.kind == "g":
            g = value(e)
            p, m, cp, cm = (nodes.find(n) for n in e.nodes)
            add(p, cp, g)
            add(p, cm, -g)
            add(m, cp, -g)
            add(m, cm, g)
        elif e.kind == "e" and e.nullor:
            out, ref, cp, cm = (nodes.find(n) for n in e.nodes)
            if ref != "0":
                raise NetlistError(f"{e.name}: an ideal op-amp's output is referenced to ground")
            if out not in index:
                raise NetlistError(f"{e.name}: output node is ground or the input")
            if out in constraint_rows:
                raise NetlistError(f"{e.name}: two op-amps drive node '{out}'")
            constraint_rows[out] = (cp, cm)

    # The op-amp supplies whatever current its output node needs: that node's KCL row is replaced.
    for out, (cp, cm) in constraint_rows.items():
        r = index[out]
        A[r] = [sp.Integer(0)] * size
        rhs[r] = sp.Integer(0)
        for node, v in ((cp, 1), (cm, -1)):
            if node == input_node:
                rhs[r] -= v
            elif node in index:
                A[r][index[node]] += v

    for k, e in enumerate(branches):
        col = len(unknown) + k
        p, m, cp, cm = (nodes.find(n) for n in e.nodes)
        mu = value(e)
        if p in index:
            A[index[p]][col] += 1
        if m in index:
            A[index[m]][col] -= 1
        for node, v in ((p, 1), (m, -1), (cp, -mu), (cm, mu)):
            if node == input_node:
                rhs[col] -= v
            elif node in index:
                A[col][index[node]] += v

    # Clear each row's denominators; a row's factor scales both determinants alike.
    Rq = sp.polys.rings.PolyRing(gens, sp.QQ)
    Rz = sp.polys.rings.PolyRing(gens, sp.ZZ)
    rows = []
    for r in range(size):
        entries = [sp.together(x) for x in A[r] + [rhs[r]]]
        dens = [sp.fraction(x)[1] for x in entries]
        lcm = sp.lcm_list(dens) if dens else 1
        polys = [Rq.from_expr(sp.expand(sp.cancel(x * lcm))) for x in entries]
        common = 1
        for q in polys:
            if q:
                common = sp.ilcm(common, int(q.clear_denoms()[0]))
        rows.append({k: (q * common).set_ring(Rz) for k, q in enumerate(polys) if q})

    det, det_out = _bareiss(rows, size, index[output_node], Rz)
    if not det:
        raise NetlistError("the network matrix is singular (a floating node or an op-amp loop)")
    g = det.gcd(det_out) if det_out else det
    if det_out:
        det, det_out = det.exquo(g), det_out.exquo(g)
    else:
        det = Rz.one
    N = sp.Poly(det_out.as_expr(), *gens)
    D = sp.Poly(det.as_expr(), *gens)
    return N, D


def _bareiss(rows, size, out, ring):
    """Fraction-free elimination of [A | b] (rows: {column: entry}, b in column `size`) with
    Markowitz pivoting, the output's column last: the last pivot is det (A) and the last right-hand
    side det (A with the output's column replaced by b), each up to the same permutation sign."""
    active = set(range(size))
    columns = set(range(size)) - {out}
    previous = ring.one
    for _ in range(size - 1):
        counts = {}
        for r in active:
            for c in rows[r]:
                counts[c] = counts.get(c, 0) + 1
        best = None
        for r in active:
            row = rows[r]
            for c, v in row.items():
                if c not in columns:
                    continue
                key = ((len(row) - 1) * (counts[c] - 1), len(v))
                if best is None or key < best[0]:
                    best = (key, r, c)
        if best is None:
            return ring.zero, ring.zero
        _, r, c = best
        pivot_row = rows[r]
        p = pivot_row[c]
        active.remove(r)
        columns.remove(c)
        for i in active:
            row = rows[i]
            a = row.get(c)
            new = {}
            for j, v in row.items():
                if j != c:
                    new[j] = p * v
            if a:
                for j, v in pivot_row.items():
                    if j != c:
                        new[j] = new.get(j, ring.zero) - a * v
            rows[i] = {j: v.exquo(previous) for j, v in new.items() if v}
        previous = p
    (last,) = active
    return rows[last].get(out, ring.zero), rows[last].get(size, ring.zero)
