package config_validator

import (
	"config"
	"fmt"
	"testing"
	"time"
)

////////////////////////////////////////////////////////////////////////////////
// Tests concerning sets of metrics.
////////////////////////////////////////////////////////////////////////////////

// Test that repeated ids are rejected.
func TestValidateUniqueMetricId(t *testing.T) {
	m1 := makeSomeValidMetric()
	m1.MetricName = "name1"
	m1.Id = 2

	m2 := makeSomeValidMetric()
	m2.MetricName = "name2"
	m2.Id = 2

	metrics := []*config.MetricDefinition{&m1, &m2}

	if err := validateConfiguredMetricDefinitions(metrics); err == nil {
		t.Error("Accepted metric definitions with identical ids.")
	}
}

////////////////////////////////////////////////////////////////////////////////
// Tests for all/most metric types.
////////////////////////////////////////////////////////////////////////////////

// Test that repeated names are rejected.
func TestValidateUniqueMetricName(t *testing.T) {
	m1 := makeSomeValidMetric()
	m1.MetricName = "name"
	m1.Id = 2

	m2 := makeSomeValidMetric()
	m2.MetricName = "name"
	m2.Id = 3

	metrics := []*config.MetricDefinition{&m1, &m2}
	if err := validateConfiguredMetricDefinitions(metrics); err == nil {
		t.Error("Accepted metric definitions with identical names.")
	}
}

// Test that all metrics except CUSTOM accept 0 or more dimensions.
func TestValidMetricWithNoDimensions(t *testing.T) {
	for _, mt := range metricTypesExcept() {
		m := makeValidMetric(mt)
		m.MetricDimensions = nil
		if err := validateMetricDefinition(makeValidMetric(mt)); err != nil {
			t.Errorf("Rejected valid %v metric with no dimension: %v", mt.String(), err)
		}
	}

	for _, mt := range metricTypesExcept(config.MetricDefinition_CUSTOM) {
		m := makeValidMetric(mt)
		addDimensions(&m, 2)
		if err := validateMetricDefinition(makeValidMetric(mt)); err != nil {
			t.Errorf("Rejected valid %v metric with 2+ dimensions: %v", mt.String(), err)
		}
	}
}

// Test that invalid names are rejected.
func TestValidateMetricInvalidMetricName(t *testing.T) {
	m := makeSomeValidMetric()
	m.MetricName = "_invalid_name"

	if err := validateMetricDefinition(m); err == nil {
		t.Error("Accepted metric definition with invalid name.")
	}
}

// Test that metric id 0 is not accepted.
func TestValidateZeroMetricId(t *testing.T) {
	m := makeSomeValidMetric()
	m.Id = 0

	if err := validateMetricDefinition(m); err == nil {
		t.Error("Accepted metric definition with 0 id.")
	}
}

// Test that we do not accept a metric with type UNSET.
func TestValidateUnsetMetricType(t *testing.T) {
	m := makeValidOccurrenceMetric()
	m.MetricType = config.MetricDefinition_UNSET

	if err := validateMetricDefinition(m); err == nil {
		t.Error("Accepted metric definition with unset metric type.")
	}
}

////////////////////////////////////////////////////////////////////////////////
// Tests concerning metadata.
////////////////////////////////////////////////////////////////////////////////

// Test that meta_data must be set.
func TestValidatePartsNoMetadata(t *testing.T) {
	m := makeSomeValidMetric()
	m.MetaData = nil

	if err := validateMetricDefinition(m); err == nil {
		t.Error("Accepted metric definition with no meta_data set.")
	}
}

func TestValidateMetadataNoExpirationDate(t *testing.T) {
	m := makeValidMetadata()
	m.ExpirationDate = ""

	if err := validateMetadata(m); err == nil {
		t.Error("Accepted metadata with no expiration date.")
	}
}

func TestValidateMetadataInvalidExpirationDate(t *testing.T) {
	m := makeValidMetadata()
	m.ExpirationDate = "abcd"

	if err := validateMetadata(m); err == nil {
		t.Error("Accepted invalid expiration date")
	}
}

