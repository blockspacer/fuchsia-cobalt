// Copyright 2018 The Fuchsia Authors. All rights reserved.  Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE
// file.

#include "encoder/file_observation_store.h"

#include <ctime>
#include <iomanip>
#include <regex>
#include <utility>

#include "./logging.h"
#include "./tracing.h"
#include "encoder/file_observation_store_internal.pb.h"
#include "logger/logger_interface.h"
#include "third_party/protobuf/src/google/protobuf/util/delimited_message_util.h"

namespace cobalt::encoder {

using statusor::StatusOr;
using util::FileSystem;
using util::Status;
using util::StatusCode;

constexpr char kActiveFileName[] = "in_progress.data";
// The first part of the filename is 13 digits representing the milliseconds
// since the unix epoch. The millisecond timestamp became 13 digits in
// September 2001, and won't be 14 digits until 2286.
//
// The second part of the filename is 10 digits, which is a random number in the
// range 1000000000-9999999999.
const std::regex kFinalizedFileRegex(R"(\d{13}-\d{10}.data)");
constexpr uint32_t kTimestampWidth = 13;
constexpr uint64_t kMinRandomNumber = 1000000000;
constexpr uint64_t kMaxRandomNumber = 9999999999;

FileObservationStore::FileObservationStore(
    size_t max_bytes_per_observation, size_t max_bytes_per_envelope,
    size_t max_bytes_total, std::unique_ptr<FileSystem> fs,
    std::string root_directory, std::string name,
    logger::LoggerInterface *internal_logger)
    : ObservationStore(max_bytes_per_observation, max_bytes_per_envelope,
                       max_bytes_total),
      fs_(std::move(fs)),
      root_directory_(std::move(root_directory)),
      active_file_name_(FullPath(kActiveFileName)),
      name_(std::move(name)),
      internal_metrics_(
          logger::InternalMetrics::NewWithLogger(internal_logger)) {
  CHECK(fs_);

  // Check if root_directory_ already exists.
  if (!fs_->ListFiles(root_directory_).ok()) {
    // If it doesn't exist, create it here.
    // TODO(zmbush): If MakeDirectory doesn't work, we should fail over to
    // MemoryObservationStore.
    CHECK(fs_->MakeDirectory(root_directory_));
  }

  {
    auto fields = protected_fields_.lock();
    fields->finalized_bytes = 0;

    for (const auto &file : ListFinalizedFiles()) {
      fields->finalized_bytes +=
          fs_->FileSize(FullPath(file)).ConsumeValueOr(0);
    }

    // If there exists an active file, it likely means that the process
    // terminated unexpectedly last time. In this case, the file should be
    // finalized in order to be Taken from the store.
    //
    // For simplicity's sake, we attempt to finalize the active_file_name_ while
    // ignoring the result. If the operation succeeds, then we rescued the
    // active file. Otherwise, there probably was no active file in the first
    // place.
    FinalizeActiveFile(&fields);
  }
}

ObservationStore::StoreStatus FileObservationStore::AddEncryptedObservation(
    std::unique_ptr<EncryptedMessage> message,
    std::unique_ptr<ObservationMetadata> metadata) {
  TRACE_DURATION("cobalt_core",
                 "FileObservationStore::AddEncryptedObservation");
  auto fields = protected_fields_.lock();

  // "+1" below is for the |scheme| field of EncryptedMessage.
  size_t obs_size = message->ciphertext().size() +
                    message->public_key_fingerprint().size() + 1;
  internal_metrics_->BytesStored(
      logger::PerProjectBytesStoredMetricDimensionStatus::Attempted, obs_size,
      metadata->customer_id(), metadata->project_id());

  auto active_file = GetActiveFile(&fields);
  if (active_file == nullptr) {
    return kWriteFailed;
  }

  auto metadata_str = metadata->SerializeAsString();
  auto report_id = metadata->report_id();

  if (obs_size > max_bytes_per_observation_) {
    LOG(WARNING) << "An observation that was too big was passed in to "
                    "FileObservationStore::AddEncryptedObservation(): "
                 << obs_size;
    return kObservationTooBig;
  }

  VLOG(6) << name_ << ": AddEncryptedObservation() metric=("
          << metadata->customer_id() << "," << metadata->project_id() << ","
          << metadata->metric_id() << "), size=" << obs_size << ".";

  size_t estimated_new_byte_count =
      fields->finalized_bytes + active_file->ByteCount() + obs_size;
  if (estimated_new_byte_count > max_bytes_total_) {
    VLOG(4) << name_
            << ": The observation store is full. estimated_new_byte_count="
            << estimated_new_byte_count << " > " << max_bytes_total_ << ".";
    return kStoreFull;
  }

  if (!fields->metadata_written ||
      metadata_str != fields->last_written_metadata) {
    VLOG(5) << name_ << ": Writing observation metadata.";
    ObservationStoreRecord stored_metadata;
    stored_metadata.mutable_meta_data()->Swap(metadata.get());
    if (!google::protobuf::util::SerializeDelimitedToZeroCopyStream(
            stored_metadata, active_file)) {
      LOG(WARNING) << name_ << ": Unable to write metadata to `"
                   << active_file_name_ << "`";
      return kWriteFailed;
    }
    fields->metadata_written = true;
    fields->last_written_metadata = metadata_str;
  }

  ObservationStoreRecord stored_message;
  stored_message.mutable_encrypted_observation()->Swap(message.get());
  if (!google::protobuf::util::SerializeDelimitedToZeroCopyStream(
          stored_message, active_file)) {
    LOG(WARNING) << "Unable to write encrypted_observation to `"
                 << active_file_name_ << "`";
    return kWriteFailed;
  }

  if (active_file->ByteCount() >=
      static_cast<int64_t>(max_bytes_per_envelope_)) {
    VLOG(4) << name_ << ": In-progress file contains "
            << active_file->ByteCount()
            << " bytes (>= " << max_bytes_per_envelope_ << "). Finalizing it.";

    if (!FinalizeActiveFile(&fields)) {
      LOG(WARNING) << "Unable to finalize `" << active_file_name_;
      return kWriteFailed;
    }
  }

  num_obs_per_report_[report_id]++;
  internal_metrics_->BytesStored(
      logger::PerProjectBytesStoredMetricDimensionStatus::Succeeded, obs_size,
      metadata->customer_id(), metadata->project_id());
  return kOk;
}

bool FileObservationStore::FinalizeActiveFile(
    util::ProtectedFields<Fields>::LockedFieldsPtr *fields) {
  VLOG(6) << name_ << ": FinalizeActiveFile()";
  auto &f = *fields;

  // Close the current file (if it is open).
  f->active_file = nullptr;
  if (f->active_fstream.is_open()) {
    f->active_fstream.close();
  }
  f->metadata_written = false;

  auto filesize = fs_->FileSize(active_file_name_);
  if (filesize.ok()) {
    if (filesize.ConsumeValueOrDie() == 0) {
      // File exists, but is empty. Let's just delete it instead of renaming.
      fs_->Delete(active_file_name_);
      return false;
    }
  } else {
    // if !filesize.ok(), the file likely doesn't even exist.
    return false;
  }

  auto new_name = FullPath(filename_generator_.GenerateFilename());
  if (!fs_->Rename(active_file_name_, new_name)) {
    return false;
  }

  f->finalized_bytes += fs_->FileSize(new_name).ConsumeValueOr(0);
  return true;
}

FileObservationStore::FilenameGenerator::FilenameGenerator()
    : FilenameGenerator([]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
      }) {}

