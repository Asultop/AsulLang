# HTTP Design Extensions for ALang

This document describes the HTTP functionality extensions added to the `std.network` package.

## Overview

The HTTP design has been expanded with comprehensive support for modern HTTP client and server operations, including:

1. Additional HTTP methods (PUT, DELETE, PATCH, HEAD)
2. HTTP status code constants
3. Query parameter parsing
4. Header parsing utilities
5. Enhanced server request/response handling

## API Reference

### HTTP Methods

#### GET Request
```alang
import std.network;
let response = std.network.get("http://example.com/api/data");
println(response.status);  // 200
println(response.body);    // Response body
```

#### POST Request
```alang
let response = std.network.post("http://example.com/api/data", "key=value");
```

#### PUT Request
```alang
let response = std.network.put("http://example.com/api/data/1", "updated=true");
```

#### DELETE Request
```alang
let response = std.network.delete("http://example.com/api/data/1");
```

#### PATCH Request
```alang
let response = std.network.patch("http://example.com/api/data/1", "field=newvalue");
```

#### HEAD Request
```alang
let response = std.network.head("http://example.com/api/data");
```

#### Generic Request
```alang
let response = std.network.request("OPTIONS", "http://example.com/api/data");
```

### HTTP Status Codes

Access standard HTTP status codes through `std.network.http.status`:

```alang
import std.network;

// 2xx Success
std.network.http.status.OK                    // 200
std.network.http.status.CREATED               // 201
std.network.http.status.ACCEPTED              // 202
std.network.http.status.NO_CONTENT            // 204

// 3xx Redirection
std.network.http.status.MOVED_PERMANENTLY     // 301
std.network.http.status.FOUND                 // 302
std.network.http.status.NOT_MODIFIED          // 304

// 4xx Client Errors
std.network.http.status.BAD_REQUEST           // 400
std.network.http.status.UNAUTHORIZED          // 401
std.network.http.status.FORBIDDEN             // 403
std.network.http.status.NOT_FOUND             // 404
std.network.http.status.METHOD_NOT_ALLOWED    // 405

// 5xx Server Errors
std.network.http.status.INTERNAL_SERVER_ERROR // 500
std.network.http.status.NOT_IMPLEMENTED       // 501
std.network.http.status.BAD_GATEWAY           // 502
std.network.http.status.SERVICE_UNAVAILABLE   // 503
```

### Status Text Helper

Convert status codes to human-readable text:

```alang
let text = std.network.http.getStatusText(200);  // "OK"
let text = std.network.http.getStatusText(404);  // "Not Found"
let text = std.network.http.getStatusText(500);  // "Internal Server Error"
```

### URL Class

Parse and work with URLs:

```alang
let url = new std.network.URL("http://example.com:8080/api/users?name=john&age=30");

println(url.protocol);  // "http"
println(url.host);      // "example.com"
println(url.port);      // 8080
println(url.path);      // "/api/users"
println(url.query);     // "name=john&age=30"

// Parse query parameters
let params = url.parseQuery();
println(params.name);   // "john"
println(params.age);    // "30"
```

### Header Parsing

Parse HTTP headers from a response:

```alang
let headerStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 1234\r\n\r\n";
let headers = std.network.parseHeaders(headerStr);

println(headers["Content-Type"]);    // "text/html"
println(headers["Content-Length"]);  // "1234"
```

### HTTP Server

Create an HTTP server with enhanced request/response handling:

```alang
import std.network;

let server = new std.network.http.Server();

server.listen(8080, [](req, res) {
    // Request object fields
    println(req.method);   // "GET", "POST", etc.
    println(req.url);      // "/api/data"
    println(req.version);  // "HTTP/1.1"
    println(req.headers);  // Raw header string
    println(req.body);     // Request body
    
    // Handle different routes
    if (req.url == "/hello") {
        res.writeHead(std.network.http.status.OK, {
            "Content-Type": "text/plain",
            "X-Custom-Header": "Value"
        });
        res.end("Hello, World!");
        return;
    }
    
    // Default 404
    res.writeHead(std.network.http.status.NOT_FOUND, {
        "Content-Type": "text/plain"
    });
    res.end("Not Found");
});

// Keep server alive
await sleep(60000);
```

## Examples

See the following example files for complete demonstrations:

- `Example/http_methods_test.alang` - Tests all new HTTP methods and utilities
- `Example/http_server_enhanced_test.alang` - Enhanced server with multiple routes
- `Example/http_client_enhanced_test.alang` - Client testing various endpoints
- `Example/http_enhanced_integration_test.alang` - Full integration test

## Response Object Format

All HTTP request methods return a response object with the following fields:

- `status` (number): HTTP status code (e.g., 200, 404, 500)
- `headers` (string): Raw HTTP headers
- `body` (string): Response body

## Implementation Notes

1. **Blocking vs Async**: The basic HTTP methods (`get`, `post`, etc.) are blocking. For async operations, use the `fetch()` function which returns a Promise.

2. **HTTPS Support**: Currently, only HTTP is supported. HTTPS support requires additional SSL/TLS libraries.

3. **Timeout**: Request timeout support is planned for future releases.

4. **Error Handling**: All HTTP methods throw exceptions on network errors. Use try-catch blocks for error handling.

## Future Enhancements

Planned features for future releases:

- Request timeout configuration
- Cookie parsing and management
- Automatic redirect following
- HTTPS/TLS support
- Request/response streaming
- Multipart form data support
