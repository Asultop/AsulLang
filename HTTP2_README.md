# HTTP/2 Support in AsulLang

## Overview

AsulLang's `std.network` module now supports HTTP/2 protocol through integration with libcurl. This provides modern, efficient HTTP communication with automatic protocol negotiation.

## Features

- **HTTP/2 by Default**: The `fetch()` function now uses HTTP/2 when available
- **Automatic Fallback**: Gracefully falls back to HTTP/1.1 if HTTP/2 is not supported
- **Protocol Detection**: Response includes a `version` field indicating the actual protocol used
- **Explicit Control**: Option to force HTTP/1.1 or HTTP/2
- **Full Method Support**: GET, POST, PUT, DELETE, PATCH, HEAD with HTTP/2
- **Custom Headers**: Full support for custom headers in HTTP/2 requests
- **Redirect Handling**: Automatic redirect following with HTTP/2
- **Backward Compatibility**: Original socket-based implementation available as `fetchLegacy()`

## Requirements

- libcurl with HTTP/2 support (libnghttp2)
- OpenSSL for HTTPS connections

## Usage

### Basic HTTP/2 Request

```javascript
import std.network;

// HTTP/2 is used by default
let res = await std.network.fetch("https://example.com");
println("Status: " + res.status);
println("Protocol: " + res.version);  // "HTTP/2" or "HTTP/1.1"

let body = await res.text();
println("Body: " + body);
```

### Force HTTP/1.1

```javascript
let res = await std.network.fetch("https://example.com", {
    http2: false
});
println("Protocol: " + res.version);  // Will be "HTTP/1.1" or "HTTP/1.0"
```

### POST Request with HTTP/2

```javascript
let res = await std.network.fetch("https://api.example.com/data", {
    method: "POST",
    headers: {
        "Content-Type": "application/json"
    },
    body: '{"key": "value"}'
});

println("Status: " + res.status);
println("Protocol: " + res.version);
```

### Custom Headers

```javascript
let res = await std.network.fetch("https://api.example.com", {
    headers: {
        "User-Agent": "AsulLang/2.0",
        "Authorization": "Bearer token123",
        "X-Custom-Header": "value"
    }
});
```

### All HTTP Methods

```javascript
// GET (default)
let res = await std.network.fetch("https://example.com");

// POST
let res = await std.network.fetch("https://example.com", {
    method: "POST",
    body: "data"
});

// PUT
let res = await std.network.fetch("https://example.com", {
    method: "PUT",
    body: "updated data"
});

// DELETE
let res = await std.network.fetch("https://example.com", {
    method: "DELETE"
});

// PATCH
let res = await std.network.fetch("https://example.com", {
    method: "PATCH",
    body: "partial update"
});

// HEAD
let res = await std.network.fetch("https://example.com", {
    method: "HEAD"
});
```

## Response Object

The response object returned by `fetch()` includes:

- `status`: HTTP status code (e.g., 200, 404)
- `version`: Protocol version used (e.g., "HTTP/2", "HTTP/1.1", "HTTP/1.0")
- `headers`: Response headers as string
- `url`: Final URL (after redirects)
- `redirected`: Boolean indicating if redirects occurred
- `text()`: Function returning Promise<string> with response body
- `json()`: Function returning Promise<any> with parsed JSON

## Options

The `fetch()` function accepts an options object with:

- `method`: HTTP method (default: "GET")
- `headers`: Object with custom headers
- `body`: Request body (for POST, PUT, PATCH)
- `http2`: Boolean to enable/disable HTTP/2 (default: true)
- `redirect`: "follow" or "manual"/"error" (default: "follow")
- `maxRedirects`: Maximum redirects to follow (default: 5)

## Backward Compatibility

The original socket-based HTTP implementation is still available:

```javascript
// Use old socket-based implementation
let res = await std.network.fetchLegacy("http://example.com");
```

The simple `get()`, `post()`, etc. functions also continue to work with the original implementation.

## Examples

See the following example files in the `Example/` directory:

- `http2_basic_test.alang`: Basic feature demonstration
- `http2_simple_test.alang`: Feature verification without network
- `http2_test.alang`: Full test suite with real HTTP/2 servers
- `http2_live_test.alang`: Live testing examples

## HTTP/2 Benefits

- **Multiplexing**: Multiple requests over single connection
- **Header Compression**: Reduced overhead with HPACK
- **Binary Protocol**: More efficient than text-based HTTP/1.1
- **Server Push**: Server can proactively send resources (when supported)
- **Better Performance**: Faster page loads and reduced latency

## Implementation Details

- Uses libcurl for HTTP/2 support
- Automatic protocol negotiation via ALPN
- Thread-safe implementation
- Async/Promise-based API
- Full error handling and recovery

## Troubleshooting

### "CURL error" messages

Ensure libcurl is installed with HTTP/2 support:
```bash
curl --version | grep HTTP2
```

### Protocol is HTTP/1.1 instead of HTTP/2

- Server may not support HTTP/2
- Connection may be using HTTP (not HTTPS)
- libcurl may not have HTTP/2 support enabled

### Build errors

Ensure development headers are installed:
```bash
# Ubuntu/Debian
sudo apt-get install libcurl4-openssl-dev

# macOS
brew install curl
```

## License

Same as AsulLang project (MIT License).
