// This file was generated by Cobalt's Config parser based on the configuration
// YAML in the cobalt_config repository. Edit the YAML there to make changes.
pub const CUSTOMER_NAME: &str = "customer";
pub const CUSTOMER_ID: u32 = 10;
pub const PROJECT_NAME: &str = "project";
pub const PROJECT_ID: u32 = 5;

// Linear bucket constants for linear buckets
pub const LINEAR_BUCKETS_INT_BUCKETS_FLOOR: i64 = 0;
pub const LINEAR_BUCKETS_INT_BUCKETS_NUM_BUCKETS: u32 = 140;
pub const LINEAR_BUCKETS_INT_BUCKETS_STEP_SIZE: u32 = 5;

// Exponential bucket constants for exponential buckets report
pub const EXPONENTIAL_BUCKETS_REPORT_INT_BUCKETS_FLOOR: i64 = 0;
pub const EXPONENTIAL_BUCKETS_REPORT_INT_BUCKETS_NUM_BUCKETS: u32 = 3;
pub const EXPONENTIAL_BUCKETS_REPORT_INT_BUCKETS_INITIAL_STEP: u32 = 2;
pub const EXPONENTIAL_BUCKETS_REPORT_INT_BUCKETS_STEP_MULTIPLIER: u32 = 2;

// Metric ID Constants
// the_metric_name
pub const THE_METRIC_NAME_METRIC_ID: u32 = 100;
// the_other_metric_name
pub const THE_OTHER_METRIC_NAME_METRIC_ID: u32 = 200;
// event groups
pub const EVENT_GROUPS_METRIC_ID: u32 = 300;
// linear buckets
pub const LINEAR_BUCKETS_METRIC_ID: u32 = 400;
// exponential buckets
pub const EXPONENTIAL_BUCKETS_METRIC_ID: u32 = 500;
// metric
pub const METRIC_METRIC_ID: u32 = 600;
// second metric
pub const SECOND_METRIC_METRIC_ID: u32 = 601;

// Report ID Constants
// the_metric_name the_report
pub const THE_METRIC_NAME_THE_REPORT_REPORT_ID: u32 = 10;
// the_other_metric_name the_report
pub const THE_OTHER_METRIC_NAME_THE_REPORT_REPORT_ID: u32 = 10;
// the_metric_name the_other_report
pub const THE_METRIC_NAME_THE_OTHER_REPORT_REPORT_ID: u32 = 20;
// event groups the_report
pub const EVENT_GROUPS_THE_REPORT_REPORT_ID: u32 = 30;
// exponential buckets report
pub const EXPONENTIAL_BUCKETS_REPORT_REPORT_ID: u32 = 40;

// Enum for the_other_metric_name (Metric Dimension 0)
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
pub enum TheOtherMetricNameMetricDimension0 {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
}

// Alias for event groups (Metric Dimension The First Group) which has the same event codes
pub use TheOtherMetricNameMetricDimension0 as EventGroupsMetricDimensionTheFirstGroup;

// Enum for project (Metric Dimension A second group)
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
pub enum ProjectMetricDimensionASecondGroup {
  This = 1,
  Is = 2,
  Another = 3,
  Test = 4,
}
// Alias for event groups (Metric Dimension A second group) which has the same event codes
pub use ProjectMetricDimensionASecondGroup as EventGroupsMetricDimensionASecondGroup;

// Enum for project (Metric Dimension 2)
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
pub enum ProjectMetricDimension2 {
  ThisMetric = 0,
  HasNo = 2,
  Name = 4,
}
impl ProjectMetricDimension2 {
  #[allow(non_upper_case_globals)]
  pub const Alias: ProjectMetricDimension2 = ProjectMetricDimension2::HasNo;
}
// Alias for event groups (Metric Dimension 2) which has the same event codes
pub use ProjectMetricDimension2 as EventGroupsMetricDimension2;

