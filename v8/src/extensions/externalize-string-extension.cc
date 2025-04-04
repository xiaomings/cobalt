// Copyright 2010 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/extensions/externalize-string-extension.h"

#include "include/v8-template.h"
#include "src/api/api-inl.h"
#include "src/base/strings.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

template <typename Char, typename Base>
class SimpleStringResource : public Base {
 public:
  // Takes ownership of |data|.
  SimpleStringResource(Char* data, size_t length)
      : data_(data),
        length_(length) {}

  ~SimpleStringResource() override { delete[] data_; }

  const Char* data() const override { return data_; }

  size_t length() const override { return length_; }

 private:
  Char* const data_;
  const size_t length_;
};

using SimpleOneByteStringResource =
    SimpleStringResource<char, v8::String::ExternalOneByteStringResource>;
using SimpleTwoByteStringResource =
    SimpleStringResource<base::uc16, v8::String::ExternalStringResource>;

const char* const ExternalizeStringExtension::kSource =
    "native function externalizeString();"
    "native function isOneByteString();"
    "function x() { return 1; }";

v8::Local<v8::FunctionTemplate>
ExternalizeStringExtension::GetNativeFunctionTemplate(
    v8::Isolate* isolate, v8::Local<v8::String> str) {
  if (strcmp(*v8::String::Utf8Value(isolate, str), "externalizeString") == 0) {
    return v8::FunctionTemplate::New(isolate,
                                     ExternalizeStringExtension::Externalize);
  } else {
    DCHECK_EQ(strcmp(*v8::String::Utf8Value(isolate, str), "isOneByteString"),
              0);
    return v8::FunctionTemplate::New(isolate,
                                     ExternalizeStringExtension::IsOneByte);
  }
}

namespace {

bool HasExternalForwardingIndex(Isolate* isolate, Handle<String> string) {
  if (!string->IsShared(isolate)) return false;
  uint32_t raw_hash = string->raw_hash_field(kAcquireLoad);
  return Name::IsExternalForwardingIndex(raw_hash);
}

}  // namespace

void ExternalizeStringExtension::Externalize(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  if (info.Length() < 1 || !info[0]->IsString()) {
    info.GetIsolate()->ThrowError(
        "First parameter to externalizeString() must be a string.");
    return;
  }
  bool force_two_byte = false;
  if (info.Length() >= 2) {
    if (info[1]->IsBoolean()) {
      force_two_byte = info[1]->BooleanValue(info.GetIsolate());
    } else {
      info.GetIsolate()->ThrowError(
          "Second parameter to externalizeString() must be a boolean.");
      return;
    }
  }
  bool result = false;
  Handle<String> string = Utils::OpenHandle(*info[0].As<v8::String>());
  if (!string->SupportsExternalization()) {
    info.GetIsolate()->ThrowError("string does not support externalization.");
    return;
  }
  if (string->IsOneByteRepresentation() && !force_two_byte) {
    uint8_t* data = new uint8_t[string->length()];
    String::WriteToFlat(*string, data, 0, string->length());
    SimpleOneByteStringResource* resource = new SimpleOneByteStringResource(
        reinterpret_cast<char*>(data), string->length());
    result = Utils::ToLocal(string)->MakeExternal(resource);
    if (!result) delete resource;
  } else {
    base::uc16* data = new base::uc16[string->length()];
    String::WriteToFlat(*string, data, 0, string->length());
    SimpleTwoByteStringResource* resource = new SimpleTwoByteStringResource(
        data, string->length());
    result = Utils::ToLocal(string)->MakeExternal(resource);
    if (!result) delete resource;
  }
  // If the string is shared, testing with the combination of
  // --shared-string-table and --isolate in d8 may result in races to
  // externalize the same string. Those races manifest as externalization
  // sometimes failing if another thread won and already forwarded the string to
  // the external resource. Don't consider those races as failures.
  if (!result && !HasExternalForwardingIndex(
                     reinterpret_cast<Isolate*>(info.GetIsolate()), string)) {
    info.GetIsolate()->ThrowError("externalizeString() failed.");
    return;
  }
}

void ExternalizeStringExtension::IsOneByte(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  if (info.Length() != 1 || !info[0]->IsString()) {
    info.GetIsolate()->ThrowError(
        "isOneByteString() requires a single string argument.");
    return;
  }
  bool is_one_byte =
      Utils::OpenHandle(*info[0].As<v8::String>())->IsOneByteRepresentation();
  info.GetReturnValue().Set(is_one_byte);
}

}  // namespace internal
}  // namespace v8
