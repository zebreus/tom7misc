
#include "database.h"

#include <string>
#include <memory>

#include "sqlite3.h"
#include "base/logging.h"

using Row = Database::Row;
using Query = Database::Query;

namespace {

static const char *ErrString(int status) {
  switch (status) {
  case SQLITE_OK: return "SQLITE_OK: Successful result";
  case SQLITE_ERROR: return "SQLITE_ERROR: Generic error";
  case SQLITE_INTERNAL: return "SQLITE_INTERNAL: Internal logic error in SQLite";
  case SQLITE_PERM: return "SQLITE_PERM: Access permission denied";
  case SQLITE_ABORT: return "SQLITE_ABORT: Callback routine requested an abort";
  case SQLITE_BUSY: return "SQLITE_BUSY: The database file is locked";
  case SQLITE_LOCKED: return "SQLITE_LOCKED: A table in the database is locked";
  case SQLITE_NOMEM: return "SQLITE_NOMEM: A malloc() failed";
  case SQLITE_READONLY: return "SQLITE_READONLY: Attempt to write a readonly database";
  case SQLITE_INTERRUPT: return "SQLITE_INTERRUPT: Operation terminated by sqlite3_interrupt()";
  case SQLITE_IOERR: return "SQLITE_IOERR:  /* Some kind of disk I/O error occurred";
  case SQLITE_CORRUPT: return "SQLITE_CORRUPT:  /* The database disk image is malformed";
  case SQLITE_NOTFOUND: return "SQLITE_NOTFOUND:  /* Unknown opcode in sqlite3_file_control()";
  case SQLITE_FULL: return "SQLITE_FULL:  /* Insertion failed because database is full";
  case SQLITE_CANTOPEN: return "SQLITE_CANTOPEN:  /* Unable to open the database file";
  case SQLITE_PROTOCOL: return "SQLITE_PROTOCOL:  /* Database lock protocol error";
  case SQLITE_EMPTY: return "SQLITE_EMPTY:  /* Internal use only";
  case SQLITE_SCHEMA: return "SQLITE_SCHEMA:  /* The database schema changed";
  case SQLITE_TOOBIG: return "SQLITE_TOOBIG:  /* String or BLOB exceeds size limit";
  case SQLITE_CONSTRAINT: return "SQLITE_CONSTRAINT:  /* Abort due to constraint violation";
  case SQLITE_MISMATCH: return "SQLITE_MISMATCH:  /* Data type mismatch";
  case SQLITE_MISUSE: return "SQLITE_MISUSE:  /* Library used incorrectly";
  case SQLITE_NOLFS: return "SQLITE_NOLFS:  /* Uses OS features not supported on host";
  case SQLITE_AUTH: return "SQLITE_AUTH:  /* Authorization denied";
  case SQLITE_FORMAT: return "SQLITE_FORMAT:  /* Not used";
  case SQLITE_RANGE: return "SQLITE_RANGE:  /* 2nd parameter to sqlite3_bind out of range";
  case SQLITE_NOTADB: return "SQLITE_NOTADB:  /* File opened that is not a database file";
  case SQLITE_NOTICE: return "SQLITE_NOTICE:  /* Notifications from sqlite3_log()";
  case SQLITE_WARNING: return "SQLITE_WARNING:  /* Warnings from sqlite3_log()";
  case SQLITE_ROW: return "SQLITE_ROW:  /* sqlite3_step() has another row ready";
  case SQLITE_DONE: return "SQLITE_DONE:  /* sqlite3_step() has finished executing";

  case SQLITE_ERROR_MISSING_COLLSEQ: return "SQLITE_ERROR_MISSING_COLLSEQ";
  case SQLITE_ERROR_RETRY: return "SQLITE_ERROR_RETRY";
  case SQLITE_ERROR_SNAPSHOT: return "SQLITE_ERROR_SNAPSHOT";
  case SQLITE_IOERR_READ: return "SQLITE_IOERR_READ";
  case SQLITE_IOERR_SHORT_READ: return "SQLITE_IOERR_SHORT_READ";
  case SQLITE_IOERR_WRITE: return "SQLITE_IOERR_WRITE";
  case SQLITE_IOERR_FSYNC: return "SQLITE_IOERR_FSYNC";
  case SQLITE_IOERR_DIR_FSYNC: return "SQLITE_IOERR_DIR_FSYNC";
  case SQLITE_IOERR_TRUNCATE: return "SQLITE_IOERR_TRUNCATE";
  case SQLITE_IOERR_FSTAT: return "SQLITE_IOERR_FSTAT";
  case SQLITE_IOERR_UNLOCK: return "SQLITE_IOERR_UNLOCK";
  case SQLITE_IOERR_RDLOCK: return "SQLITE_IOERR_RDLOCK";
  case SQLITE_IOERR_DELETE: return "SQLITE_IOERR_DELETE";
  case SQLITE_IOERR_BLOCKED: return "SQLITE_IOERR_BLOCKED";
  case SQLITE_IOERR_NOMEM: return "SQLITE_IOERR_NOMEM";
  case SQLITE_IOERR_ACCESS: return "SQLITE_IOERR_ACCESS";
  case SQLITE_IOERR_CHECKRESERVEDLOCK: return "SQLITE_IOERR_CHECKRESERVEDLOCK";
  case SQLITE_IOERR_LOCK: return "SQLITE_IOERR_LOCK";
  case SQLITE_IOERR_CLOSE: return "SQLITE_IOERR_CLOSE";
  case SQLITE_IOERR_DIR_CLOSE: return "SQLITE_IOERR_DIR_CLOSE";
  case SQLITE_IOERR_SHMOPEN: return "SQLITE_IOERR_SHMOPEN";
  case SQLITE_IOERR_SHMSIZE: return "SQLITE_IOERR_SHMSIZE";
  case SQLITE_IOERR_SHMLOCK: return "SQLITE_IOERR_SHMLOCK";
  case SQLITE_IOERR_SHMMAP: return "SQLITE_IOERR_SHMMAP";
  case SQLITE_IOERR_SEEK: return "SQLITE_IOERR_SEEK";
  case SQLITE_IOERR_DELETE_NOENT: return "SQLITE_IOERR_DELETE_NOENT";
  case SQLITE_IOERR_MMAP: return "SQLITE_IOERR_MMAP";
  case SQLITE_IOERR_GETTEMPPATH: return "SQLITE_IOERR_GETTEMPPATH";
  case SQLITE_IOERR_CONVPATH: return "SQLITE_IOERR_CONVPATH";
  case SQLITE_IOERR_VNODE: return "SQLITE_IOERR_VNODE";
  case SQLITE_IOERR_AUTH: return "SQLITE_IOERR_AUTH";
  case SQLITE_IOERR_BEGIN_ATOMIC: return "SQLITE_IOERR_BEGIN_ATOMIC";
  case SQLITE_IOERR_COMMIT_ATOMIC: return "SQLITE_IOERR_COMMIT_ATOMIC";
  case SQLITE_IOERR_ROLLBACK_ATOMIC: return "SQLITE_IOERR_ROLLBACK_ATOMIC";
  case SQLITE_IOERR_DATA: return "SQLITE_IOERR_DATA";
  case SQLITE_IOERR_CORRUPTFS: return "SQLITE_IOERR_CORRUPTFS";
  case SQLITE_IOERR_IN_PAGE: return "SQLITE_IOERR_IN_PAGE";
  case SQLITE_LOCKED_SHAREDCACHE: return "SQLITE_LOCKED_SHAREDCACHE";
  case SQLITE_LOCKED_VTAB: return "SQLITE_LOCKED_VTAB";
  case SQLITE_BUSY_RECOVERY: return "SQLITE_BUSY_RECOVERY";
  case SQLITE_BUSY_SNAPSHOT: return "SQLITE_BUSY_SNAPSHOT";
  case SQLITE_BUSY_TIMEOUT: return "SQLITE_BUSY_TIMEOUT";
  case SQLITE_CANTOPEN_NOTEMPDIR: return "SQLITE_CANTOPEN_NOTEMPDIR";
  case SQLITE_CANTOPEN_ISDIR: return "SQLITE_CANTOPEN_ISDIR";
  case SQLITE_CANTOPEN_FULLPATH: return "SQLITE_CANTOPEN_FULLPATH";
  case SQLITE_CANTOPEN_CONVPATH: return "SQLITE_CANTOPEN_CONVPATH";
  case SQLITE_CANTOPEN_DIRTYWAL: return "SQLITE_CANTOPEN_DIRTYWAL";
  case SQLITE_CANTOPEN_SYMLINK: return "SQLITE_CANTOPEN_SYMLINK";
  case SQLITE_CORRUPT_VTAB: return "SQLITE_CORRUPT_VTAB";
  case SQLITE_CORRUPT_SEQUENCE: return "SQLITE_CORRUPT_SEQUENCE";
  case SQLITE_CORRUPT_INDEX: return "SQLITE_CORRUPT_INDEX";
  case SQLITE_READONLY_RECOVERY: return "SQLITE_READONLY_RECOVERY";
  case SQLITE_READONLY_CANTLOCK: return "SQLITE_READONLY_CANTLOCK";
  case SQLITE_READONLY_ROLLBACK: return "SQLITE_READONLY_ROLLBACK";
  case SQLITE_READONLY_DBMOVED: return "SQLITE_READONLY_DBMOVED";
  case SQLITE_READONLY_CANTINIT: return "SQLITE_READONLY_CANTINIT";
  case SQLITE_READONLY_DIRECTORY: return "SQLITE_READONLY_DIRECTORY";
  case SQLITE_ABORT_ROLLBACK: return "SQLITE_ABORT_ROLLBACK";
  case SQLITE_CONSTRAINT_CHECK: return "SQLITE_CONSTRAINT_CHECK";
  case SQLITE_CONSTRAINT_COMMITHOOK: return "SQLITE_CONSTRAINT_COMMITHOOK";
  case SQLITE_CONSTRAINT_FOREIGNKEY: return "SQLITE_CONSTRAINT_FOREIGNKEY";
  case SQLITE_CONSTRAINT_FUNCTION: return "SQLITE_CONSTRAINT_FUNCTION";
  case SQLITE_CONSTRAINT_NOTNULL: return "SQLITE_CONSTRAINT_NOTNULL";
  case SQLITE_CONSTRAINT_PRIMARYKEY: return "SQLITE_CONSTRAINT_PRIMARYKEY";
  case SQLITE_CONSTRAINT_TRIGGER: return "SQLITE_CONSTRAINT_TRIGGER";
  case SQLITE_CONSTRAINT_UNIQUE: return "SQLITE_CONSTRAINT_UNIQUE";
  case SQLITE_CONSTRAINT_VTAB: return "SQLITE_CONSTRAINT_VTAB";
  case SQLITE_CONSTRAINT_ROWID: return "SQLITE_CONSTRAINT_ROWID";
  case SQLITE_CONSTRAINT_PINNED: return "SQLITE_CONSTRAINT_PINNED";
  case SQLITE_CONSTRAINT_DATATYPE: return "SQLITE_CONSTRAINT_DATATYPE";
  case SQLITE_NOTICE_RECOVER_WAL: return "SQLITE_NOTICE_RECOVER_WAL";
  case SQLITE_NOTICE_RECOVER_ROLLBACK: return "SQLITE_NOTICE_RECOVER_ROLLBACK";
  case SQLITE_NOTICE_RBU: return "SQLITE_NOTICE_RBU";
  case SQLITE_WARNING_AUTOINDEX: return "SQLITE_WARNING_AUTOINDEX";
  case SQLITE_AUTH_USER: return "SQLITE_AUTH_USER";
  case SQLITE_OK_LOAD_PERMANENTLY: return "SQLITE_OK_LOAD_PERMANENTLY";
  case SQLITE_OK_SYMLINK: return "SQLITE_OK_SYMLINK";
  default:
    return "??? Unknown error ???";
  }
}

struct QueryImpl;
struct RowImpl : public Database::Row {
  RowImpl(QueryImpl *parent);
  ~RowImpl() override;

