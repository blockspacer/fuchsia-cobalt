// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/report_serializer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>

#include "glog/logging.h"
#include "util/crypto_util/base64.h"
#include "util/crypto_util/hash.h"
#include "util/datetime_util.h"
#include "util/log_based_metrics.h"

namespace cobalt {
namespace analyzer {

using crypto::byte;
using crypto::hash::DIGEST_SIZE;
using crypto::hash::Hash;

// Stackdriver metric constants
namespace {
const char kStartSerializingReportFailure[] =
    "report-serializer-start-serializing-report-failure";
const char kAppendRowsFailure[] = "report-serializir-append-rows-failure";
}  // namespace

namespace {
// The field separator to use for our CSV output.
const char kSeparator[] = ",";

// Due to the nature of the Cobalt encodings, there are cases where a
// generated report contains rows with no information. In those cases it
// is more useful to omit serializing the row. One example is the case in
// which Basic RAPPOR is being used with the INDEX data type. In this case we
// may pre-allocate a large block of indices that are not currently being used
// by the client application. We have therefore not assigned labels for these
// indices and the client application will never encode those indices.
// The Basic RAPPOR analyzer will still generate a row for such an index
// that will likely have a small value for the count field, and in case
// we are using zero statistical noise (i.e. p=0, q=1) the count field will
// be exactly zero. This method implements a heuristic for detecting that
// case: The value is an index, there is no label, the count is close to zero.
bool ShouldSkipRow(const HistogramReportRow& report_row) {
  if (report_row.value().data_case() == ValuePart::kIndexValue &&
      report_row.label().empty() &&
      std::fabs(report_row.count_estimate()) < 0.0001) {
    return true;
  }
  return false;
}

// Produces a value that is appropriate to use for a column header in a CSV
// file, assuming that the input is a metric part name. Metric part names
// are restricted by the regular expression |validMetricPartName| in the
// file //config/config_parser/src/config_parser/project_config.go.
// We reproduce that regular expression here for convenience:
//     ^[a-zA-Z][_a-zA-Z0-9\\- ]+$
// The logic in this function must be kept in sync with that regular expression.
// The column headers produced by this function will:
//     - Contain only letters, numbers, or underscores.
//     - Start with a letter or underscore
//     - Be at most 128 characters long
//
// See comment below before StartSerializingCSVReport().
std::string EscapeMetricPartNameForCSVColumHeader(
    const std::string& metric_part_name) {
  size_t size =
      (metric_part_name.size() <= 128 ? metric_part_name.size() : 128);
  std::string out(size, '_');
  for (size_t i = 0; i < size; i++) {
    const char& c = metric_part_name[i];
    if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9')) {
      out[i] = c;
    }
  }
  return out;
}

// Produces a string that is appropriate to use as a non-column-header value
// in a CSV file. The string produced by this function will:
// - be enclosed in double quotes.
// - have all non-printable bytes and all occurrences of the double-quote
//   character (") and all occurrences of the percent character (%) replaced by
//   their URL encoding (i.e. their %hh encoding).
// - have a maximum length of 258.
std::string ToCSVString(const std::string& in) {
  const char* kHexDigits = "0123456789ABCDEF";

  // Truncate at 256. After enclosing in quotes the max length is 258.
  size_t size = (in.size() <= 256 ? in.size() : 256);
  std::string out;
  // Reserve space in the out string. The +2 is for enclosing in quotes.
  out.reserve(size + 2);
  out.push_back('"');
  for (size_t i = 0; i < size; i++) {
    const char& c = in[i];
    if (std::isprint(c) && c != '"' && c != '%') {
      // Append unescaped byte.
      out.push_back(c);
    } else {
      // Append %hh encoding of byte,
      out.push_back('%');
      out.push_back(kHexDigits[(c >> 4) & 0xf]);
      out.push_back(kHexDigits[c & 0xf]);
    }
  }
  out.push_back('"');
  return out;
}

std::string FloatToString(float x) {
  int sz = std::snprintf(nullptr, 0, "%.3f", x);
  std::vector<char> buf(sz + 1);  // note +1 for null terminator
  std::snprintf(&buf[0], buf.size(), "%.3f", x);
  std::string s = std::string(&buf[0]);
  if (s == "0.000") {
    return "0";
  }
  return s;
}

std::string ValueToString(const ValuePart& value,
                          const IndexLabels* index_labels = nullptr) {
  switch (value.data_case()) {
    case ValuePart::kStringValue:
      return ToCSVString(value.string_value());

    case ValuePart::kIntValue:
      return std::to_string(value.int_value());

    case ValuePart::kDoubleValue:
      return FloatToString(value.double_value());

    case ValuePart::kBlobValue: {
      // Build the Sha256 hash of the blob.
      byte hash_bytes[DIGEST_SIZE];
      const std::string& blob = value.blob_value();
      Hash(reinterpret_cast<const byte*>(&blob[0]), blob.size(), hash_bytes);
      // Return the Base64 encoding of the Sha256 hash of the blob.
      std::string encoded;
      crypto::Base64Encode(hash_bytes, DIGEST_SIZE, &encoded);
      return encoded;
    }

    case ValuePart::kIndexValue: {
      // Replace the index value with a string if such a string is available.
      if (index_labels != nullptr) {
        auto iter = index_labels->labels().find(value.index_value());
        if (iter != index_labels->labels().end()) {
          return iter->second;
        }
      }
      // Return <index kIndexValue> if no string mapping is available.
      std::ostringstream stream;
      stream << "<index " << value.index_value() << ">";
      return stream.str();
    }

    default:
      return "<Unrecognized value data type>";
  }
}

std::string CountEstimateToString(float count_estimate) {
  // We clip the count estimate to zero. Techniques such as RAPPOR produce
  // unbiased estimates which may be negative. But exporting a report with
  // negative values for the count will likely cause more confusion than
  // its worth.
  double x = std::max(0.0f, count_estimate);
  return FloatToString(x);
}

std::string StdErrToString(float std_err) {
  // It doesn't make sense for the errors to be negative.
  double x = std::max(0.0f, std_err);
  return FloatToString(x);
}

// Returns a human-readable representation of the report config ID.
// Used in forming error messages.
std::string IdString(const ReportConfig& report_config) {
  std::ostringstream stream;
  stream << "(" << report_config.customer_id() << kSeparator
         << report_config.project_id() << kSeparator << report_config.id()
         << ")";
  return stream.str();
}

std::string DayIndexToDateString(uint32_t day_index) {
  util::CalendarDate cd = util::DayIndexToCalendarDate(day_index);
  std::ostringstream stream;
  stream << cd.year << "-" << cd.month << "-" << cd.day_of_month;
  return stream.str();
}

}  // namespace

ReportSerializer::ReportSerializer(const ReportConfig* report_config,
                                   const ReportMetadataLite* metadata,
                                   const ReportExportConfig* export_config)
    : report_config_(report_config),
      metadata_(metadata),
      export_config_(export_config) {}

grpc::Status ReportSerializer::SerializeReport(
    const std::vector<ReportRow>& report_rows,
    std::string* serialized_report_out, std::string* mime_type_out) {
  CHECK(serialized_report_out);
  CHECK(mime_type_out);
  ReportRowVectorIterator row_iterator(&report_rows);
  std::ostringstream stream;
  auto status = StartSerializingReport(&stream);
  if (!status.ok()) {
    return status;
  }
  status = AppendRows(UINT32_MAX, &row_iterator, &stream);
  if (!status.ok()) {
    return status;
  }
  serialized_report_out->assign(stream.str());
  mime_type_out->assign(mime_type_);
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::StartSerializingReport(std::ostream* stream) {
  CHECK(stream);
  auto serialization_case = export_config_->export_serialization_case();
  switch (serialization_case) {
    case ReportExportConfig::kCsv:
      return StartSerializingCSVReport(stream);

    case ReportExportConfig::EXPORT_SERIALIZATION_NOT_SET: {
      std::ostringstream error_stream;
      error_stream
          << "Invalid ReportExportConfig: No export_serialization is set. "
             "In ReportConfig "
          << IdString(*report_config_);
      std::string message = error_stream.str();
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kStartSerializingReportFailure)
          << message;
      return grpc::Status(grpc::INVALID_ARGUMENT, message);
    }
    default: {
      std::ostringstream error_stream;
      error_stream
          << "Invalid ReportExportConfig: Unrecognized export_serialization: "
          << serialization_case << " In ReportConfig "
          << IdString(*report_config_);
      std::string message = error_stream.str();
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kStartSerializingReportFailure)
          << message;
      return grpc::Status(grpc::INVALID_ARGUMENT, message);
    }
  }
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendRows(size_t max_num_bytes,
                                          ReportRowIterator* row_iterator,
                                          std::ostream* stream) {
  CHECK(stream);
  auto serialization_case = export_config_->export_serialization_case();
  switch (serialization_case) {
    case ReportExportConfig::kCsv:
      return AppendCSVRows(max_num_bytes, row_iterator, stream);

    case ReportExportConfig::EXPORT_SERIALIZATION_NOT_SET: {
      std::ostringstream error_stream;
      error_stream
          << "Invalid ReportExportConfig: No export_serialization is set. "
             "In ReportConfig "
          << IdString(*report_config_);
      std::string message = error_stream.str();
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAppendRowsFailure) << message;
      return grpc::Status(grpc::INVALID_ARGUMENT, message);
    }
    default: {
      std::ostringstream error_stream;
      error_stream
          << "Invalid ReportExportConfig: Unrecognized export_serialization: "
          << serialization_case << " In ReportConfig "
          << IdString(*report_config_);
      std::string message = error_stream.str();
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAppendRowsFailure) << message;
      return grpc::Status(grpc::INVALID_ARGUMENT, message);
    }
  }
  return grpc::Status::OK;
}

