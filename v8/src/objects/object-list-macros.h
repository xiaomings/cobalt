// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_OBJECT_LIST_MACROS_H_
#define V8_OBJECTS_OBJECT_LIST_MACROS_H_

#include "torque-generated/instance-types.h"

namespace v8 {
namespace internal {

class AbstractCode;
class AccessorPair;
class AccessCheckInfo;
class AllocationSite;
class ByteArray;
class Cell;
class ClosureFeedbackCellArray;
class ConsString;
class DependentCode;
class ElementsAccessor;
class EnumCache;
class FixedArrayBase;
class FixedDoubleArray;
class FreeSpace;
class FunctionLiteral;
class FunctionTemplateInfo;
class JSAsyncGeneratorObject;
class JSGlobalProxy;
class SourceTextModule;
class JSPromise;
class JSProxy;
class JSProxyRevocableResult;
class KeyAccumulator;
class LookupIterator;
class FieldType;
class Module;
class SourceTextModuleInfoEntry;
class HeapNumber;
class ObjectHashTable;
class ObjectTemplateInfo;
class ObjectVisitor;
class OSROptimizedCodeCache;
class PreparseData;
class PropertyArray;
class PropertyCell;
class PropertyDescriptor;
class PrototypeInfo;
class ReadOnlyRoots;
class RegExpMatchInfo;
class RootVisitor;
class SafepointEntry;
class ScriptContextTable;
class SharedFunctionInfo;
class StringStream;
class Symbol;
class SyntheticModule;
class FeedbackCell;
class FeedbackMetadata;
class FeedbackVector;
class UncompiledData;
class TemplateInfo;
class TransitionArray;
class TemplateList;
class WasmInstanceObject;
class WasmMemoryObject;
template <typename T>
class ZoneForwardList;

#define OBJECT_TYPE_LIST(V) \
  V(Primitive)              \
  V(Number)                 \
  V(Numeric)

#define HEAP_OBJECT_ORDINARY_TYPE_LIST_BASE(V)  \
  V(AbstractCode)                               \
  V(AccessCheckNeeded)                          \
  V(AccessorInfo)                               \
  V(AllocationSite)                             \
  V(AlwaysSharedSpaceJSObject)                  \
  V(ArrayList)                                  \
  V(BigInt)                                     \
  V(BigIntBase)                                 \
  V(BigIntWrapper)                              \
  V(ObjectBoilerplateDescription)               \
  V(Boolean)                                    \
  V(BooleanWrapper)                             \
  V(ByteArray)                                  \
  V(BytecodeArray)                              \
  V(CallHandlerInfo)                            \
  V(Callable)                                   \
  V(Cell)                                       \
  V(ClassBoilerplate)                           \
  V(InstructionStream)                          \
  V(Code)                                       \
  V(CompilationCacheTable)                      \
  V(ConsString)                                 \
  V(Constructor)                                \
  V(Context)                                    \
  V(CoverageInfo)                               \
  V(ClosureFeedbackCellArray)                   \
  V(DataHandler)                                \
  V(DeoptimizationData)                         \
  V(DependentCode)                              \
  V(DescriptorArray)                            \
  V(EmbedderDataArray)                          \
  V(EphemeronHashTable)                         \
  V(ExternalOneByteString)                      \
  V(ExternalString)                             \
  V(ExternalTwoByteString)                      \
  V(FeedbackCell)                               \
  V(FeedbackMetadata)                           \
  V(FeedbackVector)                             \
  V(Filler)                                     \
  V(FixedArray)                                 \
  V(FixedArrayBase)                             \
  V(FixedArrayExact)                            \
  V(FixedDoubleArray)                           \
  V(Foreign)                                    \
  V(FreeSpace)                                  \
  V(Function)                                   \
  V(GcSafeCode)                                 \
  V(GlobalDictionary)                           \
  V(HandlerTable)                               \
  V(HeapNumber)                                 \
  V(InternalizedString)                         \
  V(JSArgumentsObject)                          \
  V(JSArray)                                    \
  V(JSArrayBuffer)                              \
  V(JSArrayBufferView)                          \
  V(JSArrayIterator)                            \
  V(JSAsyncFromSyncIterator)                    \
  V(JSAsyncFunctionObject)                      \
  V(JSAsyncGeneratorObject)                     \
  V(JSAtomicsCondition)                         \
  V(JSAtomicsMutex)                             \
  V(JSBoundFunction)                            \
  V(JSCollection)                               \
  V(JSCollectionIterator)                       \
  V(JSContextExtensionObject)                   \
  V(JSCustomElementsObject)                     \
  V(JSDataView)                                 \
  V(JSDataViewOrRabGsabDataView)                \
  V(JSDate)                                     \
  V(JSError)                                    \
  V(JSExternalObject)                           \
  V(JSFinalizationRegistry)                     \
  V(JSFunction)                                 \
  V(JSFunctionOrBoundFunctionOrWrappedFunction) \
  V(JSGeneratorObject)                          \
  V(JSGlobalObject)                             \
  V(JSGlobalProxy)                              \
  V(JSIteratorHelper)                           \
  V(JSIteratorFilterHelper)                     \
  V(JSIteratorMapHelper)                        \
  V(JSIteratorTakeHelper)                       \
  V(JSIteratorDropHelper)                       \
  V(JSIteratorFlatMapHelper)                    \
  V(JSMap)                                      \
  V(JSMapIterator)                              \
  V(JSMessageObject)                            \
  V(JSModuleNamespace)                          \
  V(JSObject)                                   \
  V(JSObjectWithEmbedderSlots)                  \
  V(JSPrimitiveWrapper)                         \
  V(JSPromise)                                  \
  V(JSProxy)                                    \
  V(JSRabGsabDataView)                          \
  V(JSRawJson)                                  \
  V(JSReceiver)                                 \
  V(JSRegExp)                                   \
  V(JSRegExpStringIterator)                     \
  V(JSSet)                                      \
  V(JSSetIterator)                              \
  V(JSShadowRealm)                              \
  V(JSSharedArray)                              \
  V(JSSharedStruct)                             \
  V(JSSpecialObject)                            \
  V(JSStringIterator)                           \
  V(JSSynchronizationPrimitive)                 \
  V(JSTemporalCalendar)                         \
  V(JSTemporalDuration)                         \
  V(JSTemporalInstant)                          \
  V(JSTemporalPlainDate)                        \
  V(JSTemporalPlainTime)                        \
  V(JSTemporalPlainDateTime)                    \
  V(JSTemporalPlainMonthDay)                    \
  V(JSTemporalPlainYearMonth)                   \
  V(JSTemporalTimeZone)                         \
  V(JSTemporalZonedDateTime)                    \
  V(JSTypedArray)                               \
  V(JSValidIteratorWrapper)                     \
  V(JSWeakCollection)                           \
  V(JSWeakRef)                                  \
  V(JSWeakMap)                                  \
  V(JSWeakSet)                                  \
  V(JSWrappedFunction)                          \
  V(LoadHandler)                                \
  V(Map)                                        \
  V(MapCache)                                   \
  V(MegaDomHandler)                             \
  V(Module)                                     \
  V(Microtask)                                  \
  V(Name)                                       \
  V(NameDictionary)                             \
  V(NameToIndexHashTable)                       \
  V(NativeContext)                              \
  V(NormalizedMapCache)                         \
  V(NumberDictionary)                           \
  V(NumberWrapper)                              \
  V(ObjectHashSet)                              \
  V(ObjectHashTable)                            \
  V(ObjectTwoHashTable)                         \
  V(Oddball)                                    \
  V(OrderedHashMap)                             \
  V(OrderedHashSet)                             \
  V(OrderedNameDictionary)                      \
  V(OSROptimizedCodeCache)                      \
  V(PreparseData)                               \
  V(PrimitiveHeapObject)                        \
  V(PromiseReactionJobTask)                     \
  V(PropertyArray)                              \
  V(PropertyCell)                               \
  V(RegExpMatchInfo)                            \
  V(ScopeInfo)                                  \
  V(ScriptContextTable)                         \
  V(ScriptWrapper)                              \
  V(SeqOneByteString)                           \
  V(SeqString)                                  \
  V(SeqTwoByteString)                           \
  V(SharedFunctionInfo)                         \
  V(SimpleNumberDictionary)                     \
  V(SlicedString)                               \
  V(SmallOrderedHashMap)                        \
  V(SmallOrderedHashSet)                        \
  V(SmallOrderedNameDictionary)                 \
  V(SourceTextModule)                           \
  V(SourceTextModuleInfo)                       \
  V(StoreHandler)                               \
  V(String)                                     \
  V(StringSet)                                  \
  V(RegisteredSymbolTable)                      \
  V(StringWrapper)                              \
  V(Struct)                                     \
  V(SwissNameDictionary)                        \
  V(Symbol)                                     \
  V(SymbolWrapper)                              \
  V(SyntheticModule)                            \
  V(TemplateInfo)                               \
  V(TemplateList)                               \
  V(TemplateLiteralObject)                      \
  V(ThinString)                                 \
  V(TransitionArray)                            \
  V(TurboshaftFloat64RangeType)                 \
  V(TurboshaftFloat64SetType)                   \
  V(TurboshaftFloat64Type)                      \
  V(TurboshaftType)                             \
  V(TurboshaftWord32RangeType)                  \
  V(TurboshaftWord32SetType)                    \
  V(TurboshaftWord32Type)                       \
  V(TurboshaftWord64RangeType)                  \
  V(TurboshaftWord64SetType)                    \
  V(TurboshaftWord64Type)                       \
  V(UncompiledData)                             \
  V(UncompiledDataWithPreparseData)             \
  V(UncompiledDataWithoutPreparseData)          \
  V(UncompiledDataWithPreparseDataAndJob)       \
  V(UncompiledDataWithoutPreparseDataWithJob)   \
  V(Undetectable)                               \
  V(UniqueName)                                 \
  IF_WASM(V, WasmApiFunctionRef)                \
  IF_WASM(V, WasmArray)                         \
  IF_WASM(V, WasmCapiFunctionData)              \
  IF_WASM(V, WasmTagObject)                     \
  IF_WASM(V, WasmExceptionPackage)              \
  IF_WASM(V, WasmExportedFunctionData)          \
  IF_WASM(V, WasmFunctionData)                  \
  IF_WASM(V, WasmGlobalObject)                  \
  IF_WASM(V, WasmInternalFunction)              \
  IF_WASM(V, WasmInstanceObject)                \
  IF_WASM(V, WasmJSFunctionData)                \
  IF_WASM(V, WasmMemoryObject)                  \
  IF_WASM(V, WasmModuleObject)                  \
  IF_WASM(V, WasmObject)                        \
  IF_WASM(V, WasmResumeData)                    \
  IF_WASM(V, WasmStruct)                        \
  IF_WASM(V, WasmTypeInfo)                      \
  IF_WASM(V, WasmTableObject)                   \
  IF_WASM(V, WasmValueObject)                   \
  IF_WASM(V, WasmSuspenderObject)               \
  IF_WASM(V, WasmContinuationObject)            \
  IF_WASM(V, WasmNull)                          \
  V(WeakFixedArray)                             \
  V(WeakArrayList)                              \
  V(WeakCell)                                   \
  TORQUE_DEFINED_CLASS_LIST(V)

#ifdef V8_INTL_SUPPORT
#define HEAP_OBJECT_ORDINARY_TYPE_LIST(V) \
  HEAP_OBJECT_ORDINARY_TYPE_LIST_BASE(V)  \
  V(JSV8BreakIterator)                    \
  V(JSCollator)                           \
  V(JSDateTimeFormat)                     \
  V(JSDisplayNames)                       \
  V(JSDurationFormat)                     \
  V(JSListFormat)                         \
  V(JSLocale)                             \
  V(JSNumberFormat)                       \
  V(JSPluralRules)                        \
  V(JSRelativeTimeFormat)                 \
  V(JSSegmentIterator)                    \
  V(JSSegmenter)                          \
  V(JSSegments)
#else
#define HEAP_OBJECT_ORDINARY_TYPE_LIST(V) HEAP_OBJECT_ORDINARY_TYPE_LIST_BASE(V)
#endif  // V8_INTL_SUPPORT

#define HEAP_OBJECT_TEMPLATE_TYPE_LIST(V) V(HashTable)

// Logical sub-types of heap objects that don't correspond to a C++ class but
// represent some specialization in terms of additional constraints.
#define HEAP_OBJECT_SPECIALIZED_TYPE_LIST(V) \
  V(AwaitContext)                            \
  V(BlockContext)                            \
  V(CallableApiObject)                       \
  V(CallableJSProxy)                         \
  V(CatchContext)                            \
  V(DebugEvaluateContext)                    \
  V(EvalContext)                             \
  V(FreeSpaceOrFiller)                       \
  V(FunctionContext)                         \
  V(JSApiObject)                             \
  V(JSClassConstructor)                      \
  V(JSLastDummyApiObject)                    \
  V(JSPromiseConstructor)                    \
  V(JSArrayConstructor)                      \
  V(JSRegExpConstructor)                     \
  V(JSMapKeyIterator)                        \
  V(JSMapKeyValueIterator)                   \
  V(JSMapValueIterator)                      \
  V(JSSetKeyValueIterator)                   \
  V(JSSetValueIterator)                      \
  V(JSSpecialApiObject)                      \
  V(ModuleContext)                           \
  V(NonNullForeign)                          \
  V(ScriptContext)                           \
  V(WithContext)                             \
  V(JSPrototype)                             \
  V(JSObjectPrototype)                       \
  V(JSRegExpPrototype)                       \
  V(JSPromisePrototype)                      \
  V(JSSetPrototype)                          \
  V(JSIteratorPrototype)                     \
  V(JSArrayIteratorPrototype)                \
  V(JSMapIteratorPrototype)                  \
  V(JSTypedArrayPrototype)                   \
  V(JSSetIteratorPrototype)                  \
  V(JSStringIteratorPrototype)               \
  V(TypedArrayConstructor)                   \
  V(Uint8TypedArrayConstructor)              \
  V(Int8TypedArrayConstructor)               \
  V(Uint16TypedArrayConstructor)             \
  V(Int16TypedArrayConstructor)              \
  V(Uint32TypedArrayConstructor)             \
  V(Int32TypedArrayConstructor)              \
  V(Float32TypedArrayConstructor)            \
  V(Float64TypedArrayConstructor)            \
  V(Uint8ClampedTypedArrayConstructor)       \
  V(Biguint64TypedArrayConstructor)          \
  V(Bigint64TypedArrayConstructor)

#define HEAP_OBJECT_TYPE_LIST(V)    \
  HEAP_OBJECT_ORDINARY_TYPE_LIST(V) \
  HEAP_OBJECT_TEMPLATE_TYPE_LIST(V) \
  HEAP_OBJECT_SPECIALIZED_TYPE_LIST(V)

#define ODDBALL_LIST(V)                                     \
  V(Undefined, undefined_value, UndefinedValue)             \
  V(Null, null_value, NullValue)                            \
  V(TheHole, the_hole_value, TheHoleValue)                  \
  V(Exception, exception, Exception)                        \
  V(Uninitialized, uninitialized_value, UninitializedValue) \
  V(True, true_value, TrueValue)                            \
  V(False, false_value, FalseValue)                         \
  V(ArgumentsMarker, arguments_marker, ArgumentsMarker)     \
  V(OptimizedOut, optimized_out, OptimizedOut)              \
  V(StaleRegister, stale_register, StaleRegister)

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_OBJECT_LIST_MACROS_H_
