import 'dart:async';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

void main() => runApp(const WpcApp());

class WpcApp extends StatelessWidget {
  const WpcApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'WPC',
      theme: ThemeData(colorSchemeSeed: Colors.teal, useMaterial3: true),
      home: const StatusScreen(),
    );
  }
}

class StatusScreen extends StatefulWidget {
  const StatusScreen({super.key});

  @override
  State<StatusScreen> createState() => _StatusScreenState();
}

class _StatusScreenState extends State<StatusScreen> {
  // Assumes the phone's WiFi is joined to the Master's SoftAP
  // (WPC-Master-XXXX). Default AP gateway IP for an ESP32 SoftAP.
  static const String _statusUrl = 'http://192.168.4.1/status';
  static const Duration _pollInterval = Duration(seconds: 3);

  Map<String, dynamic>? _status;
  String? _error;
  Timer? _timer;

  @override
  void initState() {
    super.initState();
    _fetchStatus();
    _timer = Timer.periodic(_pollInterval, (_) => _fetchStatus());
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  Future<void> _fetchStatus() async {
    try {
      final response = await http
          .get(Uri.parse(_statusUrl))
          .timeout(const Duration(seconds: 4));
      if (response.statusCode == 200) {
        setState(() {
          _status = jsonDecode(response.body) as Map<String, dynamic>;
          _error = null;
        });
      } else {
        setState(() => _error = 'HTTP ${response.statusCode}');
      }
    } catch (e) {
      setState(() => _error = 'Not reachable -- check WiFi is on WPC-Master-XXXX');
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('WPC Status')),
      body: RefreshIndicator(
        onRefresh: _fetchStatus,
        child: _buildBody(),
      ),
    );
  }

  Widget _buildBody() {
    if (_error != null) {
      return ListView(
        children: [
          Padding(
            padding: const EdgeInsets.all(24),
            child: Column(
              children: [
                const Icon(Icons.wifi_off, size: 48, color: Colors.grey),
                const SizedBox(height: 12),
                Text(_error!, textAlign: TextAlign.center),
              ],
            ),
          ),
        ],
      );
    }

    if (_status == null) {
      return const Center(child: CircularProgressIndicator());
    }

    final masterId = _status!['masterId'] as String? ?? '--';
    final levels = _status!['levels'] as Map<String, dynamic>? ?? {};
    final pumps = _status!['pumps'] as List<dynamic>? ?? [];

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Text('Master: $masterId', style: Theme.of(context).textTheme.bodySmall),
        const SizedBox(height: 16),
        Text('Sump Levels', style: Theme.of(context).textTheme.titleMedium),
        const SizedBox(height: 8),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: levels.entries.map((e) {
            final active = e.value == true;
            return Chip(
              label: Text(e.key.toUpperCase()),
              backgroundColor: active ? Colors.amber.shade100 : Colors.grey.shade200,
              avatar: Icon(
                active ? Icons.water_drop : Icons.water_drop_outlined,
                size: 18,
                color: active ? Colors.amber.shade800 : Colors.grey,
              ),
            );
          }).toList(),
        ),
        const SizedBox(height: 24),
        Text('Pumps', style: Theme.of(context).textTheme.titleMedium),
        const SizedBox(height: 8),
        if (pumps.isEmpty)
          const Padding(
            padding: EdgeInsets.symmetric(vertical: 16),
            child: Text('No pumps joined yet.'),
          ),
        ...pumps.map((p) {
          final map = p as Map<String, dynamic>;
          final online = map['online'] == true;
          final relay = map['relay'] == true;
          return Card(
            child: ListTile(
              leading: CircleAvatar(
                backgroundColor: online
                    ? (relay ? Colors.green : Colors.grey.shade400)
                    : Colors.red.shade200,
                child: Icon(
                  relay ? Icons.power : Icons.power_off,
                  color: Colors.white,
                  size: 20,
                ),
              ),
              title: Text('Pump ${map['pumpId']}  (slot ${map['slot']})'),
              subtitle: Text(online
                  ? (relay ? 'Running' : 'Idle')
                  : 'Offline -- not responding'),
            ),
          );
        }),
      ],
    );
  }
}
