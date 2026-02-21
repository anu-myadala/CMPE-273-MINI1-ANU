#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace nyc311 {

// ---------------------------------------------------------------------------
// CSVParser<RecordType>
//
// Template CSV reader.  RecordType must provide a static method:
//
//   static bool fromFields(const std::vector<std::string>& fields,
//                          RecordType& out);
//
// which maps a parsed row of fields into the record and returns false when
// the row should be skipped (e.g., malformed data).
//
// Usage:
//   CSVParser<Record311> parser("data.csv");
//   parser.open();
//   Record311 rec;
//   while (parser.readNext(rec)) { ... }
//   parser.close();
// ---------------------------------------------------------------------------
template<typename RecordType>
class CSVParser {
public:
    explicit CSVParser(const std::string& filepath)
        : filepath_(filepath), lines_read_(0) {}

    bool open() {
        file_.open(filepath_);
        if (!file_.is_open()) return false;

        std::string header_line;
        if (!std::getline(file_, header_line)) return false;
        headers_ = splitRow(header_line);
        return true;
    }

    void close() {
        if (file_.is_open()) file_.close();
    }

    bool readNext(RecordType& record) {
        std::string line;
        while (std::getline(file_, line)) {
            ++lines_read_;
            if (line.empty()) continue;
            auto fields = splitRow(line);
            if (RecordType::fromFields(fields, record)) return true;
        }
        return false;
    }

    const std::vector<std::string>& headers() const { return headers_; }
    size_t linesRead() const { return lines_read_; }

    // Convenience: split a single CSV row respecting double-quoted fields
    static std::vector<std::string> splitRow(const std::string& line) {
        std::vector<std::string> fields;
        std::string field;
        field.reserve(64);
        bool in_quotes = false;

        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"') {
                // Two consecutive quotes inside a quoted field → literal "
                if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    in_quotes = !in_quotes;
                }
            } else if (c == ',' && !in_quotes) {
                fields.push_back(std::move(field));
                field.clear();
            } else {
                field += c;
            }
        }
        fields.push_back(std::move(field));
        return fields;
    }

private:
    std::string              filepath_;
    std::ifstream            file_;
    size_t                   lines_read_;
    std::vector<std::string> headers_;
};

}  // namespace nyc311
