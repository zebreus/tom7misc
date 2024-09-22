
#ifndef _DATABASE_H
#define _DATABASE_H

#include <string>
#include <memory>

// Simplified SQL-like database.
struct Database {
  virtual ~Database();

  static std::unique_ptr<Database> Open(const std::string &filename);

  struct Row {
    virtual ~Row();

    // The number of columns.
    virtual int Width() const = 0;

    // These accessors may abort if the type is not correct.
    virtual int64_t GetInt(int idx) = 0;
    virtual std::string GetString(int idx) = 0;
    virtual std::vector<uint8_t> GetBlob(int idx) = 0;
   protected:
    Row();
  };

  struct Query {
    virtual ~Query();

    // This will return nullptr when the end of the dataset is
    // reached. Lifetime:
    //  The row must outlive the query.
    //  The row must be destroyed before advancing to the next row.
    virtual std::unique_ptr<Row> NextRow() = 0;
   protected:
    Query();
  };

  virtual std::unique_ptr<Query> ExecuteString(const std::string &q) = 0;

 protected:
  Database();
};

#endif
