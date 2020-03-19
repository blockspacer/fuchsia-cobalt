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

// This package enables unmarshaling YAML strings into protobuf messages. We do
// this by using the jsonpb package. We first marshal the YAML string into go
// types unsing the go-yaml package. Then, we convert those to json-compatible
// types. We then marshal this data structure to JSON and finally we use jsonpb
// to unmarshal that string to the protobuf message.

package yamlpb

import (
	"encoding/json"
	"fmt"
	yaml "github.com/go-yaml/yaml"
	jsonpb "github.com/golang/protobuf/jsonpb"
	proto "github.com/golang/protobuf/proto"
	"strconv"
)

// toJsonCompatibleValue recursively converts the YAML-compatible value to a
// JSON-comptaible value if possible.
func toJsonCompatibleValue(i interface{}) (o interface{}, err error) {
	switch t := i.(type) {
	case map[interface{}]interface{}:
		return toStrMap(t)
	case []byte:
		return t, nil
	case []interface{}:
		var arr []interface{}
		for _, v := range t {
			var j interface{}
			j, err = toJsonCompatibleValue(v)
			if err != nil {
				return nil, err
			}
			arr = append(arr, j)
		}
		return arr, nil
	}
	return i, nil
}

// toStrMap creates a string-indexed map based on the provided map if possible.
// YAML supports many different kinds of indices in maps. JSON only supports
// string-indexed maps and protobuf only supports string and integer-indexed
// maps. So, we convert integer-indexed maps to string maps according to
// https://developers.google.com/protocol-buffers/docs/proto3#json
// and recursively convert values to JSON-compatible types.
func toStrMap(i map[interface{}]interface{}) (o map[string]interface{}, err error) {
	o = make(map[string]interface{})
	for k, v := range i {
		var s string
		switch t := k.(type) {
		case string:
			s = t
		case int:
			s = strconv.FormatInt(int64(t), 10)
		default:
			err = fmt.Errorf("'%v' is not a string or integer. Only string and integer keys are supported.", k)
			return nil, err
		}
		o[s], err = toJsonCompatibleValue(v)
		if err != nil {
			return nil, err
		}
	}
	return o, nil
}

// UnmarshalString will populate the fields of a protocol buffer based on a YAML
// string.
func UnmarshalString(s string, pb proto.Message) error {
	// First, we unmarshal the yaml string into go types.
	var m interface{}
	if err := yaml.UnmarshalStrict([]byte(s), &m); err != nil {
		return fmt.Errorf("Cannot unmarshal yaml string: %v", err)
	}

	// Then, we ensure that only JSON-compatible values are used since YAML
	// is a superset of JSON.
	v, err := toJsonCompatibleValue(m)
	if err != nil {
		return err
	}

	// We marshal to JSON.
	var j []byte
	j, err = json.Marshal(v)
	if err != nil {
		return err
	}

	// And finally, we unmarshal to proto.
	if err := jsonpb.UnmarshalString(string(j), pb); err != nil {
		return err
	}

	return nil
}

// MarshalString marshals a protobuf message to a YAML string.
func MarshalString(pb proto.Message) (string, error) {
	// First, we marshal proto to JSON to recover the original field names.
	// We need to do this because if we marshal the proto.Message directly to
	// YAML, it will use the mangled-for-go names.
	ma := jsonpb.Marshaler{
		EnumsAsInts:  false,
		EmitDefaults: false,
		OrigName:     true,
	}

	js, err := ma.MarshalToString(pb)
	if err != nil {
		return "", err
	}

	// Then we unmarshal from JSON.
	var j interface{}
	if err := json.Unmarshal([]byte(js), &j); err != nil {
		return "", err
	}

	// Finally, we marshal to YAML.
	var y []byte
	y, err = yaml.Marshal(j)
	if err != nil {
		return "", err
	}

	return string(y), nil
}
