
#include "database.h"

#include <cstdio>
#include <vector>
#include <memory>
#include <string>

#include "base/logging.h"
#include "util.h"

using Query = Database::Query;
using Row = Database::Row;
using ColType = Database::ColType;

static void Test() {
  Util::RemoveFile("database-test.sqlite");
  std::unique_ptr<Database> conn = Database::Open("database-test.sqlite");

  {
    auto q = conn->ExecuteString("create table test ("
                                 "id int not null, "
                                 "name varchar(255) not null)");
    CHECK(q->NextRow().get() == nullptr);
  }

  {
    auto q = conn->ExecuteString("insert into test (id, name) "
                                 "values (7, \"Tom\")");
    CHECK(q->NextRow().get() == nullptr);
  }

  {
    auto q = conn->ExecuteString("insert into test (id, name) "
                                 "values (24, \"Jack Bauer\")");
    CHECK(q->NextRow().get() == nullptr);
  }

  {
    auto q = conn->ExecuteString("select id, name from test "
                                 "order by id");

    {
      auto r1 = q->NextRow();
      CHECK(r1.get() != nullptr);
      const std::vector<ColType> &t1 = r1->Types();
      CHECK(t1.size() == 2);
      CHECK(t1[0] == ColType::INT);
      CHECK(t1[1] == ColType::STRING);
      CHECK(r1->GetInt(0) == 7);
      CHECK(r1->GetString(1) == "Tom");
    }

    {
      auto r2 = q->NextRow();
      CHECK(r2.get() != nullptr);
      const std::vector<ColType> &t2 = r2->Types();
      CHECK(t2.size() == 2);
      CHECK(t2[0] == ColType::INT);
      CHECK(t2[1] == ColType::STRING);
      CHECK(r2->GetInt(0) == 24);
      CHECK(r2->GetString(1) == "Jack Bauer");
    }

    CHECK(q->NextRow().get() == nullptr);
  }

  {
    conn->ExecuteString("delete from test where id = 24")->Exhaust();

    auto q = conn->ExecuteString("select name, id from test");

    {
      auto r1 = q->NextRow();
      CHECK(r1.get() != nullptr);
      const std::vector<ColType> &t1 = r1->Types();
      CHECK(t1.size() == 2);
      CHECK(t1[0] == ColType::STRING);
      CHECK(t1[1] == ColType::INT);
      CHECK(r1->GetString(0) == "Tom");
      CHECK(r1->GetInt(1) == 7);
    }

    CHECK(q->NextRow().get() == nullptr);
  }

}

int main(int argc, char **argv) {
  Test();

  printf("OK\n");
  return 0;
}
