#!/usr/bin/env php
<?php
$firmwarelimit = '-violet-';
$inv8 = array
(
    1,171,205,183, 57,163,197,239,241, 27, 61,167, 41, 19, 53,223,
  225,139,173,151, 25,131,165,207,209,251, 29,135,  9,243, 21,191,
  193,107,141,119,249, 99,133,175,177,219,253,103,233,211,245,159,
  161, 75,109, 87,217, 67,101,143,145,187,221, 71,201,179,213,127,
  129, 43, 77, 55,185, 35, 69,111,113,155,189, 39,169,147,181, 95,
   97, 11, 45, 23,153,  3, 37, 79, 81,123,157,  7,137,115,149, 63,
   65,235, 13,247,121,227,  5, 47, 49, 91,125,231,105, 83,117, 31,
   33,203,237,215, 89,195,229, 15, 17, 59, 93,199, 73, 51, 85,255
);

// fun bin4toi s=
//        ((strnthchar s 0)<<24)+((strnthchar s 1)<<16)+\
//        ((strnthchar s 2)<<8)+(strnthchar s 3);;
function bin4toi($s)
{
	return (ord($s[0])<<24) | (ord($s[1]) <<16) | (ord($s[2])<<8) | ord($s[3]);
}

//fun itoh8 x=strright strcat "00000000" itoh x 8;;
function itoh8( $x )
{
	return substr(str_pad(dechex($x), 8, "0", STR_PAD_LEFT), -8);
}

//fun itoh2 x=strright strcat "00" itoh x 2;;
function itoh2( $x )
{
	return substr(str_pad(dechex($x), 2, "0", STR_PAD_LEFT), -2);
}

//fun hexdata data i imax=
//        if i<imax then (itoh2 strnthchar data i)::hexdata data i+1 imax;;
function hexdata( $data, $i, $imax)
{
  $o = '';
	while($i<$imax) {
		$o .= itoh2(ord($data[$i]));
    $i++;
  }
	return $o;
}

function strcrypt8($str,$key,$alpha)
{
  global $inv8;
  $o='';
  for($i=0;$i<strlen($str);$i++)
  {
    $v = ord($str[$i]);
    $o .= chr($alpha+($v*$inv8[$key>>1])%256);
    $key = (1+2*$v)%256;
  }
  return $o;
}

function mkfirmware($src,$out)
{
  global $firmwarelimit;
  echo 'Opening BIN file: '.$src."\n\r";
  //let load src -> data in
  $code = file_get_contents($src);
  $size = filesize($src);
  echo $size;
  //echo hexdata($data,0,$size);

  //let strcrypt8 data 0x47 47 -> [data _] in
  $code = strcrypt8($code,0x47,47);
  //let strcatlist hexdata data 0 len -> binhex in
  $binhex = hexdata($code,0,$size);
  //save strcatlist firmwarelimit::(itoh8 2*len)::\
  //                      binhex::firmwarelimit::nil strcat src ".txt"
  echo "\n\r\t".'Saving file'."\t".$out."\n";
  file_put_contents($out,
                $firmwarelimit.itoh8(2*$size).$binhex.$firmwarelimit);}
if($argc==3)
	mkfirmware($argv[1],$argv[2]);
else
	mkfirmware("Nab.bin","firmware0.0.0.13.sim");
?>
