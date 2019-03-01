// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_PROJECT_CONTEXT_FACTORY_H_
#define COBALT_LOGGER_PROJECT_CONTEXT_FACTORY_H_

#include <memory>
#include <string>

#include "config/metric_definition.pb.h"
#include "config/project.pb.h"
#include "config/project_configs.h"
#include "encoder/project_context.h"
#include "logger/project_context.h"

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
// - A single Cobalt 0.1 project.
// - A single Cobalt 1.0 project.
// - Multiple projects of either type.
//
// Invoke one of the status methods is_valid(), is_single_project()
// is_single_legacy_project() to determine which state the factory's
// CobaltRegistry is in.
//
// There are two different classes named "ProjectContext".
// logger::ProjectContext is for Cobalt 1.0 and encoder::ProjectContext is
// for Cobalt 0.1. A ProjectContextFactory can create both types of
// ProjectContext.
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
  // Constructs a ProjectContextFactory whose CobaltRegistry is obtained
  // by parsing |cobalt_registry_bytes|. Invoke is_valid() to determine
  // if the parsing succeeded.
  explicit ProjectContextFactory(const std::string& cobalt_registry_bytes);

  // Returns true if the factory's CobaltRegistry exists (meaning we were
  // able to parse the  |cobalt_regsitry_bytes| passed to the constructor)
  // and is non-empty.
  bool is_valid() {
    return project_configs_ != nullptr || client_config_ != nullptr;
  }

  // Returns true if the factory's CobaltRegistry is valid and contains
  // a single project and that is a Cobalt 1.0 project.
  bool is_single_project() {
    return client_config_ == nullptr && project_configs_ != nullptr &&
           project_configs_->is_single_project();
  }

  // Return true if the factory's CobaltRegistry is valid and contains
  // a single project and that is a Cobalt 0.1 project.
  bool is_single_legacy_project() {
    return project_configs_ == nullptr && client_config_ != nullptr &&
           client_config_->is_single_project();
  }

  // Returns a ProjectContext for the Cobalt 1.0 project with the given
  // (customer_name, project_name), if the factory's CobaltRegistry is valid and
  // contains that project. The ProjectContext will be marked as being for a
  // client at the given |release_stage|. Returns nullptr otherwise.
  //
  // Important: The returned ProjectContext contains a pointer into this
  // factory's CobaltRegistry. This ProjectContextFactory must remain alive as
  // long as the returned ProjectContext is being used.
  std::unique_ptr<ProjectContext> NewProjectContext(
      std::string customer_name, std::string project_name,
      ReleaseStage release_stage = GA);

  // If is_single_project() is true, then this returns a
  // ProjectContext for the unique Cobalt 1.0 project contained in the factory's
  // CobaltRegistry and removes the corresponding data from the registry,
  // leaving this ProjectContextFactory invalid. The returned ProjectContext
  // will be marked as being for a client at the given |release_stage|.
  // Returns nullptr otherwise.
  //
  // Note: The returned ProjectContext does *not* contain a pointer into
  // this factory's CobaltRegistry because the appropriate data is removed
  // from the CobaltRegistry and is now owned by the ProjectContext. If nullptr
  // is not returned then this ProjectContextFactory becomes invalid and
  // shoud be discarded.
  std::unique_ptr<ProjectContext> TakeSingleProjectContext(
      ReleaseStage release_stage = GA);

  // Returns a ProjectContext for the Cobalt 0.1 project with the given
  // (customer_id, project_id), if the factory's CobaltRegistry is valid and
  // contains any Cobalt 0.1 data.  Returns nullptr otherwise. Note that
  // we do not check whether or not there actually are any metrics in the
  // CobaltRegistry for the specified project so the returned ProjectContext
  // may be non-null but still empty.
  //
  // Note: The returned ProjectContext contains a shared pointer into
  // this factory's CobaltRegistry. It does not matter whether or not this
  // ProjectContextFactory remains alive.
  std::unique_ptr<encoder::ProjectContext> NewLegacyProjectContext(
      uint32_t customer_id, uint32_t project_id);

  // If is_single_legacy_project() is true, returns a ProjectContext for the
  // unique Cobalt 0.1 project contained in the factory's CobaltRegistry.
  // Returns nullptr otherwise.
  //
  // Note: The returned ProjectContext contains a shared pointer into
  // this factory's CobaltRegistry. It does not matter whether or not this
  // ProjectContextFactory remains alive.
  std::unique_ptr<encoder::ProjectContext> NewSingleLegacyProjectContext();

 private:
  // If not null, then this is a wrapper for the Cobalt 1.0 registry.
  std::unique_ptr<config::ProjectConfigs> project_configs_;

  // If not null, then this is a wrapper for the Cobalt 0.1 registry.
  std::shared_ptr<config::ClientConfig> client_config_;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_PROJECT_CONTEXT_FACTORY_H_