  int Width() const override {
    return width;
  }

  int64_t GetInt(int idx) override {
    LOG(FATAL) << "Unimplemented";
  }
  std::string GetString(int idx) override {
    LOG(FATAL) << "Unimplemented";
  }
  std::vector<uint8_t> GetBlob(int idx) override {
    LOG(FATAL) << "Unimplemented";
  }

  int width = 0;
  QueryImpl *parent = nullptr;
};

struct QueryImpl : public Database::Query {
  QueryImpl(sqlite3_stmt *stmt) : stmt(stmt), ready(true) {}

  std::unique_ptr<Row> NextRow() override {
    CHECK(ready) << "The previous row must be destroyed before "
      "calling NextRow again.";

    // For empty statements
    if (stmt == nullptr)
      return std::unique_ptr<Row>(nullptr);

    for (;;) {
      int src = sqlite3_step(stmt);
      if (src == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return std::unique_ptr<Row>(nullptr);
      }

      if (src == SQLITE_BUSY)
        continue;

      if (src == SQLITE_ROW) {
        ready = false;
        return std::unique_ptr<Row>(new RowImpl(this));
      }

      CHECK(src != SQLITE_MISUSE);
      CHECK(src != SQLITE_ERROR);
      LOG(FATAL) << "sqlite3 step failed: " << ErrString(src);
    }
  }

