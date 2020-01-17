// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_OBSERVATION_STORE_FILE_OBSERVATION_STORE_H_
#define COBALT_SRC_OBSERVATION_STORE_FILE_OBSERVATION_STORE_H_

#include <deque>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "google/protobuf/io/zero_copy_stream.h"
#include "src/lib/statusor/statusor.h"
#include "src/lib/util/file_system.h"
#include "src/lib/util/protected_fields.h"
#include "src/logger/internal_metrics.h"
#include "src/observation_store/envelope_maker.h"
#include "src/observation_store/observation_store.h"
#include "third_party/protobuf/src/google/protobuf/io/zero_copy_stream_impl.h"

namespace cobalt::observation_store {

// FileObservationStore is an implementation of ObservationStore that persists
// observations to a file system.
//
// The store returns FileEnvelopeHolders from calls to TakeNextEnvelopeHolder().
// As long as there are FileEnvelopeHolders that have not been returned or
// deleted, the store should not be destroyed.
//
// This object is thread safe.
class FileObservationStore : public ObservationStore {
 private:
  // FileEnvelopeHolder is an implementation of
  // ObservationStore::EnvelopeHolder.
  //
  // It represents the envelope as a list of filenames. The observations are not
  // actually read into memory until a call to GetEnvelope() is made.
  //
  // Note: This object is not thread safe.
  class FileEnvelopeHolder : public EnvelopeHolder {
   public:
    // |fs|. An implementation of FileSystem used to interact with the system's filesystem.
    //
    // |store|. A pointer to the store from which this envelope holder came from.
    //          NOTE: This value cannot be null, and it is expected that the ObservationStore will
    //          outlive the FileEnvelopeHolder object.
    //
    // |root_directory|. The absolute path to the directory where the observation files are written.
    // (e.g. /system/data/cobalt_legacy)
    //
    // |file_name|. The file name for the file containing the observations.
    FileEnvelopeHolder(util::FileSystem *fs, FileObservationStore *store,
                       std::string root_directory, const std::string &file_name)
        : fs_(fs),
          store_(store),
          root_directory_(std::move(root_directory)),
          file_names_({file_name}),
          envelope_read_(false) {
      CHECK(store_);
    }

    ~FileEnvelopeHolder() override;

    void MergeWith(std::unique_ptr<EnvelopeHolder> container) override;
    const Envelope &GetEnvelope(util::EncryptedMessageMaker *encrypter) override;
    size_t Size() override;
    const std::set<std::string> &file_names() { return file_names_; }
    void clear() { file_names_.clear(); }

   private:
    std::string FullPath(const std::string &filename) const;

    util::FileSystem *fs_;
    FileObservationStore *store_;
    const std::string root_directory_;

    // file_names contains a set of file names that contain observations.
    // These files should all be read into |envelope| when GetEnvelope is
    // called.
    std::set<std::string> file_names_;
    bool envelope_read_;
    Envelope envelope_;
    size_t cached_file_size_ = 0;
  };

 public:
  class FilenameGenerator {
   public:
    // Default constructor: Uses std::chrono::system_clock to calculate the
    // unix timestamp.
    FilenameGenerator();

    // Override the default method of calculating the current unix timestamp.
    //
    // |now| A function that returns the current unix timestamp (in milliseconds
    // since the unix epoch).
    explicit FilenameGenerator(std::function<int64_t()> now);

    // GenerateFilename returns a unique filename. It is based on the current
    // timestamp and a random number to avoid collisions.
    std::string GenerateFilename() const;

   private:
    std::function<int64_t()> now_;
    mutable std::random_device random_dev_;
    mutable std::uniform_int_distribution<uint64_t> random_int_;
  };

