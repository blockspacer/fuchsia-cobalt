package config_validator

import (
	"config"
	"config_parser"
	"testing"
	"time"
)

// Allows generating a list of MetricTypes for which we can run tests.
func metricTypesExcept(remove ...config.MetricDefinition_MetricType) (s []config.MetricDefinition_MetricType) {
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

func makeValidMetadata() config.MetricDefinition_Metadata {
	return config.MetricDefinition_Metadata{
		ExpirationDate:  time.Now().AddDate(1, 0, 0).Format(dateFormat),
		Owner:           []string{"google@example.com"},
		MaxReleaseStage: config.ReleaseStage_DEBUG,
	}
}

func makeValidMetric() config.MetricDefinition {
	return makeValidMetricWithName("the_metric_name", 1)
}

// makeValidMetric returns a valid instance of config.MetricDefinition which
// can be modified to fail various validation checks for testing purposes.
func makeValidMetricWithName(name string, numDimensions int) config.MetricDefinition {
	metadata := makeValidMetadata()
	metricDefinition := config.MetricDefinition{
		Id:         config_parser.IdFromName(name),
		MetricName: name,
		MetricType: config.MetricDefinition_EVENT_COUNT,
		MetaData:   &metadata,
	}
	for i := 0; i < numDimensions; i++ {
		metricDefinition.MetricDimensions = append(metricDefinition.MetricDimensions, &config.MetricDefinition_MetricDimension{
			EventCodes: map[uint32]string{1: "hello_world"},
		})
	}
	return metricDefinition
}

// Test that makeValidMetric returns a valid metric.
func TestValidateMakeValidMetric(t *testing.T) {
	m := makeValidMetric()
	if err := validateMetricDefinition(m); err != nil {
		t.Errorf("Rejected valid metric: %v", err)
	}
}

// Test that it is valid to make a MetricDefinition with no metric dimensions.
func TestValidMetricWithNoDimensions(t *testing.T) {
	m := makeValidMetricWithName("name", 0)
	if err := validateMetricDefinition(m); err != nil {
		t.Errorf("Rejected valid metric: %v", err)
	}
}

func TestValidateMakeValidMetadata(t *testing.T) {
	m := makeValidMetadata()
	if err := validateMetadata(m); err != nil {
		t.Errorf("Rejected valid metadata: %v", err)
	}
}

// Test that repeated ids are rejected.
func TestValidateUniqueMetricId(t *testing.T) {
	m1Name := "TpzQweXFfRXpQrDWvplhfXFbJptlKmkIlHBAzjPnADtJWVAawVrbPGg"
	m2Name := "zDnlfjrXpwuQYpBrNTeCbtsRBydKuKdCvEjlGwdRJlxMjbYOSGPjhif"
	m1 := makeValidMetricWithName(m1Name, 0)
	m2 := makeValidMetricWithName(m2Name, 0)

	metrics := []*config.MetricDefinition{&m1, &m2}

	if err := validateConfiguredMetricDefinitions(metrics); err == nil {
		t.Error("Accepted metric definitions with identical ids.")
	}
}

// Test that invalid names are rejected.
func TestValidateMetricInvalidMetricName(t *testing.T) {
	m := makeValidMetricWithName("_invalid_name", 0)

	if err := validateMetricDefinition(m); err == nil {
		t.Error("Accepted metric definition with invalid name.")
	}
}

// Test that metric id 0 is not accepted.
func TestValidateZeroMetricId(t *testing.T) {
	m := makeValidMetricWithName("NRaMinLNcqiYmgEypLLVGnXymNpxJzqabtbbjLycCMEohvVzZtAYpah", 0)

	if err := validateMetricDefinition(m); err == nil {
		t.Error("Accepted metric definition with 0 id.")
	}
}

// Test that we do not accept a metric with type UNSET.
func TestValidateUnsetMetricType(t *testing.T) {
	m := makeValidMetric()
	m.MetricType = config.MetricDefinition_UNSET

	if err := validateMetricDefinition(m); err == nil {
		t.Error("Accepted metric definition with unset metric type.")
	}
}

// Test that int_buckets can only be set if the metric type is INT_HISTOGRAM.
func TestValidateIntBucketsSetOnlyForIntHistogram(t *testing.T) {
	m := makeValidMetric()
	m.IntBuckets = &config.IntegerBuckets{}
	m.MetricType = config.MetricDefinition_INT_HISTOGRAM

	if err := validateMetricDefinition(m); err != nil {
		t.Errorf("Rejected valid INT_HISTOGRAM metric definition: %v", err)
	}

	for _, mt := range metricTypesExcept(config.MetricDefinition_INT_HISTOGRAM) {
		m.MetricType = mt
		if err := validateMetricDefinition(m); err == nil {
			t.Errorf("Accepted metric definition with type %s with int_buckets set.", mt)
		}
	}
}

// Test that parts can only be set if the metric type is CUSTOM.
func TestValidatePartsSetOnlyForCustom(t *testing.T) {
	m := makeValidMetric()
	m.Parts = map[string]*config.MetricPart{"hello": nil}
	m.MetricType = config.MetricDefinition_CUSTOM
	m.MetricDimensions = nil
	if err := validateMetricDefinition(m); err != nil {
		t.Errorf("Rejected valid CUSTOM metric definition: %v", err)
	}
	for _, mt := range metricTypesExcept(config.MetricDefinition_CUSTOM) {
		m.MetricType = mt
		if err := validateMetricDefinition(m); err == nil {
			t.Errorf("Accepted metric definition with type %s with parts set.", mt)
		}
	}
}

// Test that parts can only be set if the metric type is CUSTOM.
func TestValidateProtoSetOnlyForCustom(t *testing.T) {
	m := makeValidMetric()
	m.ProtoName = "$team_name.test.ProtoName"
	m.MetricType = config.MetricDefinition_CUSTOM
	m.MetricDimensions = nil

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

// Test that meta_data must be set.
func TestValidatePartsNoMetadata(t *testing.T) {
	m := makeValidMetric()
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

func TestValidateEventCodesMaxEventCodeTooBig(t *testing.T) {
	m := makeValidMetric()
	m.MetricDimensions[0].MaxEventCode = 1024
	m.MetricDimensions[0].EventCodes = map[uint32]string{
		1: "hello_world",
	}

	if err := validateMetricDimensions(m); err == nil {
		t.Error("Accepted max_event_code with value no less than 1024.")
	}
}

func TestValidateEventCodesIndexLargerThanMax(t *testing.T) {
	m := makeValidMetric()
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
	m := makeValidMetric()
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
	m := makeValidMetric()
	m.MetricDimensions[0].EventCodes = map[uint32]string{}

	if err := validateMetricDimensions(m); err == nil {
		t.Error("Accepted metric with no event types.")
	}
}

func TestValidateEventOccurredNoMax(t *testing.T) {
	m := makeValidMetric()
	m.MetricDimensions[0].MaxEventCode = 0

	if err := validateEventOccurred(m); err == nil {
		t.Error("Accepted EVENT_OCCURRED metric with no max_event_code.")
	}
}

func TestValidateIntHistogramNoBuckets(t *testing.T) {
	m := makeValidMetric()
	m.IntBuckets = nil

	if err := validateIntHistogram(m); err == nil {
		t.Error("Accepted INT_HISTOGRAM metric with no int_buckets.")
	}
}

func TestValidateStringUsedEventCodesSet(t *testing.T) {
	m := makeValidMetric()
	m.MetricDimensions[0].EventCodes = map[uint32]string{1: "hello"}

	if err := validateStringUsed(m); err == nil {
		t.Error("Accepted STRING_USED metric with event_codes set.")
	}
}

func TestValidateCustomEventCodesSetOld(t *testing.T) {
	m := makeValidMetric()
	m.Parts = map[string]*config.MetricPart{"hello": nil}
	m.MetricDimensions[0].EventCodes = map[uint32]string{1: "hello"}
	if err := validateCustom(m); err == nil {
		t.Error("Accepted CUSTOM metric with event_codes set.")
	}
}

func TestValidateCustomEventCodesSet(t *testing.T) {
	m := makeValidMetric()
	m.ProtoName = "test.ProtoName"
	m.MetricDimensions[0].EventCodes = map[uint32]string{1: "hello"}

	if err := validateCustom(m); err == nil {
		t.Error("Accepted CUSTOM metric with event_codes set.")
	}
}

func TestValidateCustomNoParts(t *testing.T) {
	m := makeValidMetric()
	m.MetricDimensions[0].EventCodes = map[uint32]string{}
	m.Parts = map[string]*config.MetricPart{}
	if err := validateCustom(m); err == nil {
		t.Error("Accepted CUSTOM metric with no parts.")
	}
}

func TestValidateCustomInvalidPartName(t *testing.T) {
	m := makeValidMetric()
	m.MetricDimensions[0].EventCodes = map[uint32]string{}
	m.Parts = map[string]*config.MetricPart{"_invalid_name": nil}
	if err := validateCustom(m); err == nil {
		t.Error("Accepted CUSTOM metric with invalid part name.")
	}
}

func TestValidateCustomNoProtoName(t *testing.T) {
	m := makeValidMetric()
	m.Parts = map[string]*config.MetricPart{}

	if err := validateCustom(m); err == nil {
		t.Error("Accepted CUSTOM metric with no proto_name.")
	}
}

func TestValidateCustomInvalidProtoName(t *testing.T) {
	m := makeValidMetric()
	m.MetricDimensions[0].EventCodes = map[uint32]string{}
	m.ProtoName = "_invalid.ProtoName"

	if err := validateCustom(m); err == nil {
		t.Error("Accepted CUSTOM metric with invalid proto_name.")
	}
}
