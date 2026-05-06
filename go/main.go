package main

/*
#cgo CFLAGS: -I${SRCDIR}/../src
#include <stdlib.h>
#include <thalamus/plugin.h>
typedef struct ThalamusNodeFactory ThalamusNodeFactory;
typedef struct ThalamusAPI ThalamusAPI;
typedef struct ThalamusNode ThalamusNode;
typedef struct ThalamusAnalogNode ThalamusAnalogNode;
typedef struct ThalamusIoContext ThalamusIoContext;
typedef struct ThalamusNodeGraph ThalamusNodeGraph;
typedef struct ThalamusDoubleSpan ThalamusDoubleSpan;
typedef struct ThalamusShortSpan ThalamusShortSpan;
typedef struct ThalamusIntSpan ThalamusIntSpan;
typedef struct ThalamusULongSpan ThalamusULongSpan;
typedef struct ThalamusByteSpan ThalamusByteSpan;
typedef struct ThalamusCharSpan ThalamusCharSpan;
typedef struct ThalamusState ThalamusState;
typedef struct ThalamusStateConnection ThalamusStateConnection;

typedef struct ThalamusNode* (*create_node)(struct ThalamusNodeFactory*, struct ThalamusState*, struct ThalamusIoContext*, struct ThalamusNodeGraph*);
typedef void (*destroy_node)(struct ThalamusNodeFactory*, struct ThalamusNode*);

extern struct ThalamusNode* CreateNode(struct ThalamusNodeFactory*, struct ThalamusState*, struct ThalamusIoContext*, struct ThalamusNodeGraph*);
extern void DestroyNode(struct ThalamusNodeFactory*, struct ThalamusNode*);

typedef uint64_t (*node_time_ns)(struct ThalamusNode*);

typedef void (*analognode_data)(struct ThalamusDoubleSpan*, struct ThalamusNode* node, int channel);
typedef void (*analognode_short_data)(struct ThalamusShortSpan*, struct ThalamusNode* node, int channel);
typedef void (*analognode_int_data)(struct ThalamusIntSpan*, struct ThalamusNode* node, int channel);
typedef void (*analognode_ulong_data)(struct ThalamusULongSpan*, struct ThalamusNode* node, int channel);
typedef int (*analognode_num_channels)(struct ThalamusNode* node);
typedef uint64_t (*analognode_sample_interval_ns)(struct ThalamusNode* node, int channel);
typedef const char* (*analognode_name)(struct ThalamusNode* node, int channel);
typedef char (*analognode_has_analog_data)(struct ThalamusNode* node);
typedef char (*analognode_is_short_data)(struct ThalamusNode* node);
typedef char (*analognode_is_int_data)(struct ThalamusNode* node);
typedef char (*analognode_is_ulong_data)(struct ThalamusNode* node);
typedef char (*analognode_is_transformed)(struct ThalamusNode* node);
typedef double (*analognode_scale)(struct ThalamusNode* node, int channel);
typedef double (*analognode_offset)(struct ThalamusNode* node, int channel);
typedef void (*analognode_name_span)(struct ThalamusCharSpan*, struct ThalamusNode* node, int channel);

extern void CNodeData(struct ThalamusDoubleSpan*, struct ThalamusNode*, int);
extern int CNodeNumChannels(struct ThalamusNode*);
extern uint64_t CNodeSampleIntervalNs(struct ThalamusNode*, int);
extern char* CNodeName(struct ThalamusNode*, int);
extern void CNodeNameSpan(struct ThalamusCharSpan*, struct ThalamusNode*, int);
extern char CNodeHasAnalogData(struct ThalamusNode*);
extern char CNodeIsShortData(struct ThalamusNode*);
extern char CNodeIsIntData(struct ThalamusNode*);
extern char CNodeIsULongData(struct ThalamusNode*);
extern char CNodeIsTransformed(struct ThalamusNode*);
extern double CNodeScale(struct ThalamusNode*, int);
extern double CNodeOffset(struct ThalamusNode*, int);
extern uint64_t CNodeTimeNs(struct ThalamusNode*);

extern void StateCallback(struct ThalamusState* source, enum ThalamusStateAction action,
                          struct ThalamusState* key, struct ThalamusState* value, void* data);
extern void PostCallback(void* data);

static struct ThalamusStateConnection* call_state_recursive_change_connect(
    struct ThalamusAPI* api, struct ThalamusState* state, ThalamusStateRecursiveCallback callback, void* data) {
  return api->state_recursive_change_connect(state, callback, data);
}

static void call_state_recursive_change_disconnect(
    struct ThalamusAPI* api, struct ThalamusStateConnection* conn) {
  api->state_recursive_change_disconnect(conn);
}

static void call_node_ready(struct ThalamusAPI* api, struct ThalamusNode* node) {
  api->node_ready(node);
}

static void call_post(struct ThalamusAPI* api) {
  api->io_context_post(PostCallback, nullptr);
}

static size_t call_time_ns(struct ThalamusAPI* api) {
  return api->time_ns();
}

static char call_state_is_bool(struct ThalamusAPI* api, struct ThalamusState* state) {
	return api->state_is_bool(state);
}

static char call_state_is_dict(struct ThalamusAPI* api, struct ThalamusState* state) {
	return api->state_is_dict(state);
}

static char call_state_is_float(struct ThalamusAPI* api, struct ThalamusState* state) {
	return api->state_is_float(state);
}

static char call_state_is_int(struct ThalamusAPI* api, struct ThalamusState* state) {
	return api->state_is_int(state);
}

static char call_state_is_list(struct ThalamusAPI* api, struct ThalamusState* state) {
	return api->state_is_list(state);
}

static char call_state_is_string(struct ThalamusAPI* api, struct ThalamusState* state) {
	return api->state_is_string(state);
}

static char call_state_get_bool(struct ThalamusAPI* api, struct ThalamusState* state) {
	return api->state_get_bool(state);
}

static double call_state_get_float(struct ThalamusAPI* api, struct ThalamusState* state) {
	return api->state_get_float(state);
}

static int64_t call_state_get_int(struct ThalamusAPI* api, struct ThalamusState* state) {
	return api->state_get_int(state);
}

static const char* call_state_get_string(struct ThalamusAPI* api, struct ThalamusState* state) {
	return api->state_get_string(state);
}

static char call_state_set_at_name_bool(struct ThalamusAPI* api, struct ThalamusState* state, const char* name, char val) {
	api->state_set_at_name_bool(state, name, val);
}

static double call_state_set_at_name_float(struct ThalamusAPI* api, struct ThalamusState* state, const char* name, double val) {
	api->state_set_at_name_float(state, name, val);
}

static int64_t call_state_set_at_name_int(struct ThalamusAPI* api, struct ThalamusState* state, const char* name, int64_t val) {
	api->state_set_at_name_int(state, name, val);
}

static const char* call_state_set_at_name_string(struct ThalamusAPI* api, struct ThalamusState* state, const char* name, const char* val) {
	api->state_set_at_name_string(state, name, val);
}

static char call_state_set_at_index_bool(struct ThalamusAPI* api, struct ThalamusState* state, int64_t index, char val) {
	api->state_set_at_index_bool(state, index, val);
}

static double call_state_set_at_index_float(struct ThalamusAPI* api, struct ThalamusState* state, int64_t index, double val) {
	api->state_set_at_index_float(state, index, val);
}

static int64_t call_state_set_at_index_int(struct ThalamusAPI* api, struct ThalamusState* state, int64_t index, int64_t val) {
	api->state_set_at_index_int(state, index, val);
}

static const char* call_state_set_at_index_string(struct ThalamusAPI* api, struct ThalamusState* state, int64_t index, const char* val) {
	api->state_set_at_index_string(state, index, val);
}

static void call_state_inc_ref(struct ThalamusAPI* api, struct ThalamusState* state) {
	api->state_inc_ref(state);
}

static void call_state_dec_ref(struct ThalamusAPI* api, struct ThalamusState* state) {
	api->state_dec_ref(state);
}

*/
import "C"

