#include "StdNetwork.h"
#include "../../../AsulInterpreter.h"
#include "../../../AsulAsync.h"
#include <cstring>

#ifdef _WIN32
    // Windows networking
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    
    // Windows compatibility typedefs
    typedef int socklen_t;
    // ssize_t is already defined in MinGW
    #define close closesocket
    #define read(fd, buf, len) recv(fd, (char*)(buf), len, 0)
    #define write(fd, buf, len) send(fd, (const char*)(buf), (int)(len), 0)
    
    // Initialize Winsock once
    struct WinsockInit {
        bool initialized = false;
        WinsockInit() {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
                initialized = true;
            }
        }
        ~WinsockInit() {
            if (initialized) {
                WSACleanup();
            }
        }
    };
    static WinsockInit g_winsockInit;
#else
    // Unix/Linux/macOS networking
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
#endif
#include <thread>
#include <sstream>
#include <iostream>
#include <cerrno>

namespace asul {

// Helper function to get HTTP status text from status code
static std::string getHttpStatusText(int statusCode) {
	switch (statusCode) {
		// 1xx Informational
		case 100: return "Continue";
		case 101: return "Switching Protocols";
		// 2xx Success
		case 200: return "OK";
		case 201: return "Created";
		case 202: return "Accepted";
		case 204: return "No Content";
		// 3xx Redirection
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 303: return "See Other";
		case 304: return "Not Modified";
		case 307: return "Temporary Redirect";
		case 308: return "Permanent Redirect";
		// 4xx Client Errors
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 406: return "Not Acceptable";
		case 409: return "Conflict";
		case 410: return "Gone";
		case 422: return "Unprocessable Entity";
		case 429: return "Too Many Requests";
		// 5xx Server Errors
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 502: return "Bad Gateway";
		case 503: return "Service Unavailable";
		case 504: return "Gateway Timeout";
		default: return "Unknown";
	}
}

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
#ifdef _WIN32
			if (fd == INVALID_SOCKET) throw std::runtime_error("socket creation failed");
			
			char opt = 1;
			setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#else
			if (fd < 0) throw std::runtime_error("socket creation failed");
			
			int opt = 1;
			setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

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
				try {
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
				} catch (const std::exception& ex) {
					asyncPtr->reject(p, Value{std::string("Socket connect exception: ") + ex.what()});
				} catch (...) {
					asyncPtr->reject(p, Value{std::string("Socket connect unknown exception")});
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
				try {
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
				} catch (const std::exception& ex) {
					asyncPtr->reject(p, Value{std::string("Socket accept exception: ") + ex.what()});
				} catch (...) {
					asyncPtr->reject(p, Value{std::string("Socket accept unknown exception")});
				}
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
				try {
					ssize_t n = write(fd, data.c_str(), data.length());
					if (n < 0) {
						asyncPtr->reject(p, Value{std::string("write failed")});
					} else {
						asyncPtr->resolve(p, Value{static_cast<double>(n)});
					}
				} catch (const std::exception& ex) {
					asyncPtr->reject(p, Value{std::string("Socket write exception: ") + ex.what()});
				} catch (...) {
					asyncPtr->reject(p, Value{std::string("Socket write unknown exception")});
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
				try {
					std::vector<char> buf(size);
					ssize_t n = read(fd, buf.data(), size);
					if (n < 0) {
						asyncPtr->reject(p, Value{std::string("read failed")});
					} else if (n == 0) {
						asyncPtr->resolve(p, Value{std::string("")});
					} else {
						asyncPtr->resolve(p, Value{std::string(buf.data(), n)});
					}
				} catch (const std::exception& ex) {
					asyncPtr->reject(p, Value{std::string("Socket read exception: ") + ex.what()});
				} catch (...) {
					asyncPtr->reject(p, Value{std::string("Socket read unknown exception")});
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

			// parseQuery() method - returns an object with query parameters
			auto parseQueryFn = std::make_shared<Function>();
			parseQueryFn->isBuiltin = true;
			parseQueryFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
				Value thisVal = closure->get("this");
				auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				std::string query = toString(inst->fields["query"]);
				
				auto params = std::make_shared<Object>();
				if (query.empty()) return Value{params};
				
				// Parse query string: key1=value1&key2=value2
				size_t pos = 0;
				while (pos < query.size()) {
					size_t ampPos = query.find('&', pos);
					if (ampPos == std::string::npos) ampPos = query.size();
					
					std::string pair = query.substr(pos, ampPos - pos);
					size_t eqPos = pair.find('=');
					if (eqPos != std::string::npos) {
						std::string key = pair.substr(0, eqPos);
						std::string value = pair.substr(eqPos + 1);
						(*params)[key] = Value{value};
					} else {
						(*params)[pair] = Value{std::string("")};
					}
					pos = ampPos + 1;
				}
				return Value{params};
			};
			urlClass->methods["parseQuery"] = parseQueryFn;

			(*netPkg)["URL"] = Value{ urlClass };
		}

		// parseHeaders(headersString) -> object with header key-value pairs
		auto parseHeadersFn = std::make_shared<Function>();
		parseHeadersFn->isBuiltin = true;
		parseHeadersFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.empty()) throw std::runtime_error("parseHeaders expects 1 argument (headers string)");
			std::string headersStr = toString(args[0]);
			auto headers = std::make_shared<Object>();
			
