
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

static constexpr bool DRY_RUN = true;

static std::string FixBlob(std::string bytes) {
  if (!UTF8::IsValid(bytes)) {
    invalid_utf8++;
    // Very common codepage. What we choose here doesn't matter
    // that much because ftfy can detect incorrect decoding. It
    // just needs the input to be valid UTF-8.
    bytes = FixEncoding::Windows1252().DecodeSloppy(bytes);
    CHECK(UTF8::IsValid(bytes));
  }

  bytes = FixEncoding::Fix(bytes, 0);

  return bytes;
}

// Note: mysqlpp does not
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

  mysqlpp::Query query =
    conn.query(std::format("SELECT id, {} FROM {}", column, table));
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
      status.Print("[" ACYAN("{}") "] Fixed " ARED("{}") " to " AGREEN("{}") "\n",
                   id,
                   original_blob, fixed_blob);

      updated++;

      if (!DRY_RUN) {
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

  Print("\n\nDone.\n"
        "Invalid UTF-8: {}.\n"
        "Updated: {},\n"
        "Failed on {}.\n",
        invalid_utf8.Read(),
        updated, failed);
}

int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc == 4 || argc == 5) << "./fixdb.exe db table column [pass]\n";

  FixIt(argv[1], argv[2], argv[3], argc > 4 ? argv[4] : "");

  return 0;
}
