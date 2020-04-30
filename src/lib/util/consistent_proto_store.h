// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LIB_UTIL_CONSISTENT_PROTO_STORE_H_
#define COBALT_SRC_LIB_UTIL_CONSISTENT_PROTO_STORE_H_

#include <memory>
#include <string>

#include <google/protobuf/message_lite.h>

#include "src/lib/util/file_system.h"
#include "src/lib/util/status.h"

namespace cobalt {
namespace util {

// ConsistentProtoStore is a persistent store of a single protocol buffer
// message that guarantees consistent updates.
class ConsistentProtoStore {
 public:
  // Constructs a ConsistentProtoStore
  //
  // |filename| the fully qualified path of the file to store data in.
  // |fs| is used for detecting the presence of, renaming, and deleting files.
  explicit ConsistentProtoStore(std::string filename, FileSystem *fs);

  // Alternate constructor, does not take a filename.
  //
  // If the ConsistentProtoStore is constructed this way, then the user must provide the filename
  // when calling 'Write' or 'Read'.
  //
  // |fs| is used for detecting the presence of, renaming, and deleting files.
  explicit ConsistentProtoStore(FileSystem *fs);

  virtual ~ConsistentProtoStore() = default;

  // Writes |proto| to the store, overwriting any previously written proto.
  // Consistency is guaranteed in that if the operation fails, the previously
  // written proto will not have been corrupted and may be read via the Read()
  // method.
  virtual Status Write(const google::protobuf::MessageLite &proto);
  virtual Status Write(const std::string &filename, const google::protobuf::MessageLite &proto);

  // Reads the previously written proto into |proto|.
  //
  // A failure either means that no proto has ever been written, or that the
  // data is corrupt (does not represent a valid protocol buffer).
  virtual Status Read(google::protobuf::MessageLite *proto);
  virtual Status Read(const std::string &filename, google::protobuf::MessageLite *proto);

 private:
  friend class TestConsistentProtoStore;

  virtual Status WriteToTmp(const std::string &tmp_filename,
                            const google::protobuf::MessageLite &proto);
  virtual Status MoveTmpToOverride(const std::string &tmp_filename,
                                   const std::string &override_filename);
  virtual Status DeletePrimary(const std::string &primary_filename);
  virtual Status MoveOverrideToPrimary(const std::string &override_filename,
                                       const std::string &primary_filename);

  // Primary filename is the base filename used for all operations. It is the
  // filename that is passed into the constructor.
  const std::string primary_filename_;
  FileSystem *fs_;
};

}  // namespace util
}  // namespace cobalt

#endif  // COBALT_SRC_LIB_UTIL_CONSISTENT_PROTO_STORE_H_
