#include "StdOs.h"
#include "../../../AsulInterpreter.h"
#include <cstdlib>
#include <csignal>
#include <filesystem>

#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
    #define getpid _getpid
    #define popen _popen
    #define pclose _pclose
    // Windows doesn't have setenv, use _putenv_s
    inline int setenv(const char* name, const char* value, int overwrite) {
        return _putenv_s(name, value);
    }
#else
    #include <unistd.h>
    #include <signal.h>
#endif

namespace asul {

void registerStdOsPackage(Interpreter& interp) {
	interp.registerLazyPackage("std.os", [&interp](std::shared_ptr<Object> osPkg) {
		// system(command)
		auto systemFn = std::make_shared<Function>(); systemFn->isBuiltin = true;
		systemFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.empty()) throw std::runtime_error("os.system 需要命令参数");
			std::string cmd = toString(args[0]);
			int ret = std::system(cmd.c_str());
			return Value{static_cast<double>(ret)};
		};
		(*osPkg)["system"] = Value{systemFn};

		// getenv(name)
		auto getenvFn = std::make_shared<Function>(); getenvFn->isBuiltin = true;
		getenvFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.empty()) throw std::runtime_error("os.getenv 需要环境变量名参数");
			std::string name = toString(args[0]);
			const char* val = std::getenv(name.c_str());
			if (val) return Value{std::string(val)};
			return Value{std::monostate{}};
		};
		(*osPkg)["getenv"] = Value{getenvFn};

		// setenv(name, value)
		auto setenvFn = std::make_shared<Function>(); setenvFn->isBuiltin = true;
		setenvFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.size() < 2) throw std::runtime_error("os.setenv 需要环境变量名和值两个参数");
			std::string name = toString(args[0]);
			std::string val = toString(args[1]);
			setenv(name.c_str(), val.c_str(), 1);
			return Value{true};
		};
		(*osPkg)["setenv"] = Value{setenvFn};

		// signal(signame, callback)
		auto signalFn = std::make_shared<Function>(); signalFn->isBuiltin = true;
		signalFn->builtin = [&interp](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.size() != 2) throw std::runtime_error("os.signal 需要信号名和回调函数两个参数");
			std::string signame = toString(args[0]);
			Value callback = args[1];
			if (!std::holds_alternative<std::shared_ptr<Function>>(callback)) throw std::runtime_error("os.signal 的回调参数必须是函数");
			
			int sig = 0;
			if (signame == "SIGINT") sig = SIGINT;
			else if (signame == "SIGTERM") sig = SIGTERM;
#ifndef _WIN32
			else if (signame == "SIGKILL") sig = SIGKILL;
			else if (signame == "SIGUSR1") sig = SIGUSR1;
			else if (signame == "SIGUSR2") sig = SIGUSR2;
#endif
			else throw std::runtime_error("os.signal 不支持的信号: " + signame);
			
			interp.setSignalHandler(sig, callback);
			std::signal(sig, globalSignalHandler);
			return Value{true};
		};
		(*osPkg)["signal"] = Value{signalFn};

		// kill(pid, signame) - send signal to process
		auto killFn = std::make_shared<Function>(); killFn->isBuiltin = true;
		killFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.size() < 2) throw std::runtime_error("os.kill 需要进程ID和信号名两个参数");
			int pid = static_cast<int>(Interpreter::getNumber(args[0], "os.kill pid"));
			std::string signame = toString(args[1]);
			
			int sig = 0;
			if (signame == "SIGINT") sig = SIGINT;
			else if (signame == "SIGTERM") sig = SIGTERM;
#ifndef _WIN32
			else if (signame == "SIGKILL") sig = SIGKILL;
			else if (signame == "SIGUSR1") sig = SIGUSR1;
			else if (signame == "SIGUSR2") sig = SIGUSR2;
#endif
			else throw std::runtime_error("os.kill 不支持的信号: " + signame);
			
