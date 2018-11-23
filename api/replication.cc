/** @file replication.cc
 * @brief Replication support for Xapian databases.
 */
/* Copyright (C) 2008 Lemur Consulting Ltd
 * Copyright (C) 2008,2009,2010,2011,2012,2013,2014,2015,2016 Olly Betts
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <config.h>

#include "replication.h"

#include "xapian/intrusive_ptr.h"
#include "xapian/constants.h"
#include "xapian/dbfactory.h"
#include "xapian/error.h"
#include "xapian/version.h"

#include "backends/database.h"
#include "backends/databasereplicator.h"
#include "debuglog.h"
#include "filetests.h"
#include "fileutils.h"
#include "io_utils.h"
#include "omassert.h"
#include "realtime.h"
#include "net/remoteconnection.h"
#include "noreturn.h"
#include "replicationprotocol.h"
#include "safesysstat.h"
#include "safeunistd.h"
#include "net/length.h"
#include "str.h"
#include "unicode/description_append.h"

#include "autoptr.h"
#include <cerrno>
#include <fstream>
#include <string>

using namespace std;
using namespace Xapian;

// The banner comment used at the top of the replica's stub database file.
#define REPLICA_STUB_BANNER \
"# Automatically generated by Xapian::DatabaseReplica v" XAPIAN_VERSION ".\n" \
"# Do not manually edit - replication operations may regenerate this file.\n"

XAPIAN_NORETURN(static void throw_connection_closed_unexpectedly());
static void
throw_connection_closed_unexpectedly()
{
    throw Xapian::NetworkError("Connection closed unexpectedly");
}

void
DatabaseMaster::write_changesets_to_fd(int fd,
				       const string & start_revision,
				       ReplicationInfo * info) const
{
    LOGCALL_VOID(REPLICA, "DatabaseMaster::write_changesets_to_fd", fd | start_revision | info);
    if (info != NULL)
	info->clear();
    Database db;
    try {
	db = Database(path);
    } catch (const Xapian::DatabaseError & e) {
	RemoteConnection conn(-1, fd);
	conn.send_message(REPL_REPLY_FAIL,
			  "Can't open database: " + e.get_msg(),
			  0.0);
	return;
    }
    if (db.internal.size() != 1) {
	throw Xapian::InvalidOperationError("DatabaseMaster needs to be pointed at exactly one subdatabase");
    }

    // Extract the UUID from start_revision and compare it to the database.
    bool need_whole_db = false;
    string revision;
    if (start_revision.empty()) {
	need_whole_db = true;
    } else {
	const char * ptr = start_revision.data();
	const char * end = ptr + start_revision.size();
	size_t uuid_length;
	decode_length_and_check(&ptr, end, uuid_length);
	string request_uuid(ptr, uuid_length);
	ptr += uuid_length;
	string db_uuid = db.internal[0]->get_uuid();
	if (request_uuid != db_uuid) {
	    need_whole_db = true;
	}
	revision.assign(ptr, end - ptr);
    }

    db.internal[0]->write_changesets_to_fd(fd, revision, need_whole_db, info);
}

string
DatabaseMaster::get_description() const
{
    string desc = "DatabaseMaster(";
    description_append(desc, path);
    desc += ")";
    return desc;
}

/// Internal implementation of DatabaseReplica
class DatabaseReplica::Internal : public Xapian::Internal::intrusive_base {
    /// Don't allow assignment.
    void operator=(const Internal &);

    /// Don't allow copying.
    Internal(const Internal &);

    /// The path to the replica directory.
    string path;

    /// The id of the currently live database in the replica (0 or 1).
    int live_id;

    /** The live database being replicated.
     *
     *  This needs to be mutable because it is sometimes lazily opened.
     */
    mutable WritableDatabase live_db;

    /** Do we have an offline database currently?
     *
     *  The offline database is a new copy of the database we're bringing up
     *  to the required revision, which can't yet be made live.
     */
    bool have_offline_db;

    /** Flag to indicate that the only valid operation next is a full copy.
     */
    bool need_copy_next;

    /** The revision that the secondary database has been updated to.
     */
    string offline_revision;

    /** The UUID of the secondary database.
     */
    string offline_uuid;

    /** The revision that the secondary database must reach before it can be
     *  made live.
     */
    string offline_needed_revision;

    /** The time at which a changeset was last applied to the live database.
     *
     *  Set to 0 if no changeset applied to the live database so far.
     */
    double last_live_changeset_time;

    /// The remote connection we're using.
    RemoteConnection * conn;

    /** Update the stub database which points to a single database.
     *
     *  The stub database file is created at a separate path, and then
     *  atomically moved into place to replace the old stub database.  This
     *  should allow searches to continue uninterrupted.
     */
    void update_stub_database() const;

    /** Delete the offline database. */
    void remove_offline_db();

    /** Apply a set of DB copy messages from the connection.
     */
    void apply_db_copy(double end_time);

    /** Check that a message type is as expected.
     *
     *  Throws a NetworkError if the type is not the expected one.
     */
    void check_message_type(int type, int expected) const;

    /** Check if the offline database has reached the required version.
     *
     *  If so, make it live, and remove the old live database.
     *
     *  @return true iff the offline database is made live
     */
    bool possibly_make_offline_live();

    string get_replica_path(int id) const {
	string p = path;
	p += "/replica_";
	p += char('0' + id);
	return p;
    }

  public:
    /// Open a new DatabaseReplica::Internal for the specified path.
    explicit Internal(const string & path_);

    /// Destructor.
    ~Internal() { delete conn; }

    /// Get a string describing the current revision of the replica.
    string get_revision_info() const;

    /// Set the file descriptor to read changesets from.
    void set_read_fd(int fd);

    /// Read and apply the next changeset.
    bool apply_next_changeset(ReplicationInfo * info,
			      double reader_close_time);

    /// Return a string describing this object.
    string get_description() const { return path; }
};

