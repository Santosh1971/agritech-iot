import 'package:mqtt_client/mqtt_client.dart';
import 'package:mqtt_client/mqtt_server_client.dart';

MqttClient makeMqttClient(String host, int port, String clientId) {
  final c = MqttServerClient.withPort(host, clientId, port);
  return c;
}
