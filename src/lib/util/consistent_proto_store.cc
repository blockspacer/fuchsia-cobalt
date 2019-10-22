// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/util/consistent_proto_store.h"

#include <iostream>
#include <utility>

#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "src/lib/statusor/status_macros.h"

namespace cobalt::util {

using google::protobuf::MessageLite;

constexpr char kTmpSuffix[] = ".tmp";
constexpr char kOverrideSuffix[] = ".override";

ConsistentProtoStore::ConsistentProtoStore(std::string filename, std::unique_ptr<FileSystem> fs)
    : primary_file_(std::move(filename)),
      tmp_file_(primary_file_ + kTmpSuffix),
      override_file_(primary_file_ + kOverrideSuffix),
      fs_(std::move(fs)) {}

// Write uses a series of operations to write new data. The goal of each of
// these operations is that if the operation is interrupted or fails, the
// store will still be valid.
//
// 1. Ensure a valid state of the files
//    - Check if a file exists at override_file_. A file here indicates an
//      interrupted Write operation, so it is cleaned up by attempting to
//      delete the file at primary_file_, and rename override_file_ to
//      primary_file_. (This is equivalent performing steps 4/5 which were not
//      completed after the previous write)
//
// 2. Write to a temporary file (tmp_file_)
//    - Writing to a file is not atomic, but this temp file is guaranteed
//      never to be read, so if the write is not finished, it is not an issue.
//
// 3. Rename the temp file to override_file_
//    - Renames are as close to an atomic operation you can get in a
//      filesystem. If the rename fails, nothing changes, but if it succeeds,
//      then the store will immediately start reading from this file.
//
// 4. Delete original file (at primary_file_)
//    - Since the Read operation has switched to using the file at
//      override_file_ for read operations, it is safe to delete the
//      original file.
//
// 5. Rename the file at override_file_ to primary_file_
//    - Again, this is a rename so essentially atomic. If this succeeds, then
//      Read operations should immediately switch to using this file. If it
//      fails, the file should still exist at override_file_ so the store
//      should continue reading it successfully.
//
Status ConsistentProtoStore::Write(const MessageLite &proto) {
  // Check if override_file_ exists
  {
    if (fs_->FileExists(override_file_)) {
      DeletePrimary();  // Ignore errors since primary_file_ might not exist
      auto status = MoveOverrideToPrimary();
      if (!status.ok()) {
        // If renaming override_file_ to primary_file_ fails, we should bail
        // since a write operation is not safe.
        return Status(status.error_code(), "Error during recovery: " + status.error_message(),
                      status.error_details());
      }
    }
  }

  RETURN_IF_ERROR(WriteToTmp(proto));
  RETURN_IF_ERROR(MoveTmpToOverride());
  RETURN_IF_ERROR(DeletePrimary());
  RETURN_IF_ERROR(MoveOverrideToPrimary());

  return Status::OK;
}

Status ConsistentProtoStore::Read(MessageLite *proto) {
  {
    auto istream_or = fs_->NewProtoInputStream(override_file_);
    if (istream_or.ok()) {
      auto istream = istream_or.ConsumeValueOrDie();
      if (proto->ParseFromZeroCopyStream(istream.get())) {
        return Status::OK;
      }
    }
  }

  CB_ASSIGN_OR_RETURN(auto istream, fs_->NewProtoInputStream(primary_file_));
  if (!proto->ParseFromZeroCopyStream(istream.get())) {
    return Status(StatusCode::INVALID_ARGUMENT,
                  "Unable to parse the protobuf from the store. Data is corrupt.");
  }

  return Status::OK;
}

Status ConsistentProtoStore::WriteToTmp(const MessageLite &proto) {
  CB_ASSIGN_OR_RETURN(auto outstream, fs_->NewProtoOutputStream(tmp_file_));
  if (!proto.SerializeToZeroCopyStream(outstream.get())) {
    return Status(StatusCode::DATA_LOSS, "Unable to serialize proto to the output stream.");
  }
  return Status::OK;
}

Status ConsistentProtoStore::MoveTmpToOverride() {
  if (!fs_->Rename(tmp_file_, override_file_)) {
    return Status(StatusCode::DATA_LOSS,
                  "Unable to rename `" + tmp_file_ + "` => `" + override_file_ + "`.",
                  strerror(errno));
  }

  return Status::OK;
}

Status ConsistentProtoStore::DeletePrimary() {
  // If the primary file doesn't exist. We don't care.
  if (!fs_->FileExists(primary_file_)) {
    return Status::OK;
  }

  if (!fs_->Delete(primary_file_)) {
    return Status(StatusCode::ABORTED, "Unable to remove old file `" + primary_file_ + "`.",
                  strerror(errno));
  }

  return Status::OK;
}

Status ConsistentProtoStore::MoveOverrideToPrimary() {
  if (!fs_->Rename(override_file_, primary_file_)) {
    return Status(StatusCode::ABORTED,
                  "Unable to rename `" + override_file_ + "` => `" + primary_file_ + "`.",
                  strerror(errno));
  }

  return Status::OK;
}

}  // namespace cobalt::util
