/// Demo build: no MQTT. Kept only for the BrokerSettings type the
/// node store refers to.
class BrokerSettings {
  final String host;
  const BrokerSettings({required this.host});
  bool get isSet => host.isNotEmpty;
  static const agrisense = BrokerSettings(host: 'demo');
  static const girish = BrokerSettings(host: 'demo');
}
