#!/usr/bin/env python
import os
import time

wc=["10000","100000","1000000"]
hits=["0","25","50","75","100"]

for i in wc:
    for cnt in xrange(0,3):
        for j in hits:
            name = " " + str(int(i)/1000) + "k_hit_" +j+"_"+str(cnt)+"%"
            os.system("python randomToSearch.py data.csv "+" "+ i +" "+ j + name)
    #time.sleep(120)
