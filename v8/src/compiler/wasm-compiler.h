// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_WASM_COMPILER_H_
#define V8_COMPILER_WASM_COMPILER_H_

#include <memory>
#include <utility>

// Clients of this interface shouldn't depend on lots of compiler internals.
// Do not include anything else from src/compiler here!
#include "src/base/small-vector.h"
#include "src/compiler/wasm-compiler-definitions.h"
#include "src/runtime/runtime.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/module-instantiate.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes.h"
#include "src/wasm/wasm-result.h"
#include "src/zone/zone.h"

namespace v8 {

class CFunctionInfo;

namespace internal {
struct AssemblerOptions;
class TurbofanCompilationJob;
enum class BranchHint : uint8_t;

namespace compiler {
// Forward declarations for some compiler data structures.
class CallDescriptor;
class Graph;
class MachineGraph;
class Node;
class NodeOriginTable;
class Operator;
class SourcePositionTable;
struct WasmCompilationData;
class WasmDecorator;
class WasmGraphAssembler;
enum class TrapId : uint32_t;
struct Int64LoweringSpecialCase;
template <size_t VarCount>
class GraphAssemblerLabel;
struct WasmTypeCheckConfig;
}  // namespace compiler

namespace wasm {
class AssemblerBufferCache;
struct DecodeStruct;
class WasmCode;
class WireBytesStorage;
enum class LoadTransformationKind : uint8_t;
enum Suspend : bool;
}  // namespace wasm

namespace compiler {

wasm::WasmCompilationResult ExecuteTurbofanWasmCompilation(
    wasm::CompilationEnv*, WasmCompilationData& compilation_data, Counters*,
    wasm::WasmFeatures* detected);

// Compiles an import call wrapper, which allows Wasm to call imports.
V8_EXPORT_PRIVATE wasm::WasmCompilationResult CompileWasmImportCallWrapper(
    wasm::CompilationEnv* env, wasm::ImportCallKind, const wasm::FunctionSig*,
    bool source_positions, int expected_arity, wasm::Suspend);

// Compiles a host call wrapper, which allows Wasm to call host functions.
wasm::WasmCode* CompileWasmCapiCallWrapper(wasm::NativeModule*,
                                           const wasm::FunctionSig*);

bool IsFastCallSupportedSignature(const v8::CFunctionInfo*);
// Compiles a wrapper to call a Fast API function from Wasm.
wasm::WasmCode* CompileWasmJSFastCallWrapper(wasm::NativeModule*,
                                             const wasm::FunctionSig*,
                                             Handle<JSReceiver> callable);

// Returns an TurbofanCompilationJob object for a JS to Wasm wrapper.
std::unique_ptr<TurbofanCompilationJob> NewJSToWasmCompilationJob(
    Isolate* isolate, const wasm::FunctionSig* sig,
    const wasm::WasmModule* module, bool is_import,
    const wasm::WasmFeatures& enabled_features);

MaybeHandle<Code> CompileWasmToJSWrapper(Isolate* isolate,
                                         const wasm::FunctionSig* sig,
                                         wasm::ImportCallKind kind,
                                         int expected_arity,
                                         wasm::Suspend suspend);

// Compiles a stub with JS linkage that serves as an adapter for function
// objects constructed via {WebAssembly.Function}. It performs a round-trip
// simulating a JS-to-Wasm-to-JS coercion of parameter and return values.
MaybeHandle<Code> CompileJSToJSWrapper(Isolate*, const wasm::FunctionSig*,
                                       const wasm::WasmModule* module);

enum CWasmEntryParameters {
  kCodeEntry,
  kObjectRef,
  kArgumentsBuffer,
  kCEntryFp,
  // marker:
  kNumParameters
};

// Compiles a stub with C++ linkage, to be called from Execution::CallWasm,
// which knows how to feed it its parameters.
V8_EXPORT_PRIVATE Handle<Code> CompileCWasmEntry(
    Isolate*, const wasm::FunctionSig*, const wasm::WasmModule* module);

// Values from the instance object are cached between Wasm-level function calls.
// This struct allows the SSA environment handling this cache to be defined
// and manipulated in wasm-compiler.{h,cc} instead of inside the Wasm decoder.
// (Note that currently, the globals base is immutable, so not cached here.)
struct WasmInstanceCacheNodes {
  Node* mem_start = nullptr;
  Node* mem_size = nullptr;
};

struct WasmLoopInfo {
  Node* header;
  uint32_t nesting_depth;
  // This loop has, to our best knowledge, no other loops nested within it. A
  // loop can obtain inner loops despite this after inlining.
  bool can_be_innermost;

  WasmLoopInfo(Node* header, uint32_t nesting_depth, bool can_be_innermost)
      : header(header),
        nesting_depth(nesting_depth),
        can_be_innermost(can_be_innermost) {}
};

struct WasmCompilationData {
  explicit WasmCompilationData(const wasm::FunctionBody& func_body)
      : func_body(func_body) {}

  size_t body_size() { return func_body.end - func_body.start; }