FileObservationStore::FilenameGenerator::FilenameGenerator(
    std::function<int64_t()> now)
    : now_(std::move(now)), random_int_(kMinRandomNumber, kMaxRandomNumber) {}

std::string FileObservationStore::FilenameGenerator::GenerateFilename() const {
  std::stringstream date;
  std::stringstream fname;
  date << std::setfill('0') << std::setw(kTimestampWidth) << now_();
  fname << date.str().substr(0, kTimestampWidth) << "-"
        << random_int_(random_dev_) << ".data";
  return fname.str();
}

std::string FileObservationStore::FullPath(const std::string &filename) const {
  return root_directory_ + "/" + filename;
}

std::string FileObservationStore::FileEnvelopeHolder::FullPath(
    const std::string &filename) const {
  return root_directory_ + "/" + filename;
}

google::protobuf::io::OstreamOutputStream *FileObservationStore::GetActiveFile(
    util::ProtectedFields<Fields>::LockedFieldsPtr *fields) {
  auto &f = *fields;

  if (f->active_file == nullptr) {
    f->active_fstream.open(active_file_name_);

    if (!f->active_fstream.is_open()) {
      LOG_FIRST_N(ERROR, 10)
          << "Failed to open file. (Perhaps the disk is full): "
          << active_file_name_ << " (" << std::strerror(errno) << ")";
      return nullptr;
    }

    f->active_file =
        std::make_unique<google::protobuf::io::OstreamOutputStream>(
            &f->active_fstream);
  }
  return f->active_file.get();
}

