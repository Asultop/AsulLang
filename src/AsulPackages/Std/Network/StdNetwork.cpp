#include "StdNetwork.h"
#include "../../../AsulInterpreter.h"
#include "../../../AsulAsync.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <thread>
#include <sstream>
#include <iostream>
#include <cerrno>

namespace asul {

void registerStdNetworkPackage(Interpreter& interp) {
	// Get pointer to async interface (Interpreter implements AsulAsync)
	// The Interpreter outlives all packages and threads
	AsulAsync* asyncPtr = &interp.getAsyncInterface();
	Interpreter* interpPtr = &interp;
	
	interp.registerLazyPackage("std.network", [interpPtr, asyncPtr](std::shared_ptr<Object> netPkg) {
		
		// Socket Class
		auto socketClass = std::make_shared<ClassInfo>();
		socketClass->name = "Socket";
		socketClass->isNative = true;
		(*netPkg)["Socket"] = Value{socketClass};

		// constructor(domain, type)
		auto ctor = std::make_shared<Function>();
		ctor->isBuiltin = true;
		ctor->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
			int domain = AF_INET;
			int type = SOCK_STREAM;
			if (args.size() >= 1) {
				std::string d = toString(args[0]);
				if (d == "inet6") domain = AF_INET6;
			}
			if (args.size() >= 2) {
				std::string t = toString(args[1]);
				if (t == "udp") type = SOCK_DGRAM;
			}
			
			int fd = socket(domain, type, 0);
			if (fd < 0) throw std::runtime_error("socket creation failed");
			
			int opt = 1;
			setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

			Value thisVal = closure->get("this");
			auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
			auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
			if (ext) {
				ext->nativeHandle = new int(fd);
				ext->nativeDestructor = [](void* p) {
					int* fdp = static_cast<int*>(p);
					close(*fdp);
					delete fdp;
				};
			}
			return Value{std::monostate{}};
		};
		socketClass->methods["constructor"] = ctor;

		// bind(host, port)
		auto bindFn = std::make_shared<Function>();
		bindFn->isBuiltin = true;
		bindFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
			if (args.size() != 2) throw std::runtime_error("bind expects host and port");
			std::string host = toString(args[0]);
			int port = static_cast<int>(getNumber(args[1], "port"));
			
			Value thisVal = closure->get("this");
			auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
			auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
			if (!ext || !ext->nativeHandle) throw std::runtime_error("Socket not initialized");
			int fd = *static_cast<int*>(ext->nativeHandle);

			struct sockaddr_in addr;
			std::memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_port = htons(port);
			if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
				throw std::runtime_error("Invalid address");
			}
			
			if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
				throw std::runtime_error("bind failed");
			}
			return Value{true};
		};
		socketClass->methods["bind"] = bindFn;

		// listen(backlog)
		auto listenFn = std::make_shared<Function>();
		listenFn->isBuiltin = true;
		listenFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
			int backlog = 5;
			if (!args.empty()) backlog = static_cast<int>(getNumber(args[0], "backlog"));
			
			Value thisVal = closure->get("this");
			auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
			auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
			int fd = *static_cast<int*>(ext->nativeHandle);
			
			if (listen(fd, backlog) < 0) throw std::runtime_error("listen failed");
			return Value{true};
		};
		socketClass->methods["listen"] = listenFn;

		// connect(host, port) -> Promise
		auto connectFn = std::make_shared<Function>();
		connectFn->isBuiltin = true;
		connectFn->builtin = [asyncPtr](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
			if (args.size() != 2) throw std::runtime_error("connect expects host and port");
			std::string host = toString(args[0]);
			int port = static_cast<int>(getNumber(args[1], "port"));
			
			Value thisVal = closure->get("this");
			auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
			auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
			int fd = *static_cast<int*>(ext->nativeHandle);

			auto p = asyncPtr->createPromise();
			
			std::thread([p, asyncPtr, fd, host, port]{
				struct sockaddr_in addr;
				std::memset(&addr, 0, sizeof(addr));
				addr.sin_family = AF_INET;
				addr.sin_port = htons(port);
				
				struct hostent* server = gethostbyname(host.c_str());
				if (server == NULL) {
					asyncPtr->reject(p, Value{std::string("Host resolution failed")});
					return;
				}
				std::memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);

				if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
					asyncPtr->reject(p, Value{std::string("Connection failed")});
				} else {
					asyncPtr->resolve(p, Value{true});
				}
			}).detach();
			
			return Value{p};
		};
		socketClass->methods["connect"] = connectFn;

		// accept() -> Promise<Socket>
		auto acceptFn = std::make_shared<Function>();
		acceptFn->isBuiltin = true;
		acceptFn->builtin = [asyncPtr, socketClass](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
			Value thisVal = closure->get("this");
			auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
			auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
			int fd = *static_cast<int*>(ext->nativeHandle);

			auto p = asyncPtr->createPromise();

			std::thread([p, asyncPtr, fd, socketClass]{
				struct sockaddr_in cli_addr;
				socklen_t clilen = sizeof(cli_addr);
				int newsockfd = accept(fd, (struct sockaddr*)&cli_addr, &clilen);
				if (newsockfd < 0) {
					asyncPtr->reject(p, Value{std::string("accept failed")});
					return;
				}
				
				auto newInst = std::make_shared<InstanceExt>();
				newInst->klass = socketClass;
				newInst->nativeHandle = new int(newsockfd);
				newInst->nativeDestructor = [](void* p) {
					int* fdp = static_cast<int*>(p);
					close(*fdp);
					delete fdp;
				};
				
				asyncPtr->resolve(p, Value{std::shared_ptr<Instance>(newInst)});
			}).detach();

			return Value{p};
		};
		socketClass->methods["accept"] = acceptFn;

		// write(data) -> Promise
		auto writeFn = std::make_shared<Function>();
		writeFn->isBuiltin = true;
		writeFn->builtin = [asyncPtr](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
			if (args.empty()) throw std::runtime_error("write expects data");
			std::string data = toString(args[0]);
			
			Value thisVal = closure->get("this");
			auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
			auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
			int fd = *static_cast<int*>(ext->nativeHandle);

			auto p = asyncPtr->createPromise();

			std::thread([p, asyncPtr, fd, data]{
				ssize_t n = write(fd, data.c_str(), data.length());
				if (n < 0) {
					asyncPtr->reject(p, Value{std::string("write failed")});
				} else {
					asyncPtr->resolve(p, Value{static_cast<double>(n)});
				}
			}).detach();

			return Value{p};
		};
		socketClass->methods["write"] = writeFn;

		// read(size) -> Promise<string>
		auto readFn = std::make_shared<Function>();
		readFn->isBuiltin = true;
		readFn->builtin = [asyncPtr](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
			int size = 1024;
			if (!args.empty()) size = static_cast<int>(getNumber(args[0], "size"));
			
			Value thisVal = closure->get("this");
			auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
			auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
			int fd = *static_cast<int*>(ext->nativeHandle);

			auto p = asyncPtr->createPromise();

			std::thread([p, asyncPtr, fd, size]{
				std::vector<char> buf(size);
				ssize_t n = read(fd, buf.data(), size);
				if (n < 0) {
					asyncPtr->reject(p, Value{std::string("read failed")});
				} else if (n == 0) {
					asyncPtr->resolve(p, Value{std::string("")});
				} else {
					asyncPtr->resolve(p, Value{std::string(buf.data(), n)});
				}
			}).detach();

			return Value{p};
		};
		socketClass->methods["read"] = readFn;

		// close()
		auto closeFn = std::make_shared<Function>();
		closeFn->isBuiltin = true;
		closeFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
			Value thisVal = closure->get("this");
			auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
			auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
			if (ext && ext->nativeHandle) {
				int* fdp = static_cast<int*>(ext->nativeHandle);
				close(*fdp);
				delete fdp;
				ext->nativeHandle = nullptr;
			}
			return Value{true};
		};
		socketClass->methods["close"] = closeFn;

		// URL class: new URL(str) -> fields: protocol, host, port, path, query
		{
			auto urlClass = std::make_shared<ClassInfo>();
			urlClass->name = "URL";
			auto urlCtor = std::make_shared<Function>();
			urlCtor->isBuiltin = true;
			urlCtor->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
				if (args.size() != 1) throw std::runtime_error("URL constructor expects 1 argument (string)");
				std::string u = toString(args[0]);
				std::string protocol; std::string host; int port = -1; std::string path = "/"; std::string query;
				size_t schemePos = u.find("://");
				if (schemePos != std::string::npos) { protocol = u.substr(0, schemePos); }
				size_t hostStart = (schemePos == std::string::npos) ? 0 : (schemePos + 3);
				size_t pathStart = u.find('/', hostStart);
				if (pathStart == std::string::npos) { pathStart = u.size(); }
				// host[:port]
				{
					size_t hpEnd = pathStart;
					size_t colon = u.find(':', hostStart);
					if (colon != std::string::npos && colon < hpEnd) {
						host = u.substr(hostStart, colon - hostStart);
						std::string pstr = u.substr(colon + 1, hpEnd - (colon + 1));
						try { port = std::stoi(pstr); } catch (...) { port = -1; }
					} else {
						host = u.substr(hostStart, hpEnd - hostStart);
					}
				}
				// path?query
				if (pathStart < u.size()) {
					size_t qmark = u.find('?', pathStart);
					if (qmark == std::string::npos) { path = u.substr(pathStart); }
					else { path = u.substr(pathStart, qmark - pathStart); query = u.substr(qmark + 1); }
				}
				if (port < 0) { if (protocol == "http") port = 80; else if (protocol == "https") port = 443; }
				Value thisVal = closure->get("this");
				auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				inst->fields["protocol"] = Value{ protocol };
				inst->fields["host"] = Value{ host };
				inst->fields["port"] = Value{ static_cast<double>(port) };
				inst->fields["path"] = Value{ path };
				inst->fields["query"] = Value{ query };
				return Value{ std::monostate{} };
			};
			urlClass->methods["constructor"] = urlCtor;
			(*netPkg)["URL"] = Value{ urlClass };
		}

		// fetch(url[, options]) -> Promise<Response-like>
		{
			auto fetchFn = std::make_shared<Function>();
			fetchFn->isBuiltin = true;
			fetchFn->builtin = [interpPtr, asyncPtr](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
				if (args.empty()) throw std::runtime_error("fetch expects at least 1 argument (url)");
				std::string url = toString(args[0]);
				std::string method = "GET";
				std::shared_ptr<Object> hdrObj;
				std::string body;
				if (args.size() >= 2) {
					if (!std::holds_alternative<std::shared_ptr<Object>>(args[1])) throw std::runtime_error("fetch options must be object");
					auto opt = std::get<std::shared_ptr<Object>>(args[1]);
					auto itM = opt->find("method"); if (itM != opt->end()) method = toString(itM->second);
					auto itH = opt->find("headers"); if (itH != opt->end() && std::holds_alternative<std::shared_ptr<Object>>(itH->second)) hdrObj = std::get<std::shared_ptr<Object>>(itH->second);
					auto itB = opt->find("body"); if (itB != opt->end()) body = toString(itB->second);
				}
				// Promise
				auto p = asyncPtr->createPromise();
				std::thread([interpPtr, asyncPtr, p, url, method, hdrObj, body]{
					try {
						// Parse URL
						std::string proto = "http"; std::string host; int port = 80; std::string path = "/";
						size_t schemePos = url.find("://"); if (schemePos != std::string::npos) proto = url.substr(0, schemePos);
						size_t hostStart = (schemePos == std::string::npos) ? 0 : (schemePos + 3);
						size_t pathStart = url.find('/', hostStart); if (pathStart == std::string::npos) pathStart = url.size();
						size_t colon = url.find(':', hostStart);
						if (colon != std::string::npos && colon < pathStart) { host = url.substr(hostStart, colon - hostStart); port = std::stoi(url.substr(colon + 1, pathStart - colon - 1)); }
						else { host = url.substr(hostStart, pathStart - hostStart); }
						if (pathStart < url.size()) path = url.substr(pathStart);
						if (proto == "https") { asyncPtr->reject(p, Value{ std::string("HTTPS not supported") }); return; }
						// DNS
						struct hostent* server = gethostbyname(host.c_str());
						if (server == NULL) { asyncPtr->reject(p, Value{ std::string("No such host: ")+host }); return; }
						int sockfd = socket(AF_INET, SOCK_STREAM, 0); if (sockfd < 0) { asyncPtr->reject(p, Value{ std::string("socket failed") }); return; }
						struct sockaddr_in serv_addr; std::memset(&serv_addr, 0, sizeof(serv_addr));
						serv_addr.sin_family = AF_INET; std::memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length); serv_addr.sin_port = htons(port);
						if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) { close(sockfd); asyncPtr->reject(p, Value{ std::string("connect failed") }); return; }
						std::ostringstream req;
						req << method << " " << path << " HTTP/1.1\r\n";
						req << "Host: " << host << "\r\n";
						req << "Connection: close\r\n";
						req << "User-Agent: ALang/1.0\r\n";
						if (hdrObj) { for (auto& kv : *hdrObj) { req << kv.first << ": " << toString(kv.second) << "\r\n"; } }
						if (!body.empty()) { req << "Content-Length: " << body.size() << "\r\n"; }
						req << "\r\n"; if (!body.empty()) req << body;
						std::string rs = req.str(); if (write(sockfd, rs.c_str(), rs.size()) < 0) { close(sockfd); asyncPtr->reject(p, Value{ std::string("write failed") }); return; }
						std::string response; char buf[4096]; int n; while ((n = read(sockfd, buf, sizeof(buf))) > 0) response.append(buf, n); close(sockfd);
						auto respObj = std::make_shared<Object>();
						size_t headerEnd = response.find("\r\n\r\n"); std::string headers, bodyStr; double status = 0.0;
						if (headerEnd != std::string::npos) { headers = response.substr(0, headerEnd); bodyStr = response.substr(headerEnd + 4);
							size_t sp1 = headers.find(' '); size_t sp2 = headers.find(' ', sp1 + 1);
							if (sp1 != std::string::npos && sp2 != std::string::npos) { try { status = std::stod(headers.substr(sp1 + 1, sp2 - sp1 - 1)); } catch (...) {} }
						}
						(*respObj)["status"] = Value{ status };
						(*respObj)["headers"] = Value{ headers };
						// text(): Promise<string>
						{
							auto textFn = std::make_shared<Function>(); textFn->isBuiltin = true;
							std::string copy = bodyStr;
							textFn->builtin = [asyncPtr, copy](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
								auto tp = asyncPtr->createPromise(); asyncPtr->resolve(tp, Value{ copy }); return Value{ tp };
							};
							(*respObj)["text"] = Value{ textFn };
						}
						// json(): Promise<any>
						{
							auto jsonFn = std::make_shared<Function>(); jsonFn->isBuiltin = true; std::string copy = bodyStr;
							jsonFn->builtin = [interpPtr, asyncPtr, copy](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
								auto tp = asyncPtr->createPromise();
								asyncPtr->postTask([interpPtr, asyncPtr, tp, copy]{
									try {
										auto jsonPkg = interpPtr->ensurePackage("json");
										Value parseV = (*jsonPkg)["parse"]; if (!std::holds_alternative<std::shared_ptr<Function>>(parseV)) { asyncPtr->reject(tp, Value{ std::string("json.parse not found") }); return; }
										auto parseFn = std::get<std::shared_ptr<Function>>(parseV);
										Value res = parseFn->builtin({ Value{ copy } }, parseFn->closure);
										asyncPtr->resolve(tp, res);
									} catch (const std::exception& ex) {
										asyncPtr->reject(tp, Value{ std::string(ex.what()) });
									}
								});
								return Value{ tp };
							};
							(*respObj)["json"] = Value{ jsonFn };
						}
						asyncPtr->resolve(p, Value{ respObj });
					} catch (const std::exception& ex) {
						asyncPtr->reject(p, Value{ std::string(ex.what()) });
					}
				}).detach();
				return Value{ p };
			};
			(*netPkg)["fetch"] = Value{ fetchFn };
		}

		// Helper for HTTP requests (Simple blocking implementation)
		auto httpRequest = [](const std::string& method, const std::string& url, const std::string& data = "") -> Value {
			// 1. Parse URL
			std::string host;
			int port = 80;
			std::string path = "/";
			
			std::string protocol = "http://";
			if (url.substr(0, 7) != protocol) throw std::runtime_error("Only http:// supported currently");
			
			size_t hostStart = 7;
			size_t pathStart = url.find('/', hostStart);
			size_t portStart = url.find(':', hostStart);
			
			if (pathStart == std::string::npos) {
				if (portStart == std::string::npos) {
					host = url.substr(hostStart);
				} else {
					host = url.substr(hostStart, portStart - hostStart);
					port = std::stoi(url.substr(portStart + 1));
				}
			} else {
				if (portStart != std::string::npos && portStart < pathStart) {
					host = url.substr(hostStart, portStart - hostStart);
					port = std::stoi(url.substr(portStart + 1, pathStart - portStart - 1));
				} else {
					host = url.substr(hostStart, pathStart - hostStart);
				}
				path = url.substr(pathStart);
			}

			// 2. Resolve Host
			struct hostent* server = gethostbyname(host.c_str());
			if (server == NULL) throw std::runtime_error("No such host: " + host);

			// 3. Create Socket
			int sockfd = socket(AF_INET, SOCK_STREAM, 0);
			if (sockfd < 0) throw std::runtime_error("Error opening socket");

			struct sockaddr_in serv_addr;
			std::memset(&serv_addr, 0, sizeof(serv_addr));
			serv_addr.sin_family = AF_INET;
			std::memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
			serv_addr.sin_port = htons(port);

			// 4. Connect
			if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
				close(sockfd);
				throw std::runtime_error("Error connecting to " + host);
			}

			// 5. Send Request
			std::ostringstream req;
			req << method << " " << path << " HTTP/1.1\r\n";
			req << "Host: " << host << "\r\n";
			req << "Connection: close\r\n";
			req << "User-Agent: ALang/1.0\r\n";
			if (!data.empty()) {
				req << "Content-Length: " << data.length() << "\r\n";
				req << "Content-Type: application/x-www-form-urlencoded\r\n";
			}
			req << "\r\n";
			if (!data.empty()) req << data;

			std::string reqStr = req.str();
			if (write(sockfd, reqStr.c_str(), reqStr.length()) < 0) {
				close(sockfd);
				throw std::runtime_error("Error writing to socket");
			}

			// 6. Read Response
			std::string response;
			char buffer[4096];
			int n;
			while ((n = read(sockfd, buffer, sizeof(buffer))) > 0) {
				response.append(buffer, n);
			}
			close(sockfd);

			// 7. Parse Response
			auto obj = std::make_shared<Object>();
			
			size_t headerEnd = response.find("\r\n\r\n");
			if (headerEnd != std::string::npos) {
				std::string headers = response.substr(0, headerEnd);
				std::string body = response.substr(headerEnd + 4);
				(*obj)["body"] = Value{body};
				(*obj)["headers"] = Value{headers};
				
				// Parse status code (e.g., "HTTP/1.1 200 OK")
				size_t firstSpace = headers.find(' ');
				size_t secondSpace = headers.find(' ', firstSpace + 1);
				if (firstSpace != std::string::npos && secondSpace != std::string::npos) {
					std::string statusStr = headers.substr(firstSpace + 1, secondSpace - firstSpace - 1);
					try {
						(*obj)["status"] = Value{std::stod(statusStr)};
					} catch(...) {
						(*obj)["status"] = Value{0.0};
					}
				} else {
					(*obj)["status"] = Value{0.0};
				}
			} else {
				 (*obj)["body"] = Value{response};
				 (*obj)["status"] = Value{0.0};
				 (*obj)["headers"] = Value{std::string("")};
			}

			return Value{obj};
		};

		auto getFn = std::make_shared<Function>();
		getFn->isBuiltin = true;
		getFn->builtin = [httpRequest](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() != 1) throw std::runtime_error("http.get expects 1 argument (url)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("url must be string");
			return httpRequest("GET", std::get<std::string>(args[0]), "");
		};
		(*netPkg)["get"] = Value{getFn};

		auto postFn = std::make_shared<Function>();
		postFn->isBuiltin = true;
		postFn->builtin = [httpRequest](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() != 2) throw std::runtime_error("http.post expects 2 arguments (url, data)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("url must be string");
			return httpRequest("POST", std::get<std::string>(args[0]), toString(args[1]));
		};
		(*netPkg)["post"] = Value{postFn};
	});
}

} // namespace asul
