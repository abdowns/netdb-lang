#pragma once
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "reflect.hpp"

namespace nql {

class Arena {
public:
  StrRef intern(std::string_view s) {
    chunks_.emplace_back(s);
    return {chunks_.back().data(), chunks_.back().size()};
  }

private:
  std::deque<std::string> chunks_; // deque keeps element addresses stable
};

class RecordSet {
public:
  explicit RecordSet(const Schema& schema) : schema_(&schema) {}

  const Schema& schema() const { return *schema_; }
  size_t count() const { return count_; }
  const uint8_t* data() const { return data_.data(); }
  const uint8_t* at(size_t i) const { return data_.data() + i * schema_->size; }
  size_t bytes() const { return data_.size(); }

  uint8_t* append() {
    data_.resize(data_.size() + schema_->size, 0);
    return data_.data() + (count_++) * schema_->size;
  }

  void reserve(size_t n) { data_.reserve(n * schema_->size); }

  Arena& arena() { return arena_; }

private:
  const Schema* schema_;
  std::vector<uint8_t> data_;
  size_t count_ = 0;
  Arena arena_;
};

void parseFieldValue(uint8_t* rec, const FieldInfo& f, std::string_view text, Arena& arena,
                     const std::string& context);

RecordSet loadCsv(const Schema& schema, const std::string& path);

RecordSet synthesize(const Schema& schema, size_t n, uint64_t seed);

} // namespace nql
