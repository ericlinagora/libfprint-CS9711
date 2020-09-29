#!/bin/bash
SRCROOT=`git rev-parse --show-toplevel`
CFG="$SRCROOT/scripts/uncrustify.cfg"
echo "srcroot: $SRCROOT"

case "$1" in
    -c|--check)
	OPTS="--check"
        ;;
    *)
	OPTS="--replace --no-backup"
        ;;
esac

ARGS=4
JOBS=4

pushd "$SRCROOT"
git ls-tree --name-only -r HEAD | grep -E '.*\.[ch]$' | grep -v nbis | grep -v fpi-byte | grep -v build/ | xargs -n$ARGS -P $JOBS uncrustify -c "$CFG" $OPTS
RES=$?
popd
exit $RES