// Methods of DatabaseReplica

DatabaseReplica::DatabaseReplica(const string & path)
	: internal(new DatabaseReplica::Internal(path))
{
    LOGCALL_CTOR(REPLICA, "DatabaseReplica", path);
}

DatabaseReplica::~DatabaseReplica()
{
    LOGCALL_DTOR(REPLICA, "DatabaseReplica");
    delete internal;
}

string
DatabaseReplica::get_revision_info() const
{
    LOGCALL(REPLICA, string, "DatabaseReplica::get_revision_info", NO_ARGS);
    RETURN(internal->get_revision_info());
}

void
DatabaseReplica::set_read_fd(int fd)
{
    LOGCALL_VOID(REPLICA, "DatabaseReplica::set_read_fd", fd);
    internal->set_read_fd(fd);
}

bool
DatabaseReplica::apply_next_changeset(ReplicationInfo * info,
				      double reader_close_time)
{
    LOGCALL(REPLICA, bool, "DatabaseReplica::apply_next_changeset", info | reader_close_time);
    if (info != NULL)
	info->clear();
    RETURN(internal->apply_next_changeset(info, reader_close_time));
}

string
DatabaseReplica::get_description() const
{
    string desc("DatabaseReplica(");
    desc += internal->get_description();
    desc += ')';
    return desc;
}

// Methods of DatabaseReplica::Internal

void
DatabaseReplica::Internal::update_stub_database() const
{
    string stub_path = path;
    stub_path += "/XAPIANDB";
    string tmp_path = stub_path;
    tmp_path += ".tmp";
    {
	ofstream stub(tmp_path.c_str());
	stub << REPLICA_STUB_BANNER
		"auto replica_" << live_id << endl;
    }
    if (!io_tmp_rename(tmp_path, stub_path)) {
	string msg("Failed to update stub db file for replica: ");
	msg += path;
	throw Xapian::DatabaseOpeningError(msg, errno);
    }
}

DatabaseReplica::Internal::Internal(const string & path_)
	: path(path_), live_id(0), live_db(), have_offline_db(false),
	  need_copy_next(false), offline_revision(), offline_needed_revision(),
	  last_live_changeset_time(), conn(NULL)
{
    LOGCALL_CTOR(REPLICA, "DatabaseReplica::Internal", path_);
#if !defined XAPIAN_HAS_CHERT_BACKEND && !defined XAPIAN_HAS_GLASS_BACKEND
    throw FeatureUnavailableError("Replication requires the chert or glass backends to be enabled");
#else
    if (mkdir(path.c_str(), 0777) == 0) {
	// The database doesn't already exist - make a directory, containing a
	// stub database, and point it to a new database.
	//
	// Create an empty database - the backend doesn't matter as if the
	// master is a different type, then the replica will become that type
	// automatically.
	live_db = WritableDatabase(get_replica_path(live_id),
				   Xapian::DB_CREATE);
	update_stub_database();
    } else {
	if (errno != EEXIST) {
	    throw DatabaseOpeningError("Couldn't create directory '" + path + "'", errno);
	}
	if (!dir_exists(path)) {
	    throw DatabaseOpeningError("Replica path must be a directory");
	}
	string stub_path = path;
	stub_path += "/XAPIANDB";
	try {
	    live_db = WritableDatabase(stub_path,
				       Xapian::DB_OPEN|Xapian::DB_BACKEND_STUB);
	} catch (const Xapian::DatabaseCorruptError &) {
	    // If the database is too corrupt to open, force a full copy so we
	    // auto-heal from this condition.  Instance seen in the wild was
	    // that the replica had all files truncated to size 0.
	    live_db.internal.push_back(NULL);
	}
	// FIXME: simplify all this?
	ifstream stub(stub_path.c_str());
	string line;
	while (getline(stub, line)) {
	    if (!line.empty() && line[0] != '#') {
		live_id = line[line.size() - 1] - '0';
		break;
	    }
	}
    }
#endif
}

