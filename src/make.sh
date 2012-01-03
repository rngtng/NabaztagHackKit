#! /usr/bin/env bash

HOST="ssh-21560@warteschlange.de"
REMOTE="/kunden/warteschlange.de/.tmp/OpenJabNab/bootcode"
IN="bootcode2.mtl"
TMP="bootcode2.mtl"
OUT="bootcode.bin"
DIR=`dirname $0`

ruby $DIR/merge.rb $1 > $IN
scp $IN ${HOST}:${REMOTE}/${IN}
rm $IN
ssh $HOST "cd $REMOTE && rm -f $OUT && compiler/mtl_linux/mtl_comp -s $IN $OUT"
scp ${HOST}:${REMOTE}/${OUT} ${OUT}