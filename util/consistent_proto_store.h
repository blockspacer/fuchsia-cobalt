// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_UTIL_CONSISTENT_PROTO_STORE_H_
#define COBALT_UTIL_CONSISTENT_PROTO_STORE_H_

#include <google/protobuf/message_lite.h>

#include <memory>
#include <string>

#include "util/file_system.h"
#include "util/status.h"

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
  explicit ConsistentProtoStore(std::string filename,
                                std::unique_ptr<FileSystem> fs);

  virtual ~ConsistentProtoStore() = default;

  // Writes |proto| to the store, overwriting any previously written proto.
  // Consistency is guaranteed in that if the operation fails, the previously
  // written proto will not have been corrupted and may be read via the Read()
  // method.
  virtual Status Write(const google::protobuf::MessageLite &proto);

  // Reads the previously written proto into |proto|.
  //
  // A failure either means that no proto has ever been written, or that the
  // data is corrupt (does not represent a valid protocol buffer).
  virtual Status Read(google::protobuf::MessageLite *proto);

 private:
  virtual Status WriteToTmp(const google::protobuf::MessageLite &proto);
  virtual Status MoveTmpToOverride();
  virtual Status DeletePrimary();
  virtual Status MoveOverrideToPrimary();

  // Primary file is the base filename used for all operations. It is the
  // filename that is passed into the constructor.
  const std::string primary_file_;

  // Temporary file name. It will never be used during a Read() operation.
  const std::string tmp_file_;

  // Overrides primary_file_. If there is data in override_file_, it will be
  // read instead of primary_file_.
  const std::string override_file_;

  std::unique_ptr<FileSystem> fs_;
};

}  // namespace util
}  // namespace cobalt

#endif  // COBALT_UTIL_CONSISTENT_PROTO_STORE_H_
