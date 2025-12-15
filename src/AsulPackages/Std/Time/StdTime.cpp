#include "StdTime.h"
#include "../../../AsulInterpreter.h"
#include <chrono>
#include <ctime>
#include <mutex>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
// Windows implementation of setenv/unsetenv
inline int win_setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite) {
        size_t envsize = 0;
        int errcode = getenv_s(&envsize, NULL, 0, name);
        if (!errcode && envsize) return 0;
    }
    return _putenv_s(name, value);
}

inline int win_unsetenv(const char* name) {
    return _putenv_s(name, "");
}

// Windows implementation of timegm (converts UTC tm to time_t)
inline time_t win_timegm(struct tm* tm) {
    return _mkgmtime(tm);
}

// Simple strptime implementation for Windows
inline char* win_strptime(const char* s, const char* format, struct tm* tm) {
    std::istringstream input(s);
    input.imbue(std::locale(setlocale(LC_ALL, nullptr)));
    input >> std::get_time(tm, format);
    if (input.fail()) return nullptr;
    return const_cast<char*>(s + input.tellg());
}
#endif

// External mutex for timezone operations
extern std::mutex tzMutex;

namespace asul {

void registerStdTimePackage(Interpreter& interp) {
	interp.registerLazyPackage("std.time", [&interp](std::shared_ptr<Object> timePkg) {

		// Date class: constructor(epochMillis)
		{
			auto dateClass = std::make_shared<ClassInfo>();
			dateClass->name = "Date";
			// constructor(epochMillis)
			auto ctor = std::make_shared<Function>();
			ctor->isBuiltin = true;
			ctor->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
				if (args.size() != 1) throw std::runtime_error("Date.constructor 需要1个纪元毫秒数参数");
				double ms = getNumber(args[0], "Date.constructor epochMillis");
				Value thisVal = closure->get("this");
				auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				// store epochMillis
				inst->fields["epochMillis"] = Value{ ms };
				// compute broken-down UTC time
				using namespace std::chrono;
				auto tp = time_point<system_clock, milliseconds>(milliseconds(static_cast<long long>(ms)));
				auto tt = system_clock::to_time_t(time_point_cast<system_clock::duration>(tp));
				std::tm tmUTC{};
#ifdef _WIN32
				gmtime_s(&tmUTC, &tt);
#else
				gmtime_r(&tt, &tmUTC);
#endif
				inst->fields["year"] = Value{ static_cast<double>(tmUTC.tm_year + 1900) };
				inst->fields["month"] = Value{ static_cast<double>(tmUTC.tm_mon + 1) };
				inst->fields["day"] = Value{ static_cast<double>(tmUTC.tm_mday) };
				inst->fields["hour"] = Value{ static_cast<double>(tmUTC.tm_hour) };
				inst->fields["minute"] = Value{ static_cast<double>(tmUTC.tm_min) };
				inst->fields["second"] = Value{ static_cast<double>(tmUTC.tm_sec) };
				inst->fields["millisecond"] = Value{ static_cast<double>(static_cast<long long>(ms) % 1000) };
				// ISO string (UTC, Z)
				char buf[64];
				std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
					(int)(tmUTC.tm_year + 1900),(int)(tmUTC.tm_mon + 1),(int)tmUTC.tm_mday,(int)tmUTC.tm_hour,(int)tmUTC.tm_min,(int)tmUTC.tm_sec,(long long)(static_cast<long long>(ms) % 1000));
				inst->fields["iso"] = Value{ std::string(buf) };
				return Value{ std::monostate{} };
			};
			dateClass->methods["constructor"] = ctor;
			// toISO()
			auto toIsoM = std::make_shared<Function>(); toIsoM->isBuiltin = true; toIsoM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
				if (!args.empty()) throw std::runtime_error("Date.toISO 不需要参数");
				Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				return inst->fields["iso"];
			}; dateClass->methods["toISO"] = toIsoM;
			// simple accessors (year, month, day, hour, minute, second, millisecond, epochMillis)
			auto makeFieldGetter = [](const std::string& field){
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->builtin = [field](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
					if (!args.empty()) throw std::runtime_error("Date." + field + " expects 0 arguments");
					Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal); return inst->fields[field];
				}; return fn;
			};
			dateClass->methods["getYear"] = makeFieldGetter("year");
			dateClass->methods["getMonth"] = makeFieldGetter("month");
			dateClass->methods["getDay"] = makeFieldGetter("day");
			dateClass->methods["getHour"] = makeFieldGetter("hour");
			dateClass->methods["getMinute"] = makeFieldGetter("minute");
			dateClass->methods["getSecond"] = makeFieldGetter("second");
			dateClass->methods["getMillisecond"] = makeFieldGetter("millisecond");
			dateClass->methods["getEpochMillis"] = makeFieldGetter("epochMillis");

