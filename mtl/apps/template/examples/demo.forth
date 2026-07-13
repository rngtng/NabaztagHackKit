\ ===========================================================================
\ app-template Forth playground — guided tour of every word pack
\ ===========================================================================
\ Run the whole file through /eval in one shot:
\
\   task simulate:app:template          # serves the playground on :8080
\   curl -s --noproxy localhost \
\        --data-binary @src/app-template/examples/demo.forth \
\        localhost:8080/eval
\
\ Everything here runs in the SIMULATOR except the "Net" section at the very
\ bottom (http-get / http-post / mac / ip), which needs the device build —
\ see the note there. `\` starts a line comment; `( ... )` is a block comment
\ and is how we write stack effects: ( before -- after ), top of stack right.
\ ===========================================================================

\ --- Compilation & dictionary --------------------------------------------
\ `:` ... `;` defines a new word; `constant` names a value; `defined?` tests
\ the dictionary; `( ... )` is skipped by the tokenizer (you're reading one).
: square ( n -- n*n ) dup * ;
5 square . cr                       \ -> 25
42 constant answer  answer . cr     \ -> 42 (constant reads the next token as name)
defined? square . cr                \ -> -1 (true; defined? also reads the next token)
defined? nope   . cr                \ -> 0  (false)

\ --- Stack manipulation ---------------------------------------------------
1 2 dup .s cr           \ 1 2 2
drop drop drop          \ clear it
1 2 swap .s cr          \ 2 1
drop drop
1 2 over .s cr          \ 1 2 1
drop drop drop
1 2 3 rot .s cr         \ 2 3 1
drop drop drop
10 20 nip . cr          \ -> 20
10 20 tuck .s cr        \ 20 10 20
drop drop drop
0 ?dup .s cr            \ 0        (unchanged, no dup on zero)
drop
7 ?dup .s cr            \ 7 7
drop drop
1 2 3 depth . cr        \ -> 3
drop drop drop
100 200 1 pick .s cr    \ 100 200 100
drop drop drop

\ --- Return stack (inside a definition) -----------------------------------
: r-demo ( n -- n ) >r r@ r> drop ;   \ stash, peek, restore
9 r-demo . cr           \ -> 9

\ --- Comparison -----------------------------------------------------------
3 4 <  . cr             \ -> -1
4 4 =  . cr             \ -> -1
5 4 >  . cr             \ -> -1
3 4 <> . cr             \ -> -1
0 0=   . cr             \ -> -1

\ --- Arithmetic -----------------------------------------------------------
7 3 +   . cr            \ -> 10
7 3 -   . cr            \ -> 4
7 3 *   . cr            \ -> 21
7 3 /   . cr            \ -> 2
5 1+    . cr            \ -> 6
5 1-    . cr            \ -> 4
17 5 mod . cr           \ -> 2
17 5 /mod .s cr         \ 2 3   (remainder quotient)
drop drop
10 3 min . cr           \ -> 3
10 3 max . cr           \ -> 10
0 5 - abs . cr          \ -> 5

\ --- Logical --------------------------------------------------------------
\ These are LOGICAL (boolean) ops, not bitwise: any non-zero is "true".
true false or  . cr     \ -> -1
true false and . cr     \ -> 0
true false xor . cr     \ -> -1
0 invert . cr           \ -> -1  (invert = logical NOT)

\ --- Output ---------------------------------------------------------------
65 emit cr              \ -> A   (emit prints one character by its code)
bl . cr                 \ -> 32  (bl pushes the space character code)
"hello" . space "world" . cr     \ -> hello world  (. also prints strings)
\ NOTE: /eval buffers its whole reply, and emit writes through one shared
\ scratch byte, so several emits in one /eval collapse to the last char —
\ build multi-char output from strings with `.` (as above), not chained emit.

\ --- List -----------------------------------------------------------------
nil 1 :: 2 :: 3 :: count . cr    \ -> 3   (:: conses onto a list)
nil 10 :: 20 :: 30 :: 1 nth . cr \ -> 20  (nth is 0-indexed)
nil "a" :: "b" :: "c" :: str-join . cr   \ -> abc  (joins in insertion order)

\ --- String ---------------------------------------------------------------
"ab" 3 $repeat . cr        \ -> ababab
"hi" 5 "." pad-right . cr   \ -> ...hi  (right-align to width 5; pad char is a string)
"hi" 5 "." pad-left  . cr   \ -> hi...  (left-align to width 5)
"MiXeD" lower . cr         \ -> mixed
"MiXeD" upper . cr         \ -> MIXED
"a b&c" url-encode . cr    \ -> a%20b%26c
"a%20b" url-decode . cr    \ -> a b
"123" >number . cr         \ -> 123

\ --- JSON -----------------------------------------------------------------
"{\"a\":{\"b\":7}}" json-parse "a.b" json-get . cr   \ -> 7

\ --- Control flow (only inside : ... ;) -----------------------------------
: sgn ( n -- -1|0|1 ) dup >0 if drop 1 else <0 if -1 else 0 then then ;
5  sgn . cr             \ -> 1
-3 sgn . cr             \ -> -1
0  sgn . cr             \ -> 0
: countdown ( n -- ) begin dup . space 1- dup 0= until drop ;
5 countdown cr          \ -> 5 4 3 2 1
: name-of ( n -- ) case
    1 of "one"   . endof
    2 of "two"   . endof
        "many"   .
  endcase ;
1 name-of cr  2 name-of cr  9 name-of cr

\ --- Meta -----------------------------------------------------------------
"2 3 + 4 *" evaluate . cr        \ -> 20

\ --- Memory / variables (persist across /eval while the process lives) ----
variable counter
0 counter !            \ store
41 counter +!          \ add-to
counter @ . cr         \ -> 41
counter ? cr           \ prints the cell too
allocate-cell dup 99 swap ! dup @ . cr free-cell   \ -> 99, then release

\ --- Time (thin wrappers over lib/sys/time.mtl) ---------------------------
\ No NTP in the simulator, so the wall clock reads the epoch; uptime/time-ms
\ are real. `ms` (async sleep) is saved for the very end — see the note there.
uptime  . cr           \ seconds since boot (a small number)
time-ms . cr           \ monotonic millisecond clock
time?   . cr           \ flag: has the wall clock been synced?
utc>string . cr        \ UTC time; reads the epoch until synced (no NTP in the sim)
\ `ms` (async sleep) is demonstrated at the very end — see the note there.

\ --- Task (background words on the scheduler thread) -----------------------
\ Run a named word every 200 ms as a task, list it, then stop it. Task ids are
\ process-global: id 1 is the built-in `ears` state-machine task (main starts
\ it), so on a fresh process the first task you start is id 2 — check `.tasks`
\ for the real id and substitute it below if you've started others.
"1 counter +!" 200 "tick" task-start   \ increments `counter` every 200 ms
.tasks                                  \ 1 ears .../..  2 tick .../200
2 task-suspend .  cr    \ -> -1  (pause the tick task; leaves ears running)
2 task-resume  .  cr    \ -> -1
2 task-stop    .  cr    \ -> -1  (remove it)

\ --- Hardware (VM natives; visible in the sim log, physical on device) -----
16711680 0 led!         \ nose -> red (0xFF0000)  -> [simuleds] line in sim log
65280    2 led!         \ middle -> green
leds-off                \ all off
0 5 0 move-ear          \ left ear -> pos 5, forward (no-op until homed)
ears .s drop drop cr    \ show both ear positions (? until homing completes), then clear

\ --- Net (DEVICE BUILD ONLY — task build:app:template) ---------------------
\ These four words are compiled in only under the device net stack
\ (lib/net/net.mtl); the simulator's lib/net/tcp.mtl links no HTTP client or
\ WiFi, so they are NOT defined here and the lines below stay commented.
\ On the rabbit, uncomment to try them:
\
\   mac  . cr                              ( -- ) print WiFi MAC
\   ip   . cr                              ( -- ) print current IP
\   "http://example.com/" http-get         ( url -- content header )
\       drop . cr                          \ print the body, drop the header
\   "hello" "http://example.com/echo" http-post
\       drop . cr                          ( payload url -- content header )

\ --- Time: ms (async sleep), demonstrated LAST -----------------------------
\ `ms` suspends the interpreter and yields to the scheduler, so /eval flushes
\ its reply at that point — anything after an `ms` in the same /eval runs later
\ (async) and won't appear in the response. That's why it's the final word here.
"=== demo complete; the 250 ms sleep below runs after this reply ===" . cr
250 ms                  \ ( delay -- ) async sleep; the last thing we do
