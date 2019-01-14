// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_FILE_OBSERVATION_STORE_H_
#define COBALT_ENCODER_FILE_OBSERVATION_STORE_H_

#include <deque>
#include <fstream>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "encoder/envelope_maker.h"
#include "encoder/observation_store.h"
#include "third_party/protobuf/src/google/protobuf/io/zero_copy_stream_impl.h"
#include "third_party/tensorflow_statusor/statusor.h"
#include "util/file_system.h"
#include "util/protected_fields.h"

namespace cobalt {
namespace encoder {

// FileObservationStore is an implementation of ObservationStore that persists
// observations to a file system.
//
// The store returns FileEnvelopeHolders from calls to TakeNextEnvelopeHolder().
// As long as there are FileEnvelopeHolders that have not been returned or
// deleted, the store should not be destroyed.
//
// This object is thread safe.
class FileObservationStore : public ObservationStore {
 public:
  // FileEnvelopeHolder is an implementation of
  // ObservationStore::EnvelopeHolder.
  //
  // It represents the envelope as a list of filenames. The observations are not
  // actually read into memory until a call to GetEnvelope() is made.
  //
  // Note: This object is not thread safe.
  class FileEnvelopeHolder : public EnvelopeHolder {
   public:
    // |fs|. An implementation of FileSystem used to interact with the system's
    // filesystem.
    //
    // |root_directory|. The absolute path to the directory where the
    // observation files are written. (e.g. /system/data/cobalt_legacy)
    //
    // |file_name|. The file name for the file containing the observations.
    FileEnvelopeHolder(util::FileSystem *fs, const std::string &root_directory,
                       const std::string &file_name)
        : fs_(fs),
          root_directory_(root_directory),
          file_names_({file_name}),
          envelope_read_(false) {}

    ~FileEnvelopeHolder();

    void MergeWith(std::unique_ptr<EnvelopeHolder> container) override;
    const Envelope &GetEnvelope() override;
    size_t Size() override;
    const std::set<std::string> &file_names() { return file_names_; }
    void clear() { file_names_.clear(); }

   private:
    std::string FullPath(const std::string &filename) const;

    util::FileSystem *fs_;
    const std::string root_directory_;

    // file_names contains a set of file names that contain observations.
    // These files should all be read into |envelope| when GetEnvelope is
    // called.
    std::set<std::string> file_names_;
    bool envelope_read_;
    Envelope envelope_;
    size_t cached_file_size_ = 0;
  };

  // |fs|. An implementation of FileSystem used to interact with the system's
  // filesystem.
  //
  // |root_directory|. The absolute path to the directory where the observation
  // files should be written. (e.g. /system/data/cobalt_legacy)
  //
  // |name| is used in log messages to distinguish this instance of
  // FileObservationStore.
  FileObservationStore(size_t max_bytes_per_observation,
                       size_t max_bytes_per_envelope, size_t max_bytes_total,
                       std::unique_ptr<util::FileSystem> fs,
                       std::string root_directory,
                       std::string name = "FileObservationStore");

  StoreStatus AddEncryptedObservation(
      std::unique_ptr<EncryptedMessage> message,
      std::unique_ptr<ObservationMetadata> metadata) override;
  std::unique_ptr<EnvelopeHolder> TakeNextEnvelopeHolder() override;
  void ReturnEnvelopeHolder(std::unique_ptr<EnvelopeHolder> envelopes) override;

  size_t Size() const override;
  bool Empty() const override;

  // Delete removes all of the files associated with this FileObservationStore.
  // This is useful for cleaning up after testing.
  void Delete();

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
    std::ofstream active_fstream;
    std::unique_ptr<google::protobuf::io::OstreamOutputStream> active_file;
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
  tensorflow_statusor::StatusOr<std::string> GetOldestFinalizedFile(
      util::ProtectedFields<Fields>::LockedFieldsPtr *fields);

  // GenerateFinalizedName returns an absolute path that can be used for
  // finalizing a file. It is based on the current timestamp and a random number
  // to avoid collisions.
  std::string GenerateFinalizedName() const;

  // FullPath returns the absolute path to the filename by prefixing the file
  // name with the root directory.
  std::string FullPath(const std::string &filename) const;

  bool FinalizeActiveFile(
      util::ProtectedFields<Fields>::LockedFieldsPtr *fields);

  // GetActiveFile returns a pointer to the current OstreamOutputStream. If the
  // file is not yet opened, it will be opened by this function.
  google::protobuf::io::OstreamOutputStream *GetActiveFile(
      util::ProtectedFields<Fields>::LockedFieldsPtr *fields);

  const std::unique_ptr<util::FileSystem> fs_;
  const std::string root_directory_;
  const std::string active_file_name_;
  mutable std::random_device random_dev_;
  mutable std::uniform_int_distribution<uint32_t> random_int_;
  const std::string name_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_FILE_OBSERVATION_STORE_H_