  const wasm::FunctionBody& func_body;
  const wasm::WireBytesStorage* wire_bytes_storage;
  wasm::AssemblerBufferCache* buffer_cache;
  NodeOriginTable* node_origins{nullptr};
  std::vector<WasmLoopInfo>* loop_infos{nullptr};
  wasm::AssumptionsJournal* assumptions{nullptr};
  SourcePositionTable* source_positions{nullptr};
  int func_index;
};

// Abstracts details of building TurboFan graph nodes for wasm to separate
// the wasm decoder from the internal details of TurboFan.
class WasmGraphBuilder {
 public:
  // The parameter at index 0 in a wasm function has special meaning:
  // - For normal wasm functions, it points to the function's instance.
  // - For Wasm-to-JS and C-API wrappers, it points to a {WasmApiFunctionRef}
  //   object which represents the function's context.
  // - For JS-to-Wasm and JS-to-JS wrappers (which are JS functions), it does
  //   not have a special meaning. In these cases, we need access to an isolate
  //   at compile time, i.e., {isolate_} needs to be non-null.
  enum Parameter0Mode {
    kInstanceMode,
    kWasmApiFunctionRefMode,
    kNoSpecialParameterMode
  };
  enum EnforceBoundsCheck : bool {  // --
    kNeedsBoundsCheck = true,
    kCanOmitBoundsCheck = false
  };
  enum BoundsCheckResult {
    // Statically OOB.
    kOutOfBounds,
    // Dynamically checked (using 1-2 conditional branches).
    kDynamicallyChecked,
    // OOB handled via the trap handler.
    kTrapHandler,
    // Statically known to be in bounds.
    kInBounds
  };

  V8_EXPORT_PRIVATE WasmGraphBuilder(
      wasm::CompilationEnv* env, Zone* zone, MachineGraph* mcgraph,
      const wasm::FunctionSig* sig,
      compiler::SourcePositionTable* spt = nullptr)
      : WasmGraphBuilder(env, zone, mcgraph, sig, spt, kInstanceMode, nullptr,
                         env->enabled_features) {}

  V8_EXPORT_PRIVATE WasmGraphBuilder(wasm::CompilationEnv* env, Zone* zone,
                                     MachineGraph* mcgraph,
                                     const wasm::FunctionSig* sig,
                                     compiler::SourcePositionTable* spt,
                                     Parameter0Mode parameter_mode,
                                     Isolate* isolate,
                                     wasm::WasmFeatures enabled_features);

  V8_EXPORT_PRIVATE ~WasmGraphBuilder();

  bool TryWasmInlining(int fct_index, wasm::NativeModule* native_module);

  //-----------------------------------------------------------------------
  // Operations independent of {control} or {effect}.
  //-----------------------------------------------------------------------
  void Start(unsigned params);
  Node* Param(int index, const char* debug_name = nullptr);
  Node* Loop(Node* entry);
  void TerminateLoop(Node* effect, Node* control);
  Node* LoopExit(Node* loop_node);
  // Assumes current control() is the corresponding loop exit.
  Node* LoopExitValue(Node* value, MachineRepresentation representation);
  void TerminateThrow(Node* effect, Node* control);
  Node* Merge(unsigned count, Node** controls);
  template <typename... Nodes>
  Node* Merge(Node* fst, Nodes*... args);
  Node* Phi(wasm::ValueType type, unsigned count, Node** vals_and_control);
  Node* CreateOrMergeIntoPhi(MachineRepresentation rep, Node* merge,
                             Node* tnode, Node* fnode);
  Node* CreateOrMergeIntoEffectPhi(Node* merge, Node* tnode, Node* fnode);
  Node* EffectPhi(unsigned count, Node** effects_and_control);
  Node* RefNull(wasm::ValueType type);
  Node* RefFunc(uint32_t function_index);
  Node* AssertNotNull(
      Node* object, wasm::ValueType type, wasm::WasmCodePosition position,
      wasm::TrapReason reason = wasm::TrapReason::kTrapNullDereference);
  Node* TraceInstruction(uint32_t mark_id);
  Node* Int32Constant(int32_t value);
  Node* Int64Constant(int64_t value);
  Node* Float32Constant(float value);
  Node* Float64Constant(double value);
  Node* Simd128Constant(const uint8_t value[16]);
  Node* Binop(wasm::WasmOpcode opcode, Node* left, Node* right,
              wasm::WasmCodePosition position = wasm::kNoCodePosition);
  // The {type} argument is only required for null-checking operations.
  Node* Unop(wasm::WasmOpcode opcode, Node* input,
             wasm::ValueType type = wasm::kWasmBottom,
             wasm::WasmCodePosition position = wasm::kNoCodePosition);
  Node* MemoryGrow(Node* input);
  Node* Throw(uint32_t tag_index, const wasm::WasmTag* tag,
              const base::Vector<Node*> values,
              wasm::WasmCodePosition position);
  Node* Rethrow(Node* except_obj);
  Node* ExceptionTagEqual(Node* caught_tag, Node* expected_tag);
  Node* LoadTagFromTable(uint32_t tag_index);
  Node* GetExceptionTag(Node* except_obj);
  Node* GetExceptionValues(Node* except_obj, const wasm::WasmTag* tag,
                           base::Vector<Node*> values_out);
  bool IsPhiWithMerge(Node* phi, Node* merge);
  bool ThrowsException(Node* node, Node** if_success, Node** if_exception);
  void AppendToMerge(Node* merge, Node* from);
  void AppendToPhi(Node* phi, Node* from);

  void StackCheck(WasmInstanceCacheNodes* shared_memory_instance_cache,
                  wasm::WasmCodePosition);

  void PatchInStackCheckIfNeeded();

