// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_CODE_MANAGER_H_
#define V8_WASM_WASM_CODE_MANAGER_H_

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "src/base/address-region.h"
#include "src/base/bit-field.h"
#include "src/base/macros.h"
#include "src/base/vector.h"
#include "src/builtins/builtins.h"
#include "src/codegen/source-position.h"
#include "src/common/code-memory-access.h"
#include "src/handles/handles.h"
#include "src/tasks/operations-barrier.h"
#include "src/trap-handler/trap-handler.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-module-sourcemap.h"
#include "src/wasm/wasm-tier.h"

namespace v8 {
namespace internal {

class InstructionStream;
class CodeDesc;
class Isolate;

namespace wasm {

class AssumptionsJournal;
class DebugInfo;
class NamesProvider;
class NativeModule;
struct WasmCompilationResult;
class WasmEngine;
class WasmImportWrapperCache;
struct WasmModule;
enum class WellKnownImport : uint8_t;

// Convenience macro listing all wasm runtime stubs. Note that the first few
// elements of the list coincide with {compiler::TrapId}, order matters.
#define WASM_RUNTIME_STUB_LIST(V, VTRAP) \
  FOREACH_WASM_TRAPREASON(VTRAP)         \
  V(WasmCompileLazy)                     \
  V(WasmTriggerTierUp)                   \
  V(WasmLiftoffFrameSetup)               \
  V(WasmDebugBreak)                      \
  V(WasmInt32ToHeapNumber)               \
  V(WasmTaggedNonSmiToInt32)             \
  V(WasmFloat32ToNumber)                 \
  V(WasmFloat64ToNumber)                 \
  V(WasmTaggedToFloat64)                 \
  V(WasmAllocateJSArray)                 \
  V(WasmAtomicNotify)                    \
  V(WasmI32AtomicWait)                   \
  V(WasmI64AtomicWait)                   \
  V(WasmGetOwnProperty)                  \
  V(WasmRefFunc)                         \
  V(WasmInternalFunctionCreateExternal)  \
  V(WasmMemoryGrow)                      \
  V(WasmTableInit)                       \
  V(WasmTableCopy)                       \
  V(WasmTableFill)                       \
  V(WasmTableGrow)                       \
  V(WasmTableGet)                        \
  V(WasmTableSet)                        \
  V(WasmTableGetFuncRef)                 \
  V(WasmTableSetFuncRef)                 \
  V(WasmStackGuard)                      \
  V(WasmStackOverflow)                   \
  V(WasmAllocateFixedArray)              \
  V(WasmThrow)                           \
  V(WasmRethrow)                         \
  V(WasmRethrowExplicitContext)          \
  V(WasmTraceEnter)                      \
  V(WasmTraceExit)                       \
  V(WasmTraceMemory)                     \
  V(BigIntToI32Pair)                     \
  V(BigIntToI64)                         \
  V(CallRefIC)                           \
  V(DoubleToI)                           \
  V(I32PairToBigInt)                     \
  V(I64ToBigInt)                         \
  V(RecordWriteSaveFP)                   \
  V(RecordWriteIgnoreFP)                 \
  V(ToNumber)                            \
  IF_TSAN(V, TSANRelaxedStore8IgnoreFP)  \
  IF_TSAN(V, TSANRelaxedStore8SaveFP)    \
  IF_TSAN(V, TSANRelaxedStore16IgnoreFP) \
  IF_TSAN(V, TSANRelaxedStore16SaveFP)   \
  IF_TSAN(V, TSANRelaxedStore32IgnoreFP) \
  IF_TSAN(V, TSANRelaxedStore32SaveFP)   \
  IF_TSAN(V, TSANRelaxedStore64IgnoreFP) \
  IF_TSAN(V, TSANRelaxedStore64SaveFP)   \
  IF_TSAN(V, TSANSeqCstStore8IgnoreFP)   \
  IF_TSAN(V, TSANSeqCstStore8SaveFP)     \
  IF_TSAN(V, TSANSeqCstStore16IgnoreFP)  \
  IF_TSAN(V, TSANSeqCstStore16SaveFP)    \
  IF_TSAN(V, TSANSeqCstStore32IgnoreFP)  \
  IF_TSAN(V, TSANSeqCstStore32SaveFP)    \
  IF_TSAN(V, TSANSeqCstStore64IgnoreFP)  \
  IF_TSAN(V, TSANSeqCstStore64SaveFP)    \
  IF_TSAN(V, TSANRelaxedLoad32IgnoreFP)  \
  IF_TSAN(V, TSANRelaxedLoad32SaveFP)    \
  IF_TSAN(V, TSANRelaxedLoad64IgnoreFP)  \
  IF_TSAN(V, TSANRelaxedLoad64SaveFP)    \
  V(WasmAllocateArray_Uninitialized)     \
  V(WasmArrayCopy)                       \
  V(WasmArrayCopyWithChecks)             \
  V(WasmArrayNewSegment)                 \
  V(WasmArrayInitSegment)                \
  V(WasmAllocateStructWithRtt)           \
  V(WasmSubtypeCheck)                    \
  V(WasmOnStackReplace)                  \
  V(WasmSuspend)                         \
  V(WasmStringNewWtf8)                   \
  V(WasmStringNewWtf16)                  \
  V(WasmStringConst)                     \
  V(WasmStringMeasureUtf8)               \
  V(WasmStringMeasureWtf8)               \
  V(WasmStringEncodeWtf8)                \
  V(WasmStringEncodeWtf16)               \
  V(WasmStringConcat)                    \
  V(WasmStringEqual)                     \
  V(WasmStringIsUSVSequence)             \
  V(WasmStringAsWtf16)                   \
  V(WasmStringViewWtf16GetCodeUnit)      \
  V(WasmStringViewWtf16Encode)           \
  V(WasmStringViewWtf16Slice)            \
  V(WasmStringNewWtf8Array)              \
  V(WasmStringNewWtf16Array)             \
  V(WasmStringEncodeWtf8Array)           \
  V(WasmStringEncodeWtf16Array)          \
  V(WasmStringAsWtf8)                    \
  V(WasmStringViewWtf8Advance)           \
  V(WasmStringViewWtf8Encode)            \
  V(WasmStringViewWtf8Slice)             \
  V(WasmStringAsIter)                    \
  V(WasmStringViewIterNext)              \
  V(WasmStringViewIterAdvance)           \
  V(WasmStringViewIterRewind)            \
  V(WasmStringViewIterSlice)             \
  V(StringCompare)                       \
  V(WasmStringFromCodePoint)             \
  V(WasmStringHash)                      \
  V(WasmExternInternalize)

// Sorted, disjoint and non-overlapping memory regions. A region is of the
// form [start, end). So there's no [start, end), [end, other_end),
// because that should have been reduced to [start, other_end).
class V8_EXPORT_PRIVATE DisjointAllocationPool final {
 public:
  MOVE_ONLY_WITH_DEFAULT_CONSTRUCTORS(DisjointAllocationPool);
  explicit DisjointAllocationPool(base::AddressRegion region)
      : regions_({region}) {}