#ifdef _WIN32
			// On Windows, use raise() for self, or GenerateConsoleCtrlEvent for SIGINT
			if (pid == _getpid()) {
				// Sending to self
				raise(sig);
				return Value{true};
			} else {
				// For other processes, we can only send CTRL_C_EVENT or CTRL_BREAK_EVENT
				// This only works for console processes in the same console group
				if (sig == SIGINT) {
					// Try GenerateConsoleCtrlEvent - requires process to be in same console
					BOOL result = GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
					return Value{result != 0};
				} else {
					// For SIGTERM, try to terminate the process
					HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
					if (hProcess) {
						BOOL result = TerminateProcess(hProcess, 1);
						CloseHandle(hProcess);
						return Value{result != 0};
					}
					return Value{false};
				}
			}
#else
			int result = kill(pid, sig);
			return Value{result == 0};
#endif
		};
		(*osPkg)["kill"] = Value{killFn};

		// raise(signame) - send signal to self
		auto raiseFn = std::make_shared<Function>(); raiseFn->isBuiltin = true;
		raiseFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.empty()) throw std::runtime_error("os.raise 需要信号名参数");
			std::string signame = toString(args[0]);
			
			int sig = 0;
			if (signame == "SIGINT") sig = SIGINT;
			else if (signame == "SIGTERM") sig = SIGTERM;
#ifndef _WIN32
			else if (signame == "SIGUSR1") sig = SIGUSR1;
			else if (signame == "SIGUSR2") sig = SIGUSR2;
#endif
			else throw std::runtime_error("os.raise 不支持的信号: " + signame);
			
			int result = raise(sig);
			return Value{result == 0};
		};
		(*osPkg)["raise"] = Value{raiseFn};

		// getpid()
		auto getpidFn = std::make_shared<Function>(); getpidFn->isBuiltin = true;
		getpidFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			return Value{static_cast<double>(getpid())};
		};
		(*osPkg)["getpid"] = Value{getpidFn};

		// popen(command, mode)
		auto popenFn = std::make_shared<Function>(); popenFn->isBuiltin = true;
		popenFn->builtin = [&interp](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
			if (args.size() < 1) throw std::runtime_error("os.popen 需要命令参数");
			std::string cmd = toString(args[0]);
			std::string mode = "r";
			if (args.size() > 1) mode = toString(args[1]);
			
			FILE* fp = popen(cmd.c_str(), mode.c_str());
			if (!fp) throw std::runtime_error("os.popen 执行失败");
			
			auto ioPkgLocal = interp.ensurePackage("std.io");
			auto itFS = ioPkgLocal->find("FileStream");
			if (itFS == ioPkgLocal->end() || !std::holds_alternative<std::shared_ptr<ClassInfo>>(itFS->second)) {
				pclose(fp);
				throw std::runtime_error("未找到 FileStream 类");
			}
			auto streamClass = std::get<std::shared_ptr<ClassInfo>>(itFS->second);

			auto fsInst = std::make_shared<InstanceExt>(); 
			fsInst->klass = streamClass; 
			fsInst->fields["path"] = Value{cmd}; 
			fsInst->fields["mode"] = Value{mode}; 
			fsInst->fields["closed"] = Value{false}; 
			
			fsInst->nativeHandle = new FilePtrWrapper(fp, [](FILE* f) { pclose(f); });
			fsInst->nativeDestructor = [](void* ptr) { delete static_cast<StreamWrapper*>(ptr); };
			
			return Value{std::shared_ptr<Instance>(fsInst)};
		};
		(*osPkg)["popen"] = Value{popenFn};

		#ifdef _WIN32
		(*osPkg)["platform"] = Value{std::string("windows")};
		#elif __linux__
		(*osPkg)["platform"] = Value{std::string("linux")};
		#elif __APPLE__
		(*osPkg)["platform"] = Value{std::string("darwin")};
		#else
		(*osPkg)["platform"] = Value{std::string("unknown")};
		#endif
	});
}

PackageMeta getStdOsPackageMeta() {
    PackageMeta pkg;
    pkg.name = "std.os";
    pkg.exports = { "system", "getenv", "setenv", "signal", "kill", "raise", "getpid", "popen", "platform" };
    return pkg;
}

} // namespace asul
