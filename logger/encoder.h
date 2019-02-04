// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_ENCODER_H_
#define COBALT_LOGGER_ENCODER_H_

#include <memory>
#include <string>
#include <vector>

#include "./event.pb.h"
#include "./observation2.pb.h"
#include "algorithms/rappor/rappor_encoder.h"
#include "config/metric_definition.pb.h"
#include "config/report_definition.pb.h"
#include "encoder/client_secret.h"
#include "encoder/system_data.h"
#include "logger/project_context.h"
#include "logger/status.h"
#include "util/crypto_util/random.h"

namespace cobalt {
namespace logger {

// A HistogramPtr provides a moveable way of passing the buckets of a Histogram.
typedef std::unique_ptr<google::protobuf::RepeatedPtrField<HistogramBucket>>
    HistogramPtr;

// A EventValuesPtr provides a moveable way of passing the dimensions of a
// custom event.
typedef std::unique_ptr<
    google::protobuf::Map<std::string, CustomDimensionValue>>
    EventValuesPtr;

// An Encoder is used for creating Observations, including applying any
// privacy-preserving encodings that may be employed. An Observation
// is the unit of encoded data that is sent from a client device to the Shuffler
// and ultimately to the Analyzer.
//
// Observations are derived from Events. Events are the raw data directly
// logged by a Cobalt user on the client system.
//
// There are two broad categories of Observations: immediate Observations and
// locally-aggregated observations. An immediate Observation is generated
// directly from a single Event at the time the Event is logged. A
// locally-aggregated Observation is computed based on the data of many
// logged Events over a period of time.
//
// An Observation is associated with a Metric and this means that the
// Observation is derived from one or more Events belonging to that Metric.
//
// An Observation is always generated for a particular Report. The Report
// definition indicates whether the Observation should be an immediate or
// locally aggregated Observation and how the Observation should be encoded.
//
// An Observation is always tagged with a day_index indicating the day on
// which the Observation was encoded. For immediate Observations this will
// be the same as the day the corresponding Event was logged. For
// locally-aggregated Observations this will be the day the aggregation was
// completed.
//
// An Observation is always associated with an instance of ObservationMetadata
// that contains the metric_id, report_id and day_index, among other data.
//
// There will usually be a singleton instance of Encoder on a client device.
// The Encoder interface is not exposed directly to Cobalt users. Instead it
// is used by the Logger implementation in order to encode immediate
// Observations and it is used by the Local Aggregator to encode
// locally-aggregated Observations.
//
// All of the Encode*() methods take the same first three parameters:
// (1) |metric| A MetricRef that provides the names and IDs of the customer,
//     project and metric associated with the Observation being encoded.
//     Note that the methods of this class do not see the MetricDefinition
//     itself and have no knowledge of the different Metric types or their
//     meanings. In particular no validation against the MetricDefintion or type
//     is performed by this class. If any such validation is needed it must be
//     performed by the caller prior to invoking the Encode*() methods of this
//     class.
// (2) |report| A pointer to the definition of the Report associated with the
//     Observation being encoded. The ReportDefinition may carry fields
//     particular to the encoding to be performed. The following
//     ReportDefinition fields are always required to be populated: |name|,
//     |id|, |system_profile_field|. Additionaly, each Encode*() method may
//     require other fields of ReportDefinition to be populated. This will be
//     specified in the comments for each Encode*() method.
// (3) |day_index| The day associated with the Observation being encoded.
//
// Historical note: This Encoder class is in the |logger| package and was
// created as part of Cobalt 1.0. There is also an older class named "Encoder"
// in the |encoder| package that was created as part of Cobalt 0.1. In Cobalt
// 0.1 there were only immediate Observations, there was no Logger class, and
// the older Encoder class played the role of both the newer Logger and the
// newer Encoder. During the transition from Cobalt 0.1 to Cobalt 1.0
// code in the logger package may reference code in the older encoder package.
class Encoder {
 public:
  // Constructor
  //
  // client_secret: A random secret that is generated once on the client
  //     and then persisted by the client and used repeatedly. It is used as
  //     an input by some of the encodings.
  //
  // system_data: Used to obtain the SystemProfile, a filtered copy of which
  //     will be included in the generated ObservationMetadata. The Encoder does
  //     not take ownership of system_data and system_data is allowed to be
  //     NULL, in which case no SystemProfile will be added to the
  //     ObservationMetadata.
  Encoder(encoder::ClientSecret client_secret,
          const encoder::SystemDataInterface* system_data);

