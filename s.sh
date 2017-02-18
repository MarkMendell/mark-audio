#!/bin/sh
if test "$1"; then
	math -i 44100 "$1" \*
else
	while read -r cmd; do
		set $cmd
		time=$(math -i 44100 "$1" \*)
		shift
		echo $time $@
	done
fi