std::vector<std::string> FileObservationStore::ListFinalizedFiles() const {
  auto files = fs_->ListFiles(root_directory_).ConsumeValueOr({});
  std::vector<std::string> retval;
  for (const auto &file : files) {
    if (std::regex_match(file, kFinalizedFileRegex)) {
      retval.push_back(file);
    }
  }
  return retval;
}

StatusOr<std::string> FileObservationStore::GetOldestFinalizedFile(
    util::ProtectedFields<Fields>::LockedFieldsPtr *fields) {
  auto &f = *fields;

  std::string found_file_name;
  for (const auto &file : ListFinalizedFiles()) {
    if (f->files_taken.find(file) == f->files_taken.end()) {
      if (found_file_name.empty()) {
        found_file_name = file;
      } else {
        // We compare the file names to try to find the oldest one. This works
        // because file names are prefixed with the timestamp when they were
        // finalized and that timestamp is always 13 digits long.
        //
        // A lexigraphic order of fixed length number strings is identical to
        // ordering their numerical values.
        auto result = strcmp(file.c_str(), found_file_name.c_str());
        if (result < 0) {
          found_file_name = file;
        }
      }
    }
  }
  if (found_file_name.empty()) {
    return Status(StatusCode::NOT_FOUND, "No finalized file");
  }
  return found_file_name;
}

std::unique_ptr<ObservationStore::EnvelopeHolder>
FileObservationStore::TakeNextEnvelopeHolder() {
  auto fields = protected_fields_.lock();

  auto oldest_file_name_or = GetOldestFinalizedFile(&fields);
  if (!oldest_file_name_or.ok()) {
    if (!fields->active_file || fields->active_file->ByteCount() == 0) {
      // Active file isn't open or is empty. Return nullptr.
      return nullptr;
    }
    if (!FinalizeActiveFile(&fields)) {
      // Finalizing the active file failed, no envelope to return.
      return nullptr;
    }
    oldest_file_name_or = GetOldestFinalizedFile(&fields);
    if (!oldest_file_name_or.ok()) {
      return nullptr;
    }
  }

  auto oldest_file_name = oldest_file_name_or.ConsumeValueOrDie();
  fields->files_taken.insert(oldest_file_name);
  fields->finalized_bytes -=
      fs_->FileSize(FullPath(oldest_file_name)).ConsumeValueOr(0);
  return std::make_unique<FileEnvelopeHolder>(fs_.get(), root_directory_,
                                              oldest_file_name);
}