  // Merge the parameter region into this object. The assumption is that the
  // passed parameter is not intersecting this object - for example, it was
  // obtained from a previous Allocate. Returns the merged region.
  base::AddressRegion Merge(base::AddressRegion);

  // Allocate a contiguous region of size {size}. Return an empty region on
  // failure.
  base::AddressRegion Allocate(size_t size);

  // Allocate a contiguous region of size {size} within {region}. Return an
  // empty region on failure.
  base::AddressRegion AllocateInRegion(size_t size, base::AddressRegion);

  bool IsEmpty() const { return regions_.empty(); }

  const auto& regions() const { return regions_; }

 private:
  std::set<base::AddressRegion, base::AddressRegion::StartAddressLess> regions_;
};

class V8_EXPORT_PRIVATE WasmCode final {
 public:
  enum Kind { kWasmFunction, kWasmToCapiWrapper, kWasmToJsWrapper, kJumpTable };

  // Each runtime stub is identified by an id. This id is used to reference the
  // stub via {RelocInfo::WASM_STUB_CALL} and gets resolved during relocation.
  enum RuntimeStubId {
#define DEF_ENUM(Name) k##Name,
#define DEF_ENUM_TRAP(Name) kThrowWasm##Name,
    WASM_RUNTIME_STUB_LIST(DEF_ENUM, DEF_ENUM_TRAP)
#undef DEF_ENUM_TRAP
#undef DEF_ENUM
        kRuntimeStubCount
  };

  static constexpr RuntimeStubId GetRecordWriteStub(SaveFPRegsMode fp_mode) {
    switch (fp_mode) {
      case SaveFPRegsMode::kIgnore:
        return RuntimeStubId::kRecordWriteIgnoreFP;
      case SaveFPRegsMode::kSave:
        return RuntimeStubId::kRecordWriteSaveFP;
    }
  }

#ifdef V8_IS_TSAN
  static RuntimeStubId GetTSANStoreStub(SaveFPRegsMode fp_mode, int size,
                                        std::memory_order order) {
    if (order == std::memory_order_relaxed) {
      if (size == kInt8Size) {
        return fp_mode == SaveFPRegsMode::kIgnore
                   ? RuntimeStubId::kTSANRelaxedStore8IgnoreFP
                   : RuntimeStubId::kTSANRelaxedStore8SaveFP;
      } else if (size == kInt16Size) {
        return fp_mode == SaveFPRegsMode::kIgnore
                   ? RuntimeStubId::kTSANRelaxedStore16IgnoreFP
                   : RuntimeStubId::kTSANRelaxedStore16SaveFP;
      } else if (size == kInt32Size) {
        return fp_mode == SaveFPRegsMode::kIgnore
                   ? RuntimeStubId::kTSANRelaxedStore32IgnoreFP
                   : RuntimeStubId::kTSANRelaxedStore32SaveFP;
      } else {
        CHECK_EQ(size, kInt64Size);
        return fp_mode == SaveFPRegsMode::kIgnore
                   ? RuntimeStubId::kTSANRelaxedStore64IgnoreFP
                   : RuntimeStubId::kTSANRelaxedStore64SaveFP;
      }
    } else {
      DCHECK_EQ(order, std::memory_order_seq_cst);
      if (size == kInt8Size) {
        return fp_mode == SaveFPRegsMode::kIgnore
                   ? RuntimeStubId::kTSANSeqCstStore8IgnoreFP
                   : RuntimeStubId::kTSANSeqCstStore8SaveFP;
      } else if (size == kInt16Size) {
        return fp_mode == SaveFPRegsMode::kIgnore
                   ? RuntimeStubId::kTSANSeqCstStore16IgnoreFP
                   : RuntimeStubId::kTSANSeqCstStore16SaveFP;
      } else if (size == kInt32Size) {
        return fp_mode == SaveFPRegsMode::kIgnore
                   ? RuntimeStubId::kTSANSeqCstStore32IgnoreFP
                   : RuntimeStubId::kTSANSeqCstStore32SaveFP;
      } else {
        CHECK_EQ(size, kInt64Size);
        return fp_mode == SaveFPRegsMode::kIgnore
                   ? RuntimeStubId::kTSANSeqCstStore64IgnoreFP
                   : RuntimeStubId::kTSANSeqCstStore64SaveFP;
      }
    }
  }

  static RuntimeStubId GetTSANRelaxedLoadStub(SaveFPRegsMode fp_mode,
                                              int size) {
    if (size == kInt32Size) {
      return fp_mode == SaveFPRegsMode::kIgnore
                 ? RuntimeStubId::kTSANRelaxedLoad32IgnoreFP
                 : RuntimeStubId::kTSANRelaxedLoad32SaveFP;
    } else {
      CHECK_EQ(size, kInt64Size);
      return fp_mode == SaveFPRegsMode::kIgnore
                 ? RuntimeStubId::kTSANRelaxedLoad64IgnoreFP
                 : RuntimeStubId::kTSANRelaxedLoad64SaveFP;
    }
  }
#endif  // V8_IS_TSAN

  base::Vector<byte> instructions() const {
    return base::VectorOf(instructions_,
                          static_cast<size_t>(instructions_size_));
  }
  Address instruction_start() const {
    return reinterpret_cast<Address>(instructions_);
  }
  base::Vector<const byte> reloc_info() const {
    return {protected_instructions_data().end(),
            static_cast<size_t>(reloc_info_size_)};
  }
  base::Vector<const byte> source_positions() const {
    return {reloc_info().end(), static_cast<size_t>(source_positions_size_)};
  }
  base::Vector<const byte> inlining_positions() const {
    return {source_positions().end(),
            static_cast<size_t>(inlining_positions_size_)};
  }

