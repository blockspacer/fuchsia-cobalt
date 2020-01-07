// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_ALGORITHMS_EXPERIMENTAL_COUNT_MIN_H_
#define COBALT_SRC_ALGORITHMS_EXPERIMENTAL_COUNT_MIN_H_

#include <cstdint>
#include <string>
#include <vector>

namespace cobalt {

// Implements Count-Min Sketch.
//
// The count-min sketch represents a distribution of hashable observations as |num_hashes| fields
// of size |num_cells|.
//
// This implementation flattens the representation into a single vector of integers.
// The field corresponding the kth hash function is from k*num_cells to k*(num_cells+1).
// e.g. If the kth hash of an observation is n, this corresponds to GetSketch()[k *num_cells + n]
class CountMin {
 public:
  CountMin(size_t num_cells, size_t num_hashes);

  CountMin(size_t num_cells, size_t num_hashes, std::vector<uint32_t> cells);

  // Increment the number of observations of |data| which has length |len| by |num|.
  void Increment(const char* data, size_t len, int num);

  // Increment the number of observations of |data| which has length |len| by 1.
  void Increment(const char* data, size_t len);

  // Increment the number of observations of |data| by |num|.
  void Increment(const std::string& data, int num);

  // Increment the number of observations of |data| by 1.
  void Increment(const std::string& data);

  // Get the estimated count for the specified |data| which has length |len|.
  [[nodiscard]] uint32_t GetCount(const char* data, size_t len) const;

  // Get the estimated count for the specified |data|.
  [[nodiscard]] uint32_t GetCount(const std::string& data) const;

  // Return a pointer to the cells of the sketch.
  [[nodiscard]] const std::vector<uint32_t>* GetSketch() const;

 private:
  // Returns the index in the sketch cells of the |data| of length |len| for hash function |index|.
  size_t GetSketchCell(const char* data, size_t len, uint64_t index) const;

  size_t num_cells_;
  size_t num_hashes_;
  std::vector<uint32_t> cells_;
};

}  // namespace cobalt
#endif  // COBALT_SRC_ALGORITHMS_EXPERIMENTAL_COUNT_MIN_H_
