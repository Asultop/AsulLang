#include "StdCollections.h"
#include "../../../AsulInterpreter.h"
#include <algorithm>
#include <queue>
#include <deque>

namespace asul {


void registerStdCollectionsPackage(Interpreter& interp) {
	auto globals = interp.globalsEnv();
	// Get interpreter pointer for lambdas that need it
	Interpreter* interpPtr = &interp;
	
	// Collections are registered in a block scope to organize the code
	{
					auto mapClass = std::make_shared<ClassInfo>(); mapClass->name = "Map"; mapClass->isNative = true;

					// helpers to extract InstanceExt from closure
					auto getThisInstanceExt = [](std::shared_ptr<Environment> clos)->InstanceExt* {
						if (!clos) throw std::runtime_error("internal: instance method called without closure");
						Value tv = clos->get("this");
						if (!std::holds_alternative<std::shared_ptr<Instance>>(tv)) throw std::runtime_error("internal: invalid 'this' value");
						auto pins = std::get<std::shared_ptr<Instance>>(tv);
						if (!pins) throw std::runtime_error("internal: null 'this'");
						return static_cast<InstanceExt*>(pins.get());
					};

					// constructor method for Map class (enables "new Map()")
					auto mapConstructor = std::make_shared<Function>();
					mapConstructor->isBuiltin = true;
					mapConstructor->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value {
						Value thisVal = clos->get("this");
						if (!std::holds_alternative<std::shared_ptr<Instance>>(thisVal)) 
							throw std::runtime_error("Map.constructor: 'this' is not an instance");
						auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						auto instExt = static_cast<InstanceExt*>(inst.get());
						// Allocate native map handle
						auto nm = new NativeMap();
						instExt->nativeHandle = nm;
						instExt->nativeDestructor = [](void* p){ delete static_cast<NativeMap*>(p); };
						return Value{std::monostate{}};
					};
					mapClass->methods["constructor"] = mapConstructor;


					// map.set(key, value)
					auto setFn = std::make_shared<Function>(); setFn->isBuiltin = true;
					setFn->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
						if (args.size() != 2) throw std::runtime_error("map.set expects 2 arguments");
						InstanceExt* ie = getThisInstanceExt(clos);
						auto nm = static_cast<NativeMap*>(ie->nativeHandle);
						if (!nm) throw std::runtime_error("map: native handle missing");
						auto it = nm->m.find(args[0]);
						if (it == nm->m.end()) {
							nm->order.push_back(args[0]);
							nm->index[args[0]] = nm->order.size() - 1;
						}
						nm->m[args[0]] = args[1];
						return Value{std::monostate{}};
					};
					mapClass->methods["set"] = setFn;

					// map.get(key)
					auto getFn = std::make_shared<Function>(); getFn->isBuiltin = true;
					getFn->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
						if (args.size() != 1) throw std::runtime_error("map.get expects 1 argument");
						InstanceExt* ie = getThisInstanceExt(clos);
						auto nm = static_cast<NativeMap*>(ie->nativeHandle);
						auto it = nm->m.find(args[0]);
						if (it == nm->m.end()) return Value{std::monostate{}};
						return it->second;
					};
					mapClass->methods["get"] = getFn;