  int index() const { return index_; }
  // Anonymous functions are functions that don't carry an index.
  bool IsAnonymous() const { return index_ == kAnonymousFuncIndex; }
  Kind kind() const { return KindField::decode(flags_); }
  NativeModule* native_module() const { return native_module_; }
  ExecutionTier tier() const { return ExecutionTierField::decode(flags_); }
  Address constant_pool() const;
  Address handler_table() const;
  int handler_table_size() const;
  Address code_comments() const;
  int code_comments_size() const;
  int constant_pool_offset() const { return constant_pool_offset_; }
  int safepoint_table_offset() const { return safepoint_table_offset_; }
  int handler_table_offset() const { return handler_table_offset_; }
  int code_comments_offset() const { return code_comments_offset_; }
  int unpadded_binary_size() const { return unpadded_binary_size_; }
  int stack_slots() const { return stack_slots_; }
  uint16_t first_tagged_parameter_slot() const {
    return tagged_parameter_slots_ >> 16;
  }
  uint16_t num_tagged_parameter_slots() const {
    return tagged_parameter_slots_ & 0xFFFF;
  }
  uint32_t raw_tagged_parameter_slots_for_serialization() const {
    return tagged_parameter_slots_;
  }

  bool is_liftoff() const { return tier() == ExecutionTier::kLiftoff; }

  bool is_turbofan() const { return tier() == ExecutionTier::kTurbofan; }

  bool contains(Address pc) const {
    return reinterpret_cast<Address>(instructions_) <= pc &&
           pc < reinterpret_cast<Address>(instructions_ + instructions_size_);
  }

  // Only Liftoff code that was generated for debugging can be inspected
  // (otherwise debug side table positions would not match up).
  bool is_inspectable() const { return is_liftoff() && for_debugging(); }

  base::Vector<const uint8_t> protected_instructions_data() const {
    return {meta_data_.get(),
            static_cast<size_t>(protected_instructions_size_)};
  }

  base::Vector<const trap_handler::ProtectedInstructionData>
  protected_instructions() const {
    return base::Vector<const trap_handler::ProtectedInstructionData>::cast(
        protected_instructions_data());
  }

  void Validate() const;
  void Print(const char* name = nullptr) const;
  void MaybePrint() const;
  void Disassemble(const char* name, std::ostream& os,
                   Address current_pc = kNullAddress) const;

  static bool ShouldBeLogged(Isolate* isolate);
  void LogCode(Isolate* isolate, const char* source_url, int script_id) const;

  WasmCode(const WasmCode&) = delete;
  WasmCode& operator=(const WasmCode&) = delete;
  ~WasmCode();

  void IncRef() {
    int old_val = ref_count_.fetch_add(1, std::memory_order_acq_rel);
    DCHECK_LE(1, old_val);
    DCHECK_GT(kMaxInt, old_val);
    USE(old_val);
  }

  // Decrement the ref count. Returns whether this code becomes dead and needs
  // to be freed.
  V8_WARN_UNUSED_RESULT bool DecRef() {
    int old_count = ref_count_.load(std::memory_order_acquire);
    while (true) {
      DCHECK_LE(1, old_count);
      if (V8_UNLIKELY(old_count == 1)) return DecRefOnPotentiallyDeadCode();
      if (ref_count_.compare_exchange_weak(old_count, old_count - 1,
                                           std::memory_order_acq_rel)) {
        return false;
      }
    }
  }

  // Decrement the ref count on code that is known to be in use (i.e. the ref
  // count cannot drop to zero here).
  void DecRefOnLiveCode() {
    int old_count = ref_count_.fetch_sub(1, std::memory_order_acq_rel);
    DCHECK_LE(2, old_count);
    USE(old_count);
  }

  // Decrement the ref count on code that is known to be dead, even though there
  // might still be C++ references. Returns whether this drops the last
  // reference and the code needs to be freed.
  V8_WARN_UNUSED_RESULT bool DecRefOnDeadCode() {
    return ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1;
  }

  // Decrement the ref count on a set of {WasmCode} objects, potentially
  // belonging to different {NativeModule}s. Dead code will be deleted.
  static void DecrementRefCount(base::Vector<WasmCode* const>);

  // Returns the last source position before {offset}.
  SourcePosition GetSourcePositionBefore(int code_offset);
  int GetSourceOffsetBefore(int code_offset);

  std::pair<int, SourcePosition> GetInliningPosition(int inlining_id) const;

  // Returns whether this code was generated for debugging. If this returns
  // {kForDebugging}, but {tier()} is not {kLiftoff}, then Liftoff compilation
  // bailed out.
  ForDebugging for_debugging() const {
    return ForDebuggingField::decode(flags_);
  }

  // Returns {true} for Liftoff code that sets up a feedback vector slot in its
  // stack frame.
  // TODO(jkummerow): This can be dropped when we ship Wasm inlining.
  bool frame_has_feedback_slot() const {
    return FrameHasFeedbackSlotField::decode(flags_);
  }

  enum FlushICache : bool { kFlushICache = true, kNoFlushICache = false };

 private:
  friend class NativeModule;

