#!/bin/zsh
# usage: prof.sh <port> <loadcmd...>  -> writes sample_<port>.txt, prints top frames
port=$1; shift
pid=$(lsof -nP -iTCP:$port -sTCP:LISTEN -t | head -1)
"$@" > load_$port.txt 2>&1 &
lp=$!
sleep 2; sample $pid 10 -mayDie -file sample_$port.txt >/dev/null 2>&1; wait $lp; cat load_$port.txt