  //-----------------------------------------------------------------------
  // Operations that read and/or write {control} and {effect}.
  //-----------------------------------------------------------------------
  Node* BranchNoHint(Node* cond, Node** true_node, Node** false_node);
  Node* BranchExpectFalse(Node* cond, Node** true_node, Node** false_node);
  Node* BranchExpectTrue(Node* cond, Node** true_node, Node** false_node);

  void TrapIfTrue(wasm::TrapReason reason, Node* cond,
                  wasm::WasmCodePosition position);
  void TrapIfFalse(wasm::TrapReason reason, Node* cond,
                   wasm::WasmCodePosition position);
  Node* Select(Node *cond, Node* true_node, Node* false_node,
               wasm::ValueType type);

  void TrapIfEq32(wasm::TrapReason reason, Node* node, int32_t val,
                  wasm::WasmCodePosition position);
  void ZeroCheck32(wasm::TrapReason reason, Node* node,
                   wasm::WasmCodePosition position);
  void TrapIfEq64(wasm::TrapReason reason, Node* node, int64_t val,
                  wasm::WasmCodePosition position);
  void ZeroCheck64(wasm::TrapReason reason, Node* node,
                   wasm::WasmCodePosition position);

  Node* Switch(unsigned count, Node* key);
  Node* IfValue(int32_t value, Node* sw);
  Node* IfDefault(Node* sw);
  Node* Return(base::Vector<Node*> nodes);
  template <typename... Nodes>
  Node* Return(Node* fst, Nodes*... more) {
    Node* arr[] = {fst, more...};
    return Return(base::ArrayVector(arr));
  }

  void TraceFunctionEntry(wasm::WasmCodePosition position);
  void TraceFunctionExit(base::Vector<Node*> vals,
                         wasm::WasmCodePosition position);

  void Trap(wasm::TrapReason reason, wasm::WasmCodePosition position);

  // In all six call-related public functions, we pass a signature based on the
  // real arguments for this call. This signature gets stored in the Call node
  // and will later help us generate better code if this call gets inlined.
  Node* CallDirect(uint32_t index, base::Vector<Node*> args,
                   base::Vector<Node*> rets, wasm::WasmCodePosition position);
  Node* CallIndirect(uint32_t table_index, uint32_t sig_index,
                     base::Vector<Node*> args, base::Vector<Node*> rets,
                     wasm::WasmCodePosition position);
  Node* CallRef(const wasm::FunctionSig* sig, base::Vector<Node*> args,
                base::Vector<Node*> rets, CheckForNull null_check,
                wasm::WasmCodePosition position);

  Node* ReturnCall(uint32_t index, base::Vector<Node*> args,
                   wasm::WasmCodePosition position);
  Node* ReturnCallIndirect(uint32_t table_index, uint32_t sig_index,
                           base::Vector<Node*> args,
                           wasm::WasmCodePosition position);
  Node* ReturnCallRef(const wasm::FunctionSig* sig, base::Vector<Node*> args,
                      CheckForNull null_check, wasm::WasmCodePosition position);

  void CompareToInternalFunctionAtIndex(Node* func_ref, uint32_t function_index,
                                        Node** success_control,
                                        Node** failure_control,
                                        bool is_last_case);

  void BrOnNull(Node* ref_object, wasm::ValueType type, Node** non_null_node,
                Node** null_node);

  Node* Invert(Node* node);

  Node* GlobalGet(uint32_t index);
  void GlobalSet(uint32_t index, Node* val);
  Node* TableGet(uint32_t table_index, Node* index,
                 wasm::WasmCodePosition position);
  void TableSet(uint32_t table_index, Node* index, Node* val,
                wasm::WasmCodePosition position);
  //-----------------------------------------------------------------------
  // Operations that concern the linear memory.
  //-----------------------------------------------------------------------
  Node* CurrentMemoryPages();
  void TraceMemoryOperation(bool is_store, MachineRepresentation, Node* index,
                            uintptr_t offset, wasm::WasmCodePosition);
  Node* LoadMem(wasm::ValueType type, MachineType memtype, Node* index,
                uint64_t offset, uint32_t alignment,
                wasm::WasmCodePosition position);
#if defined(V8_TARGET_BIG_ENDIAN) || defined(V8_TARGET_ARCH_S390_LE_SIM)
  Node* LoadTransformBigEndian(wasm::ValueType type, MachineType memtype,
                               wasm::LoadTransformationKind transform,
                               Node* index, uint64_t offset, uint32_t alignment,
                               wasm::WasmCodePosition position);
#endif
  Node* LoadTransform(wasm::ValueType type, MachineType memtype,
                      wasm::LoadTransformationKind transform, Node* index,
                      uint64_t offset, uint32_t alignment,
                      wasm::WasmCodePosition position);
  Node* LoadLane(wasm::ValueType type, MachineType memtype, Node* value,
                 Node* index, uint64_t offset, uint32_t alignment,
                 uint8_t laneidx, wasm::WasmCodePosition position);
  void StoreMem(MachineRepresentation mem_rep, Node* index, uint64_t offset,
                uint32_t alignment, Node* val, wasm::WasmCodePosition position,
                wasm::ValueType type);
  void StoreLane(MachineRepresentation mem_rep, Node* index, uint64_t offset,
                 uint32_t alignment, Node* val, uint8_t laneidx,
                 wasm::WasmCodePosition position, wasm::ValueType type);
  static void PrintDebugName(Node* node);

