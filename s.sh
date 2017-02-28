#!/bin/sh
if test "$1"; then
	math -i 44100 "$1" \*
else
	oldifs=$IFS
	while read -r cmd; do
		set $cmd
		time=$(math -i 44100 "$1" \*)
		shift
		IFS='	'  # tab
		printf '%s\t%s\n' $time "$*"
		IFS=$oldifs
	done
fi