  WasmCode(NativeModule* native_module, int index,
           base::Vector<byte> instructions, int stack_slots,
           uint32_t tagged_parameter_slots, int safepoint_table_offset,
           int handler_table_offset, int constant_pool_offset,
           int code_comments_offset, int unpadded_binary_size,
           base::Vector<const byte> protected_instructions_data,
           base::Vector<const byte> reloc_info,
           base::Vector<const byte> source_position_table,
           base::Vector<const byte> inlining_positions, Kind kind,
           ExecutionTier tier, ForDebugging for_debugging,
           bool frame_has_feedback_slot = false)
      : native_module_(native_module),
        instructions_(instructions.begin()),
        meta_data_(
            ConcatenateBytes({protected_instructions_data, reloc_info,
                              source_position_table, inlining_positions})),
        instructions_size_(instructions.length()),
        reloc_info_size_(reloc_info.length()),
        source_positions_size_(source_position_table.length()),
        inlining_positions_size_(inlining_positions.length()),
        protected_instructions_size_(protected_instructions_data.length()),
        index_(index),
        constant_pool_offset_(constant_pool_offset),
        stack_slots_(stack_slots),
        tagged_parameter_slots_(tagged_parameter_slots),
        safepoint_table_offset_(safepoint_table_offset),
        handler_table_offset_(handler_table_offset),
        code_comments_offset_(code_comments_offset),
        unpadded_binary_size_(unpadded_binary_size),
        flags_(KindField::encode(kind) | ExecutionTierField::encode(tier) |
               ForDebuggingField::encode(for_debugging) |
               FrameHasFeedbackSlotField::encode(frame_has_feedback_slot)) {
    DCHECK_LE(safepoint_table_offset, unpadded_binary_size);
    DCHECK_LE(handler_table_offset, unpadded_binary_size);
    DCHECK_LE(code_comments_offset, unpadded_binary_size);
    DCHECK_LE(constant_pool_offset, unpadded_binary_size);
  }

  std::unique_ptr<const byte[]> ConcatenateBytes(
      std::initializer_list<base::Vector<const byte>>);

  // Tries to get a reasonable name. Lazily looks up the name section, and falls
  // back to the function index. Return value is guaranteed to not be empty.
  std::string DebugName() const;

  // Code objects that have been registered with the global trap
  // handler within this process, will have a {trap_handler_index} associated
  // with them.
  int trap_handler_index() const {
    CHECK(has_trap_handler_index());
    return trap_handler_index_;
  }
  void set_trap_handler_index(int value) {
    CHECK(!has_trap_handler_index());
    trap_handler_index_ = value;
  }
  bool has_trap_handler_index() const { return trap_handler_index_ >= 0; }

  // Register protected instruction information with the trap handler. Sets
  // trap_handler_index.
  void RegisterTrapHandlerData();

  // Slow path for {DecRef}: The code becomes potentially dead.
  // Returns whether this code becomes dead and needs to be freed.
  V8_NOINLINE bool DecRefOnPotentiallyDeadCode();

  NativeModule* const native_module_ = nullptr;
  byte* const instructions_;
  // {meta_data_} contains several byte vectors concatenated into one:
  //  - protected instructions data of size {protected_instructions_size_}
  //  - relocation info of size {reloc_info_size_}
  //  - source positions of size {source_positions_size_}
  // Note that the protected instructions come first to ensure alignment.
  std::unique_ptr<const byte[]> meta_data_;
  const int instructions_size_;
  const int reloc_info_size_;
  const int source_positions_size_;
  const int inlining_positions_size_;
  const int protected_instructions_size_;
  const int index_;
  const int constant_pool_offset_;
  const int stack_slots_;
  // Number and position of tagged parameters passed to this function via the
  // stack, packed into a single uint32. These values are used by the stack
  // walker (e.g. GC) to find references.
  const uint32_t tagged_parameter_slots_;
  // We care about safepoint data for wasm-to-js functions, since there may be
  // stack/register tagged values for large number conversions.
  const int safepoint_table_offset_;
  const int handler_table_offset_;
  const int code_comments_offset_;
  const int unpadded_binary_size_;
  int trap_handler_index_ = -1;

  const uint8_t flags_;  // Bit field, see below.
  // Bits encoded in {flags_}:
  using KindField = base::BitField8<Kind, 0, 2>;
  using ExecutionTierField = KindField::Next<ExecutionTier, 2>;
  using ForDebuggingField = ExecutionTierField::Next<ForDebugging, 2>;
  using FrameHasFeedbackSlotField = ForDebuggingField::Next<bool, 1>;

  // WasmCode is ref counted. Counters are held by:
  //   1) The jump table / code table.
  //   2) {WasmCodeRefScope}s.
  //   3) The set of potentially dead code in the {WasmEngine}.
  // If a decrement of (1) would drop the ref count to 0, that code becomes a
  // candidate for garbage collection. At that point, we add a ref count for (3)
  // *before* decrementing the counter to ensure the code stays alive as long as
  // it's being used. Once the ref count drops to zero (i.e. after being removed
  // from (3) and all (2)), the code object is deleted and the memory for the
  // machine code is freed.
  std::atomic<int> ref_count_{1};
};

WasmCode::Kind GetCodeKind(const WasmCompilationResult& result);

// Return a textual description of the kind.
const char* GetWasmCodeKindAsString(WasmCode::Kind);

// Manages the code reservations and allocations of a single {NativeModule}.
class WasmCodeAllocator {
 public:
  explicit WasmCodeAllocator(std::shared_ptr<Counters> async_counters);
  ~WasmCodeAllocator();

  // Call before use, after the {NativeModule} is set up completely.
  void Init(VirtualMemory code_space);

  size_t committed_code_space() const {
    return committed_code_space_.load(std::memory_order_acquire);
  }
  size_t generated_code_size() const {
    return generated_code_size_.load(std::memory_order_acquire);
  }
  size_t freed_code_size() const {
    return freed_code_size_.load(std::memory_order_acquire);
  }

  // Allocate code space. Returns a valid buffer or fails with OOM (crash).
  // Hold the {NativeModule}'s {allocation_mutex_} when calling this method.
  base::Vector<byte> AllocateForCode(NativeModule*, size_t size);

  // Allocate code space within a specific region. Returns a valid buffer or
  // fails with OOM (crash).
  // Hold the {NativeModule}'s {allocation_mutex_} when calling this method.
  base::Vector<byte> AllocateForCodeInRegion(NativeModule*, size_t size,
                                             base::AddressRegion);

  // Free memory pages of all given code objects. Used for wasm code GC.
  // Hold the {NativeModule}'s {allocation_mutex_} when calling this method.
  void FreeCode(base::Vector<WasmCode* const>);

  // Retrieve the number of separately reserved code spaces.
  // Hold the {NativeModule}'s {allocation_mutex_} when calling this method.
  size_t GetNumCodeSpaces() const;

  Counters* counters() const { return async_counters_.get(); }

 private:
  //////////////////////////////////////////////////////////////////////////////
  // These fields are protected by the mutex in {NativeModule}.