  // Null for a query with an empty result set, or once we've
  // reached the end of the result set.
  sqlite3_stmt *stmt = nullptr;
  bool ready = false;
};

RowImpl::RowImpl(QueryImpl *parent) : parent(parent) {
  width = sqlite3_column_count(parent->stmt);
}

RowImpl::~RowImpl() {
  CHECK(!parent->ready) << "Bug";
  parent->ready = true;
}


struct DatabaseImpl : public Database {

  std::unique_ptr<Query> ExecuteString(const std::string &q) override {
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v3(db, q.data(), (int)q.size(),
                                // flags
                                0,
                                &stmt,
                                nullptr);
    CHECK(rc == SQLITE_OK) << "Could not prepare statement (parse error?): " << q;
    return std::unique_ptr<Query>(new QueryImpl(stmt));
  }

  sqlite3 *db = nullptr;
};

}  // namespace

Database::Row::~Row() {}
Database::Query::~Query() {}
Database::~Database() {}

std::unique_ptr<Database> Database::Open(const std::string &filename) {
  std::unique_ptr<DatabaseImpl> ret(new DatabaseImpl);

  int rc = sqlite3_open(filename.c_str(), &ret->db);
  CHECK(rc == SQLITE_OK) << "Couldn't open database from " << filename << "\n"
                         << ErrString(rc);
  return ret;
}