// Implementation note: In the current version of Cobalt the CSV files we are
// producing will be saved to Google Cloud Storage and read by a Google
// Data Studio data connector. Consequently we want to ensure that the
// CSV files we produce adhere to the format specified here:
//
// https://support.google.com/datastudio/answer/7511998?hl=en&ref_topic=7332552#file-format
//
// We summarize the salient points:
//
// - Each row must have the same number of columns, even if data is missing for
//   a particular cell in the table
// - Every CSV file in the same folder must have the same format
// - The column separator must be a comma.
// - If there are commas within the actual data in a field, that field must be
//   surrounded by quotes. If your data includes double quotes, you can use a
//   single quote character to surround the field.
// - The first line in your file must be a header row.
// - Field names must be unique, so you can't have duplicate values in your
//   header row.
// - Column names must:
//     - Contain only letters, numbers, or underscores.
//     - Start with a letter or underscore
//     - Be at most 128 characters long
// - Each line in the file must end with a line break.
// - The GCS connector does not support line breaks in your data even if these
//   are escaped by quotes.
//
// These formatting rules will be followed by this method and by the other
// methods used by this method.
grpc::Status ReportSerializer::StartSerializingCSVReport(std::ostream* stream) {
  mime_type_ = "text/csv";
  return AppendCSVHeaderRow(stream);
}