  // Code space that was reserved and is available for allocations
  // (subset of {owned_code_space_}).
  DisjointAllocationPool free_code_space_;
  // Code space that was allocated before but is dead now. Full
  // pages within this region are discarded. It's still a subset of
  // {owned_code_space_}.
  DisjointAllocationPool freed_code_space_;
  std::vector<VirtualMemory> owned_code_space_;

  // End of fields protected by {mutex_}.
  //////////////////////////////////////////////////////////////////////////////

  std::atomic<size_t> committed_code_space_{0};
  std::atomic<size_t> generated_code_size_{0};
  std::atomic<size_t> freed_code_size_{0};

  std::shared_ptr<Counters> async_counters_;
};

class V8_EXPORT_PRIVATE NativeModule final {
 public:
#if V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_S390X || V8_TARGET_ARCH_ARM64 || \
    V8_TARGET_ARCH_PPC64
  static constexpr bool kNeedsFarJumpsBetweenCodeSpaces = true;
#else
  static constexpr bool kNeedsFarJumpsBetweenCodeSpaces = false;
#endif

  NativeModule(const NativeModule&) = delete;
  NativeModule& operator=(const NativeModule&) = delete;
  ~NativeModule();

  // {AddCode} is thread safe w.r.t. other calls to {AddCode} or methods adding
  // code below, i.e. it can be called concurrently from background threads.
  // The returned code still needs to be published via {PublishCode}.
  std::unique_ptr<WasmCode> AddCode(
      int index, const CodeDesc& desc, int stack_slots,
      uint32_t tagged_parameter_slots,
      base::Vector<const byte> protected_instructions,
      base::Vector<const byte> source_position_table, WasmCode::Kind kind,
      ExecutionTier tier, ForDebugging for_debugging);

  // {PublishCode} makes the code available to the system by entering it into
  // the code table and patching the jump table. It returns a raw pointer to the
  // given {WasmCode} object. Ownership is transferred to the {NativeModule}.
  // Returns {nullptr} if the {AssumptionsJournal} is non-nullptr and contains
  // invalid assumptions.
  WasmCode* PublishCode(std::unique_ptr<WasmCode>,
                        AssumptionsJournal* = nullptr);
  std::vector<WasmCode*> PublishCode(base::Vector<std::unique_ptr<WasmCode>>);

  // ReinstallDebugCode does a subset of PublishCode: It installs the code in
  // the code table and patches the jump table. The given code must be debug
  // code (with breakpoints) and must be owned by this {NativeModule} already.
  // This method is used to re-instantiate code that was removed from the code
  // table and jump table via another {PublishCode}.
  void ReinstallDebugCode(WasmCode*);

  struct JumpTablesRef {
    Address jump_table_start = kNullAddress;
    Address far_jump_table_start = kNullAddress;

    bool is_valid() const { return far_jump_table_start != kNullAddress; }
  };

  std::pair<base::Vector<uint8_t>, JumpTablesRef> AllocateForDeserializedCode(
      size_t total_code_size);

  std::unique_ptr<WasmCode> AddDeserializedCode(
      int index, base::Vector<byte> instructions, int stack_slots,
      uint32_t tagged_parameter_slots, int safepoint_table_offset,
      int handler_table_offset, int constant_pool_offset,
      int code_comments_offset, int unpadded_binary_size,
      base::Vector<const byte> protected_instructions_data,
      base::Vector<const byte> reloc_info,
      base::Vector<const byte> source_position_table,
      base::Vector<const byte> inlining_positions, WasmCode::Kind kind,
      ExecutionTier tier);

  // Adds anonymous code for testing purposes.
  WasmCode* AddCodeForTesting(Handle<Code> code);

  // Allocates and initializes the {lazy_compile_table_} and initializes the
  // first jump table with jumps to the {lazy_compile_table_}.
  void InitializeJumpTableForLazyCompilation(uint32_t num_wasm_functions);

  // Use {UseLazyStubLocked} to setup lazy compilation per function. It will use
  // the existing {WasmCode::kWasmCompileLazy} runtime stub and populate the
  // jump table with trampolines accordingly.
  void UseLazyStubLocked(uint32_t func_index);

  // Creates a snapshot of the current state of the code table, along with the
  // current import statuses that these code objects depend on. This is useful
  // to get a consistent view of the table (e.g. used by the serializer).
  std::pair<std::vector<WasmCode*>, std::vector<WellKnownImport>>
  SnapshotCodeTable() const;
  // Creates a snapshot of all {owned_code_}, will transfer new code (if any) to
  // {owned_code_}.
  std::vector<WasmCode*> SnapshotAllOwnedCode() const;

  WasmCode* GetCode(uint32_t index) const;
  bool HasCode(uint32_t index) const;
  bool HasCodeWithTier(uint32_t index, ExecutionTier tier) const;

  void SetWasmSourceMap(std::unique_ptr<WasmModuleSourceMap> source_map);
  WasmModuleSourceMap* GetWasmSourceMap() const;

  Address jump_table_start() const {
    return main_jump_table_ ? main_jump_table_->instruction_start()
                            : kNullAddress;
  }

  // Get the call target in the jump table previously looked up via
  // {FindJumpTablesForRegionLocked}.
  Address GetNearCallTargetForFunction(uint32_t func_index,
                                       const JumpTablesRef&) const;

  // Get a runtime stub entry (which is a far jump table slot) in the jump table
  // previously looked up via {FindJumpTablesForRegionLocked}.
  Address GetNearRuntimeStubEntry(WasmCode::RuntimeStubId index,
                                  const JumpTablesRef&) const;

  // Reverse lookup from a given call target (which must be a jump table slot)
  // to a function index.
  uint32_t GetFunctionIndexFromJumpTableSlot(Address slot_address) const;

  // For cctests, where we build both WasmModule and the runtime objects
  // on the fly, and bypass the instance builder pipeline.
  void ReserveCodeTableForTesting(uint32_t max_functions);

  void LogWasmCodes(Isolate*, Script);

  CompilationState* compilation_state() const {
    return compilation_state_.get();
  }

  // Create a {CompilationEnv} object for compilation. The caller has to ensure
  // that the {WasmModule} pointer stays valid while the {CompilationEnv} is
  // being used.
  CompilationEnv CreateCompilationEnv() const;

