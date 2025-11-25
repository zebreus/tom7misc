
// Note: msql++ doesn't work with clang. Perhaps better to
// just write your own wrapper.

// Don't sort! Explicit include works around bustage with mysql+clang.
#include <mystring.h>
#include <mysql++.h>
#include <options.h>
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

DECLARE_COUNTERS(invalid_utf8);

static bool dry_run = false;
static int verbose = 0;

static std::string FixBlob(std::string bytes) {
  if (!UTF8::IsValid(bytes)) {
    invalid_utf8++;
    // Very common codepage. What we choose here doesn't matter
    // that much because ftfy can detect incorrect decoding. It
    // just needs the input to be valid UTF-8.
    std::string new_bytes = FixEncoding::Windows1252().DecodeSloppy(bytes);
    CHECK(UTF8::IsValid(new_bytes));

    if (verbose > 0) {
      Print("Fixed UTF-8:\n" ARED("{}") "\n" ACYAN("to:") "\n"
            AGREEN("{}") "\n", bytes, new_bytes);
    }

    bytes = std::move(new_bytes);
  }

  // Don't change stylistic stuff like ligatures and quotes.
  bytes = FixEncoding::Fix(bytes, FixEncoding::NEWLINES);

  return bytes;
}

// Note: mysqlpp does not work well with string_view.
static void FixIt(std::string db,
                  std::string table,
                  std::string column,
                  std::string pass) {
  // or nullptr for default
  const char *server = nullptr;
  std::string user = "root";

  StatusBar status(1);
  status.Status("Connect");

  // Print("Using database '{}' as '{}' with password '{}'\n", db, user, pass);

  // false = No exceptions.
  mysqlpp::Connection conn(false);
  // Act like mysql command-line client
  // conn.set_option(new mysqlpp::ReadDefaultOption("client"));
  CHECK(conn.connect(db.c_str(), server, user.c_str(), pass.c_str()))
    << conn.error();

  status.Status("Select");

  std::string qs = std::format("SELECT id, {} FROM {}", column, table);
  if (verbose) {
    status.Print("{}\n", qs);
  }
  mysqlpp::Query query = conn.query(qs);
  mysqlpp::StoreQueryResult res = query.store();

  CHECK(query.errnum() == 0) << query.error() << std::endl;

  Periodically status_per(1);
  const int64_t num_rows = res.num_rows();

  int64_t updated = 0, failed = 0;

  mysqlpp::Query update_q = conn.query();

  for (size_t i = 0; i < num_rows; i++) {
    status_per.RunIf([&]() {
        status.Progress(i, num_rows, "Fixing. {} bad utf8 {} updated {} failed",
                        invalid_utf8.Read(),
                        updated, failed);
      });
    mysqlpp::Row row = res[i];

    uint64_t id = row["id"];
    std::string original_blob = row[column.c_str()].data();

    std::string fixed_blob = FixBlob(original_blob);

    if (fixed_blob != original_blob) {
      if (verbose > 1) {
        status.Print("[" ACYAN("{}") "] "
                     "Fixed " ARED("{}") " to " AGREEN("{}") "\n",
                     id,
                     original_blob, fixed_blob);
      }

      updated++;

      if (!dry_run) {
        update_q.reset();
        update_q << "UPDATE " << table << " SET " << column << " = "
                 << mysqlpp::quote << fixed_blob
                 << " WHERE id = " << id;

        if (!update_q.execute()) {
          failed++;
          status.Print("Update failed for ID {}: {}\n", id, update_q.error());
        }
      }
    }
  }

  // Now updating column type.
  if (!dry_run) {
    update_q.reset();
    std::string uqs = std::format("alter table {}.{} modify column {} TEXT "
                                  "character set utf8mb4 "
                                  "collate utf8mb4_unicode_ci not null",
                                  db, table, column);
    if (verbose > 0) {
      status.Print("{}\n", uqs);
    }
    update_q << uqs;
    CHECK(update_q.execute()) << "Couldn't fix column type?";
  }

  Print("\n\nDone.\n"
        "Invalid UTF-8: {}.\n"
        "Updated: {},\n"
        "Failed on {}.\n",
        invalid_utf8.Read(),
        updated, failed);

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
      } else {
        LOG(FATAL) << "Only -dry-run and -v args.";
      }
    } else {
      args.push_back(std::string(arg));
    }
  }

  // Empty password by default.
  if (args.size() == 3)
    args.push_back("");

  CHECK(args.size() == 4) <<
    "./fixdb.exe [-v] [-dry-run] db table column [pass]\n";

  FixIt(args[0], args[1], args[2], args[3]);

  return 0;
}