  // The output of the Encode*() methods is a triple consisting of a status
  // and, if the status is kOK, a new observation and its metadata. The
  // observation will have been assigned a new quasi-unique |random_id|.
  struct Result {
    Status status;
    std::unique_ptr<Observation2> observation;
    std::unique_ptr<ObservationMetadata> metadata;
  };

  // Encodes an Observation of type BasicRapporObservation.
  //
  // metric: Provides access to the names and IDs of the customer, project and
  // metric associated with the Observation being encoded.
  //
  // report: The definition of the Report associated with the Observation being
  // encoded. In addition to the common fields always required, this method also
  // requires that the |local_privacy_noise_level| field be set. This is used to
  // determine the p and q values for Basic RAPPOR.
  //
  // day_index: The day index associated with the Observation being encoded.
  //
  // value_index: The index to encode using Basic RAPPOR. It must be in
  // the range [0, num_categories - 1]
  //
  // num_categories: The number of categories to use in the Basic RAPPOR
  // encoding.
  Result EncodeBasicRapporObservation(MetricRef metric,
                                      const ReportDefinition* report,
                                      uint32_t day_index, uint32_t value_index,
                                      uint32_t num_categories) const;

  // Encodes an Observation of type IntegerEventObservation.
  //
  // metric: Provides access to the names and IDs of the customer, project and
  // metric associated with the Observation being encoded.
  //
  // report: The definition of the Report associated with the Observation being
  // encoded.
  //
  // day_index: The day index associated with the Observation being encoded.
  //
  // event_codes: This will be used to populate the bits of the Obseravtion's
  // |event_code|.
  //
  // component: The hash of this value will populate the Observation's
  // |component_name_hash| field.
  //
  // value: This will populate the Observation's |value| field.
  Result EncodeIntegerEventObservation(
      MetricRef metric, const ReportDefinition* report, uint32_t day_index,
      const google::protobuf::RepeatedField<uint32_t>& event_codes,
      const std::string component, int64_t value) const;

  // Encodes an Observation of type HistogramObservation.
  //
  // metric: Provides access to the names and IDs of the customer, project and
  // metric associated with the Observation being encoded.
  //
  // report: The definition of the Report associated with the Observation being
  // encoded.
  //
  // day_index: The day index associated with the Observation being encoded.
  //
  // event_codes: This will be used to populate the bits of the Obseravtion's
  // |event_code|.
  //
  // component: The hash of this value will populate the Observation's
  // |component_name_hash| field.
  //
  // histogram: This will be used to populate the Observation's |buckets| field.
  // This method does not validate |histogram| against the Metric definition.
  // That is the caller's responsibility.
  Result EncodeHistogramObservation(
      MetricRef metric, const ReportDefinition* report, uint32_t day_index,
      const google::protobuf::RepeatedField<uint32_t>& event_codes,
      const std::string component, HistogramPtr histogram) const;

  // Encodes an Observation of type CustomObservation.
  //
  // metric: Provides access to the names and IDs of the customer, project and
  // metric associated with the Observation being encoded.
  //
  // report: The definition of the Report associated with the Observation being
  // encoded.
  //
  // day_index: The day index associated with the Observation being encoded.
  //
  // event_values: This will be used to populate the Observation's |values|
  // field. This method does not validate |event_values| against the Metric's
  // proto definition. That is the caller's responsibility.
  Result EncodeCustomObservation(MetricRef metric,
                                 const ReportDefinition* report,
                                 uint32_t day_index,
                                 EventValuesPtr event_values) const;

  // Encodes an Observation of type RapporObservation.
  //
  // metric: Provides access to the names and IDs of the customer, project and
  // metric associated with the Observation being encoded.
  //
  // report: The definition of the Report associated with the Observation being
  // encoded. In addition to the common fields always required, this method also
  // requires that the |local_privacy_noise_level| field be set. This is used to
  // determine the p and q values for String RAPPOR. Additionally
  // The fields |expected_population_size| and |expected_string_set_size| from
  // the ReportDefinition will be consulted when configuring the String
  // RAPPOR algorithm.
  //
  // day_index: The day index associated with the Observation being encoded.
  //
  // str: The string to encode using String RAPPOR.
  Result EncodeRapporObservation(MetricRef metric,
                                 const ReportDefinition* report,
                                 uint32_t day_index,
                                 const std::string& str) const;