  uint32_t num_functions() const {
    return module_->num_declared_functions + module_->num_imported_functions;
  }
  uint32_t num_imported_functions() const {
    return module_->num_imported_functions;
  }
  BoundsCheckStrategy bounds_checks() const { return bounds_checks_; }
  void set_lazy_compile_frozen(bool frozen) { lazy_compile_frozen_ = frozen; }
  bool lazy_compile_frozen() const { return lazy_compile_frozen_; }
  base::Vector<const uint8_t> wire_bytes() const {
    return std::atomic_load(&wire_bytes_)->as_vector();
  }
  const WasmModule* module() const { return module_.get(); }
  std::shared_ptr<const WasmModule> shared_module() const { return module_; }
  size_t committed_code_space() const {
    return code_allocator_.committed_code_space();
  }
  size_t generated_code_size() const {
    return code_allocator_.generated_code_size();
  }
  size_t liftoff_bailout_count() const {
    return liftoff_bailout_count_.load(std::memory_order_relaxed);
  }
  size_t liftoff_code_size() const {
    return liftoff_code_size_.load(std::memory_order_relaxed);
  }
  size_t turbofan_code_size() const {
    return turbofan_code_size_.load(std::memory_order_relaxed);
  }

  void AddLazyCompilationTimeSample(int64_t sample);

  int num_lazy_compilations() const {
    return num_lazy_compilations_.load(std::memory_order_relaxed);
  }

  int64_t sum_lazy_compilation_time_in_ms() const {
    return sum_lazy_compilation_time_in_micro_sec_.load(
               std::memory_order_relaxed) /
           1000;
  }

  int64_t max_lazy_compilation_time_in_ms() const {
    return max_lazy_compilation_time_in_micro_sec_.load(
               std::memory_order_relaxed) /
           1000;
  }

  // To avoid double-reporting, only the first instantiation should report lazy
  // compilation performance metrics.
  bool ShouldLazyCompilationMetricsBeReported() {
    return should_metrics_be_reported_.exchange(false,
                                                std::memory_order_relaxed);
  }

  // Similar to above, scheduling a repeated task to write out PGO data is only
  // needed once per module, not per instantiation.
  bool ShouldPgoDataBeWritten() {
    return should_pgo_data_be_written.exchange(false,
                                               std::memory_order_relaxed);
  }

  bool HasWireBytes() const {
    auto wire_bytes = std::atomic_load(&wire_bytes_);
    return wire_bytes && !wire_bytes->empty();
  }
  void SetWireBytes(base::OwnedVector<const uint8_t> wire_bytes);

  void AddLiftoffBailout() {
    liftoff_bailout_count_.fetch_add(1, std::memory_order_relaxed);
  }

  WasmCode* Lookup(Address) const;

  WasmImportWrapperCache* import_wrapper_cache() const {
    return import_wrapper_cache_.get();
  }

  const WasmFeatures& enabled_features() const { return enabled_features_; }

  // Returns the runtime stub id that corresponds to the given address (which
  // must be a far jump table slot). Returns {kRuntimeStubCount} on failure.
  WasmCode::RuntimeStubId GetRuntimeStubId(Address runtime_stub_target) const;

  // Sample the current code size of this modules to the given counters.
  void SampleCodeSize(Counters*) const;

  V8_WARN_UNUSED_RESULT std::unique_ptr<WasmCode> AddCompiledCode(
      const WasmCompilationResult&);
  V8_WARN_UNUSED_RESULT std::vector<std::unique_ptr<WasmCode>> AddCompiledCode(
      base::Vector<const WasmCompilationResult>);

  // Set a new debugging state, but don't trigger any recompilation;
  // recompilation happens lazily.
  void SetDebugState(DebugState);

  // Check whether this modules is in debug state.
  DebugState IsInDebugState() const {
    base::RecursiveMutexGuard lock(&allocation_mutex_);
    return debug_state_;
  }

  enum class RemoveFilter {
    kRemoveDebugCode,
    kRemoveNonDebugCode,
    kRemoveLiftoffCode,
    kRemoveTurbofanCode,
    kRemoveAllCode,
  };
  // Remove all compiled code from the {NativeModule} and replace it with
  // {CompileLazy} builtins.
  void RemoveCompiledCode(RemoveFilter filter);

  // Free a set of functions of this module. Uncommits whole pages if possible.
  // The given vector must be ordered by the instruction start address, and all
  // {WasmCode} objects must not be used any more.
  // Should only be called via {WasmEngine::FreeDeadCode}, so the engine can do
  // its accounting.
  void FreeCode(base::Vector<WasmCode* const>);

  // Retrieve the number of separately reserved code spaces for this module.
  size_t GetNumberOfCodeSpacesForTesting() const;

  // Check whether there is DebugInfo for this NativeModule.
  bool HasDebugInfo() const;

  // Get or create the debug info for this NativeModule.
  DebugInfo* GetDebugInfo();

  // Get or create the NamesProvider. Requires {HasWireBytes()}.
  NamesProvider* GetNamesProvider();

  uint32_t* tiering_budget_array() const { return tiering_budgets_.get(); }

  Counters* counters() const { return code_allocator_.counters(); }

 private:
  friend class WasmCode;
  friend class WasmCodeAllocator;
  friend class WasmCodeManager;
  friend class CodeSpaceWriteScope;

  struct CodeSpaceData {
    base::AddressRegion region;
    WasmCode* jump_table;
    WasmCode* far_jump_table;
  };

  // Private constructor, called via {WasmCodeManager::NewNativeModule()}.
  NativeModule(const WasmFeatures& enabled_features,
               DynamicTiering dynamic_tiering, VirtualMemory code_space,
               std::shared_ptr<const WasmModule> module,
               std::shared_ptr<Counters> async_counters,
               std::shared_ptr<NativeModule>* shared_this);

  std::unique_ptr<WasmCode> AddCodeWithCodeSpace(
      int index, const CodeDesc& desc, int stack_slots,
      uint32_t tagged_parameter_slots,
      base::Vector<const byte> protected_instructions_data,
      base::Vector<const byte> source_position_table,
      base::Vector<const byte> inlining_positions, WasmCode::Kind kind,
      ExecutionTier tier, ForDebugging for_debugging,
      bool frame_has_feedback_slot, base::Vector<uint8_t> code_space,
      const JumpTablesRef& jump_tables_ref);