func TestValidateMetadataExpirationDateTooFar(t *testing.T) {
	m := makeValidMetadata()
	m.ExpirationDate = time.Now().AddDate(1, 0, 2).Format(dateFormat)

	if err := validateMetadata(m); err == nil {
		t.Errorf("Accepted expiration date more than 1 year out: %v", m.ExpirationDate)
	}
}

func TestValidateMetadataExpirationDateInPast(t *testing.T) {
	m := makeValidMetadata()
	m.ExpirationDate = "2010/01/01"

	if err := validateMetadata(m); err != nil {
		t.Errorf("Rejected expiration date in the past: %v", err)
	}
}

func TestValidateMetadataInvalidOwner(t *testing.T) {
	m := makeValidMetadata()
	m.Owner = append(m.Owner, "not a valid email")

	if err := validateMetadata(m); err == nil {
		t.Error("Accepted owner with invalid email address.")
	}
}

func TestValidateMetadataReleaseStageNotSet(t *testing.T) {
	m := makeValidMetadata()
	m.MaxReleaseStage = config.ReleaseStage_RELEASE_STAGE_NOT_SET

	if err := validateMetadata(m); err == nil {
		t.Error("Accepted owner with no max_release_stage set.")
	}
}

////////////////////////////////////////////////////////////////////////////////
// Tests concerning event codes.
////////////////////////////////////////////////////////////////////////////////

func TestValidateEventCodesMaxEventCodeTooBig(t *testing.T) {
	for _, mt := range cobalt10MetricTypes() {
		m := makeValidMetric(mt)
		addDimensions(&m, 1)
		m.MetricDimensions[0].MaxEventCode = 1024
		m.MetricDimensions[0].EventCodes = map[uint32]string{
			1: "hello_world",
		}

		if err := validateMetricDimensions(m); err == nil {
			t.Error("Accepted max_event_code with value no less than 1024 for Cobalt 1.0 metric.")
		}
	}

	for _, mt := range cobalt11MetricTypes() {
		m := makeValidMetric(mt)
		addDimensions(&m, 1)
		m.MetricDimensions[0].MaxEventCode = 1024
		m.MetricDimensions[0].EventCodes = map[uint32]string{
			1: "hello_world",
		}

		if err := validateMetricDimensions(m); err != nil {
			t.Errorf("Rejected max_event_code with value no less than 1024 for Cobalt 1.1 metric: %v", err)
		}
	}
}

func TestValidateMetricDimensionsTooManyVariants(t *testing.T) {
	for _, mt := range cobalt10MetricTypes() {
		m := makeValidMetric(mt)
		tenCodes := map[uint32]string{1: "A", 2: "B", 3: "C", 4: "D", 5: "E", 6: "F", 7: "G", 8: "H", 9: "I", 10: "J"}
		m.MetricDimensions = []*config.MetricDefinition_MetricDimension{
			&config.MetricDefinition_MetricDimension{Dimension: "Dimension 1", EventCodes: tenCodes},
			&config.MetricDefinition_MetricDimension{Dimension: "Dimension 2", EventCodes: tenCodes},
			&config.MetricDefinition_MetricDimension{Dimension: "Dimension 3", EventCodes: tenCodes},
		}

		if err := validateMetricDimensions(m); err != nil {
			t.Errorf("Rejected valid metric with 10^3 (1000) event codes: %v", err)
		}

		m.MetricDimensions[0].EventCodes[11] = "K"
		if err := validateMetricDimensions(m); err == nil {
			t.Error("Accepted invalid metric with 11*10*10 (1100) event codes")
		}
	}

	for _, mt := range cobalt11MetricTypes() {
		m := makeValidMetric(mt)
		manyCodes := map[uint32]string{1: "A", 2: "B", 3: "C", 4: "D", 5: "E", 6: "F", 7: "G", 8: "H", 9: "I", 10: "J", 1055: "K"}
		m.MetricDimensions = []*config.MetricDefinition_MetricDimension{
			&config.MetricDefinition_MetricDimension{Dimension: "Dimension 1", EventCodes: manyCodes},
			&config.MetricDefinition_MetricDimension{Dimension: "Dimension 2", EventCodes: manyCodes},
			&config.MetricDefinition_MetricDimension{Dimension: "Dimension 3", EventCodes: manyCodes},
			&config.MetricDefinition_MetricDimension{Dimension: "Dimension 4", EventCodes: manyCodes},
		}

		if err := validateMetricDimensions(m); err != nil {
			t.Errorf("Rejected valid metric with many event codes: %v", err)
		}
	}
}

