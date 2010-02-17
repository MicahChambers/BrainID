#!/usr/bin/python

import pylab as P
import sys
from nifti import *

#t = arange(0.0, 2.0, 0.01)
#s = sin(2*pi*t)
#plot(t, s, linewidth=1.0)
#
#xlabel('time (s)')
#ylabel('voltage (mV)')
#title('About as simple as it gets, folks')
#grid(True)
#show()

images = list();
for arg in sys.argv[1:]:
    images.append(image.NiftiImage(arg))

leg = []

data = sys.stdin.readlines()
time = []
for lineno in range(0,len(data)):
    data[lineno] = data[lineno].split()
    time.append(float(data[lineno][0])/2)
    data[lineno] = float(data[lineno][1])*.0002-.0004

for line in data:
    print line

for nifti in images:
    print nifti
    for iter in range(0,nifti.extent[0]):
        P.plot(nifti.data[:, 0,0,iter])
#        leg = leg + [nifti.filename+":"+str(iter)]
#P.legend(sys.argv[1:])
P.plot(time, data)
#P.legend(["BOLD", "With Noise SNR = 2", "Input"])

P.xlabel("Time, Seconds")
P.ylabel("BOLD Signal %")
P.show()
#
#names = ['Ts', 'Tf', 'epsilon', 'T0', 'alpha', 'E_0', 'V0', 'Vt', 'Qt', 'St', 'Ft']
#for i in range(0,11):
#    P.subplot(6, 2, i+1)
#    line1 = P.plot(truestat.data[:, 0, i, 0])
#    line2 = P.plot(eststat.data[:, 0, i, 0])
#    P.ylabel(names[i])
#
#P.show()