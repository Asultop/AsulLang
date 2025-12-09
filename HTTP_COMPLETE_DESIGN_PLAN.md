# ALang Complete HTTP Design Plan
# ALang完整HTTP设计方案

## Overview / 概述

This document outlines a comprehensive HTTP enhancement plan for ALang, covering HTTPS support, request control, connection management, routing systems, and middleware architecture to address current implementation limitations.

本文档概述了ALang的全面HTTP增强方案，涵盖HTTPS支持、请求控制、连接管理、路由系统和中间件架构，以解决当前实现的核心限制。

---

## Phase 1: Enhanced HTTP Client Core Features
## 第一阶段：增强HTTP客户端核心功能

### 1.1 Request Timeout Control / 超时控制

**Implementation / 实现：**
- Add `timeout` option to `fetch()` function
- Use `select()` or `poll()` for timeout enforcement on socket operations
- Configurable default timeout (30s) with per-request override

**API Design / API设计：**
```alang
// Basic timeout
let res = await fetch("http://example.com", {
    timeout: 5000  // milliseconds
});

// Global timeout configuration
std.network.setDefaultTimeout(10000);
```

**File Changes / 文件变更：**
- `src/AsulPackages/Std/Network/StdNetwork.cpp`: Extend fetch function

### 1.2 Automatic Redirect Handling / 自动重定向

**Implementation / 实现：**
- Parse Location header from 3xx responses
- Follow redirects up to configurable limit (default: 5)
- Track redirect chain in response object
- Support both absolute and relative redirect URLs

**API Design / API设计：**
```alang
let res = await fetch("http://example.com", {
    redirect: "follow",    // "follow", "manual", "error"
    maxRedirects: 5
});

// Access redirect information
println(res.redirected);  // true/false
println(res.url);         // final URL after redirects
```

**File Changes / 文件变更：**
- `src/AsulPackages/Std/Network/StdNetwork.cpp`: Add redirect logic in fetch

### 1.3 Retry Mechanism / 重试机制

**Implementation / 实现：**
- Exponential backoff for retries
- Configurable retry conditions (network errors, 5xx responses)
- Per-request retry configuration

**API Design / API设计：**
```alang
let res = await fetch("http://example.com", {
    retry: {
        count: 3,
        delay: 1000,        // initial delay
        backoff: 2.0,       // exponential multiplier
        on: ["network", "5xx"]
    }
});
```

### 1.4 Request Cancellation / 请求取消

**Implementation / 实现：**
- AbortController class for cancellation signal
- Cancel in-flight requests
- Cleanup resources on abort

**API Design / API设计：**
```alang
let controller = new std.network.AbortController();
let signal = controller.signal;

let promise = fetch("http://example.com", { signal: signal });

// Cancel after 2 seconds
await sleep(2000);
controller.abort();

try {
    await promise;
} catch (e) {
    println("Request aborted: " + e.message);
}
```

**File Changes / 文件变更：**
- `src/AsulPackages/Std/Network/StdNetwork.cpp`: Add AbortController class
- Thread-safe abort signaling mechanism

---

## Phase 2: HTTPS/TLS Support
## 第二阶段：HTTPS/TLS支持

### 2.1 TLSSocket Class / TLS套接字类

**Implementation / 实现：**
- Wrapper around OpenSSL for TLS connections
- Certificate verification with CA bundle
- SNI (Server Name Indication) support
- Support for TLS 1.2 and 1.3

**API Design / API设计：**
```alang
// Automatic HTTPS detection in fetch
let res = await fetch("https://example.com");

// Manual TLS socket (advanced)
let tlsSocket = new std.network.TLSSocket();
await tlsSocket.connect("example.com", 443);
await tlsSocket.write("GET / HTTP/1.1\r\n\r\n");
let data = await tlsSocket.read();
```

**File Changes / 文件变更：**
- New file: `src/AsulPackages/Std/Network/TLSSocket.cpp`
- New file: `src/AsulPackages/Std/Network/TLSSocket.h`
- `CMakeLists.txt`: Add OpenSSL detection and conditional compilation
- `src/AsulPackages/Std/Network/StdNetwork.cpp`: Integrate TLSSocket into fetch

