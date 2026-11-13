import 'package:mqtt_client/mqtt_browser_client.dart';
import 'package:mqtt_client/mqtt_client.dart';

// Browsers can only reach a broker over WebSockets (e.g. ws://host:9001).
MqttClient makeMqttClient(String host, int port, String clientId) {
  final url = host.startsWith('ws') ? host : 'ws://$host';
  return MqttBrowserClient.withPort(url, clientId, port);
}