import (
	"fmt"
	"math"
	"runtime"
	"time"
	"unsafe"
)

var STATE_CALLBACKS []func(State, int, State, State)
var POST_CHANNEL chan func()

//export StateCallback
func StateCallback(raw_source *C.ThalamusState, action C.enum_ThalamusStateAction,
	raw_key *C.ThalamusState, raw_value *C.ThalamusState, data unsafe.Pointer) {
	source := NewState(raw_source)
	key := NewState(raw_key)
	value := NewState(raw_value)
	index := int(uintptr(data))
	callback := STATE_CALLBACKS[index]
	callback(source, int(action), key, value)
}

//export PostCallback
func PostCallback(unsafe.Pointer) {
	callback := <-POST_CHANNEL
	callback()
}

type StateConnection struct {
	raw *C.ThalamusStateConnection
}

func (this *StateConnection) Close() {
	C.call_state_recursive_change_disconnect(api, this.raw)
}

type State struct {
	raw *C.ThalamusState
}

func NewState(raw *C.ThalamusState) State {
	C.call_state_inc_ref(api, raw)
	this := State{raw}
	runtime.SetFinalizer(&this, func(this *State) {
		fmt.Println("Finalize State")
		C.call_state_dec_ref(api, raw)
	})
	return this
}

func (this *State) Get() any {
	if C.call_state_is_bool(api, this.raw) != 0 {
		return C.call_state_get_bool(api, this.raw) != 0
	} else if C.call_state_is_dict(api, this.raw) != 0 {
		panic("State list unwrapping is unsupported")
	} else if C.call_state_is_float(api, this.raw) != 0 {
		return float64(C.call_state_get_float(api, this.raw))
	} else if C.call_state_is_int(api, this.raw) != 0 {
		return int64(C.call_state_get_int(api, this.raw))
	} else if C.call_state_is_list(api, this.raw) != 0 {
		panic("State list unwrapping is unsupported")
	} else if C.call_state_is_string(api, this.raw) != 0 {
		return C.GoString(C.call_state_get_string(api, this.raw))
	} else {
		panic("State type check failed")
	}
}

