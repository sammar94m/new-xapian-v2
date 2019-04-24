#!/usr/bin/env python
import json
import sys
import xapian
from support import parse_csv_file
import support
import random
import string

querystring = " "
offset = 0
pagesize = 10


def random_string(size=random.randint(7,10), chars=string.ascii_letters):
    return ''.join(random.choice(chars) for _ in range(size))

### Start of example code.
def pickupRandomX(datapath, num, miss_num, res_name):
    print(num)
    print(miss_num)
    toSearch=[]

    for fields in parse_csv_file(datapath):
        description = fields.get('DESCRIPTION', u'')
        title = fields.get('TITLE', u'')
        identifier = fields.get('id_NUMBER', u'')
        toSearch.append(title)
    
    random.shuffle(toSearch)
    
    #words_num =  str(num / 1000) + "K"

    f= open(res_name,"w+")
    index = 0 
    while num > 0:
        if len(toSearch[index]) > 0 and toSearch[index].isalnum():
            f.write(toSearch[index] + "\n")
            num = num -1
        index = index + 1
    
    for i in xrange(0,miss_num):
        strng = random_string()
        f.write(strng + "\n")

    f.close()

if len(sys.argv) != 5:
    print("args num:" + str(len(sys.argv)))
    print("Run: %s CSV_FILE WORDS_NUM HIT_PERCENT res_name" % sys.argv[0])
    
    sys.exit(1)

pickupRandomX(datapath = sys.argv[1], num = int(sys.argv[2]) * int(sys.argv[3]) / 100, miss_num = int(sys.argv[2])* (100 - int(sys.argv[3])) / 100, res_name = sys.argv[4])

