#pragma once
#include <deque>
#include <string>
#include <vector>

#include "reflect.hpp"

namespace nql {

class RecordSet {
public:
  explicit RecordSet(const Schema& schema) : schema_(&schema) {}

  const Schema& schema() const { return *schema_; }
  size_t count() const { return count_; }
  const uint8_t* data() const { return data_.data(); }
  const uint8_t* at(size_t i) const { return data_.data() + i * schema_->size; }

  uint8_t* append() {
    data_.resize(data_.size() + schema_->size, 0);
    return data_.data() + (count_++) * schema_->size;
  }

  StrRef intern(const std::string& s) {
    strings_.push_back(s);
    return {strings_.back().data(), strings_.back().size()};
  }

private:
  const Schema* schema_;
  std::vector<uint8_t> data_;
  size_t count_ = 0;
  std::deque<std::string> strings_;
};

RecordSet synthesize(const Schema& schema, size_t n, uint64_t seed);

} // namespace nql
