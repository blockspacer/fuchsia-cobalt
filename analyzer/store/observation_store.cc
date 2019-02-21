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

#include "analyzer/store/observation_store.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./observation.pb.h"
#include "analyzer/store/data_store.h"
#include "analyzer/store/observation_store_internal.h"
#include "glog/logging.h"
#include "util/crypto_util/hash.h"
#include "util/crypto_util/random.h"

namespace cobalt {
namespace analyzer {
namespace store {

using crypto::byte;
using crypto::hash::DIGEST_SIZE;
using crypto::hash::Hash;
using internal::DayIndexFromRowKey;
using internal::GenerateNewRowKey;
using internal::ParseEncryptedObservationPart;
using internal::ParseEncryptedSystemProfile;
using internal::RangeLimitKey;
using internal::RangeStartKey;

namespace {
// The name of the column in which we store the serialized SystemProfile
// of each Observation. This column cannot be confused with a metric part
// column because metric parts names are not allowed to begin with an
// underscore.
static const char kSystemProfileColumnName[] = "_CobaltSystemProfile";
}  // namespace

// The internal namespace contains private implementation functions that need
// to be accessible to unit tests. The functions are declared in
// observation_store_internal.h.
namespace internal {

// Returns the row key that encapsulates the given data. In the current version
// of Cobalt we are using human-readable row keys for the sake of debugability.
// In the future we should replace this with a more compact representation.
// The row key is 75 bytes long and is an ASCII string of the form:
// <customer>:<project>:<metric>:<day>:<random>:<hash> where
// customer: The customer id as a positive 10 digit decimal string
// project: The project id as a positive 10 digit decimal string
// metric: The metric id as a positive 10 digit decimal string
// random: A random 64-bit number as a positive 20 digit decimal string
// hash: A 32-bit hash of the Observation as a positive 10 digit decimal string.
//
// The first four components of the row key
//     <customer>:<project>:<metric>:<day>
// represent the metric-day. This unit is important because it is the unit on
// which a report runs. The QueryObservations() method operates on whole
// metric days.
//
// The remainder of the row key
//  <random>:<hash>
// serves to form, along with the metric-day, a unique identifier for the
// Observation. The random 64-bit number is generated on the client. This allows
// the add-observation operation to be idempotent: If the client sends us
// the identical Observation twice we will only store it once. The hash is
// computed on the server. It reduces the probability of collision while
// maintaining idempotency. That is, using the <hash> maintains the property
// that if the client sends us the identical Observation twice we will only
// store it once, and it reduces the probability that we will receive what is
// supposed to be two different Observations but store only one and discard
// the other.
//
// In an earlier version of this code the last two components of the row key
// were different:
// <time>:<random> where
// time: Arrival time in millis as a positive 20 digit decimal string
// random: A random 32-bit number as a positive 10 digit decimal string.
// Both the arrival time and the random were generated on the server. This older
// scheme sufficed for obtaining unique identifiers but did not give us the
// idempotency of the add-observation operation.
//
// Our Observation store will contain a mixture of both the old and the new
// row keys. The new row key format was chosen in a way to make this harmless.
// Because the last two components of the row key are never interpreted (in fact
// they are never even parsed) they are only used for the purpose of having
// unique row keys, there is no harm in replacing the old format with the new
// one.
std::string RowKey(uint32_t customer_id, uint32_t project_id,
                   uint32_t metric_id, uint32_t day_index, uint64_t random,
                   uint32_t hash) {
  // We write five ten-digit numbers, plus one twenty-digit number plus five
  // colons. The string has size 76 to accommodate a trailing null character.
  std::string out(76, 0);

  // TODO(rudominer): Replace human-readable row key with smaller more efficient
  // representation.
  std::snprintf(&out[0], out.size(), "%.10u:%.10u:%.10u:%.10u:%.20lu:%.10u",
                customer_id, project_id, metric_id, day_index, random, hash);

  // Discard the trailing null character.
  out.resize(75);

  return out;
}

// Returns a 32-bit hash of (|observation|, |metadata|) appropriate for use as
// the <hash> component of a row key. See comments on RowKey() above.
uint32_t HashObservation(const Observation& observation,
                         const ObservationMetadata metadata) {
  std::string serialized_observation;
  observation.SerializeToString(&serialized_observation);
  std::string serialized_metadta;
  metadata.SerializeToString(&serialized_metadta);
  serialized_observation += serialized_metadta;
  byte hash_bytes[DIGEST_SIZE];
  Hash(reinterpret_cast<const byte*>(serialized_observation.data()),
       serialized_observation.size(), hash_bytes);
  uint32_t return_value = 0;
  std::memcpy(&return_value, hash_bytes,
              std::min(DIGEST_SIZE, sizeof(return_value)));
  return return_value;
}

// Returns the common prefix of all rows keys for the given metric.
std::string RowKeyPrefix(uint32_t customer_id, uint32_t project_id,
                         uint32_t metric_id) {
  // TODO(rudominer) This length corresponds to our current, temporary,
  // human-readable row-keys built in RowKey() above. This function needs
  // to change when the implementation changes. The prefix we return
  // includes three ten-digit numbers plus three colons.
  static const size_t kPrefixLength = 33;
  std::string row_key = RowKey(customer_id, project_id, metric_id, 0, 0, 0);
  row_key.resize(kPrefixLength);
  return row_key;
}

// Returns the day_index encoded by |row_key|.
uint32_t DayIndexFromRowKey(const std::string& row_key) {
  uint32_t day_index = 0;
  // Parse the string produced by the RowKey() function above. We skip three
  // ten-digit integers and three colons and then parse 10 digits.
  CHECK_GT(row_key.size(), 33u);
  std::sscanf(&row_key[33], "%10u", &day_index);
  return day_index;
}

// Returns the lexicographically least row key for rows with the given
// data.
std::string RangeStartKey(uint32_t customer_id, uint32_t project_id,
                          uint32_t metric_id, uint32_t day_index) {
  return RowKey(customer_id, project_id, metric_id, day_index, 0, 0);
}

// Returns the lexicographically least row key that is greater than all row
// keys for rows with the given metadata, if day_index < UINT32_MAX. In the case
// that |day_index| = UINT32_MAX, returns the lexicographically least row key
// that is greater than all row keys for rows with the given values of
// the other parameters.
std::string RangeLimitKey(uint32_t customer_id, uint32_t project_id,
                          uint32_t metric_id, uint32_t day_index) {
  if (day_index < UINT32_MAX) {
    return RowKey(customer_id, project_id, metric_id, day_index + 1, 0, 0);
  } else {
    // UINT32_MAX is already greater than all valid values of day_index.
    return RowKey(customer_id, project_id, metric_id, UINT32_MAX, 0, 0);
  }
}

// Generates a new row key for a row for the given Observation.
std::string GenerateNewRowKey(const ObservationMetadata& metadata,
                              const Observation& observation) {
  uint64_t random;
  if (observation.random_id().size() > 0 &&
      observation.random_id().size() != sizeof(random)) {
    LOG(WARNING) << "Unexpected size for random_id field: "
                 << observation.random_id().size();
  }
  if (observation.random_id().size() >= sizeof(random)) {
    std::memcpy(&random, observation.random_id().data(), sizeof(random));
    VLOG(5) << "ObservationStore: received random_id from client: " << random;
  } else {
    // If the client did not send us a random we will generate one.
    cobalt::crypto::Random rand;
    random = rand.RandomUint64();
    VLOG(5) << "ObservationStore: No random_id from client.";
  }
  return RowKey(metadata.customer_id(), metadata.project_id(),
                metadata.metric_id(), metadata.day_index(), random,
                HashObservation(observation, metadata));
}

bool ParseEncryptedObservationPart(ObservationPart* observation_part,
                                   std::string bytes) {
  // TODO(rudominer) Arrange for ObservationParts to be encrypted.
  return observation_part->ParseFromString(bytes);
}

bool ParseEncryptedSystemProfile(SystemProfile* system_profile,
                                 std::string bytes) {
  // TODO(rudominer) Arrange for SystemProfiles to be encrypted.
  return system_profile->ParseFromString(bytes);
}

// Copies SystemProfile fields from |src| to |dst| based on the list of
// SystemProfileFields.
void WriteFilteredSystemProfile(SystemProfile* dst,
                                std::unique_ptr<SystemProfile> src,
                                const SystemProfileFields& fields) {
  if (src != nullptr && dst != nullptr) {
    for (int field : fields) {
      switch (field) {
        case SystemProfileField::OS:
          dst->set_os(src->os());
          break;
        case SystemProfileField::ARCH:
          dst->set_arch(src->arch());
          break;
        case SystemProfileField::BOARD_NAME:
          dst->set_allocated_board_name(src->release_board_name());
          break;
        case SystemProfileField::PRODUCT_NAME:
          dst->set_allocated_product_name(src->release_product_name());
          break;
        case SystemProfileField::SYSTEM_VERSION:
          dst->set_allocated_system_version(src->release_system_version());
      }
    }
  }
}

}  // namespace internal

ObservationStore::ObservationStore(std::shared_ptr<DataStore> store)
    : store_(store) {}

Status ObservationStore::AddObservation(const ObservationMetadata& metadata,
                                        const Observation& observation) {
  std::vector<Observation> observations;
  observations.emplace_back(observation);
  return AddObservationBatch(metadata, observations);
}

Status ObservationStore::AddObservationBatch(
    const ObservationMetadata& metadata,
    const std::vector<Observation>& observations) {
  std::string serialized_system_profile;
  if (metadata.has_system_profile()) {
    metadata.system_profile().SerializeToString(&serialized_system_profile);
  }
  std::vector<DataStore::Row> rows;
  for (const Observation& observation : observations) {
    DataStore::Row row;
    row.key = GenerateNewRowKey(metadata, observation);
    for (const auto& pair : observation.parts()) {
      std::string serialized_observation_part;
      pair.second.SerializeToString(&serialized_observation_part);
      // TODO(rudominer) Consider ways to avoid having so many copies of the
      // part names.
      row.column_values[pair.first] = std::move(serialized_observation_part);
    }
    if (!serialized_system_profile.empty()) {
      row.column_values[kSystemProfileColumnName] = serialized_system_profile;
    }

    rows.emplace_back(std::move(row));
  }

  return store_->WriteRows(DataStore::kObservations, std::move(rows));
}

ObservationStore::QueryResponse ObservationStore::QueryObservations(
    uint32_t customer_id, uint32_t project_id, uint32_t metric_id,
    uint32_t start_day_index, uint32_t end_day_index,
    std::vector<std::string> parts,
    const SystemProfileFields& system_profile_fields, size_t max_results,
    std::string pagination_token) {
  ObservationStore::QueryResponse query_response;
  std::string start_row;
  bool inclusive = true;
  std::string range_start_key =
      RangeStartKey(customer_id, project_id, metric_id, start_day_index);
  if (!pagination_token.empty()) {
    // The pagination token should be the row key of the last row returned the
    // previous time this method was invoked.
    if (pagination_token < range_start_key) {
      query_response.status = kInvalidArguments;
      return query_response;
    }
    start_row.swap(pagination_token);
    inclusive = false;
  } else {
    start_row.swap(range_start_key);
  }

  std::string limit_row =
      RangeLimitKey(customer_id, project_id, metric_id, end_day_index);

  if (limit_row <= start_row) {
    query_response.status = kInvalidArguments;
    return query_response;
  }

  if (!parts.empty() && system_profile_fields.size() > 0) {
    // If parts is empty this will indicate to the underlying DataStore that
    // we wish to retrieve all columns and so we don't want to append the
    // column name for SystemProfile because this would change the meaning
    // of the query to indicate that we want to retrieve that column
    // *only*, which is not what we want.
    parts.emplace_back(kSystemProfileColumnName);
  }

  DataStore::ReadResponse read_response = store_->ReadRows(
      DataStore::kObservations, std::move(start_row), inclusive,
      std::move(limit_row), std::move(parts), max_results);

  query_response.status = read_response.status;
  if (query_response.status != kOK) {
    return query_response;
  }

  for (const DataStore::Row& row : read_response.rows) {
    // For each row of the read_response we add a query_result to the
    // query_response.
    query_response.results.emplace_back();
    auto& query_result = query_response.results.back();
    query_result.metadata.set_customer_id(customer_id);
    query_result.metadata.set_project_id(project_id);
    query_result.metadata.set_metric_id(metric_id);
    query_result.metadata.set_day_index(DayIndexFromRowKey(row.key));

    for (auto& pair : row.column_values) {
      const std::string& column_name = pair.first;
      const std::string& column_value = pair.second;
      if (column_name == kSystemProfileColumnName) {
        if (system_profile_fields.size() > 0) {
          auto profile = std::make_unique<SystemProfile>();
          if (!ParseEncryptedSystemProfile(profile.get(), column_value)) {
            query_response.status = kOperationFailed;
            return query_response;
          }
          internal::WriteFilteredSystemProfile(
              query_result.metadata.mutable_system_profile(),
              std::move(profile), system_profile_fields);
        }
      } else {
        // The column name is a metric part name so we add an ObservationPart
        // with this metric part name as the key to the map.
        // The insert_result is a pair of the form < <key, value>, bool> where
        // the bool indicates whether or not the key was newly added to the map.
        auto insert_result = query_result.observation.mutable_parts()->insert(
            google::protobuf::Map<std::string, ObservationPart>::value_type(
                column_name, ObservationPart()));
        // The column names should all be unique so each insert should return
        // true.
        DCHECK(insert_result.second);
        // The ObservationPart is the value and so the second element of the
        // first element of insert_result.
        auto& observation_part = insert_result.first->second;
        // We deserialize the ObservationPart from the column value.
        if (!ParseEncryptedObservationPart(&observation_part, column_value)) {
          query_response.status = kOperationFailed;
          return query_response;
        }
      }
    }
  }
  if (read_response.more_available) {
    // If the underlying store says that there are more rows available, then
    // we return the row key of the last row as the pagination_token.
    if (read_response.rows.empty()) {
      // There Read operation indicated that there were more rows available yet
      // it did not return even one row. In this pathological case we return
      // an error.
      query_response.status = kOperationFailed;
      return query_response;
    }
    size_t last_index = read_response.rows.size() - 1;
    query_response.pagination_token.swap(read_response.rows[last_index].key);
  }

  return query_response;
}

Status ObservationStore::DeleteAllForMetric(uint32_t customer_id,
                                            uint32_t project_id,
                                            uint32_t metric_id) {
  return store_->DeleteRowsWithPrefix(
      DataStore::kObservations,
      internal::RowKeyPrefix(customer_id, project_id, metric_id));
}

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
