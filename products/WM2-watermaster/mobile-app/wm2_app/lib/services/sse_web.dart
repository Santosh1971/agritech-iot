import 'dart:js_interop';

import 'package:web/web.dart' as web;

class SseConnection {
  SseConnection(this._es);
  final web.EventSource _es;
  void close() => _es.close();
}

/// Browser EventSource (same site, so the session cookie is sent). The
/// browser reconnects by itself after a drop; we report the error so the
/// app shows "reconnecting", and the next event marks it connected again.
SseConnection openSse(String url, Map<String, String> headers, void Function(String event, String data) onEvent,
    void Function(Object err) onError) {
  final es = web.EventSource(url, web.EventSourceInit(withCredentials: true));
  for (final name in ['status', 'state', 'event']) {
    es.addEventListener(
        name,
        ((web.Event e) {
          final m = e as web.MessageEvent;
          onEvent(name, (m.data as JSString).toDart);
        }).toJS);
  }
  es.onerror = ((web.Event _) => onError('stream error')).toJS;
  return SseConnection(es);
}
