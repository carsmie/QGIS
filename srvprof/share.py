# Inclusive share of handleRequest samples for given function patterns in macOS `sample` call trees.
# usage: share.py sample.txt 'label=regex' ...
import re, sys
path = sys.argv[1]
pats = [(a.split('=', 1)[0], re.compile(a.split('=', 1)[1])) for a in sys.argv[2:]]
line_re = re.compile(r'^([\s+!:|]*)(\d+)\s+(.*?)\s+\(in ([^)]*)\)')
root_depth = None; total = 0
stack = []  # (depth, matched_labels)
counts = {l: 0 for l, _ in pats}
for line in open(path, errors='replace'):
    m = line_re.match(line)
    if not m:
        continue
    depth = len(m.group(1)); n = int(m.group(2)); fn = m.group(3) + ' ' + m.group(4)
    while stack and stack[-1][0] >= depth:
        stack.pop()
    if 'QgsServer::handleRequest' in fn and root_depth is None:
        root_depth = depth
    inside = root_depth is not None and depth >= root_depth
    if 'QgsServer::handleRequest' in fn and depth == root_depth:
        total += n
    parent = stack[-1][1] if stack else frozenset()
    labels = set(parent)
    if inside:
        for l, rx in pats:
            if l not in parent and rx.search(fn):
                counts[l] += n
                labels.add(l)
    stack.append((depth, frozenset(labels)))
print(f"handleRequest samples: {total}")
for l, _ in pats:
    print(f"  {l:28} {counts[l] / total * 100 if total else 0:5.1f}%")
