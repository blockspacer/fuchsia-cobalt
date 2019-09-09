// This file was generated by Cobalt's Config parser based on the configuration
// YAML in the cobalt_config repository. Edit the YAML there to make changes.
#ifndef COBALT_REGISTRY_CUSTOMER_PROJECT_NS1_NS2_GEN_
#define COBALT_REGISTRY_CUSTOMER_PROJECT_NS1_NS2_GEN_

#include <cstdint>

namespace ns1::ns2 {
const char kCustomerName[] = "customer";
const char kProjectName[] = "project";
// Metric ID Constants
// the_metric_name
const uint32_t kTheMetricNameMetricId = 100;
// the_other_metric_name
const uint32_t kTheOtherMetricNameMetricId = 200;
// event groups
const uint32_t kEventGroupsMetricId = 300;

// Enum for the_other_metric_name (Metric Dimension 0)
namespace the_other_metric_name_metric_dimension_0_scope {
enum Enum {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
};
}  // the_other_metric_name_metric_dimension_0_scope
using TheOtherMetricNameMetricDimension0 = the_other_metric_name_metric_dimension_0_scope::Enum;
const TheOtherMetricNameMetricDimension0 TheOtherMetricNameMetricDimension0_AnEvent = TheOtherMetricNameMetricDimension0::AnEvent;
const TheOtherMetricNameMetricDimension0 TheOtherMetricNameMetricDimension0_AnotherEvent = TheOtherMetricNameMetricDimension0::AnotherEvent;
const TheOtherMetricNameMetricDimension0 TheOtherMetricNameMetricDimension0_AThirdEvent = TheOtherMetricNameMetricDimension0::AThirdEvent;

// Enum for event groups (Metric Dimension The First Group)
namespace event_groups_metric_dimension_the_first_group_scope {
enum Enum {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
};
}  // event_groups_metric_dimension_the_first_group_scope
using EventGroupsMetricDimensionTheFirstGroup = event_groups_metric_dimension_the_first_group_scope::Enum;
const EventGroupsMetricDimensionTheFirstGroup EventGroupsMetricDimensionTheFirstGroup_AnEvent = EventGroupsMetricDimensionTheFirstGroup::AnEvent;
const EventGroupsMetricDimensionTheFirstGroup EventGroupsMetricDimensionTheFirstGroup_AnotherEvent = EventGroupsMetricDimensionTheFirstGroup::AnotherEvent;
const EventGroupsMetricDimensionTheFirstGroup EventGroupsMetricDimensionTheFirstGroup_AThirdEvent = EventGroupsMetricDimensionTheFirstGroup::AThirdEvent;

// Enum for event groups (Metric Dimension A second group)
namespace event_groups_metric_dimension_a_second_group_scope {
enum Enum {
  This = 1,
  Is = 2,
  Another = 3,
  Test = 4,
};
}  // event_groups_metric_dimension_a_second_group_scope
using EventGroupsMetricDimensionASecondGroup = event_groups_metric_dimension_a_second_group_scope::Enum;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_This = EventGroupsMetricDimensionASecondGroup::This;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_Is = EventGroupsMetricDimensionASecondGroup::Is;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_Another = EventGroupsMetricDimensionASecondGroup::Another;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_Test = EventGroupsMetricDimensionASecondGroup::Test;

// Enum for event groups (Metric Dimension 2)
namespace event_groups_metric_dimension_2_scope {
enum Enum {
  ThisMetric = 0,
  HasNo = 2,
  Name = 4,
  Alias = HasNo,
};
}  // event_groups_metric_dimension_2_scope
using EventGroupsMetricDimension2 = event_groups_metric_dimension_2_scope::Enum;
const EventGroupsMetricDimension2 EventGroupsMetricDimension2_ThisMetric = EventGroupsMetricDimension2::ThisMetric;
const EventGroupsMetricDimension2 EventGroupsMetricDimension2_HasNo = EventGroupsMetricDimension2::HasNo;
const EventGroupsMetricDimension2 EventGroupsMetricDimension2_Name = EventGroupsMetricDimension2::Name;
const EventGroupsMetricDimension2 EventGroupsMetricDimension2_Alias = EventGroupsMetricDimension2::Alias;

// The base64 encoding of the bytes of a serialized CobaltRegistry proto message.
const char kConfig[] = "KoAECghjdXN0b21lchAKGvEDCgdwcm9qZWN0EAUaXQoPdGhlX21ldHJpY19uYW1lEAoYBSBkYhUKCnRoZV9yZXBvcnQQu6WL8QgYj05iGgoQdGhlX290aGVyX3JlcG9ydBDK3M3qARgGcghjdXN0b21lcnoHcHJvamVjdBqDAQoVdGhlX290aGVyX21ldHJpY19uYW1lEAoYBSDIASgBUAFiFAoKdGhlX3JlcG9ydBC7pYvxCBgHcghjdXN0b21lcnoHcHJvamVjdIIBNRILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GMgBGv4BCgxldmVudCBncm91cHMQChgFIKwCKAFQAWIUCgp0aGVfcmVwb3J0ELuli/EIGAdyCGN1c3RvbWVyegdwcm9qZWN0ggFFCg9UaGUgRmlyc3QgR3JvdXASCwgAEgdBbkV2ZW50EhAIARIMQW5vdGhlckV2ZW50EhEIAhINQSB0aGlyZCBldmVudBgCggE5Cg5BIHNlY29uZCBncm91cBIICAESBFRoaXMSBggCEgJJcxILCAMSB2Fub3RoZXISCAgEEgRUZXN0ggE1Eg4IABIKVGhpc01ldHJpYxIJCAISBUhhc05vEggIBBIETmFtZSoOCgVIYXNObxIFQWxpYXM=";
}  // ns1::ns2

#endif  // COBALT_REGISTRY_CUSTOMER_PROJECT_NS1_NS2_GEN_