  // |fs|. An implementation of FileSystem used to interact with the system's
  // filesystem.
  //
  // |root_directory|. The absolute path to the directory where the observation
  // files should be written. (e.g. /system/data/cobalt_legacy)
  //
  // |name| is used in log messages to distinguish this instance of
  // FileObservationStore.
  FileObservationStore(size_t max_bytes_per_observation, size_t max_bytes_per_envelope,
                       size_t max_bytes_total, util::FileSystem *fs, std::string root_directory,
                       std::string name = "FileObservationStore",
                       logger::LoggerInterface *internal_logger = nullptr);

  // DEPRECATED: Use non-owned FileSystem
  FileObservationStore(size_t max_bytes_per_observation, size_t max_bytes_per_envelope,
                       size_t max_bytes_total, std::unique_ptr<util::FileSystem> owned_fs,
                       std::string root_directory, std::string name = "FileObservationStore",
                       logger::LoggerInterface *internal_logger = nullptr)
      : FileObservationStore(max_bytes_per_observation, max_bytes_per_envelope, max_bytes_total,
                             owned_fs.get(), std::move(root_directory), std::move(name),
                             internal_logger) {
    owned_fs_ = std::move(owned_fs);
  }

  using ObservationStore::StoreObservation;
  StoreStatus StoreObservation(std::unique_ptr<StoredObservation> observation,
                               std::unique_ptr<ObservationMetadata> metadata) override;
  std::unique_ptr<EnvelopeHolder> TakeNextEnvelopeHolder() override;
  void ReturnEnvelopeHolder(std::unique_ptr<EnvelopeHolder> envelopes) override;

  size_t Size() const override;
  bool Empty() const override;

  void DeleteData() override;

  void ResetInternalMetrics(logger::LoggerInterface *internal_logger) override {
    internal_metrics_ = logger::InternalMetrics::NewWithLogger(internal_logger);
  }

  // ListFinalizedFiles lists all files in root directory that match the format
  // <13-digit timestamp>-<7 digit random number>.data
  std::vector<std::string> ListFinalizedFiles() const;

 private:
  struct Fields {
    bool metadata_written;
    // last_written_metadata is a string encoding of the last metadata written
    // to the active_file. If another observation comes in with an identical
    // metadata, it is not necessary to write it again.
    std::string last_written_metadata;
    std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> active_file;
    // files_taken lists the filenames that have been "Taken" from the store.
    // These should not be used to construct EnvelopeHolders for
    // TakeNextEnvelopeHolder(). If an EnvelopeHolder is returned, the
    // associated file names should also be removed from this list.
    std::set<std::string> files_taken;
    // The total size in bytes of the finalized files. This should be kept up to
    // date as files are added to/removed from the store.
    size_t finalized_bytes;
  };

  util::ProtectedFields<Fields> protected_fields_;

  // GetOldestFinalizedFile returns a file name for the oldest file in the
  // store.
  lib::statusor::StatusOr<std::string> GetOldestFinalizedFile(
      util::ProtectedFields<Fields>::LockedFieldsPtr *fields);

  // FullPath returns the absolute path to the filename by prefixing the file
  // name with the root directory.
  std::string FullPath(const std::string &filename) const;

  bool FinalizeActiveFile(util::ProtectedFields<Fields>::LockedFieldsPtr *fields);

  // GetActiveFile returns a pointer to the current OstreamOutputStream. If the
  // file is not yet opened, it will be opened by this function.
  //
  // If this function is unable to open a file (if the file system is full for
  // example) it will return nullptr.
  google::protobuf::io::ZeroCopyOutputStream *GetActiveFile(
      util::ProtectedFields<Fields>::LockedFieldsPtr *fields);

  std::unique_ptr<util::FileSystem> owned_fs_;
  util::FileSystem *fs_;
  const std::string root_directory_;
  const std::string active_file_name_;
  const std::string name_;
  FilenameGenerator filename_generator_;

  std::unique_ptr<logger::InternalMetrics> internal_metrics_;
};

}  // namespace cobalt::observation_store

#endif  // COBALT_SRC_OBSERVATION_STORE_FILE_OBSERVATION_STORE_H_