  Node* effect();
  Node* control();
  Node* SetEffect(Node* node);
  Node* SetControl(Node* node);
  void SetEffectControl(Node* effect, Node* control);
  Node* SetEffectControl(Node* effect_and_control) {
    SetEffectControl(effect_and_control, effect_and_control);
    return effect_and_control;
  }

  Node* SetType(Node* node, wasm::ValueType type);

  // Utilities to manipulate sets of instance cache nodes.
  void InitInstanceCache(WasmInstanceCacheNodes* instance_cache);
  void PrepareInstanceCacheForLoop(WasmInstanceCacheNodes* instance_cache,
                                   Node* control);
  void NewInstanceCacheMerge(WasmInstanceCacheNodes* to,
                             WasmInstanceCacheNodes* from, Node* merge);
  void MergeInstanceCacheInto(WasmInstanceCacheNodes* to,
                              WasmInstanceCacheNodes* from, Node* merge);

  void set_instance_cache(WasmInstanceCacheNodes* instance_cache) {
    this->instance_cache_ = instance_cache;
  }

  const wasm::FunctionSig* GetFunctionSignature() { return sig_; }

  enum CallOrigin { kCalledFromWasm, kCalledFromJS };

  // Overload for when we want to provide a specific signature, rather than
  // build one using sig_, for example after scalar lowering.
  V8_EXPORT_PRIVATE void LowerInt64(Signature<MachineRepresentation>* sig);
  V8_EXPORT_PRIVATE void LowerInt64(CallOrigin origin);

  void SetSourcePosition(Node* node, wasm::WasmCodePosition position);

  Node* S128Zero();
  Node* S1x4Zero();
  Node* S1x8Zero();
  Node* S1x16Zero();

  Node* SimdOp(wasm::WasmOpcode opcode, Node* const* inputs);

  Node* SimdLaneOp(wasm::WasmOpcode opcode, uint8_t lane, Node* const* inputs);

  Node* Simd8x16ShuffleOp(const uint8_t shuffle[16], Node* const* inputs);

  Node* AtomicOp(wasm::WasmOpcode opcode, Node* const* inputs,
                 uint32_t alignment, uint64_t offset,
                 wasm::WasmCodePosition position);
  void AtomicFence();

  void MemoryInit(uint32_t data_segment_index, Node* dst, Node* src, Node* size,
                  wasm::WasmCodePosition position);
  void MemoryCopy(Node* dst, Node* src, Node* size,
                  wasm::WasmCodePosition position);
  void DataDrop(uint32_t data_segment_index, wasm::WasmCodePosition position);
  void MemoryFill(Node* dst, Node* fill, Node* size,
                  wasm::WasmCodePosition position);

  void TableInit(uint32_t table_index, uint32_t elem_segment_index, Node* dst,
                 Node* src, Node* size, wasm::WasmCodePosition position);
  void ElemDrop(uint32_t elem_segment_index, wasm::WasmCodePosition position);
  void TableCopy(uint32_t table_dst_index, uint32_t table_src_index, Node* dst,
                 Node* src, Node* size, wasm::WasmCodePosition position);
  Node* TableGrow(uint32_t table_index, Node* value, Node* delta);
  Node* TableSize(uint32_t table_index);
  void TableFill(uint32_t table_index, Node* start, Node* value, Node* count);

  Node* StructNew(uint32_t struct_index, const wasm::StructType* type,
                  Node* rtt, base::Vector<Node*> fields);
  Node* StructGet(Node* struct_object, const wasm::StructType* struct_type,
                  uint32_t field_index, CheckForNull null_check, bool is_signed,
                  wasm::WasmCodePosition position);
  void StructSet(Node* struct_object, const wasm::StructType* struct_type,
                 uint32_t field_index, Node* value, CheckForNull null_check,
                 wasm::WasmCodePosition position);
  Node* ArrayNew(uint32_t array_index, const wasm::ArrayType* type,
                 Node* length, Node* initial_value, Node* rtt,
                 wasm::WasmCodePosition position);
  Node* ArrayGet(Node* array_object, const wasm::ArrayType* type, Node* index,
                 CheckForNull null_check, bool is_signed,
                 wasm::WasmCodePosition position);
  void ArraySet(Node* array_object, const wasm::ArrayType* type, Node* index,
                Node* value, CheckForNull null_check,
                wasm::WasmCodePosition position);
  Node* ArrayLen(Node* array_object, CheckForNull null_check,
                 wasm::WasmCodePosition position);
  void ArrayCopy(Node* dst_array, Node* dst_index, CheckForNull dst_null_check,
                 Node* src_array, Node* src_index, CheckForNull src_null_check,
                 Node* length, const wasm::ArrayType* type,
                 wasm::WasmCodePosition position);
  void ArrayFill(Node* array, Node* index, Node* value, Node* length,
                 const wasm::ArrayType* type, CheckForNull null_check,
                 wasm::WasmCodePosition position);
  Node* ArrayNewFixed(const wasm::ArrayType* type, Node* rtt,
                      base::Vector<Node*> elements);
  Node* ArrayNewSegment(const wasm::ArrayType* type, uint32_t segment_index,
                        Node* offset, Node* length, Node* rtt,
                        wasm::WasmCodePosition position);
  void ArrayInitSegment(const wasm::ArrayType* type, uint32_t segment_index,
                        Node* array, Node* array_index, Node* segment_offset,
                        Node* length, wasm::WasmCodePosition position);
  Node* I31New(Node* input);
  Node* I31GetS(Node* input, CheckForNull null_check,
                wasm::WasmCodePosition position);
  Node* I31GetU(Node* input, CheckForNull null_check,
                wasm::WasmCodePosition position);
  Node* RttCanon(uint32_t type_index);