func TestValidateMetricDimensionsDimensionNames(t *testing.T) {
	m := makeSomeValidMetric()
	addDimensions(&m, 2)
	m.MetricDimensions[1].Dimension = ""

	if err := validateMetricDimensions(m); err == nil {
		t.Error("Accepted invalid metric with an unnamed metric dimension")
	}

	m.MetricDimensions[1].Dimension = "Dimension 2"
	if err := validateMetricDimensions(m); err != nil {
		t.Error("Rejected valid metric with two distinctly named metric dimensions")
	}

	m.MetricDimensions[0].Dimension = "Dimension 1"
	m.MetricDimensions[1].Dimension = "Dimension 1"
	if err := validateMetricDimensions(m); err == nil {
		t.Error("Accepted invalid metric with two identically named metric dimensions")
	}
}

func TestValidateMetricDimensionsEventCodeAlias(t *testing.T) {
	m := makeSomeValidMetric()
	addDimensions(&m, 1)
	m.MetricDimensions[0].EventCodes = map[uint32]string{
		0: "CodeName",
		1: "CodeName",
	}

	if err := validateMetricDimensions(m); err == nil {
		t.Error("Accepted invalid metric with duplicate event code names")
	}

	m.MetricDimensions[0].EventCodes = map[uint32]string{
		0: "CodeName",
	}
	m.MetricDimensions[0].EventCodeAliases = map[string]string{
		"Metric": "Alias",
	}

	if err := validateMetricDimensions(m); err == nil {
		t.Error("Accepted invalid metric with an invalid alias")
	}

	m.MetricDimensions[0].EventCodeAliases = map[string]string{
		"Alias": "CodeName",
	}
	if err := validateMetricDimensions(m); err == nil {
		t.Error("Accepted invalid metric with an alias in the wrong order")
	}

	m.MetricDimensions[0].EventCodeAliases = map[string]string{
		"CodeName": "Alias",
	}
	if err := validateMetricDimensions(m); err != nil {
		t.Errorf("Rejected valid metric: %v", err)
	}

	m.MetricDimensions[0].EventCodes[1] = "CodeName2"
	m.MetricDimensions[0].EventCodeAliases = map[string]string{
		"CodeName": "CodeName2",
	}
	if err := validateMetricDimensions(m); err == nil {
		t.Error("Accepted invalid metric that maps to an existing event code")
	}
}

func TestValidateEventCodesIndexLargerThanMax(t *testing.T) {
	m := makeSomeValidMetric()
	addDimensions(&m, 1)
	m.MetricDimensions[0].MaxEventCode = 100
	m.MetricDimensions[0].EventCodes = map[uint32]string{
		1:   "hello_world",
		101: "blah",
	}

	if err := validateMetricDimensions(m); err == nil {
		t.Error("Accepted event type with index larger than max_event_code.")
	}
}

func TestAllowNoEventCodesWhenMaxEventCodeIsSet(t *testing.T) {
	m := makeSomeValidMetric()
	addDimensions(&m, 1)
	m.MetricDimensions[0].EventCodes = nil

	if err := validateMetricDimensions(m); err == nil {
		t.Error("Accepted metric without max_event_code and without any event_codes.")
	}

	m.MetricDimensions[0].MaxEventCode = 100

	if err := validateMetricDimensions(m); err != nil {
		t.Error("Did not accept metric with max_event_code set and no event codes defined.")
	}
}

