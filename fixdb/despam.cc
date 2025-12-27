
// Note: msql++ doesn't work with clang. Perhaps better to
// just write your own wrapper.

#include <mystring.h>
#include <mysql++.h>
#include <options.h>
#include <dbdriver.h>
#include <mysql/mysql.h>


#include <string>
#include <cstdint>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "fix-encoding.h"
#include "periodically.h"
#include "status-bar.h"
#include "atomic-util.h"
#include "utf8.h"
#include "util.h"

DECLARE_COUNTERS(invalid_utf8);

static bool dry_run = false;
static int verbose = 0;
static int danger = 0;

struct Col {
  const char *server = nullptr;
  std::string user = "root";
  std::string pass;

  std::string db;
  std::string table;
  std::string column;
};

static std::string Escape(std::string_view s) {
  std::string str(s);
  mysqlpp::DBDriver::escape_string_no_conn(&str, nullptr, 0);
  return str;
}

static void DeSpam(const Col &col,
                   std::string_view spam) {
  CHECK(!spam.empty()) << "Empty spam would delete everything!";
  if (!dry_run && !danger && spam.size() < 5) {
    LOG(FATAL) << "Use the -danger flag to delete short spam.";
  }

  StatusBar status(1);
  status.Status("Connect");

  // false = No exceptions.
  mysqlpp::Connection conn(false);
  CHECK(conn.connect(col.db.c_str(), col.server,
                     col.user.c_str(), col.pass.c_str()))
    << conn.error();

  status.Status("Select");

  std::string qs = std::format("SELECT id, {} FROM {} where {} like '%{}%'",
                               col.column, col.table,
                               col.column, Escape(spam));
  if (verbose) {
    status.Print("{}\n", qs);
  }
  mysqlpp::Query query = conn.query(qs);
  mysqlpp::StoreQueryResult res = query.store();

  CHECK(query.errnum() == 0) << query.error() << std::endl;

  Periodically status_per(1);
  const int64_t num_rows = res.num_rows();

  int64_t deleted = 0, failed = 0;

  mysqlpp::Query update_q = conn.query();

  for (size_t i = 0; i < num_rows; i++) {
    status_per.RunIf([&]() {
        status.Progress(i, num_rows,
                        "Despam. {} results {} deleted {} failed",
                        invalid_utf8.Read(),
                        deleted, failed);
      });
    mysqlpp::Row row = res[i];

    uint64_t id = row["id"];
    std::string content = row[col.column.c_str()].data();

    if (Util::StrContains(content, spam)) {
      if (verbose > 0) {
        status.Print("[" ACYAN("{}") "] "
                     "{} {}\n",
                     id,
                     dry_run ? AYELLOW("Would delete") : ARED("Deleted"),
                     content);
      }

      deleted++;

      if (!dry_run) {
        update_q.reset();
        update_q << "DELETE FROM " << col.table
                 << " WHERE id = " << id;

        if (!update_q.execute()) {
          failed++;
          status.Print("Delete failed for ID {}: {}\n", id, update_q.error());
        }
      }
    }
  }

  Print("\n\nDone.\n"
        "Invalid UTF-8: {}.\n"
        "deleted: {},\n"
        "Failed on {}.\n",
        invalid_utf8.Read(),
        deleted, failed);

  if (dry_run) {
    Print("\nThis was a " AYELLOW("dry run") " so we didn't "
          "actually change anything!\n");
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (!arg.empty() && arg[0] == '-') {
      if (arg == "-dry-run") {
        dry_run = true;
      } else if (arg == "-v") {
        verbose++;
      } else if (arg == "-danger") {
        danger++;
      } else {
        LOG(FATAL) << "Only -dry-run and -v args.";
      }
    } else {
      args.push_back(std::string(arg));
    }
  }

  CHECK(args.size() == 3) <<
    "./fixdb.exe [-v] [-dry-run] pass db.table.column spam\n";

  std::vector<std::string> parts = Util::Split(args[1], '.');
  CHECK(parts.size() == 3) << "Need db.table.column";

  Col col{
    .pass = args[0],

    .db = parts[0],
    .table = parts[1],
    .column = parts[2],
  };

  DeSpam(col, args[2]);

  return 0;
}