			// format(fmt, [timezone])
			auto formatFn = std::make_shared<Function>(); formatFn->isBuiltin = true;
			formatFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
				if (args.size() < 1 || args.size() > 2) throw std::runtime_error("Date.format 需要1或2个参数（格式字符串，可选时区）");
				std::string fmt = toString(args[0]);
				std::string tzName;
				if (args.size() == 2) tzName = toString(args[1]);

				Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				double ms = getNumber(inst->fields["epochMillis"], "epochMillis");
				time_t tt = (time_t)(ms / 1000.0);
				std::tm tmVal{};

				if (tzName.empty() || tzName == "UTC" || tzName == "Z") {
					// Default to UTC if no timezone specified or explicitly UTC
#ifdef _WIN32
					gmtime_s(&tmVal, &tt);
#else
					gmtime_r(&tt, &tmVal);
#endif
				} else {
					// Use timezone
					std::lock_guard<std::mutex> lock(tzMutex);
					char* oldTz = getenv("TZ");
					std::string oldTzStr = oldTz ? oldTz : "";
					
#ifdef _WIN32
					win_setenv("TZ", tzName.c_str(), 1);
					_tzset();
					localtime_s(&tmVal, &tt);
					if (oldTz) win_setenv("TZ", oldTzStr.c_str(), 1);
					else win_unsetenv("TZ");
					_tzset();
#else
					setenv("TZ", tzName.c_str(), 1);
					tzset();
					localtime_r(&tt, &tmVal);
					if (oldTz) setenv("TZ", oldTzStr.c_str(), 1);
					else unsetenv("TZ");
					tzset();
#endif
				}

				char buf[128];
				std::strftime(buf, sizeof(buf), fmt.c_str(), &tmVal);
				return Value{ std::string(buf) };
			};
			dateClass->methods["format"] = formatFn;

			// Duration Class
			auto durationClass = std::make_shared<ClassInfo>();
			durationClass->name = "Duration";
			auto durCtor = std::make_shared<Function>(); durCtor->isBuiltin = true;
			durCtor->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
				if (args.size() != 1) throw std::runtime_error("Duration 构造函数需要1个毫秒数参数");
				double ms = getNumber(args[0], "Duration milliseconds");
				Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				inst->fields["milliseconds"] = Value{ms};
				return Value{std::monostate{}};
			};
			durationClass->methods["constructor"] = durCtor;
			(*timePkg)["Duration"] = Value{durationClass};

			// __add__(other)
			auto addFn = std::make_shared<Function>(); addFn->isBuiltin = true;
			addFn->builtin = [dateClass](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
				if (args.size() != 1) throw std::runtime_error("Date + expects 1 argument");
				Value other = args[0];
				Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				double ms = getNumber(inst->fields["epochMillis"], "epochMillis");
				
				if (auto otherInst = std::get_if<std::shared_ptr<Instance>>(&other)) {
					if ((*otherInst)->klass->name == "Duration") {
						double durMs = getNumber((*otherInst)->fields["milliseconds"], "Duration milliseconds");
						double newMs = ms + durMs;
						auto newInst = std::make_shared<Instance>(); newInst->klass = dateClass;
						auto ctor = dateClass->methods["constructor"];
						auto newEnv = std::make_shared<Environment>(); newEnv->define("this", Value{newInst});
						ctor->builtin({Value{newMs}}, newEnv);
						return Value{newInst};
					}
				}
				throw std::runtime_error("Date + supports Duration");
			};
			dateClass->methods["__add__"] = addFn;

			// __sub__(other)
			auto subFn = std::make_shared<Function>(); subFn->isBuiltin = true;
			subFn->builtin = [dateClass, durationClass](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
				if (args.size() != 1) throw std::runtime_error("Date - 需要1个参数");
				Value other = args[0];
				Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				double ms = getNumber(inst->fields["epochMillis"], "epochMillis");
				
				if (auto otherInst = std::get_if<std::shared_ptr<Instance>>(&other)) {
					if ((*otherInst)->klass->name == "Duration") {
						double durMs = getNumber((*otherInst)->fields["milliseconds"], "Duration milliseconds");
						double newMs = ms - durMs;
						auto newInst = std::make_shared<Instance>(); newInst->klass = dateClass;
						auto ctor = dateClass->methods["constructor"];
						auto newEnv = std::make_shared<Environment>(); newEnv->define("this", Value{newInst});
						ctor->builtin({Value{newMs}}, newEnv);
						return Value{newInst};
					}
					if ((*otherInst)->klass->name == "Date") {
						double otherMs = getNumber((*otherInst)->fields["epochMillis"], "Date epochMillis");
						double diff = ms - otherMs;
						auto newInst = std::make_shared<Instance>(); newInst->klass = durationClass;
						auto ctor = durationClass->methods["constructor"];
						auto newEnv = std::make_shared<Environment>(); newEnv->define("this", Value{newInst});
						ctor->builtin({Value{diff}}, newEnv);
						return Value{newInst};
					}
				}
				throw std::runtime_error("Date - 仅支持 Duration 或 Date");
			};
			dateClass->methods["__sub__"] = subFn;

			(*timePkg)["Date"] = Value{ dateClass };
		}

		// nowEpochMillis()
		auto nowMsFn = std::make_shared<Function>(); nowMsFn->isBuiltin = true; nowMsFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (!args.empty()) throw std::runtime_error("nowEpochMillis 不需要参数");
			using namespace std::chrono; auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
			return Value{ static_cast<double>(ms) };
		}; (*timePkg)["nowEpochMillis"] = Value{ nowMsFn };

		// nowEpochSeconds()
		auto nowSecFn = std::make_shared<Function>(); nowSecFn->isBuiltin = true; nowSecFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (!args.empty()) throw std::runtime_error("nowEpochSeconds 不需要参数");
			using namespace std::chrono; auto secs = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
			return Value{ static_cast<double>(secs) };
		}; (*timePkg)["nowEpochSeconds"] = Value{ nowSecFn };

		// nowISO() convenience
		auto nowIsoFn = std::make_shared<Function>(); nowIsoFn->isBuiltin = true; nowIsoFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (!args.empty()) throw std::runtime_error("nowISO 不需要参数");
			using namespace std::chrono; auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
			auto tt = system_clock::to_time_t(system_clock::now()); std::tm tmUTC{};
