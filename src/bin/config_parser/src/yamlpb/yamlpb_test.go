// Copyright 2017 The Fuchsia Authors
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

package yamlpb

import (
	test_pb "config/config_parser/src/yamlpb"
	"github.com/golang/glog"
	"github.com/golang/protobuf/proto"
	"reflect"
	"testing"
)

// The try-bots expect glog to be imported, but we do not use it.
var _ = glog.Info

// Test that toStrMap accepts valid input.
func TestToStrMapValid(t *testing.T) {
	i := map[interface{}]interface{}{
		"str_key": "str_value",
		"int_key": 10,
		5:         20.5,
		20:        []int{1, 2, 3},
		"dict": map[interface{}]interface{}{
			5: 10,
		},
	}

	o, err := toJsonCompatibleValue(i)
	if err != nil {
		t.Error(err)
	}

	e := map[string]interface{}{
		"str_key": "str_value",
		"int_key": 10,
		"5":       20.5,
		"20":      []int{1, 2, 3},
		"dict": map[string]interface{}{
			"5": 10,
		},
	}

	if !reflect.DeepEqual(o, e) {
		t.Errorf("%v != %v", o, e)
	}
}

// Test that toStrMap rejects invalid input.
func TestToStrMapInvalidKey(t *testing.T) {
	i := map[interface{}]interface{}{
		5.5: "this is messed up",
	}

	_, err := toJsonCompatibleValue(i)
	if err == nil {
		t.Error("Invalid key accepted by toJsonCompatibleValue!")
	}
}

// Test that toStrMap rejects invalid input in a nested map.
func TestToStrMapNestedInvalidKey(t *testing.T) {
	i := map[interface{}]interface{}{
		"dict": map[interface{}]interface{}{
			5.5: 10,
		},
	}

	_, err := toJsonCompatibleValue(i)
	if err == nil {
		t.Error("Invalid key in nested map accepted by toJsonCompatibleValue!")
	}
}

// We test unmarshaling a protobuf from a YAML string to a protobuf message.
func TestUnmarshalString(t *testing.T) {
	s := `
uint32_v: 10
float_v: 0.3
string_v: hello
bool_v: false
enum_v: VAL0
nested_v:
  uint32_v: 1
uint32_r:
- 5
- 10
- 20
nested_r:
- uint32_v: 5
- uint32_v: 10
- uint32_v: 20
second_oneof:
  string_v: something
`
	m := test_pb.TestMessage{}
	if err := UnmarshalString(s, &m); err != nil {
		t.Error(err)
	}

	e := test_pb.TestMessage{
		Uint32V: 10,
		FloatV:  0.3,
		StringV: "hello",
		BoolV:   false,
		EnumV:   test_pb.TestEnum_VAL0,
		NestedV: &test_pb.NestedTestMessage{Uint32V: 1},
		Uint32R: []uint32{5, 10, 20},
		NestedR: []*test_pb.NestedTestMessage{
			&test_pb.NestedTestMessage{Uint32V: 5},
			&test_pb.NestedTestMessage{Uint32V: 10},
			&test_pb.NestedTestMessage{Uint32V: 20},
		},
		NestedOneof: &test_pb.TestMessage_SecondOneof{
			&test_pb.OtherNestedTestMessage{StringV: "something"},
		},
	}
	if !proto.Equal(&m, &e) {
		t.Errorf("%v != %v", m, e)
	}
}

// We test marshaling a protobuf message to a YAML string and a roundtrip
// through marshaling and unmarshaling.
func TestMarshalString(t *testing.T) {
	m := test_pb.TestMessage{
		Uint32V: 10,
		FloatV:  0.3,
		StringV: "hello",
		BoolV:   false,
		EnumV:   test_pb.TestEnum_VAL0,
		NestedV: &test_pb.NestedTestMessage{Uint32V: 1},
		Uint32R: []uint32{5, 10, 20},
		NestedR: []*test_pb.NestedTestMessage{
			&test_pb.NestedTestMessage{Uint32V: 5},
			&test_pb.NestedTestMessage{Uint32V: 10},
			&test_pb.NestedTestMessage{Uint32V: 20},
		},
		NestedOneof: &test_pb.TestMessage_SecondOneof{
			&test_pb.OtherNestedTestMessage{StringV: "something"},
		},
	}

	y, err := MarshalString(&m)
	if err != nil {
		t.Error(err)
	}

	r := test_pb.TestMessage{}
	if err := UnmarshalString(y, &r); err != nil {
		t.Error(err)
	}

	if !proto.Equal(&m, &r) {
		t.Errorf("yamlpb roundtrip failed: %v != %v", m, r)
	}
}

// We test unmarshaling a protobuf from a YAML string containing a YAML anchor.
func TestUnmarshalStringWithAnchor(t *testing.T) {
	s := `
nested_r:
- &anchor_nested_r
  uint32_v: 5
- uint32_v: 7
- *anchor_nested_r
`

	m := test_pb.TestMessage{}
	if err := UnmarshalString(s, &m); err != nil {
		t.Error(err)
	}

	e := test_pb.TestMessage{
		NestedR: []*test_pb.NestedTestMessage{
			&test_pb.NestedTestMessage{Uint32V: 5},
			&test_pb.NestedTestMessage{Uint32V: 7},
			&test_pb.NestedTestMessage{Uint32V: 5},
		},
	}
	if !proto.Equal(&m, &e) {
		t.Errorf("%v != %v", m, e)
	}
}

// We test unmarshaling a protobuf from a YAML string containing a YAML alias.
func TestUnmarshalStringWithAlias(t *testing.T) {
	s := `
uint32_r:
- &num_alias 10
- 5
- *num_alias
`

	m := test_pb.TestMessage{}
	if err := UnmarshalString(s, &m); err != nil {
		t.Error(err)
	}

	e := test_pb.TestMessage{
		Uint32R: []uint32{10, 5, 10},
	}
	if !proto.Equal(&m, &e) {
		t.Errorf("%v != %v", m, e)
	}
}

func TestUnmarshalStringWithDuplicateEntry(t *testing.T) {
	s := `
uint32_v: 11
uint32_v: 12
  `

	m := test_pb.TestMessage{}
	if err := UnmarshalString(s, &m); err == nil {
		t.Error("No error returned when unmarshalling string with duplicate keys.")
	}
}
