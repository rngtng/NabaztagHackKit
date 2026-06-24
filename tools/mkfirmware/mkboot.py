#!/usr/bin/python

'''
convert bootfile bc.jsp into file for web update
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

filename="bc.jsp"
f=open(filename,"rb")
f.seek(0, 2)
lengx = int(f.tell())
f.close()
print lengx

firmwarelimit="-violet-"
text_file = open(filename+"boot.txt", "wb")
a = open(filename,"rb").read()

l=lengx*2
lh= hex(l)[2:].zfill(8)
print lh

text_file.write(firmwarelimit)
text_file.write(lh)   

z=0
alpha=47
key=0x47
#leng=leng+26
for i in a:
  #if z>25 and z <leng:
  #print hex(i)
  #b = hex(ord(i))[2:].zfill(2)
  v=ord(i);
  b=alpha + v * invb[ key >> 1 ]
  b = b & 0xff
  key = v + v + 1;
  key = key & 0xff      
  b = hex(b)[2:].zfill(2)  
  text_file.write(b)
  #z=z+1
 
text_file.write(firmwarelimit)           
text_file.close()
print filename," done"
