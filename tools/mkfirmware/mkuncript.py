#!/usr/bin/python
import struct

'''
convert firmware file web into bin
'''

#import sys
#print 'Number of arguments:', len(sys.argv), 'arguments.'
#print 'Argument List:', str(sys.argv)

invb=[1,  171,205,183,57, 163,197,239,241,27,
61, 167,41, 19, 53, 223,225,139,173,151, 
25, 131,165,207,209,251,29, 135,9,  243, 
21, 191,193,107,141,119,249,99, 133,175, 
177,219,253,103,233,211,245,159,161,75, 
109,87, 217,67, 101,143,145,187,221,71,  
201,179,213,127,129,43, 77, 55, 185,35,  
69, 111,113,155,189,39, 169,147,181,95, 
97, 11, 45, 23, 153,3,  37, 79, 81, 123,
157,7,  137,115,149,63, 65, 235,13, 247,
121,227,5,  47, 49, 91, 125,231,105,83,
117,31, 33, 203,237,215,89, 195,229,15,
17, 59, 93, 199,73, 51, 85, 255]

filename="firmware.0.0.0.10.sim.txt"

firmwarelimit="-violet-"
text_file = open(filename+".ori", "wb")
a = open(filename,"rb").read()

#01234567890123456789
#-violet-........
z1 = int(a[8:10],16)<<24
print z1
z2 = int(a[10:12],16)<<16
print z2
z3 = int(a[12:14],16)<<8
print z3
z4 = int(a[14:16],16)
print z4
l = z1+z2+z3+z4 +16 #int(a[8:10],16)<<24 + int(a[10:12],16)<<16 + int(a[12:14],16)<<8 + int(a[14:16],16)
#len = int(ord(a[22:23])<<24) + int(ord(a[23:24])<<16) + int(ord(a[24:25])<<8) + int(ord(a[25:26]))
z=16
alpha=47
key=0x47
w=len(a)/2

for i in range(8,l/2):
  
  #print hex(i)
  #b = hex(ord(i))[2:].zfill(2)
  b=a[z:z+2]
 
  v=(int(b,16)-alpha)*key
  b=v & 0xff
  c=struct.pack('B', b)
  key = v + v + 1;
  key = key & 0xff  
  text_file.write(c)
  z=z+2
    
text_file.close()
print filename," done"
