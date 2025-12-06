#include "Os.h"
#include "../../AsulInterpreter.h"
#include <cstdlib>
#include <thread>

#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
#else
    #include <unistd.h>
    #include <sys/wait.h>
#endif

namespace asul {

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
#endif

void registerOsPackage(Interpreter& interp) {
	interp.registerLazyPackage("os", [&interp](std::shared_ptr<Object> osPkg) {
		auto callFn = std::make_shared<Function>(); callFn->isBuiltin = true;
		callFn->builtin = [&interp](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.size() < 1) throw std::runtime_error("os.call expects at least 1 argument (program)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("os.call: program must be a string");
			std::string prog = std::get<std::string>(args[0]);
			std::vector<std::string> argv;
			if (args.size() >= 2 && !std::holds_alternative<std::monostate>(args[1])) {
				if (auto parr = std::get_if<std::shared_ptr<Array>>(&args[1])) {
					auto a = *parr; if (a) for (auto &v : *a) { if (!std::holds_alternative<std::string>(v)) throw std::runtime_error("os.call: args must be array of strings"); argv.push_back(std::get<std::string>(v)); }
				} else if (std::holds_alternative<std::string>(args[1])) {
					argv.push_back(std::get<std::string>(args[1]));
				} else {
					throw std::runtime_error("os.call: second argument must be array of strings or a string");
				}
			}
			std::string cwd;
			if (args.size() >= 3 && std::holds_alternative<std::string>(args[2])) cwd = std::get<std::string>(args[2]);
			auto p = std::make_shared<PromiseState>(); p->loopPtr = &interp;
			// spawn thread to run the process and settle the promise when done
#ifdef _WIN32
			std::thread([p, &interp, prog, argv, cwd]() {
				// On Windows, check if prog is a shell command or an executable
				// Shell commands: echo, dir, type, etc. need cmd /c
				// Actual executables: g++, gcc, etc. can be called directly
				bool isShellCmd = (prog == "echo" || prog == "dir" || prog == "type" || 
								   prog == "del" || prog == "copy" || prog == "move" || 
								   prog == "ver" || prog == "whoami" || prog == "set" ||
								   prog == "cmd");
				
				std::string cmdLine;
				if (isShellCmd) {
					// Use cmd /c for built-in shell commands
					cmdLine = "cmd /c " + prog;
				} else {
					// For actual executables, use program directly
					cmdLine = prog;
				}
				
				for (const auto& arg : argv) {
					cmdLine += " \"" + arg + "\"";
				}
				
				SECURITY_ATTRIBUTES saAttr;
				saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
				saAttr.bInheritHandle = TRUE;
				saAttr.lpSecurityDescriptor = NULL;
				
				HANDLE hStdOutRead, hStdOutWrite;
				HANDLE hStdErrRead, hStdErrWrite;
				
				if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &saAttr, 0) ||
					!CreatePipe(&hStdErrRead, &hStdErrWrite, &saAttr, 0)) {
					interp.settlePromise(p, true, Value{ std::string("os.call: CreatePipe failed") });
					return;
				}
				
				SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);
				SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0);
				
				STARTUPINFOA si;
				PROCESS_INFORMATION pi;
				ZeroMemory(&si, sizeof(si));
				si.cb = sizeof(si);
				si.hStdOutput = hStdOutWrite;
				si.hStdError = hStdErrWrite;
				si.dwFlags |= STARTF_USESTDHANDLES;
				ZeroMemory(&pi, sizeof(pi));
				
				BOOL success = CreateProcessA(
					NULL,  // lpApplicationName: NULL lets Windows search PATH
					const_cast<char*>(cmdLine.c_str()),
					NULL,
					NULL,
					TRUE,
					0,
					NULL,
					cwd.empty() ? NULL : cwd.c_str(),
					&si,
					&pi
				);
				
				CloseHandle(hStdOutWrite);
				CloseHandle(hStdErrWrite);
				
				if (!success) {
					CloseHandle(hStdOutRead);
					CloseHandle(hStdErrRead);
					interp.settlePromise(p, true, Value{ std::string("os.call: CreateProcess failed") });
					return;
				}
				
				// Read stdout and stderr
				std::string out, err;
				char buffer[4096];
				DWORD bytesRead;
				
				std::thread readOut([&]() {
					while (ReadFile(hStdOutRead, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
						out.append(buffer, bytesRead);
					}
				});
				
				std::thread readErr([&]() {
					while (ReadFile(hStdErrRead, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
						err.append(buffer, bytesRead);
					}
				});
				
				WaitForSingleObject(pi.hProcess, INFINITE);
				
				DWORD exitCode;
				GetExitCodeProcess(pi.hProcess, &exitCode);
				
				readOut.join();
				readErr.join();
				
				CloseHandle(hStdOutRead);
				CloseHandle(hStdErrRead);
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);
				
				auto res = std::make_shared<Object>();
				(*res)["exitCode"] = Value{ static_cast<double>(exitCode) };
				(*res)["stdout"] = Value{ out };
				(*res)["stderr"] = Value{ err };
				interp.settlePromise(p, false, Value{ res });
			}).detach();
#else
			std::thread([p, &interp, prog, argv, cwd]() {
				int outpipe[2]; int errpipe[2];
				if (pipe(outpipe) != 0 || pipe(errpipe) != 0) {
					interp.settlePromise(p, true, Value{ std::string("os.call: pipe failed") });
					return;
				}
				pid_t pid = fork();
				if (pid == 0) {
					// child
					if (!cwd.empty()) {
						auto ret = chdir(cwd.c_str());
						ret = ret; // suppress unused warning
					}
					dup2(outpipe[1], STDOUT_FILENO);
					dup2(errpipe[1], STDERR_FILENO);
					close(outpipe[0]); close(outpipe[1]); close(errpipe[0]); close(errpipe[1]);
					std::vector<char*> cargv;
					cargv.reserve(argv.size() + 2);
					cargv.push_back(const_cast<char*>(prog.c_str()));
					for (auto &s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
					cargv.push_back(nullptr);
					execvp(prog.c_str(), cargv.data());
					// if exec failed
					_exit(127);
				} else if (pid > 0) {
					// parent: close write ends and read output
					close(outpipe[1]); close(errpipe[1]);
					std::string out; std::string err;
					std::thread rout([&]{ char buf[4096]; ssize_t r; while((r = read(outpipe[0], buf, sizeof(buf))) > 0) out.append(buf, (size_t)r); close(outpipe[0]); });
					std::thread rerr([&]{ char buf[4096]; ssize_t r; while((r = read(errpipe[0], buf, sizeof(buf))) > 0) err.append(buf, (size_t)r); close(errpipe[0]); });
					int status = 0; waitpid(pid, &status, 0);
					rout.join(); rerr.join();
					int exitCode = (WIFEXITED(status) ? WEXITSTATUS(status) : -1);
					auto res = std::make_shared<Object>();
					(*res)["exitCode"] = Value{ static_cast<double>(exitCode) };
					(*res)["stdout"] = Value{ out };
					(*res)["stderr"] = Value{ err };
					interp.settlePromise(p, false, Value{ res });
				} else {
					// fork failed
					close(outpipe[0]); close(outpipe[1]); close(errpipe[0]); close(errpipe[1]);
					interp.settlePromise(p, true, Value{ std::string("os.call: fork failed") });
				}
			}).detach();
#endif
			return Value{ p };
		};
		(*osPkg)["call"] = Value{ callFn };

		// getEnv(name)
		auto getEnvFn = std::make_shared<Function>(); getEnvFn->isBuiltin=true; getEnvFn->builtin=[](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if(args.empty()) throw std::runtime_error("getEnv expects name");
			std::string name = toString(args[0]);
			const char* val = std::getenv(name.c_str());
			if(val) return Value{std::string(val)};
			return Value{std::monostate{}};
		};
		(*osPkg)["getEnv"] = Value{getEnvFn};

		// setEnv(name, value)
		auto setEnvFn = std::make_shared<Function>(); setEnvFn->isBuiltin=true; setEnvFn->builtin=[](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if(args.size()!=2) throw std::runtime_error("setEnv expects name, value");
			std::string name = toString(args[0]); std::string val = toString(args[1]);
#ifdef _WIN32
			win_setenv(name.c_str(), val.c_str(), 1);
#else
			setenv(name.c_str(), val.c_str(), 1);
#endif
			return Value{true};
		};
		(*osPkg)["setEnv"] = Value{setEnvFn};

		// exit(code)
		auto exitFn = std::make_shared<Function>(); exitFn->isBuiltin=true; exitFn->builtin=[](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			int code = 0;
			if(!args.empty()) code = static_cast<int>(getNumber(args[0], "exit code"));
			std::exit(code);
			return Value{std::monostate{}};
		};
		(*osPkg)["exit"] = Value{exitFn};

		// platform()
		auto platformFn = std::make_shared<Function>(); platformFn->isBuiltin=true; platformFn->builtin=[](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
			#ifdef __linux__
			return Value{std::string("linux")};
			#elif _WIN32
			return Value{std::string("windows")};
			#elif __APPLE__
			return Value{std::string("darwin")};
			#else
			return Value{std::string("unknown")};
			#endif
		};
		(*osPkg)["platform"] = Value{platformFn};

		// arch()
		auto archFn = std::make_shared<Function>(); archFn->isBuiltin=true; archFn->builtin=[](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
			if(sizeof(void*) == 8) return Value{std::string("x64")};
			return Value{std::string("x86")};
		};
		(*osPkg)["arch"] = Value{archFn};
	});
}

} // namespace asul
