#! /usr/bin/env bash
# -*- mode: shell-script; -*-

set -euo pipefail
if [[ ${REDO_DOFILE_TRACE:-0} = 1 ]] ; then
    set -x
fi

redo-ifchange redo.o
cc -o $3 redo.o