  Node* RefTest(Node* object, Node* rtt, WasmTypeCheckConfig config);
  Node* RefTestAbstract(Node* object, wasm::HeapType type, bool is_nullable,
                        bool null_succeeds);
  Node* RefCast(Node* object, Node* rtt, WasmTypeCheckConfig config,
                wasm::WasmCodePosition position);
  Node* RefCastAbstract(Node* object, wasm::HeapType type,
                        wasm::WasmCodePosition position, bool is_nullable,
                        bool null_succeeds);
  void BrOnCast(Node* object, Node* rtt, WasmTypeCheckConfig config,
                Node** match_control, Node** match_effect,
                Node** no_match_control, Node** no_match_effect);
  Node* RefIsEq(Node* object, bool object_can_be_null, bool null_succeeds);
  Node* RefAsEq(Node* object, bool object_can_be_null,
                wasm::WasmCodePosition position, bool null_succeeds);
  void BrOnEq(Node* object, Node* rtt, WasmTypeCheckConfig config,
              Node** match_control, Node** match_effect,
              Node** no_match_control, Node** no_match_effect);
  Node* RefIsStruct(Node* object, bool object_can_be_null, bool null_succeeds);
  Node* RefAsStruct(Node* object, bool object_can_be_null,
                    wasm::WasmCodePosition position, bool null_succeeds);
  void BrOnStruct(Node* object, Node* rtt, WasmTypeCheckConfig config,
                  Node** match_control, Node** match_effect,
                  Node** no_match_control, Node** no_match_effect);
  Node* RefIsArray(Node* object, bool object_can_be_null, bool null_succeeds);
  Node* RefAsArray(Node* object, bool object_can_be_null,
                   wasm::WasmCodePosition position, bool null_succeeds);
  void BrOnArray(Node* object, Node* rtt, WasmTypeCheckConfig config,
                 Node** match_control, Node** match_effect,
                 Node** no_match_control, Node** no_match_effect);
  Node* RefIsI31(Node* object, bool null_succeeds);
  Node* RefAsI31(Node* object, wasm::WasmCodePosition position,
                 bool null_succeeds);
  void BrOnI31(Node* object, Node* rtt, WasmTypeCheckConfig config,
               Node** match_control, Node** match_effect,
               Node** no_match_control, Node** no_match_effect);
  Node* StringNewWtf8(uint32_t memory, unibrow::Utf8Variant variant,
                      Node* offset, Node* size);
  Node* StringNewWtf8Array(unibrow::Utf8Variant variant, Node* array,
                           Node* start, Node* end);
  Node* StringNewWtf16(uint32_t memory, Node* offset, Node* size);
  Node* StringNewWtf16Array(Node* array, Node* start, Node* end);
  Node* StringAsWtf16(Node* string, CheckForNull null_check,
                      wasm::WasmCodePosition position);
  Node* StringConst(uint32_t index);
  Node* StringMeasureUtf8(Node* string, CheckForNull null_check,
                          wasm::WasmCodePosition position);
  Node* StringMeasureWtf8(Node* string, CheckForNull null_check,
                          wasm::WasmCodePosition position);
  Node* StringMeasureWtf16(Node* string, CheckForNull null_check,
                           wasm::WasmCodePosition position);
  Node* StringEncodeWtf8(uint32_t memory, unibrow::Utf8Variant variant,
                         Node* string, CheckForNull null_check, Node* offset,
                         wasm::WasmCodePosition position);
  Node* StringEncodeWtf8Array(unibrow::Utf8Variant variant, Node* string,
                              CheckForNull string_null_check, Node* array,
                              CheckForNull array_null_check, Node* start,
                              wasm::WasmCodePosition position);
  Node* StringEncodeWtf16(uint32_t memory, Node* string,
                          CheckForNull null_check, Node* offset,
                          wasm::WasmCodePosition position);
  Node* StringEncodeWtf16Array(Node* string, CheckForNull string_null_check,
                               Node* array, CheckForNull array_null_check,
                               Node* start, wasm::WasmCodePosition position);
  Node* StringConcat(Node* head, CheckForNull head_null_check, Node* tail,
                     CheckForNull tail_null_check,
                     wasm::WasmCodePosition position);
  Node* StringEqual(Node* a, CheckForNull a_null_check, Node* b,
                    CheckForNull b_null_check, wasm::WasmCodePosition position);
  Node* StringIsUSVSequence(Node* str, CheckForNull null_check,
                            wasm::WasmCodePosition position);
  Node* StringAsWtf8(Node* str, CheckForNull null_check,
                     wasm::WasmCodePosition position);
  Node* StringViewWtf8Advance(Node* view, CheckForNull null_check, Node* pos,
                              Node* bytes, wasm::WasmCodePosition position);
  void StringViewWtf8Encode(uint32_t memory, unibrow::Utf8Variant variant,
                            Node* view, CheckForNull null_check, Node* addr,
                            Node* pos, Node* bytes, Node** next_pos,
                            Node** bytes_written,
                            wasm::WasmCodePosition position);
  Node* StringViewWtf8Slice(Node* view, CheckForNull null_check, Node* pos,
                            Node* bytes, wasm::WasmCodePosition position);
  Node* StringViewWtf16GetCodeUnit(Node* string, CheckForNull null_check,
                                   Node* offset,
                                   wasm::WasmCodePosition position);
  Node* StringViewWtf16Encode(uint32_t memory, Node* string,
                              CheckForNull null_check, Node* offset,
                              Node* start, Node* length,
                              wasm::WasmCodePosition position);
  Node* StringViewWtf16Slice(Node* string, CheckForNull null_check, Node* start,
                             Node* end, wasm::WasmCodePosition position);
  Node* StringAsIter(Node* str, CheckForNull null_check,
                     wasm::WasmCodePosition position);
  Node* StringViewIterNext(Node* view, CheckForNull null_check,
                           wasm::WasmCodePosition position);
  Node* StringViewIterAdvance(Node* view, CheckForNull null_check,
                              Node* codepoints,
                              wasm::WasmCodePosition position);
  Node* StringViewIterRewind(Node* view, CheckForNull null_check,
                             Node* codepoints, wasm::WasmCodePosition position);
  Node* StringViewIterSlice(Node* view, CheckForNull null_check,
                            Node* codepoints, wasm::WasmCodePosition position);
  Node* StringCompare(Node* lhs, CheckForNull null_check_lhs, Node* rhs,
                      CheckForNull null_check_rhs,
                      wasm::WasmCodePosition position);
  Node* StringFromCodePoint(Node* code_point);
  Node* StringHash(Node* string, CheckForNull null_check,
                   wasm::WasmCodePosition position);
  Node* IsNull(Node* object, wasm::ValueType type);
  Node* TypeGuard(Node* value, wasm::ValueType type);