					// map.has(key)
					auto hasFn = std::make_shared<Function>(); hasFn->isBuiltin = true;
					hasFn->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
						if (args.size() != 1) throw std::runtime_error("map.has expects 1 argument");
						InstanceExt* ie = getThisInstanceExt(clos);
						auto nm = static_cast<NativeMap*>(ie->nativeHandle);
						return Value{ nm->m.find(args[0]) != nm->m.end() };
					};
					mapClass->methods["has"] = hasFn;

					// map.delete(key)
					auto delFn = std::make_shared<Function>(); delFn->isBuiltin = true;
					delFn->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
						if (args.size() != 1) throw std::runtime_error("map.delete expects 1 argument");
						InstanceExt* ie = getThisInstanceExt(clos);
						auto nm = static_cast<NativeMap*>(ie->nativeHandle);
						auto it = nm->m.find(args[0]);
						if (it == nm->m.end()) return Value{false};
						nm->m.erase(it);
						// O(1) removal from order via swap-with-back using index map
						auto idxIt = nm->index.find(args[0]);
						if (idxIt != nm->index.end()) {
							size_t pos = idxIt->second;
							size_t last = nm->order.size() - 1;
							if (pos != last) {
								Value swapped = nm->order[last];
								nm->order[pos] = swapped;
								nm->index[swapped] = pos;
							}
							nm->order.pop_back();
							nm->index.erase(idxIt);
						}
						return Value{true};
					};
					mapClass->methods["delete"] = delFn;

					// map.size()
					auto sizeFn = std::make_shared<Function>(); sizeFn->isBuiltin = true; sizeFn->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nm = static_cast<NativeMap*>(ie->nativeHandle); return Value{ static_cast<double>(nm->m.size()) }; };
					mapClass->methods["size"] = sizeFn;

					// map.clear()
					auto clearFn = std::make_shared<Function>(); clearFn->isBuiltin = true; clearFn->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nm = static_cast<NativeMap*>(ie->nativeHandle); nm->m.clear(); nm->order.clear(); nm->index.clear(); return Value{std::monostate{}}; };
					mapClass->methods["clear"] = clearFn;

					// map.keys()
					auto keysFn = std::make_shared<Function>(); keysFn->isBuiltin = true; keysFn->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nm = static_cast<NativeMap*>(ie->nativeHandle); auto out = std::make_shared<Array>(); for (auto &k : nm->order) out->push_back(k); return Value{out}; };
					mapClass->methods["keys"] = keysFn;

					// map.values()
					auto valuesFn = std::make_shared<Function>(); valuesFn->isBuiltin = true; valuesFn->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nm = static_cast<NativeMap*>(ie->nativeHandle); auto out = std::make_shared<Array>(); for (auto &k : nm->order) out->push_back(nm->m[k]); return Value{out}; };
					mapClass->methods["values"] = valuesFn;

					// map.entries()
					auto entriesFn = std::make_shared<Function>(); entriesFn->isBuiltin = true; entriesFn->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nm = static_cast<NativeMap*>(ie->nativeHandle); auto out = std::make_shared<Array>(); for (auto &k : nm->order){ auto pair = std::make_shared<Array>(); pair->push_back(k); pair->push_back(nm->m[k]); out->push_back(Value{pair}); } return Value{out}; };
					mapClass->methods["entries"] = entriesFn;

					// Register class in globals
					globals->define("Map", mapClass);

					// Constructor function: new Map() -> Instance with native handle
					auto mapCtor = std::make_shared<Function>(); mapCtor->isBuiltin = true;
					mapCtor->builtin = [mapClass](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
						auto inst = std::make_shared<InstanceExt>();
						inst->klass = mapClass;
						// allocate native map
						auto nm = new NativeMap();
						inst->nativeHandle = nm;
						inst->nativeDestructor = [](void* p){ delete static_cast<NativeMap*>(p); };
						return Value{inst};
					};
					globals->define("map", mapCtor);

					// ---- Set (native) ----
					auto setClass = std::make_shared<ClassInfo>(); setClass->name = "Set"; setClass->isNative = true;
					// constructor method for Set class
					auto setConstructor = std::make_shared<Function>();
					setConstructor->isBuiltin = true;
					setConstructor->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value {
						Value thisVal = clos->get("this");
						auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						auto instExt = static_cast<InstanceExt*>(inst.get());
						auto ns = new NativeSet();
						instExt->nativeHandle = ns;
						instExt->nativeDestructor = [](void* p){ delete static_cast<NativeSet*>(p); };
						return Value{std::monostate{}};
					};
					setClass->methods["constructor"] = setConstructor;
					auto setAdd = std::make_shared<Function>(); setAdd->isBuiltin = true;
					setAdd->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
						if (args.size()!=1) throw std::runtime_error("set.add expects 1 argument");
						InstanceExt* ie = getThisInstanceExt(clos);
						auto ns = static_cast<NativeSet*>(ie->nativeHandle);
						if (ns->s.find(args[0]) == ns->s.end()) { ns->s.insert(args[0]); ns->order.push_back(args[0]); ns->index[args[0]] = ns->order.size() - 1; }
						return Value{std::monostate{}};
					};
					setClass->methods["add"] = setAdd;
					auto setHas = std::make_shared<Function>(); setHas->isBuiltin = true; setHas->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value { if (args.size()!=1) throw std::runtime_error("set.has expects 1 arg"); InstanceExt* ie = getThisInstanceExt(clos); auto ns = static_cast<NativeSet*>(ie->nativeHandle); return Value{ ns->s.find(args[0]) != ns->s.end() }; };
					setClass->methods["has"] = setHas;
					auto setDelete = std::make_shared<Function>(); setDelete->isBuiltin = true; setDelete->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
						if (args.size()!=1) throw std::runtime_error("set.delete expects 1 arg");
						InstanceExt* ie = getThisInstanceExt(clos);
						auto ns = static_cast<NativeSet*>(ie->nativeHandle);
						auto it = ns->s.find(args[0]); if (it == ns->s.end()) return Value{false}; ns->s.erase(it);
						auto idxIt = ns->index.find(args[0]);
						if (idxIt != ns->index.end()) {
							size_t pos = idxIt->second;
							size_t last = ns->order.size() - 1;
							if (pos != last) {
								Value swapped = ns->order[last];
								ns->order[pos] = swapped;
								ns->index[swapped] = pos;
							}
							ns->order.pop_back();
							ns->index.erase(idxIt);
						}
						return Value{true};
					};
					setClass->methods["delete"] = setDelete;
					auto setSize = std::make_shared<Function>(); setSize->isBuiltin = true; setSize->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto ns = static_cast<NativeSet*>(ie->nativeHandle); return Value{ static_cast<double>(ns->s.size()) }; };
					setClass->methods["size"] = setSize;
					auto setValues = std::make_shared<Function>(); setValues->isBuiltin = true; setValues->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto ns = static_cast<NativeSet*>(ie->nativeHandle); auto out = std::make_shared<Array>(); for (auto &v : ns->order) out->push_back(v); return Value{out}; };
					setClass->methods["values"] = setValues;
					// Set union(other): returns new Set containing all unique elements
					auto setUnion = std::make_shared<Function>(); setUnion->isBuiltin = true; setUnion->builtin = [getThisInstanceExt,setClass](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
						if (args.size()!=1) throw std::runtime_error("set.union expects 1 Set argument");
						InstanceExt* ie = getThisInstanceExt(clos);
						auto ns = static_cast<NativeSet*>(ie->nativeHandle);
						if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0])) throw std::runtime_error("set.union expects Set instance");
						auto inst2 = std::get<std::shared_ptr<Instance>>(args[0]);
						if (inst2->klass != setClass) throw std::runtime_error("set.union expects Set instance");
						auto ie2 = static_cast<InstanceExt*>(inst2.get());
						auto ns2 = static_cast<NativeSet*>(ie2->nativeHandle);
						// create new Set instance
						auto newInst = std::make_shared<InstanceExt>(); newInst->klass = setClass; auto newNative = new NativeSet(); newInst->nativeHandle = newNative; newInst->nativeDestructor = [](void* p){ delete static_cast<NativeSet*>(p); };
						for (auto &v : ns->order) { if (newNative->s.insert(v).second) { newNative->order.push_back(v); newNative->index[v] = newNative->order.size()-1; } }
						for (auto &v : ns2->order) { if (newNative->s.insert(v).second) { newNative->order.push_back(v); newNative->index[v] = newNative->order.size()-1; } }
						return Value{newInst};
					};
					setClass->methods["union"] = setUnion;
					// Set intersection(other): returns new Set containing elements in both sets
					auto setIntersection = std::make_shared<Function>(); setIntersection->isBuiltin = true; setIntersection->builtin = [getThisInstanceExt,setClass](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
						if (args.size()!=1) throw std::runtime_error("set.intersection expects 1 Set argument");
						InstanceExt* ie = getThisInstanceExt(clos);
						auto ns = static_cast<NativeSet*>(ie->nativeHandle);
						if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0])) throw std::runtime_error("set.intersection expects Set instance");
						auto inst2 = std::get<std::shared_ptr<Instance>>(args[0]);
						if (inst2->klass != setClass) throw std::runtime_error("set.intersection expects Set instance");
						auto ie2 = static_cast<InstanceExt*>(inst2.get());
						auto ns2 = static_cast<NativeSet*>(ie2->nativeHandle);
						auto newInst = std::make_shared<InstanceExt>(); newInst->klass = setClass; auto newNative = new NativeSet(); newInst->nativeHandle = newNative; newInst->nativeDestructor = [](void* p){ delete static_cast<NativeSet*>(p); };
						for (auto &v : ns->order) { if (ns2->s.find(v)!=ns2->s.end()) { newNative->s.insert(v); newNative->order.push_back(v); newNative->index[v] = newNative->order.size()-1; } }
						return Value{newInst};
					};
					setClass->methods["intersection"] = setIntersection;
					// Set difference(other): returns elements in interpPtr set not in other
					auto setDifference = std::make_shared<Function>(); setDifference->isBuiltin = true; setDifference->builtin = [getThisInstanceExt,setClass](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
						if (args.size()!=1) throw std::runtime_error("set.difference expects 1 Set argument");
						InstanceExt* ie = getThisInstanceExt(clos);
						auto ns = static_cast<NativeSet*>(ie->nativeHandle);
						if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0])) throw std::runtime_error("set.difference expects Set instance");
						auto inst2 = std::get<std::shared_ptr<Instance>>(args[0]);
						if (inst2->klass != setClass) throw std::runtime_error("set.difference expects Set instance");
						auto ie2 = static_cast<InstanceExt*>(inst2.get());
						auto ns2 = static_cast<NativeSet*>(ie2->nativeHandle);
						auto newInst = std::make_shared<InstanceExt>(); newInst->klass = setClass; auto newNative = new NativeSet(); newInst->nativeHandle = newNative; newInst->nativeDestructor = [](void* p){ delete static_cast<NativeSet*>(p); };
						for (auto &v : ns->order) { if (ns2->s.find(v)==ns2->s.end()) { newNative->s.insert(v); newNative->order.push_back(v); newNative->index[v] = newNative->order.size()-1; } }
						return Value{newInst};
					};
					setClass->methods["difference"] = setDifference;
					globals->define("Set", setClass);
					auto setCtor = std::make_shared<Function>(); setCtor->isBuiltin = true; setCtor->builtin = [setClass](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { auto inst = std::make_shared<InstanceExt>(); inst->klass = setClass; auto ns = new NativeSet(); inst->nativeHandle = ns; inst->nativeDestructor = [](void* p){ delete static_cast<NativeSet*>(p); }; return Value{inst}; };
					globals->define("set", setCtor);
					// Package namespace bindings for std.collections
					interp.registerPackageSymbol("std.collections", "Set", Value{setClass});
					interp.registerPackageSymbol("std.collections", "set", Value{setCtor});

					// ---- Deque (native) ----
					struct NativeDeque { std::deque<Value> d; };
					auto dequeClass = std::make_shared<ClassInfo>(); dequeClass->name = "Deque"; dequeClass->isNative = true;
					// constructor method for Deque class
					auto dequeConstructor = std::make_shared<Function>();
					dequeConstructor->isBuiltin = true;
					dequeConstructor->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value {
						Value thisVal = clos->get("this");
						auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						auto instExt = static_cast<InstanceExt*>(inst.get());
						auto nd = new NativeDeque();
						instExt->nativeHandle = nd;
						instExt->nativeDestructor = [](void* p){ delete static_cast<NativeDeque*>(p); };
						return Value{std::monostate{}};
					};
					dequeClass->methods["constructor"] = dequeConstructor;
					auto dpush = std::make_shared<Function>(); dpush->isBuiltin = true; dpush->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nd = static_cast<NativeDeque*>(ie->nativeHandle); for (auto &v: args) nd->d.push_back(v); return Value{ static_cast<double>(nd->d.size()) }; };
					dequeClass->methods["push"] = dpush;
					auto dpop = std::make_shared<Function>(); dpop->isBuiltin = true; dpop->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nd = static_cast<NativeDeque*>(ie->nativeHandle); if (nd->d.empty()) return Value{std::monostate{}}; Value v = nd->d.back(); nd->d.pop_back(); return v; };
					dequeClass->methods["pop"] = dpop;
					auto dunshift = std::make_shared<Function>(); dunshift->isBuiltin = true; dunshift->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nd = static_cast<NativeDeque*>(ie->nativeHandle); for (auto it = args.rbegin(); it != args.rend(); ++it) nd->d.push_front(*it); return Value{ static_cast<double>(nd->d.size()) }; };
					dequeClass->methods["unshift"] = dunshift;
					auto dshift = std::make_shared<Function>(); dshift->isBuiltin = true; dshift->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nd = static_cast<NativeDeque*>(ie->nativeHandle); if (nd->d.empty()) return Value{std::monostate{}}; Value v = nd->d.front(); nd->d.pop_front(); return v; };
					dequeClass->methods["shift"] = dshift;
					auto dpeek = std::make_shared<Function>(); dpeek->isBuiltin = true; dpeek->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nd = static_cast<NativeDeque*>(ie->nativeHandle); if (nd->d.empty()) return Value{std::monostate{}}; if (args.size()==0) return nd->d.front(); return nd->d.back(); };
					dequeClass->methods["peek"] = dpeek;
					auto dsize = std::make_shared<Function>(); dsize->isBuiltin = true; dsize->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nd = static_cast<NativeDeque*>(ie->nativeHandle); return Value{ static_cast<double>(nd->d.size()) }; };
					dequeClass->methods["size"] = dsize;
					auto dclear = std::make_shared<Function>(); dclear->isBuiltin = true; dclear->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nd = static_cast<NativeDeque*>(ie->nativeHandle); nd->d.clear(); return Value{std::monostate{}}; };
					dequeClass->methods["clear"] = dclear;
					globals->define("Deque", dequeClass);
					auto dequeCtor = std::make_shared<Function>(); dequeCtor->isBuiltin = true; dequeCtor->builtin = [dequeClass](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { auto inst = std::make_shared<InstanceExt>(); inst->klass = dequeClass; auto nd = new NativeDeque(); inst->nativeHandle = nd; inst->nativeDestructor = [](void* p){ delete static_cast<NativeDeque*>(p); }; return Value{inst}; };
					globals->define("deque", dequeCtor);

					// ---- Stack (native) ----
					struct NativeStack { std::vector<Value> v; };
					auto stackClass = std::make_shared<ClassInfo>(); stackClass->name = "Stack"; stackClass->isNative = true;
					// constructor method for Stack class
					auto stackConstructor = std::make_shared<Function>();
					stackConstructor->isBuiltin = true;
					stackConstructor->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value {
						Value thisVal = clos->get("this");
						auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						auto instExt = static_cast<InstanceExt*>(inst.get());
						auto ns = new NativeStack();
						instExt->nativeHandle = ns;
						instExt->nativeDestructor = [](void* p){ delete static_cast<NativeStack*>(p); };
						return Value{std::monostate{}};
					};
					stackClass->methods["constructor"] = stackConstructor;
					auto spush = std::make_shared<Function>(); spush->isBuiltin = true; spush->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto ns = static_cast<NativeStack*>(ie->nativeHandle); for (auto &x: args) ns->v.push_back(x); return Value{ static_cast<double>(ns->v.size()) }; };
					stackClass->methods["push"] = spush;
					auto spop = std::make_shared<Function>(); spop->isBuiltin = true; spop->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto ns = static_cast<NativeStack*>(ie->nativeHandle); if (ns->v.empty()) return Value{std::monostate{}}; Value v = ns->v.back(); ns->v.pop_back(); return v; };
					stackClass->methods["pop"] = spop;
					auto speek = std::make_shared<Function>(); speek->isBuiltin = true; speek->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto ns = static_cast<NativeStack*>(ie->nativeHandle); if (ns->v.empty()) return Value{std::monostate{}}; return ns->v.back(); };
					stackClass->methods["peek"] = speek;
					auto ssize = std::make_shared<Function>(); ssize->isBuiltin = true; ssize->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto ns = static_cast<NativeStack*>(ie->nativeHandle); return Value{ static_cast<double>(ns->v.size()) }; };
					stackClass->methods["size"] = ssize;
					globals->define("Stack", stackClass);
					auto stackCtor = std::make_shared<Function>(); stackCtor->isBuiltin = true; stackCtor->builtin = [stackClass](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { auto inst = std::make_shared<InstanceExt>(); inst->klass = stackClass; auto ns = new NativeStack(); inst->nativeHandle = ns; inst->nativeDestructor = [](void* p){ delete static_cast<NativeStack*>(p); }; return Value{inst}; };
					globals->define("stack", stackCtor);
					interp.registerPackageSymbol("std.collections", "Stack", Value{stackClass});
					interp.registerPackageSymbol("std.collections", "stack", Value{stackCtor});

					// ---- PriorityQueue (native) ----
					struct NativePriorityQueue { struct Node { double priority; Value value; }; std::vector<Node> heap; };
					auto pqClass = std::make_shared<ClassInfo>(); pqClass->name = "PriorityQueue"; pqClass->isNative = true;
					// constructor method for PriorityQueue class
					auto pqConstructor = std::make_shared<Function>();
					pqConstructor->isBuiltin = true;
					pqConstructor->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value {
						Value thisVal = clos->get("this");
						auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						auto instExt = static_cast<InstanceExt*>(inst.get());
						auto npq = new NativePriorityQueue();
						instExt->nativeHandle = npq;
						instExt->nativeDestructor = [](void* p){ delete static_cast<NativePriorityQueue*>(p); };
						return Value{std::monostate{}};
					};
					pqClass->methods["constructor"] = pqConstructor;
					auto pqPush = std::make_shared<Function>(); pqPush->isBuiltin = true; pqPush->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
						if (args.size()!=2) throw std::runtime_error("priorityQueue.push expects value, priority");
						InstanceExt* ie = getThisInstanceExt(clos);
						auto npq = static_cast<NativePriorityQueue*>(ie->nativeHandle);
						double pr = 0.0; if (std::holds_alternative<double>(args[1])) pr = std::get<double>(args[1]); else throw std::runtime_error("priority must be number");
						npq->heap.push_back(NativePriorityQueue::Node{pr, args[0]});
						std::push_heap(npq->heap.begin(), npq->heap.end(), [](const NativePriorityQueue::Node& a, const NativePriorityQueue::Node& b){ return a.priority < b.priority; });
						return Value{ static_cast<double>(npq->heap.size()) };
					};
					pqClass->methods["push"] = pqPush;
					auto pqPop = std::make_shared<Function>(); pqPop->isBuiltin = true; pqPop->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value {
						InstanceExt* ie = getThisInstanceExt(clos);
						auto npq = static_cast<NativePriorityQueue*>(ie->nativeHandle);
						if (npq->heap.empty()) return Value{std::monostate{}};
						std::pop_heap(npq->heap.begin(), npq->heap.end(), [](const NativePriorityQueue::Node& a, const NativePriorityQueue::Node& b){ return a.priority < b.priority; });
						auto node = npq->heap.back(); npq->heap.pop_back(); return node.value;
					};
					pqClass->methods["pop"] = pqPop;
					auto pqPeek = std::make_shared<Function>(); pqPeek->isBuiltin = true; pqPeek->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value {
						InstanceExt* ie = getThisInstanceExt(clos);
						auto npq = static_cast<NativePriorityQueue*>(ie->nativeHandle);
						if (npq->heap.empty()) return Value{std::monostate{}}; return npq->heap.front().value;
					};
					pqClass->methods["peek"] = pqPeek;
					auto pqSize = std::make_shared<Function>(); pqSize->isBuiltin = true; pqSize->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto npq = static_cast<NativePriorityQueue*>(ie->nativeHandle); return Value{ static_cast<double>(npq->heap.size()) }; };
					pqClass->methods["size"] = pqSize;
					globals->define("PriorityQueue", pqClass);
					auto pqCtor = std::make_shared<Function>(); pqCtor->isBuiltin = true; pqCtor->builtin = [pqClass](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { auto inst = std::make_shared<InstanceExt>(); inst->klass = pqClass; auto npq = new NativePriorityQueue(); inst->nativeHandle = npq; inst->nativeDestructor = [](void* p){ delete static_cast<NativePriorityQueue*>(p); }; return Value{inst}; };
					globals->define("priorityQueue", pqCtor);
					interp.registerPackageSymbol("std.collections", "PriorityQueue", Value{pqClass});
					interp.registerPackageSymbol("std.collections", "priorityQueue", Value{pqCtor});

					// ---- binarySearch (function) ----
					auto binarySearchFn = std::make_shared<Function>(); binarySearchFn->isBuiltin = true; binarySearchFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
						if (args.size()!=2) throw std::runtime_error("binarySearch expects (array, target)");
						if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) throw std::runtime_error("binarySearch first arg must be array");
						auto arr = std::get<std::shared_ptr<Array>>(args[0]);
						Value target = args[1];
						// Support number or string search only
						bool targetIsNumber = std::holds_alternative<double>(target);
						bool targetIsString = std::holds_alternative<std::string>(target);
						if (!targetIsNumber && !targetIsString) throw std::runtime_error("binarySearch target must be number or string");
						int left = 0; int right = static_cast<int>(arr->size()) - 1;
						while (left <= right) {
							int mid = left + (right - left)/2;
							Value v = (*arr)[mid];
							if (targetIsNumber) {
								if (!std::holds_alternative<double>(v)) throw std::runtime_error("binarySearch array must be homogeneous numbers");
								double tv = std::get<double>(target); double mv = std::get<double>(v);
								if (mv == tv) return Value{ static_cast<double>(mid) };
								if (mv < tv) left = mid + 1; else right = mid - 1;
							} else {
								if (!std::holds_alternative<std::string>(v)) throw std::runtime_error("binarySearch array must be homogeneous strings");
								const std::string& tv = std::get<std::string>(target); const std::string& mv = std::get<std::string>(v);
								if (mv == tv) return Value{ static_cast<double>(mid) };
								if (mv < tv) left = mid + 1; else right = mid - 1;
							}
						}
						return Value{ -1.0 };
					};
					interp.registerPackageSymbol("std.collections", "binarySearch", Value{binarySearchFn});
				}

				// ---- Utility helper functions ----
				{
					// keys(obj)
					auto keysFn = std::make_shared<Function>(); keysFn->isBuiltin = true;
					keysFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ if (args.size()!=1) throw std::runtime_error("keys expects 1 object argument"); if (!std::holds_alternative<std::shared_ptr<Object>>(args[0])) throw std::runtime_error("keys expects an object"); auto po = std::get<std::shared_ptr<Object>>(args[0]); auto arr = std::make_shared<Array>(); for(auto &kv:*po) arr->push_back(Value{kv.first}); return Value{arr}; };
					globals->define("keys", keysFn);

					// values(obj)
					auto valuesFn = std::make_shared<Function>(); valuesFn->isBuiltin = true;
					valuesFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ if (args.size()!=1) throw std::runtime_error("values expects 1 object argument"); if (!std::holds_alternative<std::shared_ptr<Object>>(args[0])) throw std::runtime_error("values expects an object"); auto po = std::get<std::shared_ptr<Object>>(args[0]); auto arr = std::make_shared<Array>(); for(auto &kv:*po) arr->push_back(kv.second); return Value{arr}; };
					globals->define("values", valuesFn);

					// entries(obj)
					auto entriesFn = std::make_shared<Function>(); entriesFn->isBuiltin = true;
					entriesFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ if (args.size()!=1) throw std::runtime_error("entries expects 1 object argument"); if (!std::holds_alternative<std::shared_ptr<Object>>(args[0])) throw std::runtime_error("entries expects an object"); auto po = std::get<std::shared_ptr<Object>>(args[0]); auto arr = std::make_shared<Array>(); for(auto &kv:*po){ auto p = std::make_shared<Array>(); p->push_back(Value{kv.first}); p->push_back(kv.second); arr->push_back(Value{p}); } return Value{arr}; };
					globals->define("entries", entriesFn);

					// fromEntries(entries): convert array of [key, value] pairs to object
					auto fromEntriesFn = std::make_shared<Function>(); fromEntriesFn->isBuiltin = true;
					fromEntriesFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ 
						if(args.size()!=1) throw std::runtime_error("fromEntries expects 1 argument"); 
						if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) throw std::runtime_error("fromEntries expects an array"); 
						auto arr = std::get<std::shared_ptr<Array>>(args[0]); 
						auto obj = std::make_shared<Object>(); 
						for(auto &entry : *arr) { 
							if (!std::holds_alternative<std::shared_ptr<Array>>(entry)) throw std::runtime_error("fromEntries: each entry must be an array"); 
							auto pair = std::get<std::shared_ptr<Array>>(entry); 
							if (pair->size() < 2) throw std::runtime_error("fromEntries: each entry must have at least 2 elements"); 
							std::string key = toString((*pair)[0]); 
							(*obj)[key] = (*pair)[1]; 
						} 
						return Value{obj}; 
					};
					globals->define("fromEntries", fromEntriesFn);

					// clone(obj): shallow clone object or array
					auto cloneFn = std::make_shared<Function>(); cloneFn->isBuiltin = true;
					cloneFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ if(args.size()!=1) throw std::runtime_error("clone expects 1 argument"); if (std::holds_alternative<std::shared_ptr<Object>>(args[0])){ auto src = std::get<std::shared_ptr<Object>>(args[0]); auto dst = std::make_shared<Object>(); for(auto &kv:*src) (*dst)[kv.first]=kv.second; return Value{dst}; } if (std::holds_alternative<std::shared_ptr<Array>>(args[0])){ auto src = std::get<std::shared_ptr<Array>>(args[0]); auto dst = std::make_shared<Array>(*src); return Value{dst}; } throw std::runtime_error("clone expects object or array"); };
					globals->define("clone", cloneFn);

					// deepClone(obj): deep clone object or array
					auto deepCloneFn = std::make_shared<Function>(); deepCloneFn->isBuiltin = true;
					deepCloneFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
						if(args.size()!=1) throw std::runtime_error("deepClone expects 1 argument");
						
						std::function<Value(const Value&)> cloneRecursive;
						cloneRecursive = [&cloneRecursive](const Value& val) -> Value {
							if (std::holds_alternative<std::shared_ptr<Object>>(val)) {
								auto src = std::get<std::shared_ptr<Object>>(val);
								auto dst = std::make_shared<Object>();
								for(auto &kv : *src) {
									(*dst)[kv.first] = cloneRecursive(kv.second);
								}
								return Value{dst};
							}
							if (std::holds_alternative<std::shared_ptr<Array>>(val)) {
								auto src = std::get<std::shared_ptr<Array>>(val);
								auto dst = std::make_shared<Array>();
								for(auto &item : *src) {
									dst->push_back(cloneRecursive(item));
								}
								return Value{dst};
							}
							// For primitives and other types, return as-is
							return val;
						};
						
						return cloneRecursive(args[0]);
					};
					globals->define("deepClone", deepCloneFn);

					// merge(a,b): shallow merge objects into new object
					auto mergeFn = std::make_shared<Function>(); mergeFn->isBuiltin = true;
					mergeFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ if(args.size()!=2) throw std::runtime_error("merge expects 2 object arguments"); if(!std::holds_alternative<std::shared_ptr<Object>>(args[0])||!std::holds_alternative<std::shared_ptr<Object>>(args[1])) throw std::runtime_error("merge expects objects"); auto a=std::get<std::shared_ptr<Object>>(args[0]); auto b=std::get<std::shared_ptr<Object>>(args[1]); auto dst=std::make_shared<Object>(); for(auto &kv:*a) (*dst)[kv.first]=kv.second; for(auto &kv:*b) (*dst)[kv.first]=kv.second; return Value{dst}; };
					globals->define("merge", mergeFn);

					// range(n) -> array [0..n-1]
					auto rangeFn = std::make_shared<Function>(); rangeFn->isBuiltin = true;
					rangeFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ if(args.size()!=1) throw std::runtime_error("range expects 1 numeric argument"); double n = 0; if (std::holds_alternative<double>(args[0])) n = std::get<double>(args[0]); else throw std::runtime_error("range expects a number"); auto arr = std::make_shared<Array>(); for(int i=0;i<static_cast<int>(n);++i) arr->push_back(Value{ static_cast<double>(i) }); return Value{arr}; };
					globals->define("range", rangeFn);

					// enumerate(iterable) -> array of [index/key, value]
					auto enumFn = std::make_shared<Function>(); enumFn->isBuiltin = true;
					enumFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ if(args.size()!=1) throw std::runtime_error("enumerate expects 1 iterable"); if(std::holds_alternative<std::shared_ptr<Array>>(args[0])){ auto a=std::get<std::shared_ptr<Array>>(args[0]); auto out=std::make_shared<Array>(); for(size_t i=0;i<a->size();++i){ auto pair=std::make_shared<Array>(); pair->push_back(Value{static_cast<double>(static_cast<int>(i))}); pair->push_back((*a)[i]); out->push_back(Value{pair}); } return Value{out}; } if(std::holds_alternative<std::shared_ptr<Object>>(args[0])){ auto o=std::get<std::shared_ptr<Object>>(args[0]); auto out=std::make_shared<Array>(); for(auto &kv:*o){ auto pair=std::make_shared<Array>(); pair->push_back(Value{kv.first}); pair->push_back(kv.second); out->push_back(Value{pair}); } return Value{out}; } throw std::runtime_error("enumerate expects array or object"); };
					globals->define("enumerate", enumFn);

					// keysSorted(container [, comparator]) -> array of keys sorted by default rules or comparator
					auto keysSortedFn = std::make_shared<Function>(); keysSortedFn->isBuiltin = true;
					keysSortedFn->builtin = [interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment> env)->Value{
						if (args.size() < 1 || args.size() > 2) throw std::runtime_error("keysSorted expects 1 or 2 arguments");
						// extract keys depending on container shape
						auto keysArr = std::make_shared<Array>();
						if (std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
							auto a = std::get<std::shared_ptr<Array>>(args[0]);
							for (size_t i=0;i<a->size();++i) keysArr->push_back(Value{ static_cast<double>(static_cast<int>(i)) });
						} else if (std::holds_alternative<std::shared_ptr<Object>>(args[0])) {
							auto o = std::get<std::shared_ptr<Object>>(args[0]);
							// detect map-like backed by __data
							auto it = o->find("__data");
							if (it != o->end() && std::holds_alternative<std::shared_ptr<Array>>(it->second)) {
								auto data = std::get<std::shared_ptr<Array>>(it->second);
								for (auto &v : *data) {
									if (!std::holds_alternative<std::shared_ptr<Array>>(v)) continue;
									auto pair = std::get<std::shared_ptr<Array>>(v);
									if (pair->size()>=1) keysArr->push_back((*pair)[0]);
								}
							} else {
								for (auto &kv : *o) keysArr->push_back(Value{kv.first});
							}
						} else if (std::holds_alternative<std::shared_ptr<Instance>>(args[0])) {
							auto inst = std::get<std::shared_ptr<Instance>>(args[0]);
							if (!inst) throw std::runtime_error("keysSorted: null instance");
							if (inst->klass && inst->klass->name == "Map") {
								// InstanceExt expected
								auto ie = static_cast<InstanceExt*>(inst.get());
								auto nm = static_cast<NativeMap*>(ie->nativeHandle);
								for (auto &k : nm->order) keysArr->push_back(k);
							} else if (inst->klass && inst->klass->name == "Set") {
								auto ie = static_cast<InstanceExt*>(inst.get());
								auto ns = static_cast<NativeSet*>(ie->nativeHandle);
								for (auto &k : ns->order) keysArr->push_back(k);
							} else {
								// fallback: try to enumerate instance fields
								for (auto &kv : inst->fields) keysArr->push_back(Value{kv.first});
							}
						} else {
							throw std::runtime_error("keysSorted expects an array or object/map-like value");
						}

						// comparator handling
						std::shared_ptr<Function> cmpFn = nullptr;
						bool useComparator = false;
						if (args.size() == 2) {
							if (!std::holds_alternative<std::shared_ptr<Function>>(args[1])) throw std::runtime_error("keysSorted comparator must be a function");
							cmpFn = std::get<std::shared_ptr<Function>>(args[1]);
							useComparator = true;
						}

						// prepare index array for stable sort
						std::vector<size_t> idx(keysArr->size()); for (size_t i=0;i<idx.size();++i) idx[i]=i;

						auto typeOrder = [](const Value& v)->int{
							switch (v.index()) {
								case 1: return 0; // number
								case 2: return 1; // string
								case 3: return 2; // bool
								case 0: return 3; // null
								default: return 4; // complex types
							}
						};

						auto compareDefault = [&](const Value& A, const Value& B)->int{
							int ta = typeOrder(A); int tb = typeOrder(B);
							if (ta != tb) return (ta < tb) ? -1 : 1;
							switch (A.index()) {
								case 1: { double da = std::get<double>(A); double db = std::get<double>(B); if (da<db) return -1; if (da>db) return 1; return 0; }
								case 2: { const std::string &sa = std::get<std::string>(A); const std::string &sb = std::get<std::string>(B); if (sa<sb) return -1; if (sa>sb) return 1; return 0; }
								case 3: { bool ba = std::get<bool>(A); bool bb = std::get<bool>(B); if (ba==bb) return 0; return ba?1:-1; }
								case 0: return 0;
								default: {
									// compare by pointer address for deterministic order
									size_t pa = 0, pb = 0;
									switch (A.index()) {
										case 4: pa = reinterpret_cast<size_t>(std::get<std::shared_ptr<Function>>(A).get()); break;
										case 5: pa = reinterpret_cast<size_t>(std::get<std::shared_ptr<Array>>(A).get()); break;
										case 6: pa = reinterpret_cast<size_t>(std::get<std::shared_ptr<Object>>(A).get()); break;
										case 7: pa = reinterpret_cast<size_t>(std::get<std::shared_ptr<ClassInfo>>(A).get()); break;
										case 8: pa = reinterpret_cast<size_t>(std::get<std::shared_ptr<Instance>>(A).get()); break;
										case 9: pa = reinterpret_cast<size_t>(std::get<std::shared_ptr<PromiseState>>(A).get()); break;
									}
									switch (B.index()) {
										case 4: pb = reinterpret_cast<size_t>(std::get<std::shared_ptr<Function>>(B).get()); break;
										case 5: pb = reinterpret_cast<size_t>(std::get<std::shared_ptr<Array>>(B).get()); break;
										case 6: pb = reinterpret_cast<size_t>(std::get<std::shared_ptr<Object>>(B).get()); break;
										case 7: pb = reinterpret_cast<size_t>(std::get<std::shared_ptr<ClassInfo>>(B).get()); break;
										case 8: pb = reinterpret_cast<size_t>(std::get<std::shared_ptr<Instance>>(B).get()); break;
										case 9: pb = reinterpret_cast<size_t>(std::get<std::shared_ptr<PromiseState>>(B).get()); break;
									}
									if (pa < pb) return -1; if (pa > pb) return 1; return 0;
								}
							}
							return 0; // fallback
						};

						// comparator wrapper: returns negative/zero/positive
						auto cmpWrapper = [&](const Value& A, const Value& B)->int{
							if (useComparator) {
								try {
									std::vector<Value> cargs; cargs.push_back(A); cargs.push_back(B);
									Value res{std::monostate{}};
									if (cmpFn->isBuiltin) {
										res = cmpFn->builtin(cargs, cmpFn->closure);
									} else {
										// Execute interpreted comparator function
										auto local = std::make_shared<Environment>(cmpFn->closure);
										// bind parameters
										for (size_t i=0;i<cmpFn->params.size() && i<cargs.size();++i) local->define(cmpFn->params[i], cargs[i]);
										for (size_t i=cargs.size(); i<cmpFn->params.size(); ++i) local->define(cmpFn->params[i], Value{std::monostate{}});
										try {
											interpPtr->executeBlock(cmpFn->body, local);
										} catch (const ReturnSignal& rs) { res = rs.value; }
									}
									if (auto pd = std::get_if<double>(&res)) {
										double d = *pd; if (d < 0) return -1; if (d > 0) return 1; return 0;
									}
									if (auto ps = std::get_if<std::string>(&res)) {
										std::string s = *ps; try { double dv = std::stod(s); if (dv<0) return -1; if (dv>0) return 1; return 0; } catch(...) { return s.empty()?0: (s[0]=='-'?-1:1); }
									}
									if (auto pb = std::get_if<bool>(&res)) return *pb ? 1 : -1;
									return 0;
								} catch (const std::exception& ex) { throw; }
							}
							return compareDefault(A,B);
						};

						// stable sort indices by comparator over keysArr
						std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b){ return cmpWrapper((*keysArr)[a], (*keysArr)[b]) < 0; });

						auto out = std::make_shared<Array>();
						for (size_t i=0;i<idx.size();++i) out->push_back((*keysArr)[idx[i]]);
						return Value{out};
					};
					globals->define("keysSorted", keysSortedFn);

	}
}

} // namespace asul