  WasmCode* CreateEmptyJumpTableLocked(int jump_table_size);

  WasmCode* CreateEmptyJumpTableInRegionLocked(int jump_table_size,
                                               base::AddressRegion);

  // Finds the jump tables that should be used for given code region. This
  // information is then passed to {GetNearCallTargetForFunction} and
  // {GetNearRuntimeStubEntry} to avoid the overhead of looking this information
  // up there. Return an empty struct if no suitable jump tables exist.
  JumpTablesRef FindJumpTablesForRegionLocked(base::AddressRegion) const;

  void UpdateCodeSize(size_t, ExecutionTier, ForDebugging);

  // Hold the {allocation_mutex_} when calling one of these methods.
  // {slot_index} is the index in the declared functions, i.e. function index
  // minus the number of imported functions.
  void PatchJumpTablesLocked(uint32_t slot_index, Address target);
  void PatchJumpTableLocked(const CodeSpaceData&, uint32_t slot_index,
                            Address target);

  // Called by the {WasmCodeAllocator} to register a new code space.
  void AddCodeSpaceLocked(base::AddressRegion);

  // Hold the {allocation_mutex_} when calling {PublishCodeLocked}.
  WasmCode* PublishCodeLocked(std::unique_ptr<WasmCode>);

  // Transfer owned code from {new_owned_code_} to {owned_code_}.
  void TransferNewOwnedCodeLocked() const;

  // Add code to the code cache, if it meets criteria for being cached and we do
  // not have code in the cache yet.
  void InsertToCodeCache(WasmCode* code);

  bool should_update_code_table(WasmCode* new_code, WasmCode* prior_code) const;

  // -- Fields of {NativeModule} start here.

  // Keep the engine alive as long as this NativeModule is alive. In its
  // destructor, the NativeModule still communicates with the WasmCodeManager,
  // owned by the engine. This fields comes before other fields which also still
  // access the engine (like the code allocator), so that it's destructor runs
  // last.
  OperationsBarrier::Token engine_scope_;

  // {WasmCodeAllocator} manages all code reservations and allocations for this
  // {NativeModule}.
  WasmCodeAllocator code_allocator_;

  // Features enabled for this module. We keep a copy of the features that
  // were enabled at the time of the creation of this native module,
  // to be consistent across asynchronous compilations later.
  const WasmFeatures enabled_features_;

  // The decoded module, stored in a shared_ptr such that background compile
  // tasks can keep this alive.
  std::shared_ptr<const WasmModule> module_;

  std::unique_ptr<WasmModuleSourceMap> source_map_;

  // Wire bytes, held in a shared_ptr so they can be kept alive by the
  // {WireBytesStorage}, held by background compile tasks.
  std::shared_ptr<base::OwnedVector<const uint8_t>> wire_bytes_;

  // The first allocated jump table. Always used by external calls (from JS).
  // Wasm calls might use one of the other jump tables stored in
  // {code_space_data_}.
  WasmCode* main_jump_table_ = nullptr;

  // The first allocated far jump table.
  WasmCode* main_far_jump_table_ = nullptr;

  // Lazy compile stub table, containing entries to jump to the
  // {WasmCompileLazy} builtin, passing the function index.
  WasmCode* lazy_compile_table_ = nullptr;

  // The compilation state keeps track of compilation tasks for this module.
  // Note that its destructor blocks until all tasks are finished/aborted and
  // hence needs to be destructed first when this native module dies.
  std::unique_ptr<CompilationState> compilation_state_;

  // A cache of the import wrappers, keyed on the kind and signature.
  std::unique_ptr<WasmImportWrapperCache> import_wrapper_cache_;

  // Array to handle number of function calls.
  std::unique_ptr<uint32_t[]> tiering_budgets_;

  // This mutex protects concurrent calls to {AddCode} and friends.
  // TODO(dlehmann): Revert this to a regular {Mutex} again.
  // This needs to be a {RecursiveMutex} only because of {CodeSpaceWriteScope}
  // usages, which are (1) either at places that already hold the
  // {allocation_mutex_} or (2) because of multiple open {CodeSpaceWriteScope}s
  // in the call hierarchy. Both are fixable.
  mutable base::RecursiveMutex allocation_mutex_;

  //////////////////////////////////////////////////////////////////////////////
  // Protected by {allocation_mutex_}:

  // Holds allocated code objects for fast lookup and deletion. For lookup based
  // on pc, the key is the instruction start address of the value. Filled lazily
  // from {new_owned_code_} (below).
  mutable std::map<Address, std::unique_ptr<WasmCode>> owned_code_;

  // Holds owned code which is not inserted into {owned_code_} yet. It will be
  // inserted on demand. This has much better performance than inserting
  // individual code objects.
  mutable std::vector<std::unique_ptr<WasmCode>> new_owned_code_;

  // Table of the latest code object per function, updated on initial
  // compilation and tier up. The number of entries is
  // {WasmModule::num_declared_functions}, i.e. there are no entries for
  // imported functions.
  std::unique_ptr<WasmCode*[]> code_table_;

  // Data (especially jump table) per code space.
  std::vector<CodeSpaceData> code_space_data_;

  // Debug information for this module. You only need to hold the allocation
  // mutex while getting the {DebugInfo} pointer, or initializing this field.
  // Further accesses to the {DebugInfo} do not need to be protected by the
  // mutex.
  std::unique_ptr<DebugInfo> debug_info_;

  std::unique_ptr<NamesProvider> names_provider_;

  DebugState debug_state_ = kNotDebugging;

  // Cache both baseline and top-tier code if we are debugging, to speed up
  // repeated enabling/disabling of the debugger or profiler.
  // Maps <tier, function_index> to WasmCode.
  std::unique_ptr<std::map<std::pair<ExecutionTier, int>, WasmCode*>>
      cached_code_;

  // End of fields protected by {allocation_mutex_}.
  //////////////////////////////////////////////////////////////////////////////

  const BoundsCheckStrategy bounds_checks_;
  bool lazy_compile_frozen_ = false;
  std::atomic<size_t> liftoff_bailout_count_{0};
  std::atomic<size_t> liftoff_code_size_{0};
  std::atomic<size_t> turbofan_code_size_{0};

