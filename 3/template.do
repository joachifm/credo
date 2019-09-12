#! /usr/bin/env bash
#
# -*- mode: shell; -*-

set -euo pipefail
if [[ ${REDO_DOFILE_TRACE:-0} = 1 ]] ; then
    set -x
fi
