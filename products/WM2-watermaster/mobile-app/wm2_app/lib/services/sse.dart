// Server-sent events: the browser's EventSource on the web (the session
// cookie goes with it), a streamed HTTP request with a bearer token elsewhere.
export 'sse_io.dart' if (dart.library.js_interop) 'sse_web.dart';
