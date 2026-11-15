import 'dart:async';
import 'dart:convert';

import 'package:http/http.dart' as http;

class SseConnection {
  SseConnection(this._client, this._sub);
  final http.Client _client;
  StreamSubscription? _sub;
  void close() {
    _sub?.cancel();
    _client.close();
  }
}

/// Opens `url` as an event stream; calls onEvent(name, data) per event and
/// onError once when the stream fails or ends.
SseConnection openSse(String url, Map<String, String> headers, void Function(String event, String data) onEvent,
    void Function(Object err) onError) {
  final client = http.Client();
  final conn = SseConnection(client, null);
  final req = http.Request('GET', Uri.parse(url))
    ..headers.addAll(headers)
    ..headers['Accept'] = 'text/event-stream';
  client.send(req).then((resp) {
    if (resp.statusCode != 200) {
      onError('HTTP ${resp.statusCode}');
      return;
    }
    var event = 'message';
    final data = StringBuffer();
    conn._sub = resp.stream.transform(utf8.decoder).transform(const LineSplitter()).listen((line) {
      if (line.isEmpty) {
        if (data.isNotEmpty) onEvent(event, data.toString());
        event = 'message';
        data.clear();
      } else if (line.startsWith('event:')) {
        event = line.substring(6).trim();
      } else if (line.startsWith('data:')) {
        if (data.isNotEmpty) data.write('\n');
        data.write(line.substring(5).trimLeft());
      }
    }, onError: (Object e) => onError(e), onDone: () => onError('stream ended'), cancelOnError: true);
  }, onError: (Object e) => onError(e));
  return conn;
}