grpc::Status ReportSerializer::AppendCSVHeaderRow(std::ostream* stream) {
  switch (metadata_->report_type()) {
    case HISTOGRAM:
      return AppendCSVHistogramHeaderRow(stream);
      break;

    case JOINT:
      return AppendCSVJointHeaderRow(stream);
      break;

    case RAW_DUMP:
      return AppendCSVRawDumpHeaderRow(stream);
      break;

    default: {
      std::ostringstream error_stream;
      error_stream << "Unrecognized report type: " << metadata_->report_type();
      std::string message = error_stream.str();
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kStartSerializingReportFailure)
          << message;
      return grpc::Status(grpc::INVALID_ARGUMENT, message);
    }
  }
}

grpc::Status ReportSerializer::AppendCSVHistogramHeaderRow(
    std::ostream* stream) {
  fixed_leftmost_column_values_.clear();
  if (metadata_->variable_indices_size() != 1) {
    std::ostringstream error_stream;
    error_stream << "Invalid ReportMetadataLite: Histogram reports always "
                    "analyze exactly one variable but the number of variable "
                    "indices in metadata is "
                 << metadata_->variable_indices_size() << ". For ReportConfig "
                 << IdString(*report_config_);
    std::string message = error_stream.str();
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kStartSerializingReportFailure)
        << message;
    return grpc::Status(grpc::INVALID_ARGUMENT, message);
  }

  fixed_leftmost_column_values_.push_back(
      DayIndexToDateString(metadata_->first_day_index()));
  if (metadata_->first_day_index() == metadata_->last_day_index()) {
    (*stream) << "date" << kSeparator;
    num_columns_ = 4u;
  } else {
    (*stream) << "start_date" << kSeparator << "end_date" << kSeparator;
    fixed_leftmost_column_values_.push_back(
        DayIndexToDateString(metadata_->last_day_index()));
    num_columns_ = 5u;
  }

  auto status = AppendCSVHeaderRowVariableNames(stream);
  if (!status.ok()) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kStartSerializingReportFailure)
        << status.error_message();
    return status;
  }

  if (report_config_->system_profile_field_size() > 0) {
    num_columns_ += report_config_->system_profile_field_size();
    (*stream) << kSeparator;
    status = AppendCSVHeaderRowSystemProfileFields(stream);
    if (!status.ok()) {
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kStartSerializingReportFailure)
          << status.error_message();
      return status;
    }
  }

  // Append the "count" column header.
  (*stream) << kSeparator << "count";

  // Append the "err" column header.
  (*stream) << kSeparator << "err" << std::endl;
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendCSVRawDumpHeaderRow(std::ostream* stream) {
  num_columns_ = metadata_->variable_indices_size();
  if (num_columns_ < 1) {
    std::ostringstream error_stream;
    error_stream << "Invalid ReportMetadataLite: At least one variable needs "
                    "to be specified for RAW_DUMP reports. For ReportConfig "
                 << IdString(*report_config_);
    std::string message = error_stream.str();
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kStartSerializingReportFailure)
        << message;
    return grpc::Status(grpc::INVALID_ARGUMENT, message);
  }
  fixed_leftmost_column_values_.clear();
  fixed_leftmost_column_values_.push_back(
      DayIndexToDateString(metadata_->first_day_index()));
  if (metadata_->first_day_index() == metadata_->last_day_index()) {
    (*stream) << "date" << kSeparator;
    num_columns_ += 1u;
  } else {
    (*stream) << "start_date" << kSeparator << "end_date" << kSeparator;
    fixed_leftmost_column_values_.push_back(
        DayIndexToDateString(metadata_->last_day_index()));
    num_columns_ += 2u;
  }

  auto status = AppendCSVHeaderRowVariableNames(stream);
  if (!status.ok()) {
    return status;
  }

  if (report_config_->system_profile_field_size() > 0) {
    num_columns_ += report_config_->system_profile_field_size();
    (*stream) << kSeparator;
    status = AppendCSVHeaderRowSystemProfileFields(stream);
    if (!status.ok()) {
      return status;
    }
  }

  (*stream) << std::endl;

  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendCSVJointHeaderRow(std::ostream* stream) {
  std::ostringstream error_stream;
  error_stream << "JOINT reports are not yet implemented. For ReportConfig"
               << IdString(*report_config_);
  std::string message = error_stream.str();
  LOG_STACKDRIVER_COUNT_METRIC(ERROR, kStartSerializingReportFailure)
      << message;
  return grpc::Status(grpc::UNIMPLEMENTED, message);
}

