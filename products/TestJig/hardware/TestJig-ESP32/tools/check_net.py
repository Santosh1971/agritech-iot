"""Compare the netlist exported by kicad-cli against design.py."""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import design  # noqa: E402
from sexpr import parse, find, findall  # noqa: E402

net_file = sys.argv[1]
tree = parse(open(net_file).read())[0]
got = {}  # (ref,pin) -> net
for n in findall(find(tree, 'nets'), 'net'):
    name = str(find(n, 'name')[1]).lstrip('/')
    for node in findall(n, 'node'):
        got[(str(find(node, 'ref')[1]), str(find(node, 'pin')[1]))] = name

errors = 0
want = {}
for p in design.PARTS:
    if p['ref'].startswith('#'):
        continue
    for pin, net in p['nets'].items():
        want[(p['ref'], pin)] = net
for k, net in want.items():
    g = got.get(k)
    if g != net:
        errors += 1
        print('MISMATCH', k, 'want', net, 'got', g)
for k, g in got.items():
    if k not in want and not g.startswith('unconnected-'):
        errors += 1
        print('EXTRA', k, g)
print('checked %d pins, %d errors' % (len(want), errors))
sys.exit(1 if errors else 0)