string
DatabaseReplica::Internal::get_revision_info() const
{
    LOGCALL(REPLICA, string, "DatabaseReplica::Internal::get_revision_info", NO_ARGS);
    if (live_db.internal.empty())
	live_db = WritableDatabase(get_replica_path(live_id), Xapian::DB_OPEN);
    if (live_db.internal.size() != 1)
	throw Xapian::InvalidOperationError("DatabaseReplica needs to be pointed at exactly one subdatabase");

    if (live_db.internal[0].get() == NULL) RETURN(string());

    string uuid = (live_db.internal[0])->get_uuid();
    string buf = encode_length(uuid.size());
    buf += uuid;
    buf += (live_db.internal[0])->get_revision_info();
    RETURN(buf);
}

void
DatabaseReplica::Internal::remove_offline_db()
{
    // Delete the offline database.
    removedir(get_replica_path(live_id ^ 1));
    have_offline_db = false;
}

void
DatabaseReplica::Internal::apply_db_copy(double end_time)
{
    have_offline_db = true;
    last_live_changeset_time = 0;
    string offline_path = get_replica_path(live_id ^ 1);
    // If there's already an offline database, discard it.  This happens if one
    // copy of the database was sent, but further updates were needed before it
    // could be made live, and the remote end was then unable to send those
    // updates (probably due to not having changesets available, or the remote
    // database being replaced by a new database).
    removedir(offline_path);
    if (mkdir(offline_path.c_str(), 0777)) {
	throw Xapian::DatabaseError("Cannot make directory '" +
				    offline_path + "'", errno);
    }

    {
	string buf;
	int type = conn->get_message(buf, end_time);
	check_message_type(type, REPL_REPLY_DB_HEADER);
	const char * ptr = buf.data();
	const char * end = ptr + buf.size();
	size_t uuid_length;
	decode_length_and_check(&ptr, end, uuid_length);
	offline_uuid.assign(ptr, uuid_length);
	offline_revision.assign(buf, ptr + uuid_length - buf.data(), buf.npos);
    }

    // Now, read the files for the database from the connection and create it.
    while (true) {
	string filename;
	int type = conn->sniff_next_message_type(end_time);
	if (type < 0 || type == REPL_REPLY_FAIL)
	    return;
	if (type == REPL_REPLY_DB_FOOTER)
	    break;

	type = conn->get_message(filename, end_time);
	check_message_type(type, REPL_REPLY_DB_FILENAME);

	// Check that the filename doesn't contain '..'.  No valid database
	// file contains .., so we don't need to check that the .. is a path.
	if (filename.find("..") != string::npos) {
	    throw NetworkError("Filename in database contains '..'");
	}

	type = conn->sniff_next_message_type(end_time);
	if (type < 0 || type == REPL_REPLY_FAIL)
	    return;

	string filepath = offline_path + "/" + filename;
	type = conn->receive_file(filepath, end_time);
	if (type < 0)
	    throw_connection_closed_unexpectedly();
	check_message_type(type, REPL_REPLY_DB_FILEDATA);
    }
    int type = conn->get_message(offline_needed_revision, end_time);
    check_message_type(type, REPL_REPLY_DB_FOOTER);
    need_copy_next = false;
}

void
DatabaseReplica::Internal::check_message_type(int type, int expected) const
{
    if (type != expected) {
	if (type < 0)
	    throw_connection_closed_unexpectedly();
	string m = "Expected replication protocol message type #";
	m += str(expected);
	m += ", got #";
	m += str(type);
	throw NetworkError(m);
    }
}