func TestValidateEventCodesNoEventCodes(t *testing.T) {
	m := makeSomeValidMetric()
	addDimensions(&m, 1)
	m.MetricDimensions[0].EventCodes = map[uint32]string{}

	if err := validateMetricDimensions(m); err == nil {
		t.Error("Accepted metric with no event types.")
	}
}

////////////////////////////////////////////////////////////////////////////////
// Tests concerning metric units.
////////////////////////////////////////////////////////////////////////////////

func TestMetricUnitsForIntegerAndIntegerHistogramOnly(t *testing.T) {
	for _, mt := range metricTypesExcept(config.MetricDefinition_INTEGER, config.MetricDefinition_INTEGER_HISTOGRAM) {
		m := makeValidMetric(mt)
		m.MetricUnits = config.MetricUnits_NANOSECONDS
		m.MetricUnitsOther = ""
		if err := validateMetricDefinition(m); err == nil {
			t.Errorf("Accepted %v metric with metric_units set", mt.String())
		}

		m.MetricUnits = config.MetricUnits_METRIC_UNITS_OTHER
		m.MetricUnitsOther = "hello"
		if err := validateMetricDefinition(m); err == nil {
			t.Errorf("Accepted %v metric with metric_units_other set", mt.String())
		}
	}

}

func TestMetricUnitsForIntegers(t *testing.T) {
	m := makeValidIntegerMetric()
	m.MetricUnits = config.MetricUnits_METRIC_UNITS_OTHER
	m.MetricUnitsOther = "hello"
	if err := validateMetricDefinition(m); err != nil {
		t.Errorf("Rejected INTEGER metric with metric_units_other set: %v", err)
	}

	m.MetricUnits = config.MetricUnits_NANOSECONDS
	m.MetricUnitsOther = ""
	if err := validateMetricDefinition(m); err != nil {
		t.Errorf("Rejected INTEGER metric with metric_units set: %v", err)
	}

	m.MetricUnits = config.MetricUnits_NANOSECONDS
	m.MetricUnitsOther = "hello"
	if err := validateMetricDefinition(m); err == nil {
		t.Error("Accepted INTEGER metric with both metric_units and metric_units_other set.")
	}

	m.MetricUnits = config.MetricUnits_METRIC_UNITS_OTHER
	m.MetricUnitsOther = ""
	if err := validateMetricDefinition(m); err == nil {
		t.Error("Accepted INTEGER metric with neither metric_units nor metric_units_other set.")
	}
}

func TestMetricUnitsForIntegerHistograms(t *testing.T) {
	m := makeValidIntegerHistogramMetric()
	m.MetricUnits = config.MetricUnits_METRIC_UNITS_OTHER
	m.MetricUnitsOther = "hello"
	if err := validateMetricDefinition(m); err != nil {
		t.Errorf("Rejected INTEGER_HISTOGRAM metric with metric_units_other set: %v", err)
	}

	m.MetricUnits = config.MetricUnits_NANOSECONDS
	m.MetricUnitsOther = ""
	if err := validateMetricDefinition(m); err != nil {
		t.Errorf("Rejected INTEGER_HISTOGRAM metric with metric_units set: %v", err)
	}

	m.MetricUnits = config.MetricUnits_NANOSECONDS
	m.MetricUnitsOther = "hello"
	if err := validateMetricDefinition(m); err == nil {
		t.Error("Accepted INTEGER_HISTOGRAM metric with both metric_units and metric_units_other set.")
	}

	m.MetricUnits = config.MetricUnits_METRIC_UNITS_OTHER
	m.MetricUnitsOther = ""
	if err := validateMetricDefinition(m); err == nil {
		t.Error("Accepted INTEGER_HISTOGRAM metric with neither metric_units nor metric_units_other set.")
	}
}

////////////////////////////////////////////////////////////////////////////////
// Tests concerning INT_HISTOGRAM, INTEGER and INTEGER_HISTOGRAM metrics.
////////////////////////////////////////////////////////////////////////////////

