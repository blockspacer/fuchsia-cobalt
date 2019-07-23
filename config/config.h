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

#ifndef COBALT_CONFIG_CONFIG_H_
#define COBALT_CONFIG_CONFIG_H_

#include <fcntl.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <google/protobuf/io/tokenizer.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

namespace cobalt {
namespace config {

enum Status {
  kOK = 0,

  // The specified file could not be opened.
  kFileOpenError = 1,

  // The specified file could not be parsed as the appropriate type
  // of protocol message.
  kParsingError = 2,

  // The specified file could be parsed but it contained two different
  // objects with the same fully-qualified ID.
  kDuplicateRegistration = 3
};

// A template for a Registry.
//
// RT should be one of:
// RegisteredEncodings, RegisteredReports, RegisteredMetrics.
//
// We then define |T| to be the corresponding one of:
// EncodingConfig, ReportConfig, Metric.
//
// A Registry<RT> is then a container for all of the |T|s registered in Cobalt.
template <class RT>
class Registry {
 public:
  // This is some template meta-programming magic that has the effect of
  // defining |T| to be the type of the objects contained in a registry
  // of type |RT|.
  using T =
      typename std::remove_pointer<decltype((reinterpret_cast<RT*>(0))->mutable_element(0))>::type;

 private:
  // The container for the registry.  The keys in this map are strings that
  // encode ID triples of the form (customer_id, project_id, id).
  using Map = std::unordered_map<std::string, std::unique_ptr<T>>;

 public:
  // Iterator for going through registry items.  We just use the Map iterator
  // but return only values (without keys).
  class RegistryIterator {
   public:
    explicit RegistryIterator(const typename Map::iterator& iter) : iter_(iter) {}

    const T& operator*() const { return *iter_->second; }

    bool operator!=(const RegistryIterator& rhs) const { return iter_ != rhs.iter_; }

    const RegistryIterator& operator++() {
      ++iter_;
      return *this;
    }

   private:
    typename Map::iterator iter_;
  };
  using iterator = RegistryIterator;

  // Populates a new instance of Registry<RT> by swapping the contents out of
  // of |registered_configs|. Returns a pair consisting of a pointer to the
  // result and a Status.
  //
  // If the operation is successful then the status is kOK. Otherwise the
  // Status indicates the error.
  //
  // If |error_collector| is not null then it will be notified of any parsing
  // errors or warnings.
  static std::pair<std::unique_ptr<Registry<RT>>, Status> TakeFrom(
      RT* registered_configs, google::protobuf::io::ErrorCollector* error_collector);

  // Returns the number of |T| in this registry.
  size_t size();

  // Returns the |T| with the given ID triple, or nullptr if there is
  // no such |T|. The caller does not take ownership of the returned
  // pointer.
  [[nodiscard]] const T* Get(uint32_t customer_id, uint32_t project_id, uint32_t id) const {
    auto iterator = map_.find(MakeKey(customer_id, project_id, id));
    if (iterator == map_.end()) {
      return nullptr;
    }
    return iterator->second.get();
  }

  [[nodiscard]] const T* Get(uint32_t customer_id, uint32_t project_id,
                             const std::string& name) const {
    auto iterator = name_map_.find(MakeKey(customer_id, project_id, name));
    if (iterator == name_map_.end()) {
      return nullptr;
    }
    return iterator->second.get();
  }

  // Provide a mechansim to iterate through all registry items.
  RegistryIterator begin() { return RegistryIterator(map_.begin()); }
  RegistryIterator end() { return RegistryIterator(map_.end()); }

 private:
  // Builds a map key that encodes the triple (customer_id, project_id, id)
  static std::string MakeKey(uint32_t customer_id, uint32_t project_id, uint32_t id);

  // Builds a map key that encodes the triple (customer_id, project_id, name)
  static std::string MakeKey(uint32_t customer_id, uint32_t project_id, const std::string& name);

  // Builds a map key from the ids in |config_proto|.
  static std::string MakeKeyWithId(const T& config_proto);

  static std::string MakeKeyWithName(const T& config_proto);

  // The keys in this map are strings that encode ID triples of the form
  // (customer_id, project_id, id)
  Map map_;

  // The keys in this map are strings that encode triples of the form
  // (customer_id, project_id, name)
  Map name_map_;
};

//////////////////////////////////////////////////////////////////
/// IMPLEMENTATION BELOW
//////////////////////////////////////////////////////////////////

// TODO(zmbush): Find a less clunky way of writing the MakeKey functions.
template <class RT>
std::string Registry<RT>::MakeKey(uint32_t customer_id, uint32_t project_id, uint32_t id) {
  // Three 32-bit positive ints (at most 10 digits each) plus 3 colons plus a
  // trailing null is <= 34 bytes.
  char out[34];  // NOLINT readability-magic-numbers
  int size = snprintf(out, sizeof(out), "%u:%u:%u", customer_id, project_id, id);
  if (size <= 0) {
    return "";
  }
  return std::string(out, size);
}

template <class RT>
std::string Registry<RT>::MakeKey(uint32_t customer_id, uint32_t project_id,
                                  const std::string& name) {
  // Two 32-bit positive ints (at most 10 digits each) plus 2 colons plus a
  // trailing null is <= 23 bytes.
  char out[23];  // NOLINT readability-magic-numbers
  int size = snprintf(out, sizeof(out), "%u:%u:", customer_id, project_id);
  if (size <= 0) {
    return "";
  }
  return std::string(out, size) + name;
}

template <class RT>
std::string Registry<RT>::MakeKeyWithId(const T& config_proto) {
  return MakeKey(config_proto.customer_id(), config_proto.project_id(), config_proto.id());
}

template <class RT>
std::string Registry<RT>::MakeKeyWithName(const T& config_proto) {
  return MakeKey(config_proto.customer_id(), config_proto.project_id(), config_proto.name());
}

template <class RT>
std::pair<std::unique_ptr<Registry<RT>>, Status> Registry<RT>::TakeFrom(
    RT* registered_configs, google::protobuf::io::ErrorCollector* /*error_collector*/) {
  // Make an empty registry to return;
  std::unique_ptr<Registry<RT>> registry(new Registry<RT>());

  // Put all of the T's into the map, ensuring that the id triples
  // are unique.
  int num_configs = registered_configs->element_size();
  for (int i = 0; i < num_configs; i++) {
    T* config_proto = registered_configs->mutable_element(i);
    // First build the key and insert an empty Tg into the map
    // at that key.
    auto pair = registry->map_.insert(
        std::make_pair(MakeKeyWithId(*config_proto), std::unique_ptr<T>(new T())));
    const bool& success = pair.second;
    auto& inserted_pair = pair.first;
    if (!success) {
      return std::make_pair(std::move(registry), kDuplicateRegistration);
    }
    // Then swap in the data from the T and populate the name map;
    auto name_pair = registry->name_map_.insert(
        std::make_pair(MakeKeyWithName(*config_proto), std::unique_ptr<T>(new T())));
    name_pair.first->second->CopyFrom(*config_proto);
    inserted_pair->second->Swap(config_proto);
  }
  return std::make_pair(std::move(registry), kOK);
}

template <class RT>
size_t Registry<RT>::size() {
  return map_.size();
}

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_CONFIG_H_
