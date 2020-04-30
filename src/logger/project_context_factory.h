// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOGGER_PROJECT_CONTEXT_FACTORY_H_
#define COBALT_SRC_LOGGER_PROJECT_CONTEXT_FACTORY_H_

#include <memory>
#include <string>

#include "src/logger/project_context.h"
#include "src/registry/metric_definition.pb.h"
#include "src/registry/project.pb.h"
#include "src/registry/project_configs.h"

namespace cobalt {
namespace logger {

// A ProjectContextFactory is used in a Cobalt client application in order
// to obtain one or more ProjectContexts based on a given a CobaltRegistry.
//
// First construct a ProjectContextFactory by giving it the bytes of
// a serialized CobaltRegistry. The bytes will be deserialized and the
// factory will then have an instance of CobaltRegistry.
//
// The factory's CobaltRegistry may be in one of the following states:
// - Invalid: The bytes could not be successfully deserialized.
// - single-project
// - multi-project
//
// Invoke the status methods is_valid(), is_single_project()
// to determine which state the factory's CobaltRegistry is in.
//
// Depending on which state the factory's CobaltRegistry is in, invoke one
// of the New*() or Take*() methods to retrieve a new ProjectContext().
//
// Important: Pay attention to the notes on each method regarding the
// requirement that this ProjectContextFactory remain alive. In some cases,
// the returned ProjectContext contains a poiner into the CobaltRegistry
// owned by this ProjectContextFactory and so the factory must not
// be destructed until after the ProjectContext is no longer being used.
class ProjectContextFactory {
 public:
  // Constructs and returns an instance of ProjectContextFactory whose
  // CobaltRegistry is obtained by Base64 decoding and then deserializing
  // |cobalt_registry_base64|, which should contain the Base64 encoding of the
  // bytes of a serialized CobaltRegistry.
  //
  // Returns nullptr to indicate that the Base64 decoding failed.
  // If a non-nullptr is returned invoke is_valid() to determine if
  // the parsing succeeded and the resulting CobaltRegistry is valid.
  static std::unique_ptr<ProjectContextFactory> CreateFromCobaltRegistryBase64(
      const std::string& cobalt_registry_base64);

  // Constructs a ProjectContextFactory whose CobaltRegistry is obtained
  // by parsing |cobalt_registry_bytes|. Invoke is_valid() to determine
  // if the parsing succeeded and the resulting CobaltRegistry is valid.
  explicit ProjectContextFactory(const std::string& cobalt_registry_bytes);

  // Constructs a ProjectContextFactory containing the given CobaltRegistry.
  // |cobalt_registry| must not be null.
  // Invoke is_valid() to determine if the CobbaltRegistry is valid
  explicit ProjectContextFactory(std::unique_ptr<CobaltRegistry> cobalt_registry);

  // Returns true if the factory's CobaltRegistry exists (meaning we were
  // able to parse the |cobalt_regsitry_bytes| passed to the constructor)
  // and is non-empty.
  bool is_valid() { return project_configs_ != nullptr && !project_configs_->is_empty(); }

  // Returns true if the factory's CobaltRegistry is valid and contains
  // a single project.
  bool is_single_project() {
    return project_configs_ != nullptr && project_configs_->is_single_project();
  }

  // Returns a ProjectContext for the project with the given
  // (customer_id, project_id), if the factory's CobaltRegistry is valid and
  // contains that project. Returns nullptr otherwise.
  //
  // Important: The returned ProjectContext contains a pointer into this
  // factory's CobaltRegistry. This ProjectContextFactory must remain alive as
  // long as the returned ProjectContext is being used.
  [[nodiscard]] std::unique_ptr<ProjectContext> NewProjectContext(uint32_t customer_id,
                                                                  uint32_t project_id) const;

  // ListProjects returns a list of tuples of the form (customer_id, project_id) for all of the
  // projects in the registry.
  [[nodiscard]] std::vector<std::tuple<uint32_t, uint32_t>> ListProjects() const;

  // If is_single_project() is true, then this returns a
  // ProjectContext for the unique Cobalt 1.0 project contained in the factory's
  // CobaltRegistry and removes the corresponding data from the registry,
  // leaving this ProjectContextFactory invalid. Returns nullptr otherwise.
  //
  // Note: The returned ProjectContext does *not* contain a pointer into
  // this factory's CobaltRegistry because the appropriate data is removed
  // from the CobaltRegistry and is now owned by the ProjectContext. If nullptr
  // is not returned then this ProjectContextFactory becomes invalid and
  // shoud be discarded.
  std::unique_ptr<ProjectContext> TakeSingleProjectContext();

 private:
  // This pointer may be null in case TakeSingleProjectContext() has been
  // invoked.
  std::unique_ptr<config::ProjectConfigs> project_configs_;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_SRC_LOGGER_PROJECT_CONTEXT_FACTORY_H_
