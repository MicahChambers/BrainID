#!/usr/bin/python

import pylab as P
import sys
import nibabel 
from paramtest import State, readout, transition
from numpy import convolve
import scipy.stats
from scipy.stats.distributions import gamma
import scipy.io as io
from bar import histo, plothisto

DIVIDER =512
HRFDIVIDER = 16

#t = arange(0.0, 2.0, 0.01)
#s = sin(2*pi*t)
#plot(t, s, linewidth=1.0)
#
#xlabel('time (s)')
#ylabel('voltage (mV)')
#title('About as simple as it gets, folks')
#grid(True)
#show()

def printparams(params):
    print "TAU_0  " , params[0]
    print "ALPHA  " , params[1]
    print "E_0    " , params[2]
    print "V_0    " , params[3]
    print "TAU_S  " , params[4]
    print "TAU_F  " , params[5]
    print "EPSILON" , params[6]
    print "A_1    " , params[7]
    print "A_2    " , params[8]



def sim(stims, params, TR, num):
    TR = float(TR)
    out = [0 for i in range(0, num)]
    inputl = 0;
    inputt = 0
    statevars = State()
    stopt = num*TR/DIVIDER
    for t in range(0, num*DIVIDER):
        if inputt < len(stims) and t*TR/DIVIDER > stims[inputt][0]:
            inputl = stims[inputt][1]
            inputt = inputt+1
        statevars = transition(statevars, params, TR/DIVIDER, inputl);
        if t%DIVIDER == 0:
            out[t/DIVIDER] = readout(statevars, params)
    return out

hrfparam   = [6, 16, 1, 1, 6, 0, 32];
def HRF(stims, TR, num):
    TR = float(TR)
    inputl = 0;
    inputt = 0
    dt = TR/HRFDIVIDER
    stimarr = [i for i in range(0, int(num*HRFDIVIDER + 2*hrfparam[6]/dt))]
    for i in range(0, len(stimarr)):
        if inputt < len(stims) and i*dt > stims[inputt][0] + hrfparam[5]\
                    + hrfparam[6]:
            inputl = stims[inputt][1]
            inputt = inputt+1
        stimarr[i] = inputl

    points = [i for i in range(0,hrfparam[6]/dt)]
    hrf = [gamma.pdf(u, hrfparam[0]/hrfparam[2], scale=hrfparam[2]/dt) - \
                gamma.pdf(u, hrfparam[1]/hrfparam[3], scale=hrfparam[3]/dt) \
                / hrfparam[4] for u in points]
    scale = sum([hrf[i] for i in range(len(hrf)) if i%HRFDIVIDER == 0])
    hrf = [point/scale for point in hrf]
    
    hdsignal = convolve(stimarr, hrf, mode="full") 
    signal = [hdsignal[i] for i in range(0, len(stimarr)) \
                if i%HRFDIVIDER == 0 and i*dt > hrfparam[6] and \
                i*dt < len(stimarr)*dt-hrfparam[6]]
    delta = [(hdsignal[i+1] - hdsignal[i])/dt for i in range(0, len(stimarr)) \
                if i%HRFDIVIDER == 0 and i*dt > hrfparam[6] and \
                i*dt < len(stimarr)*dt-hrfparam[6]]
    return (signal, delta)
    

if len(sys.argv) != 2:
    print "Usage: ", sys.argv[0], "<InDir>"
    print "Looks in Dir for: "
    print "stim0, stim1 (must be shifted to match pfilter_input), histogram.nii.gz"
    print "pfilter_input.nii.gz"
    sys.exit(-1);

actual = nibabel.load(sys.argv[1] + "pfilter_input.nii.gz")

histimg = nibabel.load(sys.argv[1] + "histogram.nii.gz")
#beta1 = nibabel.load(sys.argv[1] + "beta_0001.img")
#beta2 = nibabel.load(sys.argv[1] + "beta_0002.img")
#beta3 = nibabel.load(sys.argv[1] + "beta_0003.img")
#beta4 = nibabel.load(sys.argv[1] + "beta_0004.img")
#beta5 = nibabel.load(sys.argv[1] + "beta_0005.img")

TR = histimg.get_header()['pixdim'][4];
if TR == 1:
    print "Pixdim is 1, changing to 2.1"
    TR = 2.1

#SPM = io.loadmat(sys.argv[1]+"SPM.mat")
#design = SPM['SPM'][0,0].xX[0,0].X
#b1 = beta1.get_data()[35,14,7]
#b2 = beta2.get_data()[35,14,7]
#b3 = beta3.get_data()[35,14,7]
#b4 = beta4.get_data()[35,14,7]
#b5 = beta5.get_data()[35,14,7]
#
#d1 = [b1*samp for samp in design[:,0]]
#d2 = [b2*samp for samp in design[:,1]]
#d3 = [b3*samp for samp in design[:,2]]
#d4 = [b4*samp for samp in design[:,3]]
#d5 = [b5*samp for samp in design[:,4]]
#
#result = map(lambda a,b,c,d : a+b+c+d, d1, d2, d3, d4)
#
#P.plot(result)
#P.legend(["actual", "sum"])
#P.show()
#sys.exit()
#print stims

#print real

PARAM = -1
histograms = [[] for t in range(0, histimg.get_header()['dim'][4])]
#generate the histogram for measurements
for t in range(0, histimg.get_header()['dim'][4]):
    upper = histimg.get_data()[0,0,0,t,PARAM, -1]
    lower = histimg.get_data()[0,0,0,t,PARAM, -2]
    count = histimg.get_header()['dim'][6] - 2
    width = (upper-lower)/count
    print lower, upper, count, width
    histinput = [[] for j in range(0, histimg.get_header()['dim'][6]-2)]
    for j in range(0, histimg.get_header()['dim'][6]-2):
        histinput[j] = [histimg.get_data()[0,0,0,t, PARAM, j], lower+width*j, \
                    lower+width*(j+1)]
    histograms[t] = histo(histinput)

plothisto(histograms, TR)

#print "Initial Params"
#printparams(parammu.get_data()[0,0,0,0,:])
#print "Final Params"
#printparams(parammu.get_data()[0,0,0,-1,:])
#initest = sim(stims, parammu.get_data()[0,0,0,0,:], TR, measmu.get_header()['dim'][4])
#final = sim(stims, parammu.get_data()[0,0,0,-1,:], TR, measmu.get_header()['dim'][4])


#adjusted = [real.get_data()[0,0,0,ii] + parammu.get_data()[0,0,0,-1, 11] \
#            for ii in range(0, len(real.get_data()[0,0,0,:]))]
    
P.plot([i*TR for i in range(actual.get_header()['dim'][4])], actual.get_data()[0,0,0,:], '-*');
#P.plot(initest)
#P.plot(measmu.get_data()[0,0,0,:]);
#P.plot(final)
#P.plot(canonical)

#P.legend(["Adjusted real", "Estimated", "Final Est", "HRF"])

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

