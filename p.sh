#!/bin/sh
fifo=/tmp/p.$$
mkfifo $fifo
echo $fifo
exec 3<&0 <&- >&-
(eval "$@" >$fifo; rm $fifo) <&3 3<&- &
