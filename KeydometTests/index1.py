#!/usr/bin/env python
import json
import sys
import xapian
from support import parse_csv_file
import support
import random
import time

querystring = " "
offset = 0
pagesize = 10

### Start of example code.
def index(datapath):

    # Create or open the database we're going to be writing to.
    #db = xapian.WritableDatabase(dbpath, xapian.DB_BACKEND_INMEMORY)
    db = xapian.inmemory_open()

    # Set up a TermGenerator that we'll use in indexing.
    termgenerator = xapian.TermGenerator()
    termgenerator.set_stemmer(xapian.Stem("en"))

    for fields in parse_csv_file(datapath):
        # 'fields' is a dictionary mapping from field name to value.
        # Pick out the fields we're going to index.
        description = fields.get('DESCRIPTION', u'')
        title = fields.get('TITLE', u'')
        identifier = fields.get('id_NUMBER', u'')
        
        # We make a document and tell the term generator to use this.
        doc = xapian.Document()
        termgenerator.set_document(doc)

        # Index each field with a suitable prefix.
        termgenerator.index_text(title, 1, 'S')
        termgenerator.index_text(description, 1, 'XD')

        # Index fields without prefixes for general search.
        termgenerator.index_text(title)
        termgenerator.increase_termpos()
        termgenerator.index_text(description)

        # Store all the fields for display purposes.
        doc.set_data(json.dumps(fields))

        # We use the identifier to ensure each object ends up in the
        # database only once no matter how many times we run the
        # indexer.
        idterm = u"Q" + identifier
        doc.add_boolean_term(idterm)
        db.replace_document(idterm, doc)

    print("done indexing")
    return db

def search(db, searchpath, offset=0, pagesize=10):
    total_search_time = 0
    lineNum = 0;
    queryparser = xapian.QueryParser()
    queryparser.set_stemmer(xapian.Stem("en"))
    queryparser.set_stemming_strategy(queryparser.STEM_SOME)
    queryparser.add_prefix("title", "S")
    queryparser.add_prefix("description", "XD")
    # End of prefix configuration.
    
    with open(searchpath) as fd:
        querystring = fd.readline()
        while querystring:
                lineNum = lineNum + 1
        
                start = int(round(time.time() * 1000))

		query = queryparser.parse_query(querystring)
		enquire = xapian.Enquire(db)
		enquire.set_query(query)

		# And print out something about each match
		matches = enquire.get_mset(offset, pagesize)
                end = int(round(time.time() * 1000))
                total_search_time +=  end - start
               #if matches.empty:
             #    print("miss")
              #  else:
               #     print("hit")
		
                querystring = fd.readline()
    print(searchpath + " search time: " + str(total_search_time))
    print("lines:" + str(lineNum))
    fd.close()  

if len(sys.argv) < 3:
    print("Run: %s DATA_TO_INDEX DATA_TO_SEARCH" % sys.argv[0])
    sys.exit(1)

db = index(datapath = sys.argv[1])

for i in xrange(2,len(sys.argv)):
    #print(sys.argv[i])
    search(db, searchpath = sys.argv[i])
    #print(sys.argv[i])
    time.sleep(10)








