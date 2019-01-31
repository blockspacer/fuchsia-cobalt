package config_parser

import (
	"config"
	"reflect"
	"strings"

	"github.com/golang/glog"
	"github.com/golang/protobuf/descriptor"
	"github.com/golang/protobuf/proto"
	descriptor_pb "github.com/golang/protobuf/protoc-gen-go/descriptor"
)

func FilterHideOnClient(c *config.CobaltRegistry) {
	filterReflectedValue(reflect.ValueOf(c))
}

func filterWithCobaltOptions(options *config.CobaltOptions, field reflect.Value) {
	if options.HideOnClient {
		// Set the field to its zero value (in proto-land this is the same as
		// unsetting the value).
		field.Set(reflect.Zero(field.Type()))
	}
}

func filterPointer(rc reflect.Value) {
	if rc.IsNil() {
		return
	}

	// get the value of the pointer.
	msgValue := rc.Elem()

	// Check if we can treat rc as a descriptor.Message
	msgInterface, ok := rc.Interface().(descriptor.Message)

	if !ok {
		// This might be a oneof wrapper struct, so we treat it as a generic
		// struct and blindly recurse through all fields.
		filterReflectedValue(msgValue)
		return
	}

	// The way that golang handles oneof fields does not map 1:1 onto the logical
	// protobuf structure. It is implemented as a golang interface for each of the
	// possible variants. Since we can't easily look up the descriptor information
	// for individual oneof variants, we ignore them and simply try to filter the
	// value.
	for i := 0; i < msgValue.NumField(); i++ {
		if msgValue.Type().Field(i).Tag.Get("protobuf_oneof") != "" {
			filterReflectedValue(reflect.ValueOf(msgValue.Field(i).Interface()))
		}
	}

	// Get message's proto descriptor information
	_, msgDescriptor := descriptor.ForMessage(msgInterface)
	for _, field := range msgDescriptor.GetField() {
		filterMessageField(field, msgValue)
	}
}

func findFieldWithinStruct(name string, rc reflect.Value) (foundField int) {
	foundField = -1

	nameTag := "name=" + name + ","
	// Search through the struct for a matching field tag (there's no way to
	// convert field.GetName() to the generated field name, so we're stuck
	// with this See: https://github.com/golang/protobuf/issues/457)
	for i := 0; i < rc.NumField(); i++ {
		// The go protobuf compiler adds tags to the generated go code. One of those
		// tags is called 'protobuf' and contains key-value entries, one of which is
		// the name of the field in the proto file.
		if strings.Contains(rc.Type().Field(i).Tag.Get("protobuf"), nameTag) {
			foundField = i
			break
		}
	}

	return foundField
}

func filterMessageField(field *descriptor_pb.FieldDescriptorProto, msgValue reflect.Value) {
	foundField := findFieldWithinStruct(field.GetName(), msgValue)

	// Oneof fields are not treated the same as other fields, so there will be
	// a "field" at this level, that has no corresponding real field in the
	// struct. This means that we have no straightforward way of filtering
	// individual oneof variants, or a oneof field as a whole. We can filter
	// within the sub-messages, but at the level of oneof, our handling is subtly
	// broken.
	if foundField < 0 {
		return
	}

	// Finally, we can extract the CobaltOptions extension.
	if options_extension, err := proto.GetExtension(field.GetOptions(), config.E_CobaltOptions); err == nil {
		filterWithCobaltOptions(options_extension.(*config.CobaltOptions), msgValue.Field(foundField))
	}

	// Recurse for the sub message.
	filterReflectedValue(msgValue.Field(foundField))
}

func filterReflectedValue(rc reflect.Value) {
	switch rc.Kind() {
	case reflect.Ptr:
		filterPointer(rc)
	case reflect.Struct:
		for i := 0; i < rc.NumField(); i++ {
			filterReflectedValue(rc.Field(i))
		}
	case reflect.Array, reflect.Slice:
		for i := 0; i < rc.Len(); i++ {
			filterReflectedValue(rc.Index(i))
		}
	case reflect.Map:
		for _, k := range rc.MapKeys() {
			filterReflectedValue(rc.MapIndex(k))
		}
	case
		reflect.Uint, reflect.Int,
		reflect.Uint8, reflect.Int8,
		reflect.Uint16, reflect.Int16,
		reflect.Uint32, reflect.Int32,
		reflect.Uint64, reflect.Int64,
		reflect.Float32, reflect.Float64,
		reflect.String, reflect.Bool:
		// These are expected base-cases. It may be extended when more field types are
		// used in the config proto.
		return
	case reflect.Invalid:
		// This is likely generated by filter_test.go.
	default:
		glog.Exitf("Unexpected type found in protobuf: %v. If this is expected, add another value to the case in filter.go", rc.Kind())
	}
}