func (this *State) Set(key_any any, value_any any) {
	switch key := key_any.(type) {
	case int:
		switch v := value_any.(type) {
		case bool:
			if v {
				C.call_state_set_at_index_bool(api, this.raw, C.int64_t(key), 1)
			} else {
				C.call_state_set_at_index_bool(api, this.raw, C.int64_t(key), 0)
			}
		case float64:
			C.call_state_set_at_index_float(api, this.raw, C.int64_t(key), C.double(v))
		case int:
			C.call_state_set_at_index_int(api, this.raw, C.int64_t(key), C.int64_t(v))
		case string:
			C.call_state_set_at_index_string(api, this.raw, C.int64_t(key), C.CString(v))
		default:
			panic("Unsupported value type")
		}
	case string:
		key_cstr := C.CString(key)
		switch v := value_any.(type) {
		case bool:
			if v {
				C.call_state_set_at_name_bool(api, this.raw, key_cstr, 1)
			} else {
				C.call_state_set_at_name_bool(api, this.raw, key_cstr, 0)
			}
		case float64:
			C.call_state_set_at_name_float(api, this.raw, key_cstr, C.double(v))
		case int:
			C.call_state_set_at_name_int(api, this.raw, key_cstr, C.int64_t(v))
		case string:
			C.call_state_set_at_name_string(api, this.raw, key_cstr, C.CString(v))
		default:
			panic("Unsupported value type")
		}
	}

}

func (this *State) Connect(callback func(State, int, State, State)) StateConnection {
	index := unsafe.Pointer(uintptr(len(STATE_CALLBACKS)))
	STATE_CALLBACKS = append(STATE_CALLBACKS, callback)
	connection := C.call_state_recursive_change_connect(api, this.raw, C.ThalamusStateRecursiveCallback(C.StateCallback), index)
	result := StateConnection{connection}
	return result
}

type Node interface {
	Time() time.Duration
	Close()
}

type AnalogNode interface {
	Data(int) []float64
	NumChannels() int32
	SampleInterval(int) time.Duration
	Name(int) string
	HasAnalogData() bool
	IsShortData() bool
	IsIntData() bool
	IsULongData() bool
	IsTransformed() bool
	Scale(int) float64
	Offset(int) float64
}

const SIGNAL_NODE = 1

var NODES []Node
var api *C.ThalamusAPI

type ThalamusAPI struct {
	node *C.ThalamusNode
}

func (this *ThalamusAPI) Ready() {
	C.call_node_ready(api, this.node)
}

func (this *ThalamusAPI) Post(callback func()) {
	fmt.Println("Sending")
	POST_CHANNEL <- callback
	fmt.Println("Sent")
	C.call_post(api)
	fmt.Println("Did post")
}

type SignalNodeMessage struct {
	Amplitude *float64
	Frequency *float64
	Continue  bool
}