void FileObservationStore::ReturnEnvelopeHolder(
    std::unique_ptr<ObservationStore::EnvelopeHolder> envelope) {
  std::unique_ptr<FileObservationStore::FileEnvelopeHolder> env(
      static_cast<FileObservationStore::FileEnvelopeHolder *>(
          envelope.release()));

  auto fields = protected_fields_.lock();
  for (const auto &file_name : env->file_names()) {
    fields->files_taken.erase(file_name);
    fields->finalized_bytes +=
        fs_->FileSize(FullPath(file_name)).ConsumeValueOr(0);
  }
  env->clear();
}

size_t FileObservationStore::Size() const {
  auto fields = protected_fields_.const_lock();
  auto bytes = fields->finalized_bytes;
  VLOG(4) << name_ << "::Size(): finalized_bytes=" << bytes;
  if (fields->active_file) {
    bytes += fields->active_file->ByteCount();
  } else {
    VLOG(4) << name_ << "::Size(): there is no active file.";
  }
  VLOG(4) << name_ << "::Size(): total_bytes=" << bytes;
  return bytes;
}

bool FileObservationStore::Empty() const { return Size() == 0; }

void FileObservationStore::Delete() {
  auto files = fs_->ListFiles(root_directory_).ConsumeValueOr({});
  for (const auto &file : files) {
    fs_->Delete(FullPath(file));
  }
  fs_->Delete(root_directory_);
}

FileObservationStore::FileEnvelopeHolder::~FileEnvelopeHolder() {
  for (const auto &file_name : file_names_) {
    fs_->Delete(FullPath(file_name));
  }
}

void FileObservationStore::FileEnvelopeHolder::MergeWith(
    std::unique_ptr<EnvelopeHolder> container) {
  std::unique_ptr<FileEnvelopeHolder> file_container(
      static_cast<FileEnvelopeHolder *>(container.release()));

  file_names_.insert(file_container->file_names_.begin(),
                     file_container->file_names_.end());

  file_container->file_names_.clear();

  envelope_.Clear();
  cached_file_size_ = 0;
  envelope_read_ = false;
}

const Envelope &FileObservationStore::FileEnvelopeHolder::GetEnvelope() {
  if (envelope_read_) {
    return envelope_;
  }

  std::string serialized_metadata;
  std::unordered_map<std::string, ObservationBatch *> batch_map;
  ObservationBatch *current_batch;

  ObservationStoreRecord stored;

  for (const auto &file_name : file_names_) {
    std::ifstream ifs(FullPath(file_name), std::ifstream::in);
    google::protobuf::io::IstreamInputStream iis(&ifs);

    bool clean_eof;
    while (google::protobuf::util::ParseDelimitedFromZeroCopyStream(
        &stored, &iis, &clean_eof)) {
      if (stored.has_meta_data()) {
        std::unique_ptr<ObservationMetadata> current_metadata(
            stored.release_meta_data());
        current_metadata->SerializeToString(&serialized_metadata);

        auto iter = batch_map.find(serialized_metadata);
        if (iter != batch_map.end()) {
          current_batch = iter->second;
        } else {
          current_batch = envelope_.add_batch();
          current_batch->set_allocated_meta_data(current_metadata.release());
          batch_map[serialized_metadata] = current_batch;
        }
      } else if (stored.has_encrypted_observation()) {
        current_batch->add_encrypted_observation()->Swap(
            stored.mutable_encrypted_observation());
      } else {
        clean_eof = false;
        break;
      }
    }

    if (!clean_eof) {
      VLOG(1)
          << "WARNING: Trying to read from `" << file_name
          << "` encountered a corrupted message. Returning the envelope that "
             "has been read so far.";
      break;
    }
  }

  envelope_read_ = true;
  return envelope_;
}

size_t FileObservationStore::FileEnvelopeHolder::Size() {
  if (cached_file_size_ != 0) {
    return cached_file_size_;
  }

  cached_file_size_ = 0;
  for (const auto &file_name : file_names_) {
    cached_file_size_ += fs_->FileSize(FullPath(file_name)).ConsumeValueOr(0);
  }
  return cached_file_size_;
}

}  // namespace cobalt::encoder