  // Support for well-known imports.
  // See {CheckWellKnownImport} for signature and builtin ID definitions.
  Node* WellKnown_StringToLowerCaseStringref(Node* string,
                                             CheckForNull null_check);
  Node* WellKnown_IntToString(Node* n, Node* radix);

  bool has_simd() const { return has_simd_; }

  wasm::BoundsCheckStrategy bounds_checks() const {
    return env_->bounds_checks;
  }

  Node* DefaultValue(wasm::ValueType type);

  MachineGraph* mcgraph() { return mcgraph_; }
  Graph* graph();
  Zone* graph_zone();

  void AddBytecodePositionDecorator(NodeOriginTable* node_origins,
                                    wasm::Decoder* decoder);

  void RemoveBytecodePositionDecorator();

  void StoreCallCount(Node* call, int count);
  void ReserveCallCounts(size_t num_call_instructions);

  void set_inlining_id(int inlining_id) {
    DCHECK_NE(inlining_id, -1);
    inlining_id_ = inlining_id;
  }

 protected:
  Node* NoContextConstant();

  Node* GetInstance();
  Node* BuildLoadIsolateRoot();
  Node* UndefinedValue();

  // MemBuffer is only called with valid offsets (after bounds checking), so the
  // offset fits in a platform-dependent uintptr_t.
  Node* MemBuffer(uintptr_t offset);

  // BoundsCheckMem receives a 32/64-bit index (depending on
  // WasmModule::is_memory64) and returns a ptrsize index and information about
  // the kind of bounds check performed (or why none was needed).
  std::pair<Node*, BoundsCheckResult> BoundsCheckMem(uint8_t access_size,
                                                     Node* index,
                                                     uint64_t offset,
                                                     wasm::WasmCodePosition,
                                                     EnforceBoundsCheck);

  std::pair<Node*, BoundsCheckResult> CheckBoundsAndAlignment(
      int8_t access_size, Node* index, uint64_t offset, wasm::WasmCodePosition,
      EnforceBoundsCheck);

  const Operator* GetSafeLoadOperator(int offset, wasm::ValueType type);
  const Operator* GetSafeStoreOperator(int offset, wasm::ValueType type);
  Node* BuildChangeEndiannessStore(Node* node, MachineRepresentation rep,
                                   wasm::ValueType wasmtype = wasm::kWasmVoid);
  Node* BuildChangeEndiannessLoad(Node* node, MachineType type,
                                  wasm::ValueType wasmtype = wasm::kWasmVoid);

  Node* MaskShiftCount32(Node* node);
  Node* MaskShiftCount64(Node* node);

  enum IsReturnCall : bool { kReturnCall = true, kCallContinues = false };

  template <typename... Args>
  Node* BuildCCall(MachineSignature* sig, Node* function, Args... args);
  Node* BuildCallNode(const wasm::FunctionSig* sig, base::Vector<Node*> args,
                      wasm::WasmCodePosition position, Node* instance_node,
                      const Operator* op, Node* frame_state = nullptr);
  // Helper function for {BuildIndirectCall}.
  void LoadIndirectFunctionTable(uint32_t table_index, Node** ift_size,
                                 Node** ift_sig_ids, Node** ift_targets,
                                 Node** ift_instances);
  Node* BuildIndirectCall(uint32_t table_index, uint32_t sig_index,
                          base::Vector<Node*> args, base::Vector<Node*> rets,
                          wasm::WasmCodePosition position,
                          IsReturnCall continuation);
  Node* BuildWasmCall(const wasm::FunctionSig* sig, base::Vector<Node*> args,
                      base::Vector<Node*> rets, wasm::WasmCodePosition position,
                      Node* instance_node, Node* frame_state = nullptr);
  Node* BuildWasmReturnCall(const wasm::FunctionSig* sig,
                            base::Vector<Node*> args,
                            wasm::WasmCodePosition position,
                            Node* instance_node);
  Node* BuildImportCall(const wasm::FunctionSig* sig, base::Vector<Node*> args,
                        base::Vector<Node*> rets,
                        wasm::WasmCodePosition position, int func_index,
                        IsReturnCall continuation);
  Node* BuildImportCall(const wasm::FunctionSig* sig, base::Vector<Node*> args,
                        base::Vector<Node*> rets,
                        wasm::WasmCodePosition position, Node* func_index,
                        IsReturnCall continuation);
  Node* BuildCallRef(const wasm::FunctionSig* sig, base::Vector<Node*> args,
                     base::Vector<Node*> rets, CheckForNull null_check,
                     IsReturnCall continuation,
                     wasm::WasmCodePosition position);