grpc::Status ReportSerializer::AppendCSVHeaderRowVariableNames(
    std::ostream* stream) {
  bool first_column = true;
  for (int index : metadata_->variable_indices()) {
    if (first_column) {
      first_column = false;
    } else {
      (*stream) << kSeparator;
    }
    if (index >= report_config_->variable_size() || index < 0) {
      std::ostringstream error_stream;
      error_stream
          << "Invalid ReportMetadataLite: Variable index out-of-bounds: "
          << index << ". For ReportConfig " << IdString(*report_config_);
      std::string message = error_stream.str();
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kStartSerializingReportFailure)
          << message;
      return grpc::Status(grpc::INVALID_ARGUMENT, message);
    }
    (*stream) << EscapeMetricPartNameForCSVColumHeader(
        report_config_->variable(index).metric_part());
  }
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendCSVHeaderRowSystemProfileFields(
    std::ostream* stream) {
  bool first = true;
  for (const auto& field : report_config_->system_profile_field()) {
    if (first) {
      first = false;
    } else {
      (*stream) << kSeparator;
    }
    switch (field) {
      case cobalt::SystemProfileField::OS:
        (*stream) << "OS";
        break;
      case cobalt::SystemProfileField::ARCH:
        (*stream) << "Arch";
        break;
      case cobalt::SystemProfileField::BOARD_NAME:
        (*stream) << "Board_Name";
        break;
      case cobalt::SystemProfileField::PRODUCT_NAME:
        (*stream) << "Product_Name";
        break;
      case cobalt::SystemProfileField::SYSTEM_VERSION:
        (*stream) << "System_Version";
        break;
    }
  }
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendCSVRows(size_t max_num_bytes,
                                             ReportRowIterator* row_iterator,
                                             std::ostream* stream) {
  const ReportRow* row;
  auto start = stream->tellp();
  while (true) {
    auto status = row_iterator->NextRow(&row);
    if (status.error_code() == grpc::NOT_FOUND) {
      break;
    }
    if (!status.ok()) {
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAppendRowsFailure)
          << status.error_message();
      return status;
    }
    status = AppendCSVReportRow(*row, stream);
    if (!status.ok()) {
      return status;
    }
    size_t size_so_far = static_cast<size_t>(stream->tellp() - start);
    if (size_so_far >= max_num_bytes) {
      break;
    }
  }
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendCSVReportRow(const ReportRow& report_row,
                                                  std::ostream* stream) {
  auto row_type = report_row.row_type_case();
  auto report_type = metadata_->report_type();
  switch (report_type) {
    case HISTOGRAM: {
      if (row_type != ReportRow::kHistogram) {
        std::ostringstream error_stream;
        error_stream << "Expecting a HISTOGRAM row but the row_type="
                     << row_type << ". For ReportConfig "
                     << IdString(*report_config_);
        std::string message = error_stream.str();
        LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAppendRowsFailure) << message;
        return grpc::Status(grpc::INTERNAL, message);
      }
      return AppendCSVHistogramReportRow(report_row.histogram(), stream);
      break;
    }

    case JOINT: {
      if (row_type != ReportRow::kJoint) {
        std::ostringstream error_stream;
        error_stream << "Expecting a JOINT row but the row_type=" << row_type
                     << ". For ReportConfig " << IdString(*report_config_);
        std::string message = error_stream.str();
        LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAppendRowsFailure) << message;
        return grpc::Status(grpc::INTERNAL, message);
      }
      return AppendCSVJointReportRow(report_row.joint(), stream);
      break;
    }

    case RAW_DUMP: {
      if (row_type != ReportRow::kRawDump) {
        std::ostringstream error_stream;
        error_stream << "Expecting a RAW_DUMP row but the row_type=" << row_type
                     << ". For ReportConfig " << IdString(*report_config_);
        std::string message = error_stream.str();
        LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAppendRowsFailure) << message;
        return grpc::Status(grpc::INTERNAL, message);
      }
      return AppendCSVRawDumpReportRow(report_row.raw_dump(), stream);
      break;
    }

    default: {
      std::ostringstream error_stream;
      error_stream << "Unrecognized row_type: " << row_type;
      std::string message = error_stream.str();
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAppendRowsFailure) << message;
      return grpc::Status(grpc::INVALID_ARGUMENT, message);
    }
  }
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendCSVSystemProfileFields(
    const SystemProfile& profile, std::ostream* stream) {
  if (report_config_->system_profile_field_size() > 0) {
    for (const auto& field : report_config_->system_profile_field()) {
      (*stream) << kSeparator;
      switch (field) {
        case SystemProfileField::OS:
          (*stream) << ToCSVString(cobalt::SystemProfile_OS_Name(profile.os()));
          break;
        case SystemProfileField::ARCH:
          (*stream) << ToCSVString(
              cobalt::SystemProfile_ARCH_Name(profile.arch()));
          break;
        case SystemProfileField::BOARD_NAME:
          (*stream) << ToCSVString(profile.board_name());
          break;
        case SystemProfileField::PRODUCT_NAME:
          (*stream) << ToCSVString(profile.product_name());
          break;
        case SystemProfileField::SYSTEM_VERSION:
          (*stream) << ToCSVString(profile.system_version());
          break;
      }
    }
  }
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendCSVHistogramReportRow(
    const HistogramReportRow& report_row, std::ostream* stream) {
  size_t num_fixed_values = fixed_leftmost_column_values_.size();
  if (num_columns_ !=
      3 + num_fixed_values + report_config_->system_profile_field_size()) {
    std::ostringstream error_stream;
    error_stream << "Histogram reports always contain 3 columns in addition to "
                    "the fixed leftmost columns and the "
                    "system_profile_field_size but num_columns="
                 << num_columns_
                 << " and num_fixed_values=" << num_fixed_values;
    std::string message = error_stream.str();
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAppendRowsFailure) << message;
    return grpc::Status(grpc::INTERNAL, message);
  }
  if (ShouldSkipRow(report_row)) {
    return grpc::Status::OK;
  }
  for (const std::string& v : fixed_leftmost_column_values_) {
    (*stream) << v << kSeparator;
  }
  if (!report_row.label().empty()) {
    (*stream) << ToCSVString(report_row.label());
  } else {
    (*stream) << ValueToString(report_row.value());
  }
  AppendCSVSystemProfileFields(report_row.system_profile(), stream);
  (*stream) << kSeparator << CountEstimateToString(report_row.count_estimate());
  (*stream) << kSeparator << StdErrToString(report_row.std_error())
            << std::endl;
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendCSVRawDumpReportRow(
    const RawDumpReportRow& report_row, std::ostream* stream) {
  size_t num_fixed_values = fixed_leftmost_column_values_.size();
  size_t num_values_this_row = report_row.values_size();
  if (num_columns_ != num_values_this_row + num_fixed_values +
                          report_config_->system_profile_field_size()) {
    std::ostringstream error_stream;
    error_stream << "Encountered a RawDumpReportRow with the wrong number of "
                    "values. Expecting "
                 << (num_columns_ - num_fixed_values) << ". Found "
                 << num_values_this_row << ". For ReportConfig "
                 << IdString(*report_config_);
    std::string message = error_stream.str();
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAppendRowsFailure) << message;
    return grpc::Status(grpc::INTERNAL, message);
  }
  for (const std::string& v : fixed_leftmost_column_values_) {
    (*stream) << v << kSeparator;
  }

  for (size_t i = 0; i < num_values_this_row; i++) {
    const IndexLabels* index_labels = nullptr;
    if (i < (size_t)report_config_->variable_size() &&
        report_config_->variable(i).has_index_labels()) {
      index_labels = &(report_config_->variable(i).index_labels());
    }
    if (i != 0) {
      (*stream) << kSeparator;
    }
    (*stream) << ValueToString(report_row.values(i), index_labels);
  }

  AppendCSVSystemProfileFields(report_row.system_profile(), stream);
  (*stream) << std::endl;
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendCSVJointReportRow(
    const JointReportRow& report_row, std::ostream* stream) {
  std::string message = "Joint reports are not implemented.";
  LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAppendRowsFailure) << message;
  return grpc::Status(grpc::UNIMPLEMENTED, message);
}

}  // namespace analyzer
}  // namespace cobalt
