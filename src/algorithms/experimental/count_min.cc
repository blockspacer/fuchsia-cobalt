// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/algorithms/experimental/count_min.h"

#include <iostream>

#include "src/algorithms/experimental/hash.h"
#include "src/logging.h"

namespace cobalt {

CountMin::CountMin(size_t num_cells, size_t num_hashes)
    : num_cells_(num_cells), num_hashes_(num_hashes), cells_(num_cells * num_hashes, 0u) {}

CountMin::CountMin(size_t num_cells, size_t num_hashes, std::vector<uint32_t> cells)
    : num_cells_(num_cells), num_hashes_(num_hashes), cells_(std::move(cells)) {
  CHECK_EQ(cells_.size(), num_cells_ * num_hashes_);
}

void CountMin::Increment(const char* data, size_t len, int num) {
  for (size_t i = 0; i < num_hashes_; ++i) {
    cells_[GetSketchCell(data, len, i)] += num;
  }
}

void CountMin::Increment(const char* data, size_t len) { Increment(data, len, 1); }

void CountMin::Increment(const std::string& data, int num) {
  Increment(data.data(), data.size(), num);
}

void CountMin::Increment(const std::string& data) { Increment(data.data(), data.size(), 1); }

uint32_t CountMin::GetCount(const std::string& data) const {
  return GetCount(data.data(), data.size());
}

uint32_t CountMin::GetCount(const char* data, size_t len) const {
  uint32_t min_count = 0;
  for (size_t i = 0; i < num_hashes_; ++i) {
    uint32_t count = cells_[GetSketchCell(data, len, i)];
    if (count < min_count || i == 0) {
      min_count = count;
    }
  }
  return min_count;
}

const std::vector<uint32_t>* CountMin::GetSketch() const { return &cells_; }

size_t CountMin::GetSketchCell(const char* data, size_t len, uint64_t index) const {
  return index * num_cells_ +
         TruncatedDigest(reinterpret_cast<const uint8_t*>(data), len, index, num_hashes_);
}

}  // namespace cobalt