type SignalNode struct {
	api        ThalamusAPI
	pinner     runtime.Pinner
	time       time.Duration
	cos        []float64
	sin        []float64
	len        int
	names      [2]string
	connection *StateConnection
	frequency  float64
	amplitude  float64
	toThread   chan SignalNodeMessage
	fromThread chan SignalNodeMessage
	state      State
}

func NewSignalNode(api ThalamusAPI, state State) *SignalNode {
	fmt.Println("NewSignalNode")
	this := &SignalNode{
		api:        api,
		pinner:     runtime.Pinner{},
		cos:        make([]float64, 32),
		sin:        make([]float64, 32),
		names:      [2]string{"cos", "sin"},
		connection: nil,
		toThread:   nil,
		fromThread: nil,
		amplitude:  1,
		frequency:  1,
		state:      state,
	}
	conn := state.Connect(this.OnChange)
	this.connection = &conn

	this.pinner.Pin(&this.cos[0])
	this.pinner.Pin(&this.sin[0])
	this.pinner.Pin(unsafe.StringData(this.names[0]))
	this.pinner.Pin(unsafe.StringData(this.names[1]))

	runtime.SetFinalizer(this, func(this *SignalNode) {
		fmt.Println("Finalized")
	})
	return this
}

func (this *SignalNode) Close() {
	fmt.Println("Close stopping")
	this.state.Set("Running", false)
	if this.fromThread != nil {
		close(this.toThread)
		<-this.fromThread
	}
	fmt.Println("Close stopped")

	this.connection.Close()
	this.pinner.Unpin()
}

func (this *SignalNode) SignalGen() {
	frequency := 0.0
	amplitude := 0.0
	running := true
	interval := 16 * time.Millisecond
	start := time.Now()
	last := start
	next := last.Add(interval)

	postChannel := make(chan int)

	emit := func(len int) {
		if len == 0 {
			return
		}
		this.len = len
		this.time = time.Duration(C.call_time_ns(api))
		fmt.Println("Post")
		this.api.Post(func() {
			fmt.Println("Ready")
			this.api.Ready()
			fmt.Println("Readied")
			postChannel <- 0
		})
		fmt.Println("Waiting")
		<-postChannel
		fmt.Println("Waited")
	}

	for running {
		select {
		case msg, ok := <-this.toThread:
			if !ok {
				running = false
				break
			}

			if msg.Frequency != nil {
				frequency = *msg.Frequency
			} else if msg.Amplitude != nil {
				amplitude = *msg.Amplitude
			}

			fmt.Printf("%f %f\n", frequency, amplitude)
		case <-time.After(time.Until(next)):
			fmt.Println("Timeout")
			i := 0
			for next.After(last) {
				elapsed := last.Sub(start).Seconds()
				this.sin[i] = math.Sin(elapsed)
				this.cos[i] = math.Cos(elapsed)
				i++
				if i == len(this.sin) {
					fmt.Println("Emit")
					emit(i)
					fmt.Println("Emitted")
					i = 0
				}
				last = last.Add(time.Millisecond)
			}
			next = next.Add(interval)
			fmt.Println("Emit end")
			emit(i)
			fmt.Println("Emitted end")
		}
	}
	close(this.fromThread)
}

func (this *SignalNode) OnChange(source State, action int, key State, value State) {
	fmt.Println("OnChange")
	switch key.Get() {
	case "Running":
		if this.fromThread != nil {
			close(this.toThread)
			<-this.fromThread
			this.fromThread = nil
			this.toThread = nil
		}
		if value.Get() == true {
			this.fromThread = make(chan SignalNodeMessage)
			this.toThread = make(chan SignalNodeMessage)
			go this.SignalGen()
			frequency := this.frequency
			amplitude := this.amplitude
			this.toThread <- SignalNodeMessage{Frequency: &frequency}
			this.toThread <- SignalNodeMessage{Amplitude: &amplitude}
		}
	case "Amplitude":
		fmt.Println("Amplitude")
		val, ok := value.Get().(float64)
		if !ok {
			return
		}
		this.amplitude = val
		if this.fromThread != nil {
			this.toThread <- SignalNodeMessage{Amplitude: &val}
		}
	case "Frequency":
		fmt.Println("Frequency")
		val, ok := value.Get().(float64)
		if !ok {
			return
		}
		this.frequency = val
		if this.fromThread != nil {
			this.toThread <- SignalNodeMessage{Frequency: &val}
		}
	}
}

