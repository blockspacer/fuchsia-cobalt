# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(ALL_REPORT_TYPES_TEST_REGISTRY_H "${CMAKE_CURRENT_BINARY_DIR}/all_report_types_test_registry.cb.h")
generate_test_config_h(${CMAKE_CURRENT_SOURCE_DIR}/all_report_types_test_registry.yaml ${ALL_REPORT_TYPES_TEST_REGISTRY_H} 1 1 1 "cobalt.logger.testing.all_report_types" "cobalt_registry_base64")

set(MIXED_TIME_ZONE_TEST_REGISTRY_H "${CMAKE_CURRENT_BINARY_DIR}/mixed_time_zone_test_registry.cb.h")
generate_test_config_h(${CMAKE_CURRENT_SOURCE_DIR}/mixed_time_zone_test_registry.yaml ${MIXED_TIME_ZONE_TEST_REGISTRY_H} 1 1 1 "cobalt.logger.testing.mixed_time_zone" "cobalt_registry_base64")

set(PER_DEVICE_NUMERIC_STATS_TEST_REGISTRY_H "${CMAKE_CURRENT_BINARY_DIR}/per_device_numeric_stats_test_registry.cb.h")
generate_test_config_h(${CMAKE_CURRENT_SOURCE_DIR}/per_device_numeric_stats_test_registry.yaml ${PER_DEVICE_NUMERIC_STATS_TEST_REGISTRY_H} 1 1 1 "cobalt.logger.testing.per_device_numeric_stats" "cobalt_registry_base64")

set(UNIQUE_ACTIVES_TEST_REGISTRY_H "${CMAKE_CURRENT_BINARY_DIR}/unique_actives_test_registry.cb.h")
generate_test_config_h(${CMAKE_CURRENT_SOURCE_DIR}/unique_actives_test_registry.yaml
  ${UNIQUE_ACTIVES_TEST_REGISTRY_H} 1 1 1 "cobalt.logger.testing.unique_actives" "cobalt_registry_base64")

set(UNIQUE_ACTIVES_NOISE_FREE_TEST_REGISTRY_H "${CMAKE_CURRENT_BINARY_DIR}/unique_actives_noise_free_test_registry.cb.h")
generate_test_config_h(${CMAKE_CURRENT_SOURCE_DIR}/unique_actives_noise_free_test_registry.yaml
  ${UNIQUE_ACTIVES_NOISE_FREE_TEST_REGISTRY_H} 1 1 1 "cobalt.logger.testing.unique_actives_noise_free" "cobalt_registry_base64")


add_library(test_registries
            ${ALL_REPORT_TYPES_TEST_REGISTRY_H}
            ${MIXED_TIME_ZONE_TEST_REGISTRY_H}
	    ${PER_DEVICE_NUMERIC_STATS_TEST_REGISTRY_H}
            ${UNIQUE_ACTIVES_TEST_REGISTRY_H}
            ${UNIQUE_ACTIVES_NOISE_FREE_TEST_REGISTRY_H})
set_target_properties(test_registries PROPERTIES LINKER_LANGUAGE CXX)
