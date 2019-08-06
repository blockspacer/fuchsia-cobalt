# Cobalt Registry Source Generator
This code generates source files for C++, Dart, and Rust to facilitate
interacting with the Cobalt API in those languages.

## What this generator outputs
For Cobalt 0.1 clients, it writes out constants for all customer ids, project
ids, metric ids, and report ids for the selected config. It will output these
constants with names that match the language's standard global constant naming
convention. (e.g. C++ uses kCamelCase, and rust uses UPPER_SNAKE_CASE)

For Cobalt 1.0 clients, the generator also generates enums for all of the
event_codes. If a `metric_definition` looked like the following:

```
- id: 1
  metric_name: "A Metric"
  metric_dimensions:
    - dimension: "A Dimension"
      event_codes:
        0: "An Event"
        1: "Another Event"
        2: "A Third Event"
```

It would generate enums like the following in C++:

```
enum AMetricMetricDimensionADimension {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
}
```

And the following in Rust:

```
pub enum AMetricMetricDimensionADimension {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
}
```

And the following in Dart:

```
class AMetricMetricDimensionADimension {
  static const int AnEvent = 0;
  static const int AnotherEvent = 1;
  static const int AThirdEvent = 2;
}
```

For more examples of what would be outputted, see the files under the
source_generator_test_files directory.
