/*
 *  Copyright (C) 2003, 2006, 2008 Apple Inc. All rights reserved.
 *  Copyright (C) 2005, 2006 Alexey Proskuryakov <ap@nypop.com>
 *  Copyright (C) 2011 Google Inc. All rights reserved.
 *  Copyright (C) 2012 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301 USA
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_XMLHTTPREQUEST_XML_HTTP_REQUEST_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_XMLHTTPREQUEST_XML_HTTP_REQUEST_H_

#include <memory>

#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/network/public/mojom/attribution.mojom-blink.h"
#include "services/network/public/mojom/trust_tokens.mojom-blink.h"
#include "services/network/public/mojom/url_loader_factory.mojom-blink.h"
#include "third_party/blink/renderer/bindings/core/v8/active_script_wrappable.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_typedefs.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/dom/document_parser_client.h"
#include "third_party/blink/renderer/core/dom/dom_exception.h"
#include "third_party/blink/renderer/core/execution_context/execution_context_lifecycle_observer.h"
#include "third_party/blink/renderer/core/loader/threadable_loader_client.h"
#include "third_party/blink/renderer/core/probe/async_task_context.h"
#include "third_party/blink/renderer/core/xmlhttprequest/xml_http_request_event_target.h"
#include "third_party/blink/renderer/core/xmlhttprequest/xml_http_request_progress_event_throttle.h"
#include "third_party/blink/renderer/platform/bindings/exception_code.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/bindings/trace_wrapper_v8_string.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_response.h"
#include "third_party/blink/renderer/platform/network/encoded_form_data.h"
#include "third_party/blink/renderer/platform/network/http_header_map.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cancellable_task.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class AttributionReportingRequestOptions;
class Blob;
class BlobDataHandle;
class DOMArrayBuffer;
class DOMArrayBufferView;
class DOMWrapperWorld;
class Document;
class DocumentParser;
class ExceptionState;
class ExecutionContext;
class FormData;
class PrivateToken;
class ScriptState;
class TextResourceDecoder;
class ThreadableLoader;
class URLSearchParams;
class XMLHttpRequestUpload;

class CORE_EXPORT XMLHttpRequest final
    : public XMLHttpRequestEventTarget,
      public ThreadableLoaderClient,
      public DocumentParserClient,
      public ActiveScriptWrappable<XMLHttpRequest>,
      public ExecutionContextLifecycleObserver {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static XMLHttpRequest* Create(ScriptState*);
  static XMLHttpRequest* Create(ExecutionContext*);

  XMLHttpRequest(ExecutionContext*,
                 v8::Isolate*,
                 scoped_refptr<const DOMWrapperWorld> world);
  ~XMLHttpRequest() override;

  // These exact numeric values are important because JS expects them.
  enum State {
    kUnsent = 0,
    kOpened = 1,
    kHeadersReceived = 2,
    kLoading = 3,
    kDone = 4
  };

  enum ResponseTypeCode {
    kResponseTypeDefault,
    kResponseTypeText,
    kResponseTypeJSON,
    kResponseTypeDocument,
    kResponseTypeBlob,
    kResponseTypeArrayBuffer,
  };

  // ExecutionContextLifecycleObserver
  void ContextDestroyed() override;
  ExecutionContext* GetExecutionContext() const override;

  // ScriptWrappable
  bool HasPendingActivity() const final;

  // XMLHttpRequestEventTarget
  const AtomicString& InterfaceName() const override;

  // JavaScript attributes and methods
  const KURL& Url() const { return url_; }
  String statusText() const;
  int status() const;
  State readyState() const;
  bool withCredentials() const { return with_credentials_; }
  void setWithCredentials(bool, ExceptionState&);
  bool deprecatedBrowsingTopics() const { return deprecated_browsing_topics_; }
  void setDeprecatedBrowsingTopics(bool);
  void open(const AtomicString& method, const String& url, ExceptionState&);
  void open(const AtomicString& method,
            const String& url,
            bool async,
            const String& username,
            const String& password,
            ExceptionState&);
  void open(const AtomicString& method,
            const KURL&,
            bool async,
            ExceptionState&);
  void send(const V8UnionDocumentOrXMLHttpRequestBodyInit* body,
            ExceptionState& exception_state);
  void abort();
  void Dispose();
  void setRequestHeader(const AtomicString& name,
                        const AtomicString& value,
                        ExceptionState&);
  void setPrivateToken(const PrivateToken*, ExceptionState&);
  void setAttributionReporting(const AttributionReportingRequestOptions*,
                               ExceptionState&);
  void overrideMimeType(const AtomicString& override, ExceptionState&);
  String getAllResponseHeaders() const;
  const AtomicString& getResponseHeader(const AtomicString&) const;
  v8::Local<v8::String> responseText(ExceptionState&);
  v8::Local<v8::String> ResponseJSONSource();
  Document* responseXML(ExceptionState&);
  Blob* ResponseBlob();
  DOMArrayBuffer* ResponseArrayBuffer();
  unsigned timeout() const {
    return static_cast<unsigned>(timeout_.InMilliseconds());
  }
  void setTimeout(unsigned timeout, ExceptionState&);
  ResponseTypeCode GetResponseTypeCode() const { return response_type_code_; }
  String responseType();
  void setResponseType(const String&, ExceptionState&);
  String responseURL();
  DOMException* privateTokenOperationError() const {
    return trust_token_operation_error_;
  }

  // For Inspector.
  void SendForInspectorXHRReplay(scoped_refptr<EncodedFormData>,
                                 ExceptionState&);

  XMLHttpRequestUpload* upload();
  bool IsAsync() { return async_; }

  probe::AsyncTaskContext* async_task_context() { return &async_task_context_; }

  DEFINE_ATTRIBUTE_EVENT_LISTENER(readystatechange, kReadystatechange)

  void Trace(Visitor*) const override;
  const char* NameInHeapSnapshot() const override { return "XMLHttpRequest"; }

  bool HasRequestHeaderForTesting(AtomicString name) const;

 private:
  class BlobLoader;

  void DidSendData(uint64_t bytes_sent,
                   uint64_t total_bytes_to_be_sent) override;
  void DidReceiveResponse(uint64_t identifier,
                          const ResourceResponse&) override;
  void DidReceiveData(const char* data, unsigned data_length) override;
  // When responseType is set to "blob", didDownloadData() is called instead
  // of didReceiveData().
  void DidDownloadData(uint64_t data_length) override;
  void DidDownloadToBlob(scoped_refptr<BlobDataHandle>) override;
  void DidFinishLoading(uint64_t identifier) override;
  void DidFail(uint64_t, const ResourceError&) override;
  void DidFailRedirectCheck(uint64_t) override;

  // BlobLoader notifications.
  void DidFinishLoadingInternal();
  void DidFinishLoadingFromBlob();
  void DidFailLoadingFromBlob();

  // DocumentParserClient
  void NotifyParserStopped() override;

  void EndLoading();

  // Returns the MIME type part of mime_type_override_ if present and
  // successfully parsed, or returns one of the "Content-Type" header value
  // of the received response.
  //
  // This method is named after the term "final MIME type" defined in the
  // spec but doesn't convert the result to ASCII lowercase as specified in
  // the spec. Must be lowered later or compared using case insensitive
  // comparison functions if required.
  AtomicString FinalResponseMIMETypeInternal() const;
  // The same as finalResponseMIMEType() but fallbacks to "text/xml" if
  // finalResponseMIMEType() returns an empty string.
  // https://xhr.spec.whatwg.org/#response-body
  AtomicString GetResponseMIMEType() const;
  // Returns the "final charset" defined in
  // https://xhr.spec.whatwg.org/#final-charset.
  WTF::TextEncoding FinalResponseCharset() const;
  bool ResponseIsXML() const;
  bool ResponseIsHTML() const;

  std::unique_ptr<TextResourceDecoder> CreateDecoder() const;

  void InitResponseDocument();
  void ParseDocumentChunk(const char* data, unsigned data_length);

  bool AreMethodAndURLValidForSend();

  void ThrowForLoadFailureIfNeeded(ExceptionState&, const String&);

  bool InitSend(ExceptionState&);
  void SendBytesData(const void*, size_t, ExceptionState&);
  void send(Document*, ExceptionState&);
  void send(const String&, ExceptionState&);
  void send(Blob*, ExceptionState&);
  void send(FormData*, ExceptionState&);
  void send(URLSearchParams*, ExceptionState&);
  void send(DOMArrayBuffer*, ExceptionState&);
  void send(DOMArrayBufferView*, ExceptionState&);

  bool HasContentTypeRequestHeader() const;
  void SetRequestHeaderInternal(const AtomicString& name,
                                const AtomicString& value);

  void TrackProgress(uint64_t data_length);
  // Changes m_state and dispatches a readyStateChange event if new m_state
  // value is different from last one.
  void ChangeState(State new_state);
  void DispatchReadyStateChangeEvent();

  // Clears variables used only while the resource is being loaded.
  void ClearVariablesForLoading();
  // Clears state and cancels loader.
  void InternalAbort();
  // Clears variables holding response header and body data.
  void ClearResponse();
  void ClearRequest();

  void CreateRequest(scoped_refptr<EncodedFormData>, ExceptionState&);

  // Dispatches a response ProgressEvent.
  void DispatchProgressEvent(const AtomicString&, int64_t, int64_t);
  // Dispatches a response ProgressEvent using values sampled from
  // m_receivedLength and m_response.
  void DispatchProgressEventFromSnapshot(const AtomicString&);

  // Handles didFail() call not caused by cancellation or timeout.
  void HandleNetworkError();
  // Handles didFail() call for cancellations. For example, the
  // ResourceLoader handling the load notifies m_loader of an error
  // cancellation when the frame containing the XHR navigates away.
  void HandleDidCancel();
  // Handles didFail() call for timeout.
  void HandleDidTimeout();

  void HandleRequestError(DOMExceptionCode, const AtomicString&);

  void UpdateContentTypeAndCharset(const AtomicString& content_type,
                                   const String& charset);

  XMLHttpRequestProgressEventThrottle& ProgressEventThrottle();

  // Report the memory usage associated with this object to V8 so that V8 can
  // schedule GC accordingly.  This function should be called whenever the
  // internal memory usage changes except for the following members.
  // - response_text_ of type TraceWrapperV8String
  //   ScriptString internally creates and holds a v8::String, so V8 is aware of
  //   its memory usage.
  // - response_array_buffer_ of type DOMArrayBuffer
  //   DOMArrayBuffer supports the memory usage reporting system on their own,
  //   so there is no need.
  void ReportMemoryUsageToV8();

  Member<XMLHttpRequestUpload> upload_;

  KURL url_;
  mojo::PendingRemote<network::mojom::blink::URLLoaderFactory>
      blob_url_loader_factory_;
  AtomicString method_;
  HTTPHeaderMap request_headers_;
  network::mojom::blink::TrustTokenParamsPtr trust_token_params_;
  Member<DOMException> trust_token_operation_error_;
  // Not converted to ASCII lowercase. Must be lowered later or compared
  // using case insensitive comparison functions if needed.
  AtomicString mime_type_override_;
  base::TimeDelta timeout_;
  Member<Blob> response_blob_;

  TaskHandle pending_abort_event_;

  Member<ThreadableLoader> loader_;
  State state_ = kUnsent;

  ResourceResponse response_;

  std::unique_ptr<TextResourceDecoder> decoder_;

  // Avoid using a flat WTF::String here and rather use a traced v8::String
  // which internally builds a string rope.
  TraceWrapperV8String response_text_;
  Member<Document> response_document_;
  Member<DocumentParser> response_document_parser_;

  scoped_refptr<SharedBuffer> binary_response_builder_;
  size_t binary_response_builder_last_reported_size_ = 0;
  int64_t length_downloaded_to_blob_ = 0;
  int64_t length_downloaded_to_blob_last_reported_ = 0;

  Member<DOMArrayBuffer> response_array_buffer_;

  // Used for onprogress tracking
  int64_t received_length_ = 0;

  // An exception to throw in synchronous mode. It's set when failure
  // notification is received from m_loader and thrown at the end of send() if
  // any.
  DOMExceptionCode exception_code_ = DOMExceptionCode::kNoError;

  Member<XMLHttpRequestProgressEventThrottle> progress_event_throttle_;

  // An enum corresponding to the allowed string values for the responseType
  // attribute.
  ResponseTypeCode response_type_code_ = kResponseTypeDefault;

  v8::Isolate* const isolate_;
  // The DOMWrapperWorld in which the request initiated. Can be null.
  scoped_refptr<const DOMWrapperWorld> world_;
  // Stores the SecurityOrigin associated with the |world_| if it's an isolated
  // world.
  scoped_refptr<const SecurityOrigin> isolated_world_security_origin_;

  // This blob loader will be used if |m_downloadingToFile| is true and
  // |m_responseTypeCode| is NOT ResponseTypeBlob.
  Member<BlobLoader> blob_loader_;

  // Positive if we are dispatching events.
  // This is an integer specifying the recursion level rather than a boolean
  // because in some cases we have recursive dispatching.
  int event_dispatch_recursion_level_ = 0;

  bool async_ = true;

  bool with_credentials_ = false;

  bool deprecated_browsing_topics_ = false;

  network::mojom::AttributionReportingEligibility
      attribution_reporting_eligibility_ =
          network::mojom::AttributionReportingEligibility::kUnset;

  // Used to skip m_responseDocument creation if it's done previously. We need
  // this separate flag since m_responseDocument can be 0 for some cases.
  bool parsed_response_ = false;
  bool error_ = false;
  bool upload_events_allowed_ = true;
  bool upload_complete_ = false;
  // True iff the ongoing resource loading is using the downloadToBlob
  // option.
  bool downloading_to_blob_ = false;
  bool response_text_overflow_ = false;
  bool send_flag_ = false;
  bool response_array_buffer_failure_ = false;

  probe::AsyncTaskContext async_task_context_;
};

std::ostream& operator<<(std::ostream&, const XMLHttpRequest*);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_XMLHTTPREQUEST_XML_HTTP_REQUEST_H_
