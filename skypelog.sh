#!/bin/sh

if [ $# -ne 1 ]
then
	echo >&2 "Usage: $0 path/to/skype/dir"
	exit 1
fi

dir="$(dirname "$(readlink "$0" 2> /dev/null || echo "$0")")"

# mostly sorted already, but in case the directory order is out...

"$dir"/skypelog "$1" 2> /dev/null | (which iconv && iconv -f utf-8 -t utf-8//ignore || cat  ) | sort