  // Metrics for lazy compilation.
  std::atomic<int> num_lazy_compilations_{0};
  std::atomic<int64_t> sum_lazy_compilation_time_in_micro_sec_{0};
  std::atomic<int64_t> max_lazy_compilation_time_in_micro_sec_{0};
  std::atomic<bool> should_metrics_be_reported_{true};

  // Whether the next instantiation should trigger repeated output of PGO data
  // (if --experimental-wasm-pgo-to-file is enabled).
  std::atomic<bool> should_pgo_data_be_written{true};
};

class V8_EXPORT_PRIVATE WasmCodeManager final {
 public:
  WasmCodeManager();
  WasmCodeManager(const WasmCodeManager&) = delete;
  WasmCodeManager& operator=(const WasmCodeManager&) = delete;

  ~WasmCodeManager();

#if defined(V8_OS_WIN64)
  static bool CanRegisterUnwindInfoForNonABICompliantCodeRange();
#endif  // V8_OS_WIN64

  NativeModule* LookupNativeModule(Address pc) const;
  WasmCode* LookupCode(Address pc) const;
  size_t committed_code_space() const {
    return total_committed_code_space_.load();
  }

  // Estimate the needed code space for a Liftoff function based on the size of
  // the function body (wasm byte code).
  static size_t EstimateLiftoffCodeSize(int body_size);
  // Estimate the needed code space from a completely decoded module.
  static size_t EstimateNativeModuleCodeSize(const WasmModule* module,
                                             bool include_liftoff,
                                             DynamicTiering dynamic_tiering);
  // Estimate the needed code space from the number of functions and total code
  // section length.
  static size_t EstimateNativeModuleCodeSize(int num_functions,
                                             int num_imported_functions,
                                             int code_section_length,
                                             bool include_liftoff,
                                             DynamicTiering dynamic_tiering);
  // Estimate the size of meta data needed for the NativeModule, excluding
  // generated code. This data still be stored on the C++ heap.
  static size_t EstimateNativeModuleMetaDataSize(const WasmModule* module);

  // Returns true if there is hardware support for PKU. Use
  // {MemoryProtectionKeysEnabled} to also check if PKU usage is enabled via
  // flags.
  static bool HasMemoryProtectionKeySupport();

  // Returns true if PKU should be used.
  static bool MemoryProtectionKeysEnabled();

  // Returns {true} if the memory protection key is write-enabled for the
  // current thread.
  // Can only be called if {HasMemoryProtectionKeySupport()} is {true}.
  static bool MemoryProtectionKeyWritable();

  // Allocate new memory for assembler buffers, potentially protected by PKU.
  base::AddressRegion AllocateAssemblerBufferSpace(int size);

  // Free previously allocated space for assembler buffers.
  void FreeAssemblerBufferSpace(base::AddressRegion region);

 private:
  friend class WasmCodeAllocator;
  friend class WasmEngine;

  std::shared_ptr<NativeModule> NewNativeModule(
      Isolate* isolate, const WasmFeatures& enabled_features,
      size_t code_size_estimate, std::shared_ptr<const WasmModule> module);

  V8_WARN_UNUSED_RESULT VirtualMemory TryAllocate(size_t size,
                                                  void* hint = nullptr);
  void Commit(base::AddressRegion);
  void Decommit(base::AddressRegion);

  void FreeNativeModule(base::Vector<VirtualMemory> owned_code,
                        size_t committed_size);

  void AssignRange(base::AddressRegion, NativeModule*);

  const size_t max_committed_code_space_;

  std::atomic<size_t> total_committed_code_space_{0};
  // If the committed code space exceeds {critical_committed_code_space_}, then
  // we trigger a GC before creating the next module. This value is set to the
  // currently committed space plus 50% of the available code space on creation
  // and updated after each GC.
  std::atomic<size_t> critical_committed_code_space_;

  mutable base::Mutex native_modules_mutex_;

  //////////////////////////////////////////////////////////////////////////////
  // Protected by {native_modules_mutex_}:

  std::map<Address, std::pair<Address, NativeModule*>> lookup_map_;

  // End of fields protected by {native_modules_mutex_}.
  //////////////////////////////////////////////////////////////////////////////
};

// {WasmCodeRefScope}s form a perfect stack. New {WasmCode} pointers generated
// by e.g. creating new code or looking up code by its address are added to the
// top-most {WasmCodeRefScope}.
class V8_EXPORT_PRIVATE V8_NODISCARD WasmCodeRefScope {
 public:
  WasmCodeRefScope();
  WasmCodeRefScope(const WasmCodeRefScope&) = delete;
  WasmCodeRefScope& operator=(const WasmCodeRefScope&) = delete;
  ~WasmCodeRefScope();

  // Register a {WasmCode} reference in the current {WasmCodeRefScope}. Fails if
  // there is no current scope.
  static void AddRef(WasmCode*);

 private:
  WasmCodeRefScope* const previous_scope_;
  std::vector<WasmCode*> code_ptrs_;
};

// Similarly to a global handle, a {GlobalWasmCodeRef} stores a single
// ref-counted pointer to a {WasmCode} object.
class GlobalWasmCodeRef {
 public:
  explicit GlobalWasmCodeRef(WasmCode* code,
                             std::shared_ptr<NativeModule> native_module)
      : code_(code), native_module_(std::move(native_module)) {
    code_->IncRef();
  }

  GlobalWasmCodeRef(const GlobalWasmCodeRef&) = delete;
  GlobalWasmCodeRef& operator=(const GlobalWasmCodeRef&) = delete;

  ~GlobalWasmCodeRef() { WasmCode::DecrementRefCount({&code_, 1}); }

  // Get a pointer to the contained {WasmCode} object. This is only guaranteed
  // to exist as long as this {GlobalWasmCodeRef} exists.
  WasmCode* code() const { return code_; }

 private:
  WasmCode* const code_;
  // Also keep the {NativeModule} alive.
  const std::shared_ptr<NativeModule> native_module_;
};

Builtin RuntimeStubIdToBuiltinName(WasmCode::RuntimeStubId);
const char* GetRuntimeStubName(WasmCode::RuntimeStubId);

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_CODE_MANAGER_H_
