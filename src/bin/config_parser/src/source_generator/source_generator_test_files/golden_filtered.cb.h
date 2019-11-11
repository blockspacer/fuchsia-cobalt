// This file was generated by Cobalt's Config parser based on the configuration
// YAML in the cobalt_config repository. Edit the YAML there to make changes.
#ifndef COBALT_REGISTRY_CUSTOMER_PROJECT_GEN_
#define COBALT_REGISTRY_CUSTOMER_PROJECT_GEN_

#include <cstdint>

const char kCustomerName[] = "customer";
const char kProjectName[] = "project";

// Linear bucket constants for linear buckets
const int64_t kLinearBucketsIntBucketsFloor = 0;
const uint32_t kLinearBucketsIntBucketsNumBuckets = 140;
const uint32_t kLinearBucketsIntBucketsStepSize = 5;

// Exponential bucket constants for exponential buckets report
const int64_t kExponentialBucketsReportIntBucketsFloor = 0;
const uint32_t kExponentialBucketsReportIntBucketsNumBuckets = 3;
const uint32_t kExponentialBucketsReportIntBucketsInitialStep = 2;
const uint32_t kExponentialBucketsReportIntBucketsStepMultiplier = 2;

// Metric ID Constants
// the_metric_name
const uint32_t kTheMetricNameMetricId = 100;
// the_other_metric_name
const uint32_t kTheOtherMetricNameMetricId = 200;
// event groups
const uint32_t kEventGroupsMetricId = 300;
// linear buckets
const uint32_t kLinearBucketsMetricId = 400;
// exponential buckets
const uint32_t kExponentialBucketsMetricId = 500;

// Enum for the_other_metric_name (Metric Dimension 0)
namespace __the_other_metric_name_metric_dimension_0_internal_scope_do_not_use__ {
enum Enum {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
};
}  // __the_other_metric_name_metric_dimension_0_internal_scope_do_not_use__
using TheOtherMetricNameMetricDimension0 = __the_other_metric_name_metric_dimension_0_internal_scope_do_not_use__::Enum;
const TheOtherMetricNameMetricDimension0 TheOtherMetricNameMetricDimension0_AnEvent = TheOtherMetricNameMetricDimension0::AnEvent;
const TheOtherMetricNameMetricDimension0 TheOtherMetricNameMetricDimension0_AnotherEvent = TheOtherMetricNameMetricDimension0::AnotherEvent;
const TheOtherMetricNameMetricDimension0 TheOtherMetricNameMetricDimension0_AThirdEvent = TheOtherMetricNameMetricDimension0::AThirdEvent;

// Enum for event groups (Metric Dimension The First Group)
namespace __event_groups_metric_dimension_the_first_group_internal_scope_do_not_use__ {
enum Enum {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
};
}  // __event_groups_metric_dimension_the_first_group_internal_scope_do_not_use__
using EventGroupsMetricDimensionTheFirstGroup = __event_groups_metric_dimension_the_first_group_internal_scope_do_not_use__::Enum;
const EventGroupsMetricDimensionTheFirstGroup EventGroupsMetricDimensionTheFirstGroup_AnEvent = EventGroupsMetricDimensionTheFirstGroup::AnEvent;
const EventGroupsMetricDimensionTheFirstGroup EventGroupsMetricDimensionTheFirstGroup_AnotherEvent = EventGroupsMetricDimensionTheFirstGroup::AnotherEvent;
const EventGroupsMetricDimensionTheFirstGroup EventGroupsMetricDimensionTheFirstGroup_AThirdEvent = EventGroupsMetricDimensionTheFirstGroup::AThirdEvent;

// Enum for event groups (Metric Dimension A second group)
namespace __event_groups_metric_dimension_a_second_group_internal_scope_do_not_use__ {
enum Enum {
  This = 1,
  Is = 2,
  Another = 3,
  Test = 4,
};
}  // __event_groups_metric_dimension_a_second_group_internal_scope_do_not_use__
using EventGroupsMetricDimensionASecondGroup = __event_groups_metric_dimension_a_second_group_internal_scope_do_not_use__::Enum;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_This = EventGroupsMetricDimensionASecondGroup::This;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_Is = EventGroupsMetricDimensionASecondGroup::Is;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_Another = EventGroupsMetricDimensionASecondGroup::Another;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_Test = EventGroupsMetricDimensionASecondGroup::Test;

// Enum for event groups (Metric Dimension 2)
namespace __event_groups_metric_dimension_2_internal_scope_do_not_use__ {
enum Enum {
  ThisMetric = 0,
  HasNo = 2,
  Name = 4,
  Alias = HasNo,
};
}  // __event_groups_metric_dimension_2_internal_scope_do_not_use__
using EventGroupsMetricDimension2 = __event_groups_metric_dimension_2_internal_scope_do_not_use__::Enum;
const EventGroupsMetricDimension2 EventGroupsMetricDimension2_ThisMetric = EventGroupsMetricDimension2::ThisMetric;
const EventGroupsMetricDimension2 EventGroupsMetricDimension2_HasNo = EventGroupsMetricDimension2::HasNo;
const EventGroupsMetricDimension2 EventGroupsMetricDimension2_Name = EventGroupsMetricDimension2::Name;
const EventGroupsMetricDimension2 EventGroupsMetricDimension2_Alias = EventGroupsMetricDimension2::Alias;

// The base64 encoding of the bytes of a serialized CobaltRegistry proto message.
const char kConfig[] = "KqAECghjdXN0b21lchAKGpEECgdwcm9qZWN0EAUaSgoPdGhlX21ldHJpY19uYW1lEAoYBSBkYhUKCnRoZV9yZXBvcnQQu6WL8QgYj05iGgoQdGhlX290aGVyX3JlcG9ydBDK3M3qARgGGnAKFXRoZV9vdGhlcl9tZXRyaWNfbmFtZRAKGAUgyAEoAVABYhQKCnRoZV9yZXBvcnQQu6WL8QgYB4IBNRILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GMgBGusBCgxldmVudCBncm91cHMQChgFIKwCKAFQAWIUCgp0aGVfcmVwb3J0ELuli/EIGAeCAUUKD1RoZSBGaXJzdCBHcm91cBILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GAKCATkKDkEgc2Vjb25kIGdyb3VwEggIARIEVGhpcxIGCAISAklzEgsIAxIHYW5vdGhlchIICAQSBFRlc3SCATUSDggAEgpUaGlzTWV0cmljEgkIAhIFSGFzTm8SCAgEEgROYW1lKg4KBUhhc05vEgVBbGlhcxogCg5saW5lYXIgYnVja2V0cxAKGAUgkANCBxIFEIwBGAUaNgoTZXhwb25lbnRpYWwgYnVja2V0cxAKGAUg9ANiGAoGcmVwb3J0ELuFpIMJUggKBhADGAIgAg==";

#endif  // COBALT_REGISTRY_CUSTOMER_PROJECT_GEN_
