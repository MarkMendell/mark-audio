#!/bin/sh
# TODO: this should be an awk program
IFS='	'
while read -r line; do
	set -- $line
	case $1 in
		'Note On' ) echo $2 ;;
		'Note Off' ) printf '%s\t%s\n' $2 off ;;
	esac
done
