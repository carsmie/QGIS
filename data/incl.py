# Inclusive sample counts per function from macOS `sample` call trees (each tree node counted once per path).
import re, sys, collections
f, n = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 45
pat = sys.argv[3] if len(sys.argv) > 3 else ''
incl = collections.Counter(); total = 0
line_re = re.compile(r'^([\s+!:|]*)(\d+)\s+(.+?)\s+\(in ([^)]+)\)')
stack = []
for line in open(f):
    if line.startswith('Total number in stack'): break
    m = line_re.match(line)
    if not m: continue
    depth = len(m.group(1)); cnt = int(m.group(2)); fn = m.group(3); lib = m.group(4)
    fn = re.sub(r'\s+\+ \d+.*$', '', fn)
    while stack and stack[-1][0] >= depth: stack.pop()
    names_above = {s[1] for s in stack}
    key = f'{fn}  [{lib}]'
    if key not in names_above: incl[key] += cnt   # avoid double counting recursion
    stack.append((depth, key))
    if 'QgsServer::handleRequest' in fn and total == 0: total = cnt
print('handleRequest samples:', total)
for k, v in incl.most_common(400):
    if total and v > total: continue
    if pat and not re.search(pat, k): continue
    print(f'{v:6} {100*v/max(total,1):5.1f}%  {k[:170]}')
    n -= 1
    if n == 0: break
