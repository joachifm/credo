#! /usr/bin/env bash
#
# -*- mode: shell; -*-

set -euo pipefail
if [[ ${REDO_DOFILE_TRACE:-0} = 1 ]] ; then
    set -x
fi

srcfile=${1/.o/.c}
depfile=${1/.o/.d}

cc -g3 -Wall -DBUILD_MODE_DEVEL=1 -MMD -MF "$depfile" -c "$srcfile" -o "$3"
deps=$(cut -d ':' -f 2- < "$depfile") ; rm "$depfile"
redo-ifchange $deps