#ifdef _WIN32
			gmtime_s(&tmUTC, &tt);
#else
			gmtime_r(&tt, &tmUTC);
#endif
			char buf[64]; std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
				(int)(tmUTC.tm_year + 1900),(int)(tmUTC.tm_mon + 1),(int)tmUTC.tm_mday,(int)tmUTC.tm_hour,(int)tmUTC.tm_min,(int)tmUTC.tm_sec,(long long)(ms % 1000));
			return Value{ std::string(buf) };
		}; (*timePkg)["nowISO"] = Value{ nowIsoFn };

		// now() -> Date instance
		auto nowFn = std::make_shared<Function>(); nowFn->isBuiltin = true; nowFn->builtin = [&interp](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (!args.empty()) throw std::runtime_error("now 不需要参数");
			using namespace std::chrono; auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
			
			auto timePkgLocal = interp.ensurePackage("std.time");
			auto it = timePkgLocal->find("Date"); if (it == timePkgLocal->end() || !std::holds_alternative<std::shared_ptr<ClassInfo>>(it->second)) throw std::runtime_error("未找到 Date 类");
			auto dateClass = std::get<std::shared_ptr<ClassInfo>>(it->second);
			auto inst = std::make_shared<Instance>(); inst->klass = dateClass;
			auto ctor = dateClass->methods["constructor"];
			auto newEnv = std::make_shared<Environment>(); newEnv->define("this", Value{inst});
			ctor->builtin({Value{static_cast<double>(ms)}}, newEnv);
			return Value{inst};
		}; (*timePkg)["now"] = Value{nowFn};

		// dateFromEpoch(ms) -> Date instance
		auto dateFromEpochFn = std::make_shared<Function>(); dateFromEpochFn->isBuiltin = true; dateFromEpochFn->builtin = [&interp](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.size() != 1) throw std::runtime_error("dateFromEpoch 需要1个纪元毫秒数参数");
			double ms = getNumber(args[0], "dateFromEpoch epochMillis");
			// get Date class
			auto timePkgLocal = interp.ensurePackage("std.time");
			auto it = timePkgLocal->find("Date"); if (it == timePkgLocal->end() || !std::holds_alternative<std::shared_ptr<ClassInfo>>(it->second)) throw std::runtime_error("未找到 Date 类");
			auto dateClass = std::get<std::shared_ptr<ClassInfo>>(it->second);
			auto inst = std::make_shared<Instance>(); inst->klass = dateClass;
			// manual invoke constructor logic (reuse ctor builtin)
			auto ctorIt = dateClass->methods.find("constructor"); if (ctorIt == dateClass->methods.end()) throw std::runtime_error("Date.constructor 缺失");
			auto closureEnv = std::make_shared<Environment>(); closureEnv->define("this", Value{inst});
			ctorIt->second->builtin({ Value{ ms } }, closureEnv);
			return Value{ inst };
		}; (*timePkg)["dateFromEpoch"] = Value{ dateFromEpochFn };

		// parse(dateStr, fmt, [timezone])
		auto parseFn = std::make_shared<Function>(); parseFn->isBuiltin = true;
		parseFn->builtin = [&interp](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.size() < 2 || args.size() > 3) throw std::runtime_error("parse 需要2或3个参数（日期字符串、格式字符串、可选时区）");
			std::string dateStr = toString(args[0]);
			std::string fmt = toString(args[1]);
			std::string tzName;
			if (args.size() == 3) tzName = toString(args[2]);
			
			std::tm tmVal{};
			tmVal.tm_isdst = -1;
			
