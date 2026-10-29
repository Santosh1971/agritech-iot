"""Bring a hand-edited WM-MiniC3.kicad_pcb in line with tools/netlist-WM-MiniC3.json without moving anything:
pad nets from the netlist, then every connected group of tracks takes the net of the pads it touches.
Groups that touch pads of two different nets are reported (and left alone). Run in KiCad's python."""
import json, os, sys, collections
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__)); PRJ = os.path.dirname(HERE)
PCB = os.path.join(PRJ, "WM-MiniC3.kicad_pcb")
b = pcbnew.LoadBoard(PCB)
nl = {p["ref"]: p for p in json.load(open(os.path.join(HERE, "netlist-WM-MiniC3.json")))}
def net(name):
    n = b.FindNet(name)
    if n is None:
        n = pcbnew.NETINFO_ITEM(b, name); b.Add(n)
    return n
for f in b.GetFootprints():
    p_ = nl.get(f.GetReference())
    if not p_:
        continue
    for p in f.Pads():
        name = p_["pads"].get(p.GetNumber()) or p_["nc"].get(p.GetNumber())
        if name:
            p.SetNet(net(name))
tr = list(b.GetTracks())
par = list(range(len(tr)))
def find(x):
    while par[x] != x:
        par[x] = par[par[x]]; x = par[x]
    return x
pts = collections.defaultdict(list)
for i, t in enumerate(tr):
    if t.GetClass() == "PCB_VIA":
        pts[(t.GetPosition().x, t.GetPosition().y, "via")].append(i)
    else:
        for q in (t.GetStart(), t.GetEnd()):
            pts[(q.x, q.y, t.GetLayer())].append(i)
for (x, y, lay), ids in pts.items():
    for j in ids[1:]:
        par[find(j)] = find(ids[0])
    if lay != "via":                        # a track end on a via
        for k in pts.get((x, y, "via"), []):
            par[find(k)] = find(ids[0])
pads = [(f.GetReference(), p) for f in b.GetFootprints() for p in f.Pads()]
touch = collections.defaultdict(set)
for i, t in enumerate(tr):
    ends = [(t.GetPosition(), None)] if t.GetClass() == "PCB_VIA" else [(t.GetStart(), t.GetLayer()), (t.GetEnd(), t.GetLayer())]
    for q, lay in ends:
        for ref, p in pads:
            if (lay is None or p.IsOnLayer(lay)) and p.HitTest(q):
                touch[find(i)].add(p.GetNetname())
groups = collections.defaultdict(list)
for i in range(len(tr)):
    groups[find(i)].append(i)
renamed = 0; conflicts = []; floating = 0
for g, ids in groups.items():
    nets = {n for n in touch[g] if n}
    if len(nets) == 1:
        n = net(nets.pop())
        for i in ids:
            if tr[i].GetNetname() != n.GetNetname():
                tr[i].SetNet(n); renamed += 1
    elif len(nets) > 1:
        conflicts.append(sorted(nets))
    else:
        floating += 1
pcbnew.SaveBoard(PCB, b)
print("tracks renamed: %d, groups touching no pad: %d" % (renamed, floating))
for c in conflicts:
    print("CONFLICT: one copper group touches", c)
