// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef COBALT_ALGORITHMS_RAPPOR_RAPPOR_TEST_UTILS_H_
#define COBALT_ALGORITHMS_RAPPOR_RAPPOR_TEST_UTILS_H_

#include <string>
#include <vector>

#include "./observation.pb.h"
#include "config/report_configs.pb.h"

namespace cobalt {
namespace rappor {

// Returns whether or not the bit with the given |bit_index| is set in
// |data|. The bits are indexed "from right-to-left", i.e. from least
// significant to most significant. The least significant bit has index 0.
bool IsSet(const std::string& data, int bit_index);

// Given a string of "0"s and "1"s of length a multiple of 8, returns
// the bytes whose binary representation is given by the string.
std::string BinaryStringToData(const std::string& binary_string);

// Returns a string of "0"s and "1"s that gives the binary representation of the
// bytes in |data|.
std::string DataToBinaryString(const std::string& data);

// Builds a binary string of length |num_bits| in which there are '1''s in
// exactly the indices specified by |index_of_1s|. Note that we index bits
// "from the right" so that an index of zero refers to the least significant
// bit, i.e. the last character of the returned string.
std::string BuildBinaryString(size_t num_bits,
                              const std::vector<uint16_t>& index_of_1s);

// Builds the string "category<index>" using 4 digits from index.
std::string CategoryName(uint32_t index);

// Returns a string of characters of length |num_bits| with |index_char| in
// position |index| and |other_char| in all other positions.
// REQUIRES: 0 <= index < num_bits.
std::string BuildBitPatternString(int num_bits, int index, char index_char,
                                  char other_char);

std::string CandidateString(int i);

// Populates |candidate_list| with |num_candidates| candidates;
void PopulateRapporCandidateList(uint32_t num_candidates,
                                 RapporCandidateList* candidate_list);

// Makes a RapporConfig with the given data.
RapporConfig Config(uint32_t num_bloom_bits, uint32_t num_cohorts,
                    uint32_t num_hashes, float p, float q);

// Given a string of "0"s and "1"s of length a multiple of 8, and a cohort,
// returns a RapporObservation for the given cohort whose data is equal to the
// bytes whose binary representation is given by the string.
RapporObservation RapporObservationFromString(uint32_t cohort,
                                              const std::string& binary_string);

}  // namespace rappor
}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_RAPPOR_RAPPOR_TEST_UTILS_H_
