#!/bin/sh

if [ $# -ne 1 ]
then
	echo >&2 "Usage: $0 path/to/skype/dir"
	exit 1
fi

dir="$(dirname "$(readlink "$0" 2> /dev/null || echo "$0")")"

# general sorted already, but just to be sure...

"$dir"/skypelog "$1" 2> /dev/null | sort
