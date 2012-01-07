# Findings


## Leds
Led 0 - 4, Color RGB

    led 4 0x000000; //black

    led 0 0x0000FF; //blue
    led 1 0x00FF00; //green
    led 2 0xFF0000; //red

    led 3 0xFFFF00; //yellow
    led 4 0xFF00FF; //lila
    led 0 0x00FFFF; //cyan

    led 1 0xFFFFFF; //white


## Ears

## Custom functions

  webmac // string to list of hex chars e.g.

## Main

`proto main 0;;` needs to be declared on top

minimal func:

    fun main=
      confInit;
      wifiInit 0;
      loopcb #loop; // 20 p. second

      netstart;
      srand time_ms;
      0
    ;;


## Metal Examples
    type MySum= Zero | Const _ | Add _ | Mul _ ;;

    fun eval z=
      match z with
      ( Zero -> 0)
     |( Const a -> a)
     |( Add [x y] -> (eval x)+(eval y))
     |( Mul [x y] -> (eval x)*(eval y)) ;;

    Iecholn eval Add [Const 1 Mul [Const 2 Const 3]]; // -> 1 + 2 * 3 -> 7