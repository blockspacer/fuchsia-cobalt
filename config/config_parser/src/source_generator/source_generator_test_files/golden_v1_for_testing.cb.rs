// This file was generated by Cobalt's Config parser based on the configuration
// YAML in the cobalt_config repository. Edit the YAML there to make changes.
// Metric ID Constants
// the_metric_name
pub const THE_METRIC_NAME_METRIC_ID = 100;
// the_other_metric_name
pub const THE_OTHER_METRIC_NAME_METRIC_ID = 200;
// event groups
pub const EVENT_GROUPS_METRIC_ID = 300;

// Report ID Constants
// the_other_report
pub const THE_OTHER_REPORT_REPORT_ID = 492006986;
// the_report
pub const THE_REPORT_REPORT_ID = 2384646843;

// Enum for the_other_metric_name (EventCode)
pub enum TheOtherMetricNameEventCode {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
}

// Enum for the_other_metric_name (Metric Dimension 0)
pub enum TheOtherMetricNameMetricDimension0 {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
}

// Enum for event groups (Metric Dimension The First Group)
pub enum EventGroupsMetricDimensionTheFirstGroup {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
}

// Enum for event groups (Metric Dimension A second group)
pub enum EventGroupsMetricDimensionASecondGroup {
  This = 1,
  Is = 2,
  Another = 3,
  Test = 4,
}

// Enum for event groups (Metric Dimension 2)
pub enum EventGroupsMetricDimension2 {
  ThisMetric = 0,
  HasNo = 2,
  Name = 4,
}

// The base64 encoding of the bytes of a serialized CobaltRegistry proto message.
pub const CONFIG: &str = "KqUECghjdXN0b21lchAKGpYECgdwcm9qZWN0EAUaXQoPdGhlX21ldHJpY19uYW1lEAoYBSBkYhUKCnRoZV9yZXBvcnQQu6WL8QgYj05iGgoQdGhlX290aGVyX3JlcG9ydBDK3M3qARgGcghjdXN0b21lcnoHcHJvamVjdBq4AQoVdGhlX290aGVyX21ldHJpY19uYW1lEAoYBSDIASgBMgsIABIHQW5FdmVudDIQCAESDEFub3RoZXJFdmVudDIRCAISDUEgdGhpcmQgZXZlbnQ4yAFQAWIUCgp0aGVfcmVwb3J0ELuli/EIGAdyCGN1c3RvbWVyegdwcm9qZWN0ggE1EgsIABIHQW5FdmVudBIQCAESDEFub3RoZXJFdmVudBIRCAISDUEgdGhpcmQgZXZlbnQYyAEa7gEKDGV2ZW50IGdyb3VwcxAKGAUgrAIoAVABYhQKCnRoZV9yZXBvcnQQu6WL8QgYB3IIY3VzdG9tZXJ6B3Byb2plY3SCAUUKD1RoZSBGaXJzdCBHcm91cBILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GAKCATkKDkEgc2Vjb25kIGdyb3VwEggIARIEVGhpcxIGCAISAklzEgsIAxIHYW5vdGhlchIICAQSBFRlc3SCASUSDggAEgpUaGlzTWV0cmljEgkIAhIFSGFzTm8SCAgEEgROYW1l";