			// Parse headers line by line
			std::istringstream stream(headersStr);
			std::string line;
			bool firstLine = true;
			
			while (std::getline(stream, line)) {
				if (firstLine) {
					// Skip HTTP status line (e.g., "HTTP/1.1 200 OK")
					firstLine = false;
					continue;
				}
				// Remove \r if present
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				if (line.empty()) break; // Empty line marks end of headers
				
				size_t colonPos = line.find(':');
				if (colonPos != std::string::npos) {
					std::string key = line.substr(0, colonPos);
					std::string value = line.substr(colonPos + 1);
					// Trim leading/trailing spaces from value
					size_t start = value.find_first_not_of(" \t");
					size_t end = value.find_last_not_of(" \t");
					if (start != std::string::npos && end != std::string::npos) {
						value = value.substr(start, end - start + 1);
					}
					(*headers)[key] = Value{value};
				}
			}
			return Value{headers};
		};
		(*netPkg)["parseHeaders"] = Value{parseHeadersFn};


		// fetch(url[, options]) -> Promise<Response-like>
		{
			auto fetchFn = std::make_shared<Function>();
			fetchFn->isBuiltin = true;
			fetchFn->builtin = [interpPtr, asyncPtr](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
				if (args.empty()) throw std::runtime_error("fetch expects at least 1 argument (url)");
				std::string initialUrl = toString(args[0]);
				std::string method = "GET";
				std::shared_ptr<Object> hdrObj;
				std::string body;
				bool followRedirects = true;
				int maxRedirects = 5;
				
				if (args.size() >= 2) {
					if (!std::holds_alternative<std::shared_ptr<Object>>(args[1])) throw std::runtime_error("fetch options must be object");
					auto opt = std::get<std::shared_ptr<Object>>(args[1]);
					auto itM = opt->find("method"); if (itM != opt->end()) method = toString(itM->second);
					auto itH = opt->find("headers"); if (itH != opt->end() && std::holds_alternative<std::shared_ptr<Object>>(itH->second)) hdrObj = std::get<std::shared_ptr<Object>>(itH->second);
					auto itB = opt->find("body"); if (itB != opt->end()) body = toString(itB->second);
					auto itR = opt->find("redirect"); 
					if (itR != opt->end()) {
						std::string redirectMode = toString(itR->second);
						if (redirectMode == "manual" || redirectMode == "error") followRedirects = false;
					}
					auto itMR = opt->find("maxRedirects");
					if (itMR != opt->end() && std::holds_alternative<double>(itMR->second)) {
						maxRedirects = static_cast<int>(std::get<double>(itMR->second));
					}
				}
				
				// Promise
				auto p = asyncPtr->createPromise();
				std::thread([interpPtr, asyncPtr, p, initialUrl, method, hdrObj, body, followRedirects, maxRedirects]{
					try {
						std::string currentUrl = initialUrl;
						int redirectCount = 0;
						std::string finalHeaders, finalBody;
						double finalStatus = 0.0;
						
						while (true) {
							// Parse URL
							std::string proto = "http"; std::string host; int port = 80; std::string path = "/";
							size_t schemePos = currentUrl.find("://"); 
							if (schemePos != std::string::npos) proto = currentUrl.substr(0, schemePos);
							size_t hostStart = (schemePos == std::string::npos) ? 0 : (schemePos + 3);
							size_t pathStart = currentUrl.find('/', hostStart); 
							if (pathStart == std::string::npos) pathStart = currentUrl.size();
							size_t colon = currentUrl.find(':', hostStart);
							if (colon != std::string::npos && colon < pathStart) { 
								host = currentUrl.substr(hostStart, colon - hostStart); 
								port = std::stoi(currentUrl.substr(colon + 1, pathStart - colon - 1)); 
							} else { 
								host = currentUrl.substr(hostStart, pathStart - hostStart); 
							}
							if (pathStart < currentUrl.size()) path = currentUrl.substr(pathStart);
							if (proto == "https") { asyncPtr->reject(p, Value{ std::string("HTTPS not supported") }); return; }
							
							// DNS
							struct hostent* server = gethostbyname(host.c_str());
							if (server == NULL) { asyncPtr->reject(p, Value{ std::string("No such host: ") + host }); return; }
							
							int sockfd = socket(AF_INET, SOCK_STREAM, 0); 
							if (sockfd < 0) { asyncPtr->reject(p, Value{ std::string("socket failed") }); return; }
							
							struct sockaddr_in serv_addr; 
							std::memset(&serv_addr, 0, sizeof(serv_addr));
							serv_addr.sin_family = AF_INET; 
							std::memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length); 
							serv_addr.sin_port = htons(port);
							
							if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) { 
								close(sockfd); 
								asyncPtr->reject(p, Value{ std::string("connect failed") }); 
								return; 
							}
							
							std::ostringstream req;
							req << method << " " << path << " HTTP/1.1\r\n";
							req << "Host: " << host << "\r\n";
							req << "Connection: close\r\n";
							req << "User-Agent: ALang/1.0\r\n";
							if (hdrObj) { for (auto& kv : *hdrObj) { req << kv.first << ": " << toString(kv.second) << "\r\n"; } }
							if (!body.empty()) { req << "Content-Length: " << body.size() << "\r\n"; }
							req << "\r\n"; 
							if (!body.empty()) req << body;
							
							std::string rs = req.str(); 
							if (write(sockfd, rs.c_str(), rs.size()) < 0) { 
								close(sockfd); 
								asyncPtr->reject(p, Value{ std::string("write failed") }); 
								return; 
							}
							
							std::string response; 
							char buf[4096]; 
							int n; 
							while ((n = read(sockfd, buf, sizeof(buf))) > 0) response.append(buf, n); 
							close(sockfd);
							
							// Parse response
							size_t headerEnd = response.find("\r\n\r\n"); 
							std::string headers, bodyStr; 
							double status = 0.0;
							if (headerEnd != std::string::npos) { 
								headers = response.substr(0, headerEnd); 
								bodyStr = response.substr(headerEnd + 4);
								size_t sp1 = headers.find(' '); 
								size_t sp2 = headers.find(' ', sp1 + 1);
								if (sp1 != std::string::npos && sp2 != std::string::npos) { 
									try { 
										status = std::stod(headers.substr(sp1 + 1, sp2 - sp1 - 1)); 
									} catch (...) {}
								}
							}
							
							// Check for redirect
							if (followRedirects && status >= 300 && status < 400) {
								if (redirectCount >= maxRedirects) {
									asyncPtr->reject(p, Value{ std::string("Too many redirects") });
									return;
								}
								
								// Find Location header
								std::string location;
								std::istringstream stream(headers);
								std::string line;
								std::getline(stream, line); // Skip status line
								while (std::getline(stream, line)) {
									if (!line.empty() && line.back() == '\r') line.pop_back();
									if (line.find("Location:") == 0 || line.find("location:") == 0) {
										size_t colonPos = line.find(':');
										if (colonPos != std::string::npos) {
											location = line.substr(colonPos + 1);
											// Trim spaces
											size_t start = location.find_first_not_of(" \t");
											if (start != std::string::npos) location = location.substr(start);
											size_t end = location.find_last_not_of(" \t\r\n");
											if (end != std::string::npos) location = location.substr(0, end + 1);
										}
										break;
									}
								}
								
								if (location.empty()) {
									asyncPtr->reject(p, Value{ std::string("Redirect without Location header") });
									return;
								}
								
								// Handle relative URLs
								if (location[0] == '/') {
									currentUrl = proto + "://" + host + (port != 80 ? ":" + std::to_string(port) : "") + location;
								} else if (location.find("://") == std::string::npos) {
									// Relative path
									size_t lastSlash = path.find_last_of('/');
									std::string basePath = (lastSlash != std::string::npos) ? path.substr(0, lastSlash + 1) : "/";
									currentUrl = proto + "://" + host + (port != 80 ? ":" + std::to_string(port) : "") + basePath + location;
								} else {
									currentUrl = location;
								}
								
								redirectCount++;
								continue; // Follow redirect
							}
							
							// No redirect or reached final destination
							finalHeaders = headers;
							finalBody = bodyStr;
							finalStatus = status;
							break;
						}
						
						auto respObj = std::make_shared<Object>();
						(*respObj)["status"] = Value{ finalStatus };
						(*respObj)["headers"] = Value{ finalHeaders };
						(*respObj)["redirected"] = Value{ redirectCount > 0 };
						(*respObj)["url"] = Value{ currentUrl };
						
						// text(): Promise<string>
						{
							auto textFn = std::make_shared<Function>(); textFn->isBuiltin = true;
							std::string copy = finalBody;
							textFn->builtin = [asyncPtr, copy](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
								auto tp = asyncPtr->createPromise(); asyncPtr->resolve(tp, Value{ copy }); return Value{ tp };
							};
							(*respObj)["text"] = Value{ textFn };
						}
						// json(): Promise<any>
						{
							auto jsonFn = std::make_shared<Function>(); jsonFn->isBuiltin = true; std::string copy = finalBody;
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
						asyncPtr->reject(p, Value{ std::string("Fetch exception: ") + ex.what() });
					} catch (...) {
						asyncPtr->reject(p, Value{ std::string("Fetch unknown exception") });
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

		auto putFn = std::make_shared<Function>();
		putFn->isBuiltin = true;
		putFn->builtin = [httpRequest](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() != 2) throw std::runtime_error("http.put expects 2 arguments (url, data)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("url must be string");
			return httpRequest("PUT", std::get<std::string>(args[0]), toString(args[1]));
		};
		(*netPkg)["put"] = Value{putFn};

		auto deleteFn = std::make_shared<Function>();
		deleteFn->isBuiltin = true;
		deleteFn->builtin = [httpRequest](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() != 1) throw std::runtime_error("http.delete expects 1 argument (url)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("url must be string");
			return httpRequest("DELETE", std::get<std::string>(args[0]), "");
		};
		(*netPkg)["delete"] = Value{deleteFn};

		auto patchFn = std::make_shared<Function>();
		patchFn->isBuiltin = true;
		patchFn->builtin = [httpRequest](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() != 2) throw std::runtime_error("http.patch expects 2 arguments (url, data)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("url must be string");
			return httpRequest("PATCH", std::get<std::string>(args[0]), toString(args[1]));
		};
		(*netPkg)["patch"] = Value{patchFn};

		auto headFn = std::make_shared<Function>();
		headFn->isBuiltin = true;
		headFn->builtin = [httpRequest](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() != 1) throw std::runtime_error("http.head expects 1 argument (url)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("url must be string");
			return httpRequest("HEAD", std::get<std::string>(args[0]), "");
		};
		(*netPkg)["head"] = Value{headFn};

		// Generic request function: request(method, url, data="")
		auto requestFn = std::make_shared<Function>();
		requestFn->isBuiltin = true;
		requestFn->builtin = [httpRequest](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() < 2) throw std::runtime_error("http.request expects at least 2 arguments (method, url)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("method must be string");
			if (!std::holds_alternative<std::string>(args[1])) throw std::runtime_error("url must be string");
			std::string data = args.size() >= 3 ? toString(args[2]) : "";
			return httpRequest(std::get<std::string>(args[0]), std::get<std::string>(args[1]), data);
		};
		(*netPkg)["request"] = Value{requestFn};

		// http sub-package with Server class
		{
			auto httpPkg = std::make_shared<Object>();

			// HTTP Server class
			auto serverClass = std::make_shared<ClassInfo>();
			serverClass->name = "Server";
			serverClass->isNative = true;

			// constructor()
			auto serverCtor = std::make_shared<Function>();
			serverCtor->isBuiltin = true;
			serverCtor->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
				Value thisVal = closure->get("this");
				auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
				if (ext) {
					ext->nativeHandle = nullptr; // Will be set in listen()
				}
				return Value{std::monostate{}};
			};
			serverClass->methods["constructor"] = serverCtor;

			// listen(port, callback) - starts HTTP server
			auto listenFn = std::make_shared<Function>();
			listenFn->isBuiltin = true;
			listenFn->builtin = [asyncPtr, interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
				if (args.size() < 2) throw std::runtime_error("Server.listen expects port and callback");
				int port = static_cast<int>(getNumber(args[0], "port"));
				if (!std::holds_alternative<std::shared_ptr<Function>>(args[1])) {
					throw std::runtime_error("Server.listen callback must be a function");
				}
				auto callback = std::get<std::shared_ptr<Function>>(args[1]);

				Value thisVal = closure->get("this");
				auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);

				// Create server socket
				int serverFd = socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
				if (serverFd == INVALID_SOCKET) throw std::runtime_error("Failed to create server socket");
				
				char opt = 1;
				setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#else
				if (serverFd < 0) throw std::runtime_error("Failed to create server socket");
				
				int opt = 1;
				setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

				struct sockaddr_in addr;
				std::memset(&addr, 0, sizeof(addr));
				addr.sin_family = AF_INET;
				addr.sin_addr.s_addr = INADDR_ANY;
				addr.sin_port = htons(port);

				if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
					close(serverFd);
					throw std::runtime_error("Failed to bind server socket");
				}

				if (listen(serverFd, 10) < 0) {
					close(serverFd);
					throw std::runtime_error("Failed to listen on server socket");
				}

				if (ext) {
					ext->nativeHandle = new int(serverFd);
					ext->nativeDestructor = [](void* p) {
						int* fdp = static_cast<int*>(p);
						close(*fdp);
						delete fdp;
					};
				}

				// Start accepting connections in background thread
				std::thread([serverFd, callback, asyncPtr, interpPtr]() {
					try {
						while (true) {
							struct sockaddr_in clientAddr;
							socklen_t clientLen = sizeof(clientAddr);
							int clientFd = accept(serverFd, (struct sockaddr*)&clientAddr, &clientLen);
							if (clientFd < 0) break; // Server closed

							// Handle each connection in its own thread
							std::thread([clientFd, callback, asyncPtr, interpPtr]() {
								try {
									// Read HTTP request
									std::string requestData;
									char buf[4096];
									ssize_t n = read(clientFd, buf, sizeof(buf) - 1);
									if (n > 0) {
										buf[n] = '\0';
										requestData = std::string(buf, n);
									}

									// Parse request line
									std::string method, url, version;
									std::istringstream reqStream(requestData);
									reqStream >> method >> url >> version;

									// Parse headers and body from request
									std::string headersSection;
									std::string body;
									size_t headerEndPos = requestData.find("\r\n\r\n");
									if (headerEndPos != std::string::npos) {
										headersSection = requestData.substr(0, headerEndPos);
										if (headerEndPos + 4 < requestData.size()) {
											body = requestData.substr(headerEndPos + 4);
										}
									} else {
										headersSection = requestData;
									}

									// Create request object
									auto reqObj = std::make_shared<Object>();
									(*reqObj)["method"] = Value{method};
									(*reqObj)["url"] = Value{url};
									(*reqObj)["version"] = Value{version};
									(*reqObj)["headers"] = Value{headersSection};
									(*reqObj)["body"] = Value{body};

									// Create response object
									auto resObj = std::make_shared<Object>();
									int* clientFdPtr = new int(clientFd);

							// res.writeHead(statusCode, headers)
							auto writeHeadFn = std::make_shared<Function>();
							writeHeadFn->isBuiltin = true;
							auto statusCodePtr = std::make_shared<int>(200);
							auto headersStrPtr = std::make_shared<std::string>();
							writeHeadFn->builtin = [statusCodePtr, headersStrPtr](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
								if (args.size() >= 1) {
									*statusCodePtr = static_cast<int>(getNumber(args[0], "statusCode"));
								}
								if (args.size() >= 2 && std::holds_alternative<std::shared_ptr<Object>>(args[1])) {
									auto hdrs = std::get<std::shared_ptr<Object>>(args[1]);
									std::ostringstream oss;
									for (auto& kv : *hdrs) {
										oss << kv.first << ": " << toString(kv.second) << "\r\n";
									}
									*headersStrPtr = oss.str();
								}
								return Value{std::monostate{}};
							};
							(*resObj)["writeHead"] = Value{writeHeadFn};

							// res.end(body) - send response
							auto endFn = std::make_shared<Function>();
							endFn->isBuiltin = true;
							endFn->builtin = [clientFdPtr, statusCodePtr, headersStrPtr](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
								std::string body;
								if (!args.empty()) {
									body = toString(args[0]);
								}

								// Get status text using helper function
								std::string statusText = getHttpStatusText(*statusCodePtr);

								std::ostringstream response;
								response << "HTTP/1.1 " << *statusCodePtr << " " << statusText << "\r\n";
								response << *headersStrPtr;
								response << "Content-Length: " << body.size() << "\r\n";
								response << "Connection: close\r\n";
								response << "\r\n";
								response << body;

								std::string respStr = response.str();
								ssize_t written = write(*clientFdPtr, respStr.c_str(), respStr.size());
								close(*clientFdPtr);
								delete clientFdPtr;

								if (written < 0) {
									auto errObj = std::make_shared<Object>();
									(*errObj)["message"] = Value{std::string("Failed to send response")};
									return Value{errObj};
								}
								return Value{std::monostate{}};
							};
							(*resObj)["end"] = Value{endFn};

							// Call the callback with req, res
							asyncPtr->postTask([callback, reqObj, resObj, interpPtr]() {
								try {
									std::vector<Value> callArgs = {Value{reqObj}, Value{resObj}};
									if (callback->isBuiltin) {
										callback->builtin(callArgs, callback->closure);
									} else {
										auto local = std::make_shared<Environment>(callback->closure);
										if (callback->params.size() > 0) local->define(callback->params[0], callArgs[0]);
										if (callback->params.size() > 1) local->define(callback->params[1], callArgs[1]);
										try {
											interpPtr->executeBlock(callback->body, local);
										} catch (const ReturnSignal&) {}
									}
								} catch (const std::exception& ex) {
									std::cerr << "HTTP Server callback error: " << ex.what() << std::endl;
								}
							});
						} catch (const std::exception& ex) {
							std::cerr << "HTTP Server connection handler exception: " << ex.what() << std::endl;
							close(clientFd);
						} catch (...) {
							std::cerr << "HTTP Server connection handler unknown exception" << std::endl;
							close(clientFd);
						}
					}).detach();
				}
			} catch (const std::exception& ex) {
				std::cerr << "HTTP Server accept loop exception: " << ex.what() << std::endl;
			} catch (...) {
				std::cerr << "HTTP Server accept loop unknown exception" << std::endl;
			}
		}).detach();

				return Value{std::monostate{}};
			};
			serverClass->methods["listen"] = listenFn;

			// close() - stop the server
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
			serverClass->methods["close"] = closeFn;

			(*httpPkg)["Server"] = Value{serverClass};

			// HTTP Status Codes
			auto statusObj = std::make_shared<Object>();
			// 1xx Informational
			(*statusObj)["CONTINUE"] = Value{100.0};
			(*statusObj)["SWITCHING_PROTOCOLS"] = Value{101.0};
			// 2xx Success
			(*statusObj)["OK"] = Value{200.0};
			(*statusObj)["CREATED"] = Value{201.0};
			(*statusObj)["ACCEPTED"] = Value{202.0};
			(*statusObj)["NO_CONTENT"] = Value{204.0};
			// 3xx Redirection
			(*statusObj)["MOVED_PERMANENTLY"] = Value{301.0};
			(*statusObj)["FOUND"] = Value{302.0};
			(*statusObj)["SEE_OTHER"] = Value{303.0};
			(*statusObj)["NOT_MODIFIED"] = Value{304.0};
			(*statusObj)["TEMPORARY_REDIRECT"] = Value{307.0};
			(*statusObj)["PERMANENT_REDIRECT"] = Value{308.0};
			// 4xx Client Errors
			(*statusObj)["BAD_REQUEST"] = Value{400.0};
			(*statusObj)["UNAUTHORIZED"] = Value{401.0};
			(*statusObj)["FORBIDDEN"] = Value{403.0};
			(*statusObj)["NOT_FOUND"] = Value{404.0};
			(*statusObj)["METHOD_NOT_ALLOWED"] = Value{405.0};
			(*statusObj)["NOT_ACCEPTABLE"] = Value{406.0};
			(*statusObj)["CONFLICT"] = Value{409.0};
			(*statusObj)["GONE"] = Value{410.0};
			(*statusObj)["UNPROCESSABLE_ENTITY"] = Value{422.0};
			(*statusObj)["TOO_MANY_REQUESTS"] = Value{429.0};
			// 5xx Server Errors
			(*statusObj)["INTERNAL_SERVER_ERROR"] = Value{500.0};
			(*statusObj)["NOT_IMPLEMENTED"] = Value{501.0};
			(*statusObj)["BAD_GATEWAY"] = Value{502.0};
			(*statusObj)["SERVICE_UNAVAILABLE"] = Value{503.0};
			(*statusObj)["GATEWAY_TIMEOUT"] = Value{504.0};
			(*httpPkg)["status"] = Value{statusObj};

			// Helper function to get status text from code
			auto getStatusTextFn = std::make_shared<Function>();
			getStatusTextFn->isBuiltin = true;
			getStatusTextFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
				if (args.empty()) throw std::runtime_error("getStatusText expects 1 argument (status code)");
				int code = static_cast<int>(getNumber(args[0], "status code"));
				return Value{getHttpStatusText(code)};
			};
			(*httpPkg)["getStatusText"] = Value{getStatusTextFn};

			(*netPkg)["http"] = Value{httpPkg};
		}
	});
}

} // namespace asul
