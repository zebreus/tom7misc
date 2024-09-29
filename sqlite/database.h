
#ifndef _DATABASE_H
#define _DATABASE_H

#include <string>
#include <memory>
#include <vector>
#include <optional>
#include <cstdint>

// Simplified SQL-like database.
struct Database {
  virtual ~Database();

  static std::unique_ptr<Database> Open(const std::string &filename);


  enum class ColType {
    INT,
    FLOAT,
    BLOB,
    STRING,
    SQL_NULL,
  };

  struct Row {
    virtual ~Row();

    // The number of columns.
    virtual int Width() const = 0;
    virtual const std::vector<ColType> &Types() const = 0;

    // These accessors may abort if the type is not correct.
    virtual int64_t GetInt(int idx) = 0;
    virtual std::string GetString(int idx) = 0;
    virtual double GetFloat(int idx) = 0;
    virtual std::vector<uint8_t> GetBlob(int idx) = 0;
    // For uniformity, but this function either aborts or does
    // nothing.
    virtual void GetNull(int idx) = 0;

    // These return nullopt if the value does not have the
    // specified type (for example because it is SQL_NULL).
    virtual std::optional<int64_t> GetIntOpt(int idx) = 0;
    virtual std::optional<std::string> GetStringOpt(int idx) = 0;
    virtual std::optional<double> GetFloatOpt(int idx) = 0;
    virtual std::optional<std::vector<uint8_t>> GetBlobOpt(int idx) = 0;

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
  // Usually for debugging. Unspecified output format.
  virtual void ExecuteAndPrint(const std::string &q) = 0;

 protected:
  Database();
};

#endif