bool
DatabaseReplica::Internal::possibly_make_offline_live()
{
    string replica_path(get_replica_path(live_id ^ 1));
    AutoPtr<DatabaseReplicator> replicator;
    try {
	replicator.reset(DatabaseReplicator::open(replica_path));
    } catch (const Xapian::DatabaseError &) {
	return false;
    }
    if (offline_needed_revision.empty()) {
	return false;
    }
    if (!replicator->check_revision_at_least(offline_revision,
					     offline_needed_revision)) {
	return false;
    }

    string replicated_uuid = replicator->get_uuid();
    if (replicated_uuid.empty()) {
	return false;
    }

    if (replicated_uuid != offline_uuid) {
	return false;
    }

    live_id ^= 1;
    // Open the database first, so that if there's a problem, an exception
    // will be thrown before we make the new database live.
    live_db = WritableDatabase(replica_path, Xapian::DB_OPEN);
    update_stub_database();
    remove_offline_db();
    return true;
}

void
DatabaseReplica::Internal::set_read_fd(int fd)
{
    delete conn;
    conn = NULL;
    conn = new RemoteConnection(fd, -1);
}

bool
DatabaseReplica::Internal::apply_next_changeset(ReplicationInfo * info,
						double reader_close_time)
{
    LOGCALL(REPLICA, bool, "DatabaseReplica::Internal::apply_next_changeset", info | reader_close_time);
    while (true) {
	int type = conn->sniff_next_message_type(0.0);
	switch (type) {
	    case REPL_REPLY_END_OF_CHANGES: {
		string buf;
		type = conn->get_message(buf, 0.0);
		check_message_type(type, REPL_REPLY_END_OF_CHANGES);
		RETURN(false);
	    }
	    case REPL_REPLY_DB_HEADER:
		// Apply the copy - remove offline db in case of any error.
		try {
		    apply_db_copy(0.0);
		    if (info != NULL)
			++(info->fullcopy_count);
		    string replica_uuid;
		    {
			AutoPtr<DatabaseReplicator> replicator(
				DatabaseReplicator::open(get_replica_path(live_id ^ 1)));
			replica_uuid = replicator->get_uuid();
		    }
		    if (replica_uuid != offline_uuid) {
			remove_offline_db();
			// We've been sent an database with the wrong uuid,
			// which only happens if the database at the server
			// got changed during the copy, so the only safe
			// action next is a new copy.  Set a flag to ensure
			// that this happens, or we're at risk of database
			// corruption.
			need_copy_next = true;
		    }
		} catch (...) {
		    remove_offline_db();
		    throw;
		}
		if (possibly_make_offline_live()) {
		    if (info != NULL)
			info->changed = true;
		}
		break;
	    case REPL_REPLY_CHANGESET:
		if (need_copy_next) {
		    throw NetworkError("Needed a database copy next");
		}
		if (!have_offline_db) {
		    // Close the live db.
		    string replica_path(get_replica_path(live_id));
		    live_db = WritableDatabase();

		    if (last_live_changeset_time != 0.0) {
			// Wait until at least "reader_close_time" seconds have
			// passed since the last changeset was applied, to
			// allow any active readers to finish and be reopened.
			double until;
			until = last_live_changeset_time + reader_close_time;
			RealTime::sleep(until);
		    }

		    // Open a replicator for the live path, and apply the
		    // changeset.
		    {
			AutoPtr<DatabaseReplicator> replicator(
				DatabaseReplicator::open(replica_path));

			// Ignore the returned revision number, since we are
			// live so the changeset must be safe to apply to a
			// live DB.
			replicator->apply_changeset_from_conn(*conn, 0.0, true);
		    }
		    last_live_changeset_time = RealTime::now();

		    if (info != NULL) {
			++(info->changeset_count);
			info->changed = true;
		    }
		    // Now the replicator is closed, open the live db again.
		    live_db = WritableDatabase(replica_path, Xapian::DB_OPEN);
		    RETURN(true);
		}

		{
		    AutoPtr<DatabaseReplicator> replicator(
			    DatabaseReplicator::open(get_replica_path(live_id ^ 1)));

		    offline_revision = replicator->
			    apply_changeset_from_conn(*conn, 0.0, false);

		    if (info != NULL) {
			++(info->changeset_count);
		    }
		}
		if (possibly_make_offline_live()) {
		    if (info != NULL)
			info->changed = true;
		}
		RETURN(true);
	    case REPL_REPLY_FAIL: {
		string buf;
		if (conn->get_message(buf, 0.0) < 0)
		    throw_connection_closed_unexpectedly();
		throw NetworkError("Unable to fully synchronise: " + buf);
	    }
	    case -1:
		throw_connection_closed_unexpectedly();
	    default:
		throw NetworkError("Unknown replication protocol message (" +
				   str(type) + ")");
	}
    }
}
