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

#include "encoder/encoder.h"

#include <ctime>
#include <set>
#include <utility>

#include "./logging.h"
#include "algorithms/forculus/forculus_encrypter.h"
#include "algorithms/rappor/rappor_encoder.h"
#include "config/encodings.pb.h"
#include "config/metrics.pb.h"
#include "util/crypto_util/random.h"
#include "util/datetime_util.h"

namespace cobalt {
namespace encoder {

using forculus::ForculusEncrypter;
using rappor::BasicRapporEncoder;
using rappor::RapporEncoder;
using util::TimeToDayIndex;

namespace {
std::string DataCaseToString(ValuePart::DataCase data_case) {
  switch (data_case) {
    case ValuePart::kStringValue:
      return "STRING";
    case ValuePart::kIntValue:
      return "INT";
    case ValuePart::kBlobValue:
      return "BLOB";
    case ValuePart::kIndexValue:
      return "INDEX";
    case ValuePart::kDoubleValue:
      return "DOUBLE";
    case ValuePart::kIntBucketDistribution:
      return "INT_BUCKET_DISTRIBUTION";
    case ValuePart::DATA_NOT_SET:
      return "<DATA_NOT_SET>";
  }
  return "UNKNOWN_TYPE";
}

}  // namespace

Encoder::Encoder(const std::shared_ptr<ProjectContext>& project, ClientSecret client_secret,
                 const SystemDataInterface* system_data)
    : customer_id_(project->customer_id()),
      project_id_(project->project_id()),
      project_(project),
      client_secret_(std::move(client_secret)),
      system_data_(system_data) {}

Encoder::Status Encoder::EncodeForculus(uint32_t metric_id, uint32_t encoding_config_id,
                                        const ValuePart& value,
                                        const EncodingConfig* encoding_config,
                                        const std::string& part_name, uint32_t day_index,
                                        ObservationPart* observation_part) {
  switch (value.data_case()) {
    case ValuePart::kStringValue:
    case ValuePart::kBlobValue:
      break;
    default: {
      LOG(ERROR) << "Forculus doesn't support " << DataCaseToString(value.data_case()) << "s: ("
                 << customer_id_ << ", " << project_id_ << ", " << encoding_config_id << ")";
      return kInvalidArguments;
    }
  }
  ForculusObservation* forculus_observation = observation_part->mutable_forculus();
  ForculusEncrypter forculus_encrypter(encoding_config->forculus(), customer_id_, project_id_,
                                       metric_id, part_name, client_secret_);

  switch (forculus_encrypter.EncryptValue(value, day_index, forculus_observation)) {
    case ForculusEncrypter::kOK:
      return kOK;

    case ForculusEncrypter::kInvalidConfig:
      return kInvalidConfig;

    case ForculusEncrypter::kEncryptionFailed:
      LOG(ERROR) << "Forculs encryption failed for encoding (" << customer_id_ << ", "
                 << project_id_ << ", " << encoding_config_id << ")";
      return kEncodingFailed;
  }
}

Encoder::Status Encoder::EncodeRappor(uint32_t /*metric_id*/, uint32_t encoding_config_id,
                                      const ValuePart& value, const EncodingConfig* encoding_config,
                                      const std::string& /*part_name*/,
                                      ObservationPart* observation_part) {
  if (value.data_case() != ValuePart::kStringValue) {
    LOG(ERROR) << "RAPPOR doesn't support " << DataCaseToString(value.data_case()) << "s: ("
               << customer_id_ << ", " << project_id_ << ", " << encoding_config_id << ")";
    return kInvalidArguments;
  }
  RapporObservation* rappor_observation = observation_part->mutable_rappor();
  RapporEncoder rappor_encoder(encoding_config->rappor(), client_secret_);
  switch (rappor_encoder.Encode(value, rappor_observation)) {
    case rappor::kOK:
      return kOK;

    case rappor::kInvalidConfig:
      return kInvalidConfig;

    case rappor::kInvalidInput:
      LOG(ERROR) << "Invalid arguments to RapporEncoder for encoding (" << customer_id_ << ", "
                 << project_id_ << ", " << encoding_config_id << ")";
      return kInvalidArguments;
  }
}

Encoder::Status Encoder::EncodeBasicRappor(uint32_t /*metric_id*/, uint32_t encoding_config_id,
                                           const ValuePart& value,
                                           const EncodingConfig* encoding_config,
                                           const std::string& /*part_name*/,
                                           ObservationPart* observation_part) {
  switch (value.data_case()) {
    case ValuePart::kStringValue:
    case ValuePart::kIntValue:
    case ValuePart::kIndexValue:
      break;
    default: {
      LOG(ERROR) << "Basic RAPPOR doesn't support " << DataCaseToString(value.data_case()) << "s: ("
                 << customer_id_ << ", " << project_id_ << ", " << encoding_config_id << ")";
      return kInvalidArguments;
    }
  }
  BasicRapporObservation* basic_rappor_observation = observation_part->mutable_basic_rappor();
  BasicRapporEncoder basic_rappor_encoder(encoding_config->basic_rappor(), client_secret_);
  switch (basic_rappor_encoder.Encode(value, basic_rappor_observation)) {
    case rappor::kOK:
      return kOK;

    case rappor::kInvalidConfig:
      return kInvalidConfig;

    case rappor::kInvalidInput:
      LOG(ERROR) << "Invalid arguments to BasicRapporEncoder for encoding (" << customer_id_ << ", "
                 << project_id_ << ", " << encoding_config_id << ")";
      return kInvalidArguments;
  }
}

Encoder::Status Encoder::EncodeNoOp(uint32_t /*metric_id*/, const ValuePart& value,
                                    const std::string& /*part_name*/,
                                    ObservationPart* observation_part) {
  // TODO(rudominer) Notice we are copying the value here. If we pass
  // the parameter |value| by pointer instead of by const ref then we could
  // Swap() here instead.
  observation_part->mutable_unencoded()->set_allocated_unencoded_value(new ValuePart(value));
  return kOK;
}

// Check that the specified metric_part and value_part_data are compatible.
// If they are not, return false and emit an error message.
// Else, return true.
// The part_name and metric_id are used to construct error messages.
bool Encoder::CheckValidValuePart(uint32_t metric_id, const std::string& part_name,
                                  const MetricPart& metric_part,
                                  const Encoder::Value::ValuePartData& value_part_data) {
  // Check that the data_type of the ValuePart matches the data_type of the
  // MetricPart.
  MetricPart::DataType value_data_type;
  switch (value_part_data.value_part.data_case()) {
    case ValuePart::kStringValue:
      value_data_type = MetricPart::STRING;
      break;

    case ValuePart::kIntBucketDistribution:
    case ValuePart::kIntValue:
      value_data_type = MetricPart::INT;
      break;

    case ValuePart::kDoubleValue:
      value_data_type = MetricPart::DOUBLE;
      break;

    case ValuePart::kBlobValue:
      value_data_type = MetricPart::BLOB;
      break;

    case ValuePart::kIndexValue: {
      value_data_type = MetricPart::INDEX;
      break;
    }
    case ValuePart::DATA_NOT_SET: {
      LOG(ERROR) << "Metric part (" << customer_id_ << ", " << project_id_ << ", " << metric_id
                 << ")-" << part_name << " is not set.";
      return false;
    }
  }
  if (metric_part.data_type() != value_data_type) {
    LOG(ERROR) << "Metric part (" << customer_id_ << ", " << project_id_ << ", " << metric_id
               << ")-" << part_name << " is not of type "
               << DataCaseToString(value_part_data.value_part.data_case()) << ".";
    return false;
  }

  // Check that the int bucket distribution value is allowed and valid.
  if (value_part_data.value_part.data_case() == ValuePart::kIntBucketDistribution) {
    auto counts = value_part_data.value_part.int_bucket_distribution().counts();
    if (!CheckIntBucketDistribution(metric_id, part_name, metric_part, counts)) {
      return false;
    }
  }

  return true;
}

// Check that the int bucket distribution value is allowed and valid.
bool Encoder::CheckIntBucketDistribution(uint32_t metric_id, const std::string& part_name,
                                         const MetricPart& metric_part,
                                         const google::protobuf::Map<uint32_t, uint64_t>& counts) {
  // Check that if the ValuePart is an int_bucket_distribution, the MetricPart
  // has int_buckets set.
  if (!metric_part.has_int_buckets()) {
    LOG(ERROR) << "Metric part (" << customer_id_ << ", " << project_id_ << ", " << metric_id
               << ")-" << part_name << " does not have "
               << "int_buckets set.";
    return false;
  }

  // Find the number of buckets.
  uint32_t num_buckets;
  switch (metric_part.int_buckets().buckets_case()) {
    case IntegerBuckets::kExponential:
      num_buckets = metric_part.int_buckets().exponential().num_buckets();
      break;
    case IntegerBuckets::kLinear:
      num_buckets = metric_part.int_buckets().linear().num_buckets();
      break;
    case IntegerBuckets::BUCKETS_NOT_SET:
      LOG(ERROR) << "Buckets not set. This should never happen.";
      return false;
  }
  // In addition to the specified num_buckets, there are the underflow and
  // overflow buckets.
  num_buckets += 2;

  for (const auto& it : counts) {
    // Check that all the specified bucket indices are valid.
    if (it.first >= num_buckets) {
      LOG(ERROR) << "Invalid bucket index " << it.first << " for Metric (" << customer_id_ << ", "
                 << project_id_ << ", " << metric_id << ") - part " << part_name;
      return false;
    }
  }
  return true;
}

Encoder::Result Encoder::EncodeString(uint32_t metric_id, uint32_t encoding_config_id,
                                      const std::string& string_value) {
  Value value;
  // An empty part name is a signal to the function Encoder::Encode() that
  // the metric has only a single part whose name should be looked up.
  value.AddStringPart(encoding_config_id, "", string_value);
  return Encode(metric_id, value);
}

Encoder::Result Encoder::EncodeInt(uint32_t metric_id, uint32_t encoding_config_id,
                                   int64_t int_value) {
  Value value;
  // An empty part name is a signal to the function Encoder::Encode() that the
  // metric has only a single part.
  value.AddIntPart(encoding_config_id, "", int_value);
  return Encode(metric_id, value);
}

Encoder::Result Encoder::EncodeDouble(uint32_t metric_id, uint32_t encoding_config_id,
                                      double double_value) {
  Value value;
  // An empty part name is a signal to the function Encoder::Encode() that the
  // metric has only a single part.
  value.AddDoublePart(encoding_config_id, "", double_value);
  return Encode(metric_id, value);
}

Encoder::Result Encoder::EncodeIndex(uint32_t metric_id, uint32_t encoding_config_id,
                                     uint32_t index) {
  Value value;
  // An empty part name is a signal to the function Encoder::Encode() that the
  // metric has only a single part.
  value.AddIndexPart(encoding_config_id, "", index);
  return Encode(metric_id, value);
}

Encoder::Result Encoder::EncodeBlob(uint32_t metric_id, uint32_t encoding_config_id,
                                    const void* data, size_t num_bytes) {
  Value value;
  // An empty part name is a signal to the function Encoder::Encode() that the
  // metric has only a single part.
  value.AddBlobPart(encoding_config_id, "", data, num_bytes);
  return Encode(metric_id, value);
}

Encoder::Result Encoder::EncodeIntBucketDistribution(
    uint32_t metric_id, uint32_t encoding_config_id,
    const std::map<uint32_t, uint64_t>& distribution) {
  Value value;
  // An empty part name is a signal to the function Encoder::Encode() that the
  // metric has only a single part.
  value.AddIntBucketDistributionPart(encoding_config_id, "", distribution);
  return Encode(metric_id, value);
}

Encoder::Result Encoder::Encode(uint32_t metric_id, const Value& value) {
  Result result;

  // Get the Metric.
  const Metric* metric = project_->Metric(metric_id);
  if (!metric) {
    // No such metric.
    LOG(ERROR) << "No such metric: (" << customer_id_ << ", " << project_id_ << ", " << metric_id
               << ")";
    result.status = kInvalidArguments;
    return result;
  }

  // Check that the number of values provided equals the number of metric
  // parts.
  if (metric->parts().size() != value.parts_.size()) {
    LOG(ERROR) << "Metric (" << customer_id_ << ", " << project_id_ << ", " << metric_id
               << ") does not have " << value.parts_.size() << " part(s)";
    result.status = kInvalidArguments;
    return result;
  }

  // Compute the day_index.
  time_t current_time = current_time_;
  if (current_time <= 0) {
    // Use the real clock if we have not been given a static value for
    // current_time.
    current_time = std::time(nullptr);
  }
  uint32_t day_index = TimeToDayIndex(current_time, metric->time_zone_policy());
  if (day_index == UINT32_MAX) {
    // Invalid Metric: No time_zone_policy.
    LOG(ERROR) << "TimeZonePolicy unset for metric: (" << customer_id_ << ", " << project_id_
               << ", " << metric_id << ")";
    result.status = kInvalidConfig;
    return result;
  }

  // Create a new Observation and ObservationMetadata.
  result.observation = std::make_unique<Observation>();

  // Generate the random_id field. Currently we use 8 bytes but our
  // infrastructure allows us to change that in the future if we wish to. The
  // random_id is used by the Analyzer Service as part of a unique row key
  // for the observation in the Observation Store.
  static const size_t kNumRandomBytes = 8;
  result.observation->set_allocated_random_id(new std::string(kNumRandomBytes, 0));
  random_.RandomString(result.observation->mutable_random_id());

  result.metadata = std::make_unique<ObservationMetadata>();
  result.metadata->set_customer_id(customer_id_);
  result.metadata->set_project_id(project_id_);
  result.metadata->set_metric_id(metric_id);
  result.metadata->set_day_index(day_index);

  if (system_data_) {
    // If we were provided a SystemProfile, add the subset of fields specified
    // in system_profile_field.
    const auto& profile = system_data_->system_profile();
    for (const auto& field : metric->system_profile_field()) {
      switch (field) {
        case SystemProfileField::OS:
          result.metadata->mutable_system_profile()->set_os(profile.os());
          break;
        case SystemProfileField::ARCH:
          result.metadata->mutable_system_profile()->set_arch(profile.arch());
          break;
        case SystemProfileField::BOARD_NAME:
          result.metadata->mutable_system_profile()->set_board_name(profile.board_name());
          break;
        case SystemProfileField::PRODUCT_NAME:
          result.metadata->mutable_system_profile()->set_product_name(profile.product_name());
          break;
        case SystemProfileField::SYSTEM_VERSION:
          result.metadata->mutable_system_profile()->set_system_version(profile.system_version());
          break;
        case SystemProfileField::CHANNEL:
          result.metadata->mutable_system_profile()->set_channel(profile.channel());
          break;
      }
    }
  }

  // Iterate through the provided values.
  for (const auto& key_value : value.parts_) {
    std::string part_name = key_value.first;
    const Value::ValuePartData& value_part_data = key_value.second;

    // Find the metric part with the specified name.
    google::protobuf::Map<std::basic_string<char>, cobalt::MetricPart>::const_iterator
        metric_part_iterator;
    if (part_name.empty() && metric->parts().size() == 1) {
      // Special case: If there is only one metric part and the provided
      // part_name is the empty string then use that single metric part.
      metric_part_iterator = metric->parts().begin();
      part_name = metric_part_iterator->first;

    } else {
      metric_part_iterator = metric->parts().find(part_name);
      if (metric_part_iterator == metric->parts().cend()) {
        LOG(ERROR) << "Metric (" << customer_id_ << ", " << project_id_ << ", " << metric_id
                   << ") does not have a part named " << part_name << ".";
        result.status = kInvalidArguments;
        return result;
      }
    }
    const MetricPart& metric_part = metric_part_iterator->second;

    // Check that the data type of the ValuePart is valid for the specified
    // MetricPart.
    if (!CheckValidValuePart(metric_id, part_name, metric_part, value_part_data)) {
      result.status = kInvalidArguments;
      return result;
    }

    // Get the EncodingConfig
    const EncodingConfig* encoding_config =
        project_->EncodingConfig(value_part_data.encoding_config_id);
    if (!encoding_config) {
      // No such encoding config.
      LOG(ERROR) << "No such encoding config: (" << customer_id_ << ", " << project_id_ << ", "
                 << value_part_data.encoding_config_id << ")";
      result.status = kInvalidArguments;
      return result;
    }

    // Add an ObservationPart to the Observation with the part_name.
    ObservationPart& observation_part = (*result.observation->mutable_parts())[part_name];
    observation_part.set_encoding_config_id(value_part_data.encoding_config_id);

    // Perform the encoding.
    Status status = kOK;
    switch (encoding_config->config_case()) {
      case EncodingConfig::kForculus: {
        status = EncodeForculus(metric_id, value_part_data.encoding_config_id,
                                value_part_data.value_part, encoding_config, part_name, day_index,
                                &observation_part);
        break;
      }

      case EncodingConfig::kRappor: {
        status =
            EncodeRappor(metric_id, value_part_data.encoding_config_id, value_part_data.value_part,
                         encoding_config, part_name, &observation_part);
        break;
      }

      case EncodingConfig::kBasicRappor: {
        status = EncodeBasicRappor(metric_id, value_part_data.encoding_config_id,
                                   value_part_data.value_part, encoding_config, part_name,
                                   &observation_part);
        break;
      }

      case EncodingConfig::kNoOpEncoding: {
        status = EncodeNoOp(metric_id, value_part_data.value_part, part_name, &observation_part);
        break;
      }

      default:
        status = kInvalidConfig;
        break;
    }
    if (status != kOK) {
      result.status = status;
      return result;
    }
  }
  result.status = kOK;
  return result;
}

uint32_t Encoder::MetricId(const std::string& metric_name) {
  const Metric* metric = project_->Metric(metric_name);
  if (metric) {
    return metric->id();
  }
  return 0;
}

const std::unordered_map<std::string, uint32_t>& Encoder::DefaultEncodingsForMetric(
    uint32_t metric_id) {
  return project_->DefaultEncodingsForMetric(metric_id);
}

const Metric* Encoder::GetMetric(uint32_t metric_id) { return project_->Metric(metric_id); }

ValuePart& Encoder::Value::AddPart(uint32_t encoding_config_id, const std::string& part_name) {
  // emplace() returns a pair whose first element is an iterator over
  // pairs whose second element is a ValuePartData.
  return parts_.emplace(part_name, ValuePartData(encoding_config_id)).first->second.value_part;
}

void Encoder::Value::AddStringPart(uint32_t encoding_config_id, const std::string& part_name,
                                   const std::string& value) {
  AddPart(encoding_config_id, part_name).set_string_value(value);
}

void Encoder::Value::AddIntPart(uint32_t encoding_config_id, const std::string& part_name,
                                int64_t value) {
  AddPart(encoding_config_id, part_name).set_int_value(value);
}

void Encoder::Value::AddDoublePart(uint32_t encoding_config_id, const std::string& part_name,
                                   double value) {
  AddPart(encoding_config_id, part_name).set_double_value(value);
}

void Encoder::Value::AddIndexPart(uint32_t encoding_config_id, const std::string& part_name,
                                  uint32_t index) {
  AddPart(encoding_config_id, part_name).set_index_value(index);
}

void Encoder::Value::AddBlobPart(uint32_t encoding_config_id, const std::string& part_name,
                                 const void* data, size_t num_bytes) {
  AddPart(encoding_config_id, part_name).set_blob_value(data, num_bytes);
}

void Encoder::Value::AddIntBucketDistributionPart(
    uint32_t encoding_config_id, const std::string& part_name,
    const std::map<uint32_t, uint64_t>& distribution) {
  auto distribution_out = AddPart(encoding_config_id, part_name).mutable_int_bucket_distribution();
  for (auto it : distribution) {
    (*distribution_out->mutable_counts())[it.first] = it.second;
  }
}

}  // namespace encoder
}  // namespace cobalt