// Test that int_buckets can only be set if the metric type is INT_HISTOGRAM or INTEGER_HISTOGRAM.
func TestValidateIntBucketsRequiredForIntegerHistogramsOnly(t *testing.T) {
	for _, mt := range metricTypesExcept(config.MetricDefinition_INT_HISTOGRAM, config.MetricDefinition_INTEGER_HISTOGRAM) {
		m := makeValidMetric(mt)
		m.IntBuckets = &config.IntegerBuckets{}
		if err := validateMetricDefinition(m); err == nil {
			t.Errorf("Accepted metric definition with type %s with int_buckets set.", mt)
		}
	}

	m := makeValidIntHistogramMetric()
	m.IntBuckets = nil
	if err := validateMetricDefinition(m); err == nil {
		t.Error("Accepted INT_HISTOGRAM metric definition with int_buckets not set.")
	}

	m = makeValidIntegerHistogramMetric()
	m.IntBuckets = nil
	if err := validateMetricDefinition(m); err == nil {
		t.Error("Accepted INTEGER_HISTOGRAM metric definition with int_buckets not set.")
	}
}

////////////////////////////////////////////////////////////////////////////////
// Tests concerning CUSTOM metrics.
////////////////////////////////////////////////////////////////////////////////

// Test that parts can only be set if the metric type is CUSTOM.
func TestValidatePartsSetOnlyForCustom(t *testing.T) {
	m := makeValidCustomMetric()
	m.Parts = map[string]*config.MetricPart{"hello": nil}
	m.MetricDimensions = nil
	if err := validateMetricDefinition(m); err != nil {
		t.Errorf("Rejected valid CUSTOM metric definition: %v", err)
	}

	for _, mt := range metricTypesExcept(config.MetricDefinition_CUSTOM) {
		m := makeValidMetric(mt)
		m.Parts = map[string]*config.MetricPart{"hello": nil}
		if err := validateMetricDefinition(m); err == nil {
			t.Errorf("Accepted metric definition with type %s with parts set.", mt)
		}
	}
}

// Test that parts can only be set if the metric type is CUSTOM.
func TestValidateProtoSetOnlyForCustom(t *testing.T) {
	m := makeValidCustomMetric()

	if err := validateMetricDefinition(m); err != nil {
		t.Errorf("Rejected valid CUSTOM metric definition: %v", err)
	}

	for _, mt := range metricTypesExcept(config.MetricDefinition_CUSTOM) {
		m.MetricType = mt
		if err := validateMetricDefinition(m); err == nil {
			t.Errorf("Accepted metric definition with type %s with proto_name set.", mt)
		}
	}
}

func TestValidateEventOccurredNoMax(t *testing.T) {
	m := makeValidEventOccurredMetric()
	m.MetricDimensions[0].MaxEventCode = 0

	if err := validateEventOccurred(m); err == nil {
		t.Error("Accepted EVENT_OCCURRED metric with no max_event_code.")
	}
}

func TestValidateIntHistogramNoBuckets(t *testing.T) {
	m := makeValidIntHistogramMetric()
	m.IntBuckets = nil

	if err := validateIntHistogram(m); err == nil {
		t.Error("Accepted INT_HISTOGRAM metric with no int_buckets.")
	}
}

func TestValidateCustomEventCodesSetOld(t *testing.T) {
	m := makeValidCustomMetric()
	m.ProtoName = ""
	m.Parts = map[string]*config.MetricPart{"hello": nil}
	addDimensions(&m, 1)
	m.MetricDimensions[0].EventCodes = map[uint32]string{1: "hello"}
	if err := validateCustom(m); err == nil {
		t.Error("Accepted CUSTOM metric with event_codes set.")
	}
}

func TestValidateCustomEventCodesSet(t *testing.T) {
	m := makeValidCustomMetric()
	m.ProtoName = "test.ProtoName"
	addDimensions(&m, 1)
	m.MetricDimensions[0].EventCodes = map[uint32]string{1: "hello"}

	if err := validateCustom(m); err == nil {
		t.Error("Accepted CUSTOM metric with event_codes set.")
	}
}

