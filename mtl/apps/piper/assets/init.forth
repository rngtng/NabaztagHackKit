( init.forth — served by the mock file server, issue #39 )
( Piper's forth_init fetches <server-url>/init.forth at startup and )
( interprets the response on the device. Edit this file to script the )
( rabbit; the mock server serves it straight from this assets folder. )

"init.forth loaded from mock server" . cr

( 'on-connect' is run by the interpreter right after init.forth loads )
( see _forth_init_cb -> forth_interpreter WORD_ON_CONNECT in forth.mtl )
: on-connect
  "on-connect ran; 21 * 2 = " .
  21 2 * . cr
;

( To pull more files through the mock, drop them next to this one and )
( reference them, e.g.  server-url ." /chime.mp3" ... play-url  )
