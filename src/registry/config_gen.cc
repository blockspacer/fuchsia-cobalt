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

// The purpose of this utility is to generate the text representations of
// the Cobalt configuration protocol buffer message. It is not part of
// the production cobalt system and it is not part of any automated unit
// test.  The intended use is to aid in the understanding of what the
// text format of protocol buffer messages look like in order to facilitate
// the manual editing of cobalt registration files. This
// program writes its output to the console. An operator may edit this
// file to add additional messages in order to see what their text format
// looks like.

#include <iostream>

#include <google/protobuf/text_format.h>

#include "src/registry/encoding_config.pb.h"

using cobalt::EncodingConfig;
using cobalt::ForculusConfig;
using cobalt::MetricPart;
using cobalt::RegisteredEncodings;
using cobalt::RegisteredMetrics;
using google::protobuf::TextFormat;

int main(int argc, char* argv[]) {
  std::string out;
  RegisteredEncodings registered_encodings;
  RegisteredMetrics registered_metrics;

  std::cout << out;
  std::cout << "------------------------------------\n";
  std::cout << "Encodings:\n";
  std::cout << "------------------------------------\n";

  // (1, 1, 1) Forculus 20 with WEEK epoch
  auto* encoding_config = registered_encodings.add_element();
  encoding_config->set_customer_id(1);
  encoding_config->set_project_id(1);
  encoding_config->set_id(1);
  auto* forculus_config = encoding_config->mutable_forculus();
  forculus_config->set_threshold(20);
  forculus_config->set_epoch_type(cobalt::WEEK);

  // (1, 1, 2) RAPPOR
  encoding_config = registered_encodings.add_element();
  encoding_config->set_customer_id(1);
  encoding_config->set_project_id(1);
  encoding_config->set_id(2);
  auto* rappor_config = encoding_config->mutable_rappor();
  rappor_config->set_num_bloom_bits(64);
  rappor_config->set_num_hashes(2);
  rappor_config->set_num_cohorts(100);
  rappor_config->set_prob_0_becomes_1(0.2);
  rappor_config->set_prob_1_stays_1(0.8);

  // (2, 1, 1) Basic RAPPOR
  encoding_config = registered_encodings.add_element();
  encoding_config->set_customer_id(2);
  encoding_config->set_project_id(1);
  encoding_config->set_id(1);
  auto* basic_rappor_config = encoding_config->mutable_basic_rappor();
  basic_rappor_config->set_prob_0_becomes_1(0.1);
  basic_rappor_config->set_prob_1_stays_1(0.9);
  basic_rappor_config->mutable_string_categories()->add_category("dog");
  basic_rappor_config->mutable_string_categories()->add_category("cat");
  basic_rappor_config->mutable_string_categories()->add_category("fish");

  // (2, 1, 2) Forculus 50 with DAY epoch
  encoding_config = registered_encodings.add_element();
  encoding_config->set_customer_id(2);
  encoding_config->set_project_id(1);
  encoding_config->set_id(2);
  forculus_config = encoding_config->mutable_forculus();
  forculus_config->set_threshold(50);
  forculus_config->set_epoch_type(cobalt::DAY);

  TextFormat::PrintToString(registered_encodings, &out);
  std::cout << out;
  std::cout << "------------------------------------\n";
  std::cout << "Metrics:\n";
  std::cout << "------------------------------------\n";

  auto* metric_config = registered_metrics.add_element();
  // (1, 1, 1) Fuchsia Usage and Rating
  metric_config->set_customer_id(1);
  metric_config->set_project_id(1);
  metric_config->set_id(1);
  metric_config->set_name("Fuchsia Usage and Rating");
  // City part
  MetricPart city_part;
  city_part.set_description("The name of a city");
  city_part.set_data_type(MetricPart::STRING);
  (*metric_config->mutable_parts())["city"] = city_part;
  // Rating part
  MetricPart rating_part;
  rating_part.set_description("An integer from 0 to 10");
  rating_part.set_data_type(MetricPart::INT);
  (*metric_config->mutable_parts())["rating"] = rating_part;

  TextFormat::PrintToString(registered_metrics, &out);
  std::cout << out;

  exit(0);
}