func TestValidateCustomNoParts(t *testing.T) {
	m := makeValidCustomMetric()
	m.ProtoName = ""
	m.Parts = map[string]*config.MetricPart{}
	if err := validateCustom(m); err == nil {
		t.Error("Accepted CUSTOM metric with no parts.")
	}
}

func TestValidateCustomInvalidPartName(t *testing.T) {
	m := makeValidCustomMetric()
	m.ProtoName = ""
	m.Parts = map[string]*config.MetricPart{"_invalid_name": nil}
	if err := validateCustom(m); err == nil {
		t.Error("Accepted CUSTOM metric with invalid part name.")
	}
}

func TestValidateCustomNoProtoName(t *testing.T) {
	m := makeValidCustomMetric()
	m.ProtoName = ""
	m.Parts = map[string]*config.MetricPart{}

	if err := validateCustom(m); err == nil {
		t.Error("Accepted CUSTOM metric with no proto_name.")
	}
}

func TestValidateCustomInvalidProtoName(t *testing.T) {
	m := makeValidCustomMetric()
	m.ProtoName = "_invalid.ProtoName"

	if err := validateCustom(m); err == nil {
		t.Error("Accepted CUSTOM metric with invalid proto_name.")
	}
}

// Test that all Cobalt 1.1 metrics are required to have metric_semantics set and that other metrics are prohibited from the same.
func TestValidateMetricDefinitionSemanticsForCobalt11MetricTypesOnly(t *testing.T) {
	for _, mt := range cobalt11MetricTypes() {
		m := makeValidMetric(mt)
		m.MetricSemantics = nil
		if err := validateMetricDefinition(m); err == nil {
			t.Errorf("Accepted %v metric with no metric_semantics not set.", m.MetricType.String())
		}
	}

	for _, mt := range cobalt10MetricTypes() {
		m := makeValidMetric(mt)
		m.MetricSemantics = []config.MetricSemantics{
			config.MetricSemantics_METRIC_SEMANTICS_UNSPECIFIED,
		}
		if err := validateMetricDefinition(m); err == nil {
			t.Errorf("Accepted %v metric with metric_semantics set.", m.MetricType.String())
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
// Following are utility functions to facilitate testing.
////////////////////////////////////////////////////////////////////////////////

// Allows generating a list of MetricTypes for which we can run tests.
func metricTypesExcept(remove ...config.MetricDefinition_MetricType) []config.MetricDefinition_MetricType {
	return metricTypesExceptList(remove)
}

// Allows generating a list of MetricTypes for which we can run tests.
func metricTypesExceptList(remove []config.MetricDefinition_MetricType) (s []config.MetricDefinition_MetricType) {
	types := map[config.MetricDefinition_MetricType]bool{}
	for t := range config.MetricDefinition_MetricType_name {
		types[config.MetricDefinition_MetricType(t)] = true
	}

	for _, r := range remove {
		delete(types, r)
	}
	delete(types, config.MetricDefinition_UNSET)

	for t, _ := range types {
		s = append(s, t)
	}

	return
}

func cobalt11MetricTypes() (s []config.MetricDefinition_MetricType) {
	for mt, _ := range cobalt11MetricTypesSet {
		s = append(s, mt)
	}
	return
}

func cobalt10MetricTypes() []config.MetricDefinition_MetricType {
	return metricTypesExceptList(cobalt11MetricTypes())
}

func makeValidMetadata() config.MetricDefinition_Metadata {
	return config.MetricDefinition_Metadata{
		ExpirationDate:  time.Now().AddDate(1, 0, 0).Format(dateFormat),
		Owner:           []string{"google@example.com"},
		MaxReleaseStage: config.ReleaseStage_DEBUG,
	}
}

func makeValidCobalt10BaseMetric(t config.MetricDefinition_MetricType) config.MetricDefinition {
	metadata := makeValidMetadata()
	return config.MetricDefinition{
		Id:         1,
		MetricName: "the_metric_name",
		MetaData:   &metadata,
		MetricType: t,
	}
}

func addDimensions(m *config.MetricDefinition, numDim int) {
	for i := 0; i < numDim; i++ {
		m.MetricDimensions = append(m.MetricDimensions, &config.MetricDefinition_MetricDimension{
			Dimension:  fmt.Sprintf("Dimension %d", i),
			EventCodes: map[uint32]string{1: "hello_world"},
		})
	}
}

func makeValidEventOccurredMetric() config.MetricDefinition {
	m := makeValidCobalt10BaseMetric(config.MetricDefinition_EVENT_OCCURRED)
	addDimensions(&m, 1)
	m.MetricDimensions[0].MaxEventCode = 5
	return m
}

func makeValidIntHistogramMetric() config.MetricDefinition {
	m := makeValidCobalt10BaseMetric(config.MetricDefinition_INT_HISTOGRAM)
	m.IntBuckets = &config.IntegerBuckets{}
	return m
}

func makeValidCustomMetric() config.MetricDefinition {
	m := makeValidCobalt10BaseMetric(config.MetricDefinition_CUSTOM)
	m.ProtoName = "$team_name.test.ProtoName"
	return m
}

func makeValidCobalt11BaseMetric(t config.MetricDefinition_MetricType) config.MetricDefinition {
	m := makeValidCobalt10BaseMetric(t)
	m.MetricSemantics = []config.MetricSemantics{
		config.MetricSemantics_METRIC_SEMANTICS_UNSPECIFIED,
	}
	return m
}

func makeValidOccurrenceMetric() config.MetricDefinition {
	return makeValidCobalt11BaseMetric(config.MetricDefinition_OCCURRENCE)
}

func makeValidIntegerMetric() config.MetricDefinition {
	m := makeValidCobalt11BaseMetric(config.MetricDefinition_INTEGER)
	m.MetricUnits = config.MetricUnits_SECONDS
	return m
}

func makeValidIntegerHistogramMetric() config.MetricDefinition {
	m := makeValidCobalt11BaseMetric(config.MetricDefinition_INTEGER_HISTOGRAM)
	m.MetricUnits = config.MetricUnits_SECONDS
	m.IntBuckets = &config.IntegerBuckets{}
	return m
}

func makeValidStringMetric() config.MetricDefinition {
	m := makeValidCobalt11BaseMetric(config.MetricDefinition_STRING)
	m.StringCandidateFile = "some_file"
	m.StringBufferMax = 10
	return m
}

func makeValidMetric(t config.MetricDefinition_MetricType) config.MetricDefinition {
	switch t {
	case config.MetricDefinition_EVENT_OCCURRED:
		return makeValidEventOccurredMetric()
	case config.MetricDefinition_INT_HISTOGRAM:
		return makeValidIntHistogramMetric()
	case config.MetricDefinition_CUSTOM:
		return makeValidCustomMetric()
	case config.MetricDefinition_OCCURRENCE:
		return makeValidOccurrenceMetric()
	case config.MetricDefinition_INTEGER:
		return makeValidIntegerMetric()
	case config.MetricDefinition_INTEGER_HISTOGRAM:
		return makeValidIntegerHistogramMetric()
	case config.MetricDefinition_STRING:
		return makeValidStringMetric()
	}

	return makeValidCobalt10BaseMetric(t)
}

func makeSomeValidMetric() config.MetricDefinition {
	return makeValidOccurrenceMetric()
}

// Test the makeValidMetric functions return valid metrics.
func TestMakeValidMetricsValid(t *testing.T) {
	for _, mt := range metricTypesExcept() {
		if err := validateMetricDefinition(makeValidMetric(mt)); err != nil {
			t.Errorf("Rejected valid %v metric: %v", mt.String(), err)
		}
	}
}

// Test the makeValidMetadata does return a valid metadata message.
func TestValidateMakeValidMetadata(t *testing.T) {
	m := makeValidMetadata()
	if err := validateMetadata(m); err != nil {
		t.Errorf("Rejected valid metadata: %v", err)
	}
}