  Node* BuildF32CopySign(Node* left, Node* right);
  Node* BuildF64CopySign(Node* left, Node* right);

  Node* BuildIntConvertFloat(Node* input, wasm::WasmCodePosition position,
                             wasm::WasmOpcode);
  Node* BuildI32Ctz(Node* input);
  Node* BuildI32Popcnt(Node* input);
  Node* BuildI64Ctz(Node* input);
  Node* BuildI64Popcnt(Node* input);
  Node* BuildBitCountingCall(Node* input, ExternalReference ref,
                             MachineRepresentation input_type);

  Node* BuildCFuncInstruction(ExternalReference ref, MachineType type,
                              Node* input0, Node* input1 = nullptr);
  Node* BuildF32Trunc(Node* input);
  Node* BuildF32Floor(Node* input);
  Node* BuildF32Ceil(Node* input);
  Node* BuildF32NearestInt(Node* input);
  Node* BuildF64Trunc(Node* input);
  Node* BuildF64Floor(Node* input);
  Node* BuildF64Ceil(Node* input);
  Node* BuildF64NearestInt(Node* input);
  Node* BuildI32Rol(Node* left, Node* right);
  Node* BuildI64Rol(Node* left, Node* right);

  Node* BuildF64Acos(Node* input);
  Node* BuildF64Asin(Node* input);
  Node* BuildF64Pow(Node* left, Node* right);
  Node* BuildF64Mod(Node* left, Node* right);

  Node* BuildIntToFloatConversionInstruction(
      Node* input, ExternalReference ref,
      MachineRepresentation parameter_representation,
      const MachineType result_type);
  Node* BuildF32SConvertI64(Node* input);
  Node* BuildF32UConvertI64(Node* input);
  Node* BuildF64SConvertI64(Node* input);
  Node* BuildF64UConvertI64(Node* input);

  Node* BuildCcallConvertFloat(Node* input, wasm::WasmCodePosition position,
                               wasm::WasmOpcode opcode);

  Node* BuildI32DivS(Node* left, Node* right, wasm::WasmCodePosition position);
  Node* BuildI32RemS(Node* left, Node* right, wasm::WasmCodePosition position);
  Node* BuildI32DivU(Node* left, Node* right, wasm::WasmCodePosition position);
  Node* BuildI32RemU(Node* left, Node* right, wasm::WasmCodePosition position);

  Node* BuildI64DivS(Node* left, Node* right, wasm::WasmCodePosition position);
  Node* BuildI64RemS(Node* left, Node* right, wasm::WasmCodePosition position);
  Node* BuildI64DivU(Node* left, Node* right, wasm::WasmCodePosition position);
  Node* BuildI64RemU(Node* left, Node* right, wasm::WasmCodePosition position);
  Node* BuildDiv64Call(Node* left, Node* right, ExternalReference ref,
                       MachineType result_type, wasm::TrapReason trap_zero,
                       wasm::WasmCodePosition position);

  void MemTypeToUintPtrOrOOBTrap(std::initializer_list<Node**> nodes,
                                 wasm::WasmCodePosition position);

  void GetGlobalBaseAndOffset(const wasm::WasmGlobal&, Node** base_node,
                              Node** offset_node);

  using BranchBuilder = std::function<void(Node*, BranchHint)>;
  struct Callbacks {
    BranchBuilder succeed_if;
    BranchBuilder fail_if;
    BranchBuilder fail_if_not;
  };

  // This type is used to collect control/effect nodes we need to merge at the
  // end of BrOn* functions. Nodes are collected by calling the passed callbacks
  // succeed_if, fail_if and fail_if_not. We have up to 5 control nodes to
  // merge; the EffectPhi needs an additional input.
  using SmallNodeVector = base::SmallVector<Node*, 6>;

  Callbacks TestCallbacks(GraphAssemblerLabel<1>* label);
  Callbacks CastCallbacks(GraphAssemblerLabel<0>* label,
                          wasm::WasmCodePosition position);
  Callbacks BranchCallbacks(SmallNodeVector& no_match_controls,
                            SmallNodeVector& no_match_effects,
                            SmallNodeVector& match_controls,
                            SmallNodeVector& match_effects);

  void EqCheck(Node* object, bool object_can_be_null, Callbacks callbacks,
               bool null_succeeds);
  void ManagedObjectInstanceCheck(Node* object, bool object_can_be_null,
                                  InstanceType instance_type,
                                  Callbacks callbacks, bool null_succeeds);