### 2.2 OpenSSL Integration Strategy / OpenSSL集成策略

**Approach / 方法：**
- **Optional Compilation**: Default approach
  - If OpenSSL not found, compile without HTTPS
  - Runtime check: `std.network.hasHTTPS()` returns false
  - Throw clear error when attempting HTTPS without SSL
  
**CMake Configuration / CMake配置：**
```cmake
find_package(OpenSSL)
if(OPENSSL_FOUND)
    target_link_libraries(alang PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(alang PRIVATE ASUL_HAS_OPENSSL=1)
else()
    message(WARNING "OpenSSL not found - HTTPS support disabled")
endif()
```

### 2.3 Certificate Verification / 证书验证

**Implementation / 实现：**
- Load system CA certificates
- Optional certificate pinning
- Configurable verification levels

**API Design / API设计：**
```alang
let res = await fetch("https://example.com", {
    tls: {
        verify: true,           // default
        caFile: "/path/to/ca.pem",
        allowInsecure: false    // for testing only
    }
});
```

---

## Phase 3: Enhanced Response Object
## 第三阶段：增强响应对象

### 3.1 Parsed Headers Object / 解析的头对象

**Implementation / 实现：**
- Convert headers string to structured Object
- Case-insensitive header access via `get()` method
- Support for multi-value headers

**API Design / API设计：**
```alang
let res = await fetch("http://example.com");

// Structured header access
let contentType = res.headers.get("Content-Type");
let setCookies = res.headers.getAll("Set-Cookie");  // array

// Iterate headers
res.headers.forEach([](key, value) {
    println(key + ": " + value);
});
```

**File Changes / 文件变更：**
- `src/AsulPackages/Std/Network/StdNetwork.cpp`: Create Headers class

### 3.2 Additional Response Properties / 额外响应属性

**API Design / API设计：**
```alang
let res = await fetch("http://example.com");

println(res.status);        // 200
println(res.statusText);    // "OK"
println(res.ok);            // true (status 200-299)
println(res.redirected);    // false
println(res.url);           // final URL
println(res.type);          // "basic", "cors", "error"
```

### 3.3 Binary Data Support / 二进制数据支持

**Implementation / 实现：**
- Add `blob()` method for binary data
- Byte array support in ALang runtime

**API Design / API设计：**
```alang
let res = await fetch("http://example.com/image.png");
let blob = await res.blob();           // byte array
let text = await res.text();           // string
let json = await res.json();           // parsed JSON
```

---

## Phase 4: Server Routing System
## 第四阶段：服务器路由系统

### 4.1 Route Registration / 路由注册

**Implementation / 实现：**
- Method-specific route handlers
- Pattern matching for paths
- Route parameter extraction

**API Design / API设计：**
```alang
import std.network;

let server = new std.network.http.Server();

// Route registration
server.get("/users", [](req, res) {
    res.json([{id: 1, name: "John"}]);
});

server.post("/users", [](req, res) {
    let user = req.body;  // auto-parsed JSON
    res.status(201).json({id: 2, ...user});
});

server.put("/users/:id", [](req, res) {
    let userId = req.params.id;  // path parameter
    res.json({id: userId, updated: true});
});

server.delete("/users/:id", [](req, res) {
    res.status(204).send();
});

server.listen(8080);
```

### 4.2 Path Parameter Parsing / 路径参数解析

**Implementation / 实现：**
- Support `:param` syntax for dynamic segments
- Optional parameters with `?`
- Wildcard routes with `*`

**API Design / API设计：**
```alang
// Dynamic segments
server.get("/users/:id/posts/:postId", [](req, res) {
    let userId = req.params.id;
    let postId = req.params.postId;
    res.json({userId: userId, postId: postId});
});

// Optional parameters
server.get("/files/:path*", [](req, res) {
    let path = req.params.path;  // captures everything
    res.send("Path: " + path);
});
```

### 4.3 Request Body Parsing / 请求体解析

**Implementation / 实现：**
- Automatic JSON parsing for `application/json`
- Form data parsing for `application/x-www-form-urlencoded`
- Multipart form data support (future)