// Enum for project (Metric Dimension First)
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
pub enum ProjectMetricDimensionFirst {
  A = 1,
  Set = 2,
  OfEvent = 3,
  Codes = 4,
}
// Alias for metric (Metric Dimension First) which has the same event codes
pub use ProjectMetricDimensionFirst as MetricMetricDimensionFirst;

// Alias for second metric (Metric Dimension First) which has the same event codes
pub use ProjectMetricDimensionFirst as SecondMetricMetricDimensionFirst;

// Enum for project (Metric Dimension Second)
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
pub enum ProjectMetricDimensionSecond {
  Some = 0,
  More = 4,
  Event = 8,
  Codes = 16,
}
// Alias for metric (Metric Dimension Second) which has the same event codes
pub use ProjectMetricDimensionSecond as MetricMetricDimensionSecond;

// Alias for second metric (Metric Dimension Second) which has the same event codes
pub use ProjectMetricDimensionSecond as SecondMetricMetricDimensionSecond;

// The base64 encoding of the bytes of a serialized CobaltRegistry proto message.
pub const CONFIG: &str = "KooHCghjdXN0b21lchAKGvsGCgdwcm9qZWN0EAUaVQoPdGhlX21ldHJpY19uYW1lEAoYBSBkYhEKCnRoZV9yZXBvcnQQChiPTmIWChB0aGVfb3RoZXJfcmVwb3J0EBQYA3IIY3VzdG9tZXJ6B3Byb2plY3QafwoVdGhlX290aGVyX21ldHJpY19uYW1lEAoYBSDIASgBUAFiEAoKdGhlX3JlcG9ydBAKGAdyCGN1c3RvbWVyegdwcm9qZWN0ggE1EgsIABIHQW5FdmVudBIQCAESDEFub3RoZXJFdmVudBIRCAISDUEgdGhpcmQgZXZlbnQYyAEa+gEKDGV2ZW50IGdyb3VwcxAKGAUgrAIoAVABYhAKCnRoZV9yZXBvcnQQHhgHcghjdXN0b21lcnoHcHJvamVjdIIBRQoPVGhlIEZpcnN0IEdyb3VwEgsIABIHQW5FdmVudBIQCAESDEFub3RoZXJFdmVudBIRCAISDUEgdGhpcmQgZXZlbnQYAoIBOQoOQSBzZWNvbmQgZ3JvdXASCAgBEgRUaGlzEgYIAhICSXMSCwgDEgdhbm90aGVyEggIBBIEVGVzdIIBNRIOCAASClRoaXNNZXRyaWMSCQgCEgVIYXNObxIICAQSBE5hbWUqDgoFSGFzTm8SBUFsaWFzGjMKDmxpbmVhciBidWNrZXRzEAoYBSCQA0IHEgUQjAEYBXIIY3VzdG9tZXJ6B3Byb2plY3QaRQoTZXhwb25lbnRpYWwgYnVja2V0cxAKGAUg9ANiFAoGcmVwb3J0EChSCAoGEAMYAiACcghjdXN0b21lcnoHcHJvamVjdBqJAQoGbWV0cmljEAoYBSDYBHIIY3VzdG9tZXJ6B3Byb2plY3SCAS8KBUZpcnN0EgUIARIBQRIHCAISA1NldBILCAMSB09mRXZlbnQSCQgEEgVDb2Rlc4IBMgoGU2Vjb25kEggIABIEU29tZRIICAQSBE1vcmUSCQgIEgVFdmVudBIJCBASBUNvZGVzGpABCg1zZWNvbmQgbWV0cmljEAoYBSDZBHIIY3VzdG9tZXJ6B3Byb2plY3SCAS8KBUZpcnN0EgUIARIBQRIHCAISA1NldBILCAMSB09mRXZlbnQSCQgEEgVDb2Rlc4IBMgoGU2Vjb25kEggIABIEU29tZRIICAQSBE1vcmUSCQgIEgVFdmVudBIJCBASBUNvZGVz";
