# Aggregate the callees (up to given depth) under every node matching a pattern.
# usage: children.py sample.txt 'regex' [levels]
import re, sys, collections
path, pat = sys.argv[1], re.compile(sys.argv[2])
levels = int(sys.argv[3]) if len(sys.argv) > 3 else 1
line_re = re.compile(r'^([\s+!:|]*)(\d+)\s+(.*?)\s+\(in ([^)]*)\)')
agg = collections.Counter(); total = 0
anchor = None  # depth of current matched node
lvl_depths = []
for line in open(path, errors='replace'):
    m = line_re.match(line)
    if not m:
        continue
    depth = len(m.group(1)); n = int(m.group(2)); fn = m.group(3)
    if anchor is not None and depth <= anchor:
        anchor = None
    if anchor is None:
        if pat.search(fn):
            anchor = depth; total += n; lvl_depths = []
        continue
    # track levels below anchor by distinct depths
    while lvl_depths and lvl_depths[-1] >= depth:
        lvl_depths.pop()
    lvl_depths.append(depth)
    if len(lvl_depths) <= levels:
        agg[('  ' * (len(lvl_depths) - 1)) + fn[:130]] += n
print(f"matched samples: {total}")
for k, v in agg.most_common(25):
    print(f"{v:7d} {v / total * 100 if total else 0:5.1f}%  {k}")
