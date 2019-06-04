// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package validator

import (
	"config"
	"fmt"
)

func CompareRegistries(oldRegistry, newRegistry *config.CobaltRegistry, ignoreCompatChecks bool) error {
	oldCustomers := map[string]*config.CustomerConfig{}
	newCustomers := map[string]*config.CustomerConfig{}

	for _, cust := range oldRegistry.Customers {
		oldCustomers[cust.CustomerName] = cust
	}

	for _, cust := range newRegistry.Customers {
		newCustomers[cust.CustomerName] = cust
	}

	for name, oldCust := range oldCustomers {
		newCust, ok := newCustomers[name]
		if ok {
			err := CompareCustomers(oldCust, newCust, ignoreCompatChecks)
			if err != nil {
				return fmt.Errorf("for customer named '%s': %v", name, err)
			}
		}
	}

	return nil
}