#ifdef _WIN32
			char* res = win_strptime(dateStr.c_str(), fmt.c_str(), &tmVal);
#else
			char* res = strptime(dateStr.c_str(), fmt.c_str(), &tmVal);
#endif
			if (res == nullptr) throw std::runtime_error("日期解析失败");
			
			double ms = 0;
			
			if (tzName.empty() || tzName == "UTC" || tzName == "Z") {
				// Use timegm to interpret as UTC (GNU extension, usually available on Linux)
#ifdef _WIN32
				time_t tt = win_timegm(&tmVal);
#else
				time_t tt = timegm(&tmVal);
#endif
				if (tt == -1) throw std::runtime_error("日期解析失败 (timegm)");
				ms = (double)tt * 1000.0;
			} else {
				std::lock_guard<std::mutex> lock(tzMutex);
				char* oldTz = getenv("TZ");
				std::string oldTzStr = oldTz ? oldTz : "";
				
#ifdef _WIN32
				win_setenv("TZ", tzName.c_str(), 1);
				_tzset();
				time_t tt = mktime(&tmVal); // mktime interprets tm as local time in current TZ
				if (oldTz) win_setenv("TZ", oldTzStr.c_str(), 1);
				else win_unsetenv("TZ");
				_tzset();
#else
				setenv("TZ", tzName.c_str(), 1);
				tzset();
				time_t tt = mktime(&tmVal); // mktime interprets tm as local time in current TZ
				if (oldTz) setenv("TZ", oldTzStr.c_str(), 1);
				else unsetenv("TZ");
				tzset();
#endif
				
				if (tt == -1) throw std::runtime_error("日期解析失败 (mktime)");
				ms = (double)tt * 1000.0;
			}
			
			auto timePkgLocal = interp.ensurePackage("std.time");
			auto it = timePkgLocal->find("Date"); if (it == timePkgLocal->end() || !std::holds_alternative<std::shared_ptr<ClassInfo>>(it->second)) throw std::runtime_error("未找到 Date 类");
			auto dateClass = std::get<std::shared_ptr<ClassInfo>>(it->second);

			auto newInst = std::make_shared<Instance>(); newInst->klass = dateClass;
			auto ctor = dateClass->methods["constructor"];
			auto newEnv = std::make_shared<Environment>(); newEnv->define("this", Value{newInst});
			ctor->builtin({Value{ms}}, newEnv);
			return Value{newInst};
		};
		(*timePkg)["parse"] = Value{parseFn};
	});
}

PackageMeta getStdTimePackageMeta() {
    PackageMeta pkg;
    pkg.name = "std.time";
    pkg.exports = { "Duration", "Date", "nowEpochMillis", "nowEpochSeconds", "nowISO", "now", "dateFromEpoch", "parse" };
    
    ClassMeta dateClass;
    dateClass.name = "Date";
    dateClass.methods = { {"constructor"}, {"toISO"}, {"getYear"}, {"getMonth"}, {"getDay"}, {"getHour"}, {"getMinute"}, {"getSecond"}, {"getMillisecond"}, {"getEpochMillis"}, {"format"}, {"__add__"}, {"__sub__"} };
    pkg.classes.push_back(dateClass);

    ClassMeta durationClass;
    durationClass.name = "Duration";
    durationClass.methods = { {"constructor"} };
    pkg.classes.push_back(durationClass);

    return pkg;
}

} // namespace asul