func (this *SignalNode) Time() time.Duration {
	return 0 * time.Nanosecond
}

func (this *SignalNode) Data(channel int) []float64 {
	if channel == 0 {
		return this.sin[:this.len]
	} else {
		return this.cos[:this.len]
	}
}
func (this *SignalNode) NumChannels() int32 {
	return 2
}
func (this *SignalNode) SampleInterval(int) time.Duration {
	return 1 * time.Millisecond
}
func (this *SignalNode) Name(channel int) string {
	return this.names[channel]
}
func (this *SignalNode) HasAnalogData() bool {
	return true
}
func (this *SignalNode) IsShortData() bool {
	return false
}
func (this *SignalNode) IsIntData() bool {
	return false
}
func (this *SignalNode) IsULongData() bool {
	return false
}
func (this *SignalNode) IsTransformed() bool {
	return false
}
func (this *SignalNode) Scale(int) float64 {
	return 0
}
func (this *SignalNode) Offset(int) float64 {
	return 0
}

type NodeFactory struct {
	api *C.ThalamusAPI
}

func GetNode[T any](c_node *C.ThalamusNode) T {
	index := int(uintptr(c_node.plugin_impl))
	raw_node := NODES[index]
	node, ok := raw_node.(T)
	if !ok {
		panic("Node does not implement AnalogNode")
	}
	return node
}

//export CNodeData
func CNodeData(output *C.ThalamusDoubleSpan, c_node *C.ThalamusNode, channel C.int) {
	node := GetNode[AnalogNode](c_node)
	data := node.Data(int(channel))
	output.data = (*C.double)(unsafe.Pointer(&data[0]))
	output.size = C.size_t(len(data))
}

//export CNodeNumChannels
func CNodeNumChannels(c_node *C.ThalamusNode) int32 {
	node := GetNode[AnalogNode](c_node)
	result := node.NumChannels()
	return result
}

//export CNodeSampleIntervalNs
func CNodeSampleIntervalNs(c_node *C.ThalamusNode, channel C.int) uint64 {
	node := GetNode[AnalogNode](c_node)
	result := node.SampleInterval(int(channel))
	return uint64(result.Nanoseconds())
}

//export CNodeName
func CNodeName(c_node *C.ThalamusNode, channel C.int) *C.char {
	return nil
}

//export CNodeNameSpan
func CNodeNameSpan(output *C.ThalamusCharSpan, c_node *C.ThalamusNode, channel C.int) {
	node := GetNode[AnalogNode](c_node)
	name := node.Name(int(channel))
	ptr := unsafe.Pointer(unsafe.StringData(name))

	output.data = (*C.char)(ptr)
	output.size = C.uint64_t(len(name))
}

//export CNodeHasAnalogData
func CNodeHasAnalogData(c_node *C.ThalamusNode) C.char {
	node := GetNode[AnalogNode](c_node)
	result := node.HasAnalogData()
	if result {
		return C.char(1)
	} else {
		return C.char(0)
	}
}

//export CNodeIsShortData
func CNodeIsShortData(c_node *C.ThalamusNode) C.char {
	node := GetNode[AnalogNode](c_node)
	result := node.IsShortData()
	if result {
		return C.char(1)
	} else {
		return C.char(0)
	}
}

//export CNodeIsIntData
func CNodeIsIntData(c_node *C.ThalamusNode) C.char {
	node := GetNode[AnalogNode](c_node)
	result := node.IsIntData()
	if result {
		return C.char(1)
	} else {
		return C.char(0)
	}
}

//export CNodeIsULongData
func CNodeIsULongData(c_node *C.ThalamusNode) C.char {
	node := GetNode[AnalogNode](c_node)
	result := node.IsULongData()
	if result {
		return C.char(1)
	} else {
		return C.char(0)
	}
}

//export CNodeIsTransformed
func CNodeIsTransformed(c_node *C.ThalamusNode) C.char {
	node := GetNode[AnalogNode](c_node)
	result := node.IsTransformed()
	if result {
		return C.char(1)
	} else {
		return C.char(0)
	}
}

//export CNodeScale
func CNodeScale(c_node *C.ThalamusNode, channel C.int) float64 {
	node := GetNode[AnalogNode](c_node)
	result := node.Scale(int(channel))
	return result
}

