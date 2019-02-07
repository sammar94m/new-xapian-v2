#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <xapian.h>
using namespace std;
//using namespace xapian;

// Read a file into a string.
// from: http://stackoverflow.com/a/525103
string readFile(const string &fileName)
{
    ifstream ifs(fileName.c_str(), ios::in | ios::binary | ios::ate);

    ifstream::pos_type fileSize = ifs.tellg();
    ifs.seekg(0, ios::beg);

    vector<char> bytes(fileSize);
    ifs.read(&bytes[0], fileSize);

    return string(&bytes[0], fileSize);
}

// command-line arguments will be filenames to index
int main(int argc, char **argv) {

    // Create or open the database we're going to be writing to.
//    Xapian::WritableDatabase db("", Xapian::DB_BACKEND_INMEMORY);

    // Alternative: in-memory db:
	std::cout<<"sammar ---------"<<endl;
    Xapian::WritableDatabase db = Xapian::InMemory::open();

    // Set up a TermGenerator that we'll use in indexing.
    Xapian::TermGenerator termgenerator;

    // Use a "stemmer", which reduces words like "root", "rooting", "rooted", etc. to their common stem "root"
    // before adding to the index. Queries will need to be stemmed, too (see below).
    termgenerator.set_stemmer(Xapian::Stem("en"));

    for(int i = 1; i < argc; i++)
    {
        string fname(argv[i]);
        ifstream f(fname.c_str());
        if(f.is_open())
        {
	    cout<<"in for: "<<fname<<endl;
            string body = readFile(fname);
            Xapian::Document doc;
            termgenerator.set_document(doc);

            termgenerator.index_text(body);

            // set the human-readable doc data; Xapian ignores this, but can show it to the user
            doc.set_data(fname + "\n\n" + body.substr(0, min((unsigned long)body.size(), (unsigned long)100)) + "...");

            // give each doc a unique ID based on filename; this ensures we don't add the same doc twice
            // (if the program is run multiple times, or the filename is specified twice on the command line)
            string idterm = "FNAME" + fname;
            doc.add_boolean_term(idterm);
            // instead of add_document, use replace_document, so it only is included once in the index
            db.replace_document(idterm, doc);
            f.close();
        }
    }

    // Now let's search
    int offset = 0;
    int pagesize = 10;
    Xapian::QueryParser queryparser;
    string querystring;
    while(true)
    {
        cout << "Enter a search query (or \"quit\"): ";
        getline(cin, querystring);
        if(querystring == "quit")
        {
            break;
        }
	//sets search language to english
        queryparser.set_stemmer(Xapian::Stem("en"));
	//set stategy :/
        queryparser.set_stemming_strategy(queryparser.STEM_SOME);
	
        Xapian::Query query = queryparser.parse_query(querystring);
        Xapian::Enquire enquire(db);
        enquire.set_query(query);
        Xapian::MSet mset = enquire.get_mset(offset, pagesize);

        cout << "Showing results " << offset + 1 << "-" << min(offset + pagesize, (int)mset.size())
             << " of " << mset.size() << endl;
        for(Xapian::MSetIterator m = mset.begin(); m != mset.end(); ++m)
        {
            Xapian::docid did = *m;
            cout << m.get_rank() + 1 << ": DocID " << did << ", match score: " << m.get_percent() << endl;

            string data = m.get_document().get_data();
            cout << data << endl << endl << endl;
        }
        cout << endl << endl;
    }

    return 0;
}