  void BrOnCastAbs(Node** match_control, Node** match_effect,
                   Node** no_match_control, Node** no_match_effect,
                   std::function<void(Callbacks)> type_checker);
  void BoundsCheckArray(Node* array, Node* index, CheckForNull null_check,
                        wasm::WasmCodePosition position);
  void BoundsCheckArrayWithLength(Node* array, Node* index, Node* length,
                                  CheckForNull null_check,
                                  wasm::WasmCodePosition position);
  Node* StoreInInt64StackSlot(Node* value, wasm::ValueType type);
  void ArrayFillImpl(Node* array, Node* index, Node* value, Node* length,
                     const wasm::ArrayType* type, bool emit_write_barrier);

  // Asm.js specific functionality.
  Node* BuildI32AsmjsSConvertF32(Node* input);
  Node* BuildI32AsmjsSConvertF64(Node* input);
  Node* BuildI32AsmjsUConvertF32(Node* input);
  Node* BuildI32AsmjsUConvertF64(Node* input);
  Node* BuildI32AsmjsDivS(Node* left, Node* right);
  Node* BuildI32AsmjsRemS(Node* left, Node* right);
  Node* BuildI32AsmjsDivU(Node* left, Node* right);
  Node* BuildI32AsmjsRemU(Node* left, Node* right);
  Node* BuildAsmjsLoadMem(MachineType type, Node* index);
  Node* BuildAsmjsStoreMem(MachineType type, Node* index, Node* val);

  // Wasm SIMD.
  Node* BuildF64x2Ceil(Node* input);
  Node* BuildF64x2Floor(Node* input);
  Node* BuildF64x2Trunc(Node* input);
  Node* BuildF64x2NearestInt(Node* input);
  Node* BuildF32x4Ceil(Node* input);
  Node* BuildF32x4Floor(Node* input);
  Node* BuildF32x4Trunc(Node* input);
  Node* BuildF32x4NearestInt(Node* input);

  void BuildEncodeException32BitValue(Node* values_array, uint32_t* index,
                                      Node* value);
  Node* BuildDecodeException32BitValue(Node* values_array, uint32_t* index);
  Node* BuildDecodeException64BitValue(Node* values_array, uint32_t* index);

  Node* BuildMultiReturnFixedArrayFromIterable(const wasm::FunctionSig* sig,
                                               Node* iterable, Node* context);

  Node* BuildLoadCallTargetFromExportedFunctionData(Node* function_data);

  //-----------------------------------------------------------------------
  // Operations involving the CEntry, a dependency we want to remove
  // to get off the GC heap.
  //-----------------------------------------------------------------------
  Node* BuildCallToRuntime(Runtime::FunctionId f, Node** parameters,
                           int parameter_count);

  Node* BuildCallToRuntimeWithContext(Runtime::FunctionId f, Node* js_context,
                                      Node** parameters, int parameter_count);
  TrapId GetTrapIdForTrap(wasm::TrapReason reason);

  void BuildModifyThreadInWasmFlag(bool new_value);
  void BuildModifyThreadInWasmFlagHelper(Node* thread_in_wasm_flag_address,
                                         bool new_value);

  Node* BuildChangeInt64ToBigInt(Node* input, StubCallMode stub_mode);

  Node* StoreArgsInStackSlot(
      std::initializer_list<std::pair<MachineRepresentation, Node*>> args);

  std::unique_ptr<WasmGraphAssembler> gasm_;
  Zone* const zone_;
  MachineGraph* const mcgraph_;
  wasm::CompilationEnv* const env_;
  // For the main WasmGraphBuilder class, this is identical to the features
  // field in {env_}, but the WasmWrapperGraphBuilder subclass doesn't have
  // that, so common code should use this field instead.
  wasm::WasmFeatures enabled_features_;

  Node** parameters_;

  WasmInstanceCacheNodes* instance_cache_ = nullptr;

  SetOncePointer<Node> stack_check_code_node_;
  SetOncePointer<const Operator> stack_check_call_operator_;

  bool has_simd_ = false;
  bool needs_stack_check_ = false;

  const wasm::FunctionSig* const sig_;

  compiler::WasmDecorator* decorator_ = nullptr;

  compiler::SourcePositionTable* const source_position_table_ = nullptr;
  int inlining_id_ = -1;
  Parameter0Mode parameter_mode_;
  Isolate* const isolate_;
  SetOncePointer<Node> instance_node_;
  NullCheckStrategy null_check_strategy_;
};

enum WasmCallKind { kWasmFunction, kWasmImportWrapper, kWasmCapiFunction };

V8_EXPORT_PRIVATE void BuildInlinedJSToWasmWrapper(
    Zone* zone, MachineGraph* mcgraph, const wasm::FunctionSig* signature,
    const wasm::WasmModule* module, Isolate* isolate,
    compiler::SourcePositionTable* spt, wasm::WasmFeatures features,
    Node* frame_state, bool set_in_wasm_flag);

V8_EXPORT_PRIVATE CallDescriptor* GetWasmCallDescriptor(
    Zone* zone, const wasm::FunctionSig* signature,
    WasmCallKind kind = kWasmFunction, bool need_frame_state = false);

V8_EXPORT_PRIVATE CallDescriptor* GetI32WasmCallDescriptor(
    Zone* zone, const CallDescriptor* call_descriptor);

V8_EXPORT_PRIVATE const wasm::FunctionSig* GetI32Sig(
    Zone* zone, const wasm::FunctionSig* sig);

AssemblerOptions WasmAssemblerOptions();
AssemblerOptions WasmStubAssemblerOptions();

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_WASM_COMPILER_H_