//export CNodeOffset
func CNodeOffset(c_node *C.ThalamusNode, channel C.int) float64 {
	node := GetNode[AnalogNode](c_node)
	result := node.Offset(int(channel))
	return result
}

//export CNodeTimeNs
func CNodeTimeNs(c_node *C.ThalamusNode) C.uint64_t {
	node := GetNode[Node](c_node)
	result := node.Time()
	return C.uint64_t(result.Nanoseconds())
}

func MakeAnalog(index int) *C.ThalamusAnalogNode {
	analog := (*C.ThalamusAnalogNode)(C.calloc(1, C.size_t(unsafe.Sizeof(C.ThalamusAnalogNode{}))))
	analog.data = C.analognode_data(C.CNodeData)
	analog.short_data = C.analognode_short_data(nil)
	analog.int_data = C.analognode_int_data(nil)
	analog.ulong_data = C.analognode_ulong_data(nil)
	analog.num_channels = C.analognode_num_channels(C.CNodeNumChannels)
	analog.sample_interval_ns = C.analognode_sample_interval_ns(C.CNodeSampleIntervalNs)
	analog.name = C.analognode_name(C.CNodeName)
	analog.name_span = C.analognode_name_span(C.CNodeNameSpan)
	analog.has_analog_data = C.analognode_has_analog_data(C.CNodeHasAnalogData)
	analog.is_short_data = C.analognode_is_short_data(C.CNodeIsShortData)
	analog.is_int_data = C.analognode_is_int_data(C.CNodeIsIntData)
	analog.is_ulong_data = C.analognode_is_ulong_data(C.CNodeIsULongData)
	analog.is_transformed = C.analognode_is_transformed(C.CNodeIsTransformed)
	analog.scale = C.analognode_scale(C.CNodeScale)
	analog.offset = C.analognode_offset(C.CNodeOffset)
	return analog
}

//export CreateNode
func CreateNode(factory *C.ThalamusNodeFactory, state *C.ThalamusState, io_context *C.ThalamusIoContext, graph *C.ThalamusNodeGraph) *C.ThalamusNode {
	fmt.Printf("CreateNode\n")
	type_id := int(uintptr(factory.plugin_impl))
	fmt.Printf("CreateNode %v\n")
	fmt.Printf("CreateNode %v\n")

	c_node := (*C.ThalamusNode)(C.calloc(1, C.size_t(unsafe.Sizeof(C.ThalamusNode{}))))
	fmt.Printf("1\n")
	c_node.time_ns = C.node_time_ns(C.CNodeTimeNs)
	fmt.Printf("2\n")

	api_wrapper := ThalamusAPI{c_node}
	state_wrapper := NewState(state)

	switch type_id {
	case SIGNAL_NODE:
		fmt.Printf("SignalNode\n")
		c_node.analog = MakeAnalog(len(NODES))
		node := NewSignalNode(api_wrapper, state_wrapper)
		NODES = append(NODES, node)
	}

	c_node.plugin_impl = unsafe.Pointer(uintptr(len(NODES) - 1))
	return c_node
}

//export DestroyNode
func DestroyNode(factory *C.ThalamusNodeFactory, c_node *C.ThalamusNode) {
	index := int(uintptr(c_node.plugin_impl))
	node := NODES[index]
	node.Close()
	NODES[index] = nil
	C.free(unsafe.Pointer(c_node))
	runtime.GC()
}

//export get_node_factories
func get_node_factories(_api *C.ThalamusAPI) **C.ThalamusNodeFactory {
	api = _api
	POST_CHANNEL = make(chan func(), 10)

	factory := (*C.ThalamusNodeFactory)(C.calloc(1, C.size_t(unsafe.Sizeof(C.ThalamusNodeFactory{}))))
	factory._type = C.CString("EXT_DEMO")
	factory.create = C.create_node(C.CreateNode)
	factory.destroy = C.destroy_node(C.DestroyNode)
	factory.plugin_impl = unsafe.Pointer(uintptr(SIGNAL_NODE))

	result := (**C.ThalamusNodeFactory)(C.calloc(2, C.size_t(unsafe.Sizeof((*C.ThalamusNodeFactory)(nil)))))
	slice := unsafe.Slice(result, 2)
	slice[0] = factory
	return result
}

// main is required but can be empty
func main() {
	//c := Add(1, 2)
	//fmt.Println("abc", c)
}
