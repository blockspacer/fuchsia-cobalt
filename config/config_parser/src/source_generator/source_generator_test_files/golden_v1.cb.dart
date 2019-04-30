// This file was generated by Cobalt's Config parser based on the configuration
// YAML in the cobalt_config repository. Edit the YAML there to make changes.
const String customerName = 'customer';
const String projectName = 'project';
// Metric ID Constants
// the_metric_name
// ignore: constant_identifier_names
const int theMetricNameMetricId = 100;
// the_other_metric_name
// ignore: constant_identifier_names
const int theOtherMetricNameMetricId = 200;
// event groups
// ignore: constant_identifier_names
const int eventGroupsMetricId = 300;

// Enum for the_other_metric_name (EventCode)
class TheOtherMetricNameEventCode {
  static const int AnEvent = 0;
  static const int AnotherEvent = 1;
  static const int AThirdEvent = 2;
}
const int TheOtherMetricNameEventCode_AnEvent = TheOtherMetricNameEventCode::AnEvent;
const int TheOtherMetricNameEventCode_AnotherEvent = TheOtherMetricNameEventCode::AnotherEvent;
const int TheOtherMetricNameEventCode_AThirdEvent = TheOtherMetricNameEventCode::AThirdEvent;

// Enum for the_other_metric_name (Metric Dimension 0)
class TheOtherMetricNameMetricDimension0 {
  static const int AnEvent = 0;
  static const int AnotherEvent = 1;
  static const int AThirdEvent = 2;
}
const int TheOtherMetricNameMetricDimension0_AnEvent = TheOtherMetricNameMetricDimension0::AnEvent;
const int TheOtherMetricNameMetricDimension0_AnotherEvent = TheOtherMetricNameMetricDimension0::AnotherEvent;
const int TheOtherMetricNameMetricDimension0_AThirdEvent = TheOtherMetricNameMetricDimension0::AThirdEvent;

// Enum for event groups (Metric Dimension The First Group)
class EventGroupsMetricDimensionTheFirstGroup {
  static const int AnEvent = 0;
  static const int AnotherEvent = 1;
  static const int AThirdEvent = 2;
}
const int EventGroupsMetricDimensionTheFirstGroup_AnEvent = EventGroupsMetricDimensionTheFirstGroup::AnEvent;
const int EventGroupsMetricDimensionTheFirstGroup_AnotherEvent = EventGroupsMetricDimensionTheFirstGroup::AnotherEvent;
const int EventGroupsMetricDimensionTheFirstGroup_AThirdEvent = EventGroupsMetricDimensionTheFirstGroup::AThirdEvent;

// Enum for event groups (Metric Dimension A second group)
class EventGroupsMetricDimensionASecondGroup {
  static const int This = 1;
  static const int Is = 2;
  static const int Another = 3;
  static const int Test = 4;
}
const int EventGroupsMetricDimensionASecondGroup_This = EventGroupsMetricDimensionASecondGroup::This;
const int EventGroupsMetricDimensionASecondGroup_Is = EventGroupsMetricDimensionASecondGroup::Is;
const int EventGroupsMetricDimensionASecondGroup_Another = EventGroupsMetricDimensionASecondGroup::Another;
const int EventGroupsMetricDimensionASecondGroup_Test = EventGroupsMetricDimensionASecondGroup::Test;

// Enum for event groups (Metric Dimension 2)
class EventGroupsMetricDimension2 {
  static const int ThisMetric = 0;
  static const int HasNo = 2;
  static const int Name = 4;
}
const int EventGroupsMetricDimension2_ThisMetric = EventGroupsMetricDimension2::ThisMetric;
const int EventGroupsMetricDimension2_HasNo = EventGroupsMetricDimension2::HasNo;
const int EventGroupsMetricDimension2_Name = EventGroupsMetricDimension2::Name;

// The base64 encoding of the bytes of a serialized CobaltRegistry proto message.
const String config = 'KvIDCghjdXN0b21lchAKGuMDCgdwcm9qZWN0EAUaXQoPdGhlX21ldHJpY19uYW1lEAoYBSBkYhUKCnRoZV9yZXBvcnQQu6WL8QgYj05iGgoQdGhlX290aGVyX3JlcG9ydBDK3M3qARgGcghjdXN0b21lcnoHcHJvamVjdBqFAQoVdGhlX290aGVyX21ldHJpY19uYW1lEAoYBSDIASgBUAFiFAoKdGhlX3JlcG9ydBC7pYvxCBgHcghjdXN0b21lcnoHcHJvamVjdIIBNxILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GMgBIAEa7gEKDGV2ZW50IGdyb3VwcxAKGAUgrAIoAVABYhQKCnRoZV9yZXBvcnQQu6WL8QgYB3IIY3VzdG9tZXJ6B3Byb2plY3SCAUUKD1RoZSBGaXJzdCBHcm91cBILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GAKCATkKDkEgc2Vjb25kIGdyb3VwEggIARIEVGhpcxIGCAISAklzEgsIAxIHYW5vdGhlchIICAQSBFRlc3SCASUSDggAEgpUaGlzTWV0cmljEgkIAhIFSGFzTm8SCAgEEgROYW1l';
