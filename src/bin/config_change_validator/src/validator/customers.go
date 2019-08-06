// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package validator

import (
	"config"
	"fmt"
)

func CompareCustomers(oldCfg, newCfg *config.CustomerConfig, ignoreCompatChecks bool) error {
	oldProjects := map[string]*config.ProjectConfig{}
	newProjects := map[string]*config.ProjectConfig{}

	for _, proj := range oldCfg.Projects {
		oldProjects[proj.ProjectName] = proj
	}

	for _, proj := range newCfg.Projects {
		newProjects[proj.ProjectName] = proj
	}

	for name, oldProj := range oldProjects {
		newProj, ok := newProjects[name]
		if ok {
			err := CompareProjects(oldProj, newProj, ignoreCompatChecks)
			if err != nil {
				return fmt.Errorf("for project named '%s': %v", name, err)
			}
		}
	}

	return nil
}