**API Design / API设计：**
```alang
server.post("/api/data", [](req, res) {
    // Automatic parsing based on Content-Type
    if (req.is("json")) {
        let data = req.body;  // parsed JSON object
        res.json({received: data});
    } else if (req.is("form")) {
        let formData = req.body;  // parsed form data
        res.send("Form received");
    }
});
```

**File Changes / 文件变更：**
- `src/AsulPackages/Std/Network/StdNetwork.cpp`: Add Router class
- `src/AsulPackages/Std/Network/StdNetwork.cpp`: Enhance Server class

---

## Phase 5: Middleware Architecture
## 第五阶段：中间件架构

### 5.1 Middleware Chain / 中间件链

**Implementation / 实现：**
- Middleware registration via `use()`
- Execution order: registration order
- `next()` callback for control flow

**API Design / API设计：**
```alang
let server = new std.network.http.Server();

// Global middleware
server.use([](req, res, next) {
    println("Request: " + req.method + " " + req.url);
    next();  // pass to next middleware
});

// Route-specific middleware
let authMiddleware = [](req, res, next) {
    if (req.headers.get("Authorization")) {
        next();
    } else {
        res.status(401).send("Unauthorized");
    }
};

server.get("/protected", authMiddleware, [](req, res) {
    res.send("Protected resource");
});
```

### 5.2 Built-in Middleware / 内置中间件

**Logger Middleware / 日志中间件：**
```alang
import std.network.middleware;

server.use(std.network.middleware.logger({
    format: ":method :url :status :response-time ms"
}));
```

**CORS Middleware / CORS中间件：**
```alang
server.use(std.network.middleware.cors({
    origin: "*",
    methods: ["GET", "POST", "PUT", "DELETE"],
    allowedHeaders: ["Content-Type", "Authorization"]
}));
```

**Rate Limiting Middleware / 限流中间件：**
```alang
server.use(std.network.middleware.rateLimit({
    windowMs: 60000,      // 1 minute
    max: 100,             // max requests per window
    message: "Too many requests"
}));
```

**Body Parser Middleware / 请求体解析中间件：**
```alang
server.use(std.network.middleware.json());        // parse JSON
server.use(std.network.middleware.urlencoded());  // parse forms
```

**File Changes / 文件变更：**
- New file: `src/AsulPackages/Std/Network/Middleware.cpp`
- New file: `src/AsulPackages/Std/Network/Middleware.h`
- Built-in middleware implementations

### 5.3 Error Handling Middleware / 错误处理中间件

**API Design / API设计：**
```alang
// Error handling middleware (last in chain)
server.use([](err, req, res, next) {
    println("Error: " + err.message);
    res.status(500).json({
        error: err.message,
        stack: err.stack
    });
});
```

---

## Phase 6: Connection Pool and Resource Management
## 第六阶段：连接池和资源管理

### 6.1 HTTP Connection Pool / HTTP连接池

**Implementation / 实现：**
- Keep-Alive connection reuse
- Per-host connection pooling
- Configurable pool size and timeout

**API Design / API设计：**
```alang
// Connection pool is automatic
let res1 = await fetch("http://example.com/api/1");
let res2 = await fetch("http://example.com/api/2");  // reuses connection

// Configure pool globally
std.network.setConnectionPool({
    maxConnectionsPerHost: 6,
    keepAliveTimeout: 60000,
    maxIdleConnections: 100
});
```

**File Changes / 文件变更：**
- New file: `src/AsulPackages/Std/Network/ConnectionPool.cpp`
- New file: `src/AsulPackages/Std/Network/ConnectionPool.h`

### 6.2 Resource Cleanup / 资源清理

**Implementation / 实现：**
- RAII pattern for socket cleanup
- Automatic cleanup on errors
- Proper cleanup on server shutdown

**File Changes / 文件变更：**
- Update `InstanceExt` destructor handling
- Add connection tracking and cleanup

---

## Implementation Roadmap
## 实施路线图

### Milestone 1: Client Enhancements (2-3 weeks)
- [ ] Timeout control
- [ ] Redirect handling
- [ ] Retry mechanism
- [ ] AbortController

