#! /usr/bin/env bash

HOST="ssh-21560@warteschlange.de"
REMOTE="/kunden/warteschlange.de/.tmp/OpenJabNab/bootcode"
IN="bootcode2.mtl"
OUT="bootcode.bin"

scp $1 ${HOST}:${REMOTE}/${IN}
ssh $HOST "cd $REMOTE && rm -f $OUT && compiler/mtl_linux/mtl_comp -s $IN $OUT"
scp ${HOST}:${REMOTE}/${OUT} ${OUT}