#!/usr/bin/env python

import json
import sys
import xapian
from support import parse_csv_file

### Start of example code.
def index(datapath, dbpath):
    # Create or open the database we're going to be writing to.

    # Set up a TermGenerator that we'll use in indexing.
    cnt = 0
    for fields in parse_csv_file(datapath):
        cnt = cnt + 1 
        # 'fields' is a dictionary mapping from field name to value.
        # Pick out the fields we're going to index.
        description = fields.get('word', '    ')
        title = fields.get('value', '    ')
        #identifier = fields.get('id_NUMBER', u'')
        print(description)
        print(title)
        #print(identifier)
    print(cnt)
### End of example code.

if len(sys.argv) != 3:
    print("Usage: %s DATAPATH DBPATH" % sys.argv[0])
    sys.exit(1)

index(datapath = sys.argv[1], dbpath = sys.argv[2])