  // Encodes an Observation of type ForculusObservation.
  //
  // metric: Provides access to the names and IDs of the customer, project and
  // metric associated with the Observation being encoded.
  //
  // report: The definition of the Report associated with the Observation being
  // encoded. In addition to the common fields always required, this method also
  // requires that the |threshold| field be set. This will be
  // used as the threshold in Forculus threshold encryption.
  //
  // day_index: The day index associated with the Observation being encoded.
  //
  // str: The string to encrypt using Forculus.
  Result EncodeForculusObservation(MetricRef metric,
                                   const ReportDefinition* report,
                                   uint32_t day_index,
                                   const std::string& str) const;

  // Encodes an Observation of type UniqueActivesObservation.
  //
  // metric: Provides access to the names and IDs of the customer, project and
  // metric associated with the Observation being encoded.
  //
  // report: The definition of the Report associated with the Observation being
  // encoded. In addition to the common fields always required, this method also
  // requires that the |local_privacy_noise_level| field be set. This is used to
  // determine the p and q values for Basic RAPPOR.
  //
  // day_index: The day index associated with the Observation being encoded.
  // This is the last day (inclusive) of the rolling window associated with this
  // Observation.
  //
  // event_code: The event code of the Event associated with this Observation.
  // This value should be a nonnegative integer less than or equal to the
  // max_event_code of the MetricDefinition wrapped by |metric|, but it is the
  // caller's responsibility to ensure this.
  //
  // was_active: Set to true if an event with code |event_code|
  // occurred during the window of size |window_size| ending on |day_index|,
  // false otherwise. If |was_active| is true, the BasicRapporObservation field
  // of the UniqueActivesObservation is a Basic RAPPOR encoding of a 1 bit.
  // If |was_active| is false, the BasicRapporObservation field is a Basic
  // RAPPOR encoding of a 0 bit.
  //
  // window_size: The number of days in the window associated with the
  // Observation. This should be one of the window sizes specified in |report|,
  // but it is the caller's responsibility to ensure this.
  Result EncodeUniqueActivesObservation(MetricRef metric,
                                        const ReportDefinition* report,
                                        uint32_t day_index, uint32_t event_code,
                                        bool was_active,
                                        uint32_t window_size) const;

  // Encodes an Observation of type PerDeviceCountObservation.
  //
  // metric: Provides access to the names and IDs of the customer, project and
  // metric associated with the Observation being encoded.
  //
  // report: The definition of the Report associated with the Observation being
  // encoded.
  //
  // day_index: The day index associated with the Observation being encoded.
  // This is the last day (inclusive) of the rolling window associated with this
  // Observation.
  //
  // component: The component associated with this Observation. The hash of this
  // value will populate the Observation's |component_name_hash| field.
  //
  // event_codes: This will be used to populate the bits of the Obseravtion's
  // |event_code|.
  //
  // count: This will populate the |value| field of the the
  // IntegerEventObservation wrapped by the PerDeviceCountObservation.
  //
  // window_size: The number of days in the window associated with the
  // Observation. This should be one of the window sizes specified in |report|,
  // but it is the caller's responsibility to ensure this.
  Result EncodePerDeviceCountObservation(
      MetricRef metric, const ReportDefinition* report, uint32_t day_index,
      const std::string component,
      const google::protobuf::RepeatedField<uint32_t>& event_codes,
      int64_t count, uint32_t window_size) const;

  // Encodes an Observation of type ReportParticipationObservation.
  //
  // metric: Provides access to the names and IDs of the customer, project, and
  // metric associated with the Observation being encoded.
  //
  // report: The definition of the Report associated with the Observation being
  // encoded.
  //
  // day_index: The day index associated with the Observation being encoded.
  Result EncodeReportParticipationObservation(MetricRef metric,
                                              const ReportDefinition* report,
                                              uint32_t day_index) const;

 private:
  // Encodes a BasicRapporObservation for a given |metric|, |report|, and
  // |day_index| in which the data field is a Basic RAPPOR encoding of a vector
  // of |num_categories| zero bits.
  Result EncodeNullBasicRapporObservation(MetricRef metric,
                                          const ReportDefinition* report,
                                          uint32_t day_index,
                                          uint32_t num_categories) const;

  // Makes an Observation and ObservationMetadata with all information that
  // is independent of which Encode*() method is being invoked.
  Result MakeObservation(MetricRef metric, const ReportDefinition* report,
                         uint32_t day_index) const;

  const encoder::ClientSecret client_secret_;
  const encoder::SystemDataInterface* system_data_;
  mutable crypto::Random random_;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_ENCODER_H_