### Milestone 2: HTTPS Support (2-3 weeks)
- [ ] TLSSocket class with OpenSSL
- [ ] CMake OpenSSL integration
- [ ] Certificate verification
- [ ] HTTPS in fetch()

### Milestone 3: Enhanced Response (1 week)
- [ ] Headers class with get()/getAll()
- [ ] Additional response properties
- [ ] Binary data support (blob)

### Milestone 4: Server Routing (2-3 weeks)
- [ ] Route registration (get/post/put/delete)
- [ ] Path parameter parsing
- [ ] Request body auto-parsing
- [ ] Enhanced request/response objects

### Milestone 5: Middleware (2 weeks)
- [ ] Middleware chain with next()
- [ ] Logger middleware
- [ ] CORS middleware
- [ ] Rate limiting middleware
- [ ] Error handling

### Milestone 6: Connection Management (1-2 weeks)
- [ ] Connection pool for Keep-Alive
- [ ] Resource cleanup improvements
- [ ] Performance optimization

**Total Estimated Time: 10-14 weeks**

---

## Compatibility Strategy
## 兼容性策略

### Backward Compatibility / 向后兼容
- All existing APIs remain functional
- New features via optional parameters
- No breaking changes to current code

### Feature Detection / 特性检测
```alang
// Check feature availability
if (std.network.hasHTTPS()) {
    // Use HTTPS
}

if (std.network.hasConnectionPool()) {
    // Configure pool
}
```

---

## Testing Strategy
## 测试策略

### Unit Tests / 单元测试
- Test each component independently
- Mock external dependencies
- High code coverage (>80%)

### Integration Tests / 集成测试
- End-to-end request/response tests
- Multi-threaded server tests
- Connection pool stress tests

### Example Scripts / 示例脚本
- Client examples for each feature
- Server examples with routing and middleware
- Real-world use cases

---

## Security Considerations
## 安全考虑

1. **TLS/SSL**: Proper certificate validation by default
2. **Input Validation**: Sanitize all user inputs
3. **Resource Limits**: Prevent DoS via connection/request limits
4. **Header Injection**: Validate header values
5. **Path Traversal**: Sanitize file paths in routing

---

## Performance Goals
## 性能目标

- **Latency**: <1ms overhead for routing and middleware
- **Throughput**: Handle 10,000+ req/s on modern hardware
- **Memory**: <100MB for connection pool with 1000 connections
- **CPU**: Efficient event loop, minimal blocking

---

## Dependencies
## 依赖关系

### Required / 必需
- C++17 compiler
- CMake 3.15+
- Platform sockets (POSIX/Winsock)

### Optional / 可选
- OpenSSL 1.1.1+ (for HTTPS)
- Threading library (already present)

---

## Open Questions
## 待解决问题

1. **OpenSSL Dependency Strategy?**
   - ✅ Recommended: Optional compilation (no OpenSSL = no HTTPS, with warning)
   - Alternative: Provide alternative TLS library option (mbedTLS)
   - Not recommended: Force dependency (reduces portability)

2. **API Compatibility?**
   - ✅ Recommended: Keep existing fetch() fully compatible, new features via options
   - Alternative: Introduce fetch2() for new API
   
3. **WebSocket Priority?**
   - ✅ Recommended: Separate task after HTTP enhancements complete
   - Alternative: Implement alongside HTTP (increases complexity)

4. **Async Model?**
   - Current: Promise-based with event loop
   - ✅ Keep current model, optimize performance

---

## Conclusion
## 结论

This comprehensive plan addresses all core limitations of the current HTTP implementation while maintaining backward compatibility and following best practices for web standards. The phased approach allows for incremental development and testing, with each milestone delivering tangible value.

Implementation should prioritize:
1. Client enhancements (most requested)
2. HTTPS support (critical for production)
3. Server routing (high value for web apps)
4. Middleware architecture (enables extensibility)
5. Connection pooling (performance optimization)

本综合方案解决了当前HTTP实现的所有核心限制，同时保持向后兼容性并遵循Web标准的最佳实践。分阶段的方法允许增量开发和测试，每个里程碑都能提供实际价值。
