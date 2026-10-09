// ASC Studio: one app for every ASC Student Kit. It finds kits over
// Bluetooth, asks the board for its design, and builds the student's screen
// from the layout they made in the studio's App stage. No code is written per
// student (kit spec APP-01).
import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import 'board.dart';
import 'protocol.dart';

const green = Color(0xFF1F7A5F);

void main() => runApp(const AscStudioApp());

class AscStudioApp extends StatelessWidget {
  const AscStudioApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ASC Studio',
      theme: ThemeData(colorScheme: ColorScheme.fromSeed(seedColor: green), useMaterial3: true),
      home: const ScanScreen(),
    );
  }
}

// ---------------------------------------------------------------- scanning

class ScanScreen extends StatefulWidget {
  const ScanScreen({super.key});

  @override
  State<ScanScreen> createState() => _ScanScreenState();
}

class _ScanScreenState extends State<ScanScreen> {
  List<ScanResult> _results = [];
  bool _scanning = false;
  String? _error;
  StreamSubscription<List<ScanResult>>? _sub;

  @override
  void initState() {
    super.initState();
    _sub = FlutterBluePlus.scanResults.listen((r) => setState(() => _results = r));
    FlutterBluePlus.isScanning.listen((s) => mounted ? setState(() => _scanning = s) : null);
    _scan();
  }

  Future<void> _scan() async {
    setState(() => _error = null);
    try {
      if (await FlutterBluePlus.isSupported == false) {
        setState(() => _error = 'This phone has no Bluetooth LE.');
        return;
      }
      await FlutterBluePlus.adapterState.where((s) => s == BluetoothAdapterState.on).first.timeout(const Duration(seconds: 3), onTimeout: () async {
        await FlutterBluePlus.turnOn();
        return BluetoothAdapterState.on;
      });
      await FlutterBluePlus.startScan(withServices: [serviceUuid], timeout: const Duration(seconds: 10));
    } catch (e) {
      setState(() => _error = 'Turn on Bluetooth and allow "Nearby devices", then try again. ($e)');
    }
  }

  @override
  void dispose() {
    _sub?.cancel();
    FlutterBluePlus.stopScan();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('ASC Studio'), backgroundColor: green, foregroundColor: Colors.white),
      body: RefreshIndicator(
        onRefresh: _scan,
        child: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            Text('Nearby kits', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 4),
            const Text('Your kit shows its name, for example ASC-MINI-7F2A. Pull down to search again.'),
            const SizedBox(height: 12),
            if (_error != null) Card(color: Colors.red.shade50, child: Padding(padding: const EdgeInsets.all(12), child: Text(_error!))),
            if (_scanning) const LinearProgressIndicator(),
            if (!_scanning && _results.isEmpty && _error == null)
              const Padding(padding: EdgeInsets.all(12), child: Text('No kits found. Is the kit powered on and close by?')),
            for (final r in _results)
              Card(
                child: ListTile(
                  leading: const Icon(Icons.memory, color: green),
                  title: Text(r.advertisementData.advName.isNotEmpty ? r.advertisementData.advName : r.device.remoteId.str),
                  subtitle: Text('Signal ${r.rssi} dBm'),
                  trailing: const Icon(Icons.chevron_right),
                  onTap: () {
                    FlutterBluePlus.stopScan();
                    Navigator.of(context).push(MaterialPageRoute(builder: (_) => BoardScreen(device: r.device)));
                  },
                ),
              ),
          ],
        ),
      ),
      floatingActionButton: FloatingActionButton.extended(
        onPressed: _scanning ? null : _scan,
        backgroundColor: green,
        foregroundColor: Colors.white,
        icon: const Icon(Icons.bluetooth_searching),
        label: Text(_scanning ? 'Searching…' : 'Search'),
      ),
    );
  }
}

// ---------------------------------------------------------------- one board

class BoardScreen extends StatefulWidget {
  final BluetoothDevice device;
  const BoardScreen({super.key, required this.device});

  @override
  State<BoardScreen> createState() => _BoardScreenState();
}

class _BoardScreenState extends State<BoardScreen> {
  late final BoardLink _link = BoardLink(widget.device);
  StreamSubscription<Map<String, dynamic>>? _sub;
  Map<String, dynamic>? _hello;
  AppLayout? _layout;
  Live? _live;
  bool? _hindi;
  bool _engineer = false;
  String _status = 'Connecting…';
  final Map<String, List<double>> _history = {};
  final Set<String> _shownAlerts = {};

  @override
  void initState() {
    super.initState();
    _start();
  }

  Future<void> _start() async {
    try {
      await _link.connect();
      _sub = _link.messages.listen(_onMessage);
      final hello = await _link.request({'cmd': 'hello'}, 'hello');
      // The phone knows the time; boards without a clock battery need it for their rules and log.
      await _link.request({'cmd': 'time', 'unix': DateTime.now().millisecondsSinceEpoch ~/ 1000}, 'time');
      final design = await _link.request({'cmd': 'get_design'}, 'design');
      await _link.request({'cmd': 'live', 'on': true}, 'live');
      setState(() {
        _hello = hello;
        _layout = AppLayout.fromDesign(design['design'] as Map<String, dynamic>?);
        _status = design['design'] == null ? 'This kit has no design yet. Send one from the studio.' : '';
      });
    } catch (e) {
      setState(() => _status = 'Could not connect: $e');
    }
  }

  void _onMessage(Map<String, dynamic> m) {
    final live = Live.fromMessage(m);
    if (live == null) return;
    for (final e in live.values.entries) {
      if (e.value == null) continue;
      final h = _history.putIfAbsent(e.key, () => []);
      h.add(e.value!);
      if (h.length > 120) h.removeAt(0);
    }
    for (final a in _layout?.alerts ?? const <Alert>[]) {
      final firing = a.firing(live.valueOf(a.ref));
      if (firing && _shownAlerts.add(a.ref)) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('⚠ ${a.say}'), backgroundColor: Colors.orange.shade800));
      } else if (!firing) {
        _shownAlerts.remove(a.ref);
      }
    }
    setState(() => _live = live);
  }

  Future<void> _toggle(String port, bool on) async {
    try {
      await _link.request({'cmd': 'out', 'port': port, 'on': on}, 'out');
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('$e'.replaceFirst('Exception: ', ''))));
    }
  }

  @override
  void dispose() {
    _sub?.cancel();
    _link.disconnect();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final layout = _layout;
    final hindi = _hindi ?? layout?.hindi ?? false;
    return Scaffold(
      appBar: AppBar(
        backgroundColor: green,
        foregroundColor: Colors.white,
        title: Text(layout?.name ?? widget.device.platformName),
        actions: [
          TextButton(
            onPressed: () => setState(() => _hindi = !hindi),
            child: Text(hindi ? 'English' : 'हिंदी', style: const TextStyle(color: Colors.white)),
          ),
          IconButton(
            tooltip: "Engineer's view",
            icon: Icon(_engineer ? Icons.settings : Icons.settings_outlined),
            onPressed: () => setState(() => _engineer = !_engineer),
          ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(12),
        children: [
          if (_status.isNotEmpty) Padding(padding: const EdgeInsets.all(8), child: Text(_status)),
          if (layout != null)
            GridView.count(
              crossAxisCount: 2,
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              crossAxisSpacing: 8,
              mainAxisSpacing: 8,
              childAspectRatio: 1.25,
              children: [for (final t in layout.tiles) _tile(t, hindi)],
            ),
          if (layout != null && layout.tiles.any((t) => t.isOutput))
            Padding(
              padding: const EdgeInsets.only(top: 12),
              child: Text(
                hindi ? 'फ़ोन से चालू/बंद करने के लिए पहले किट पर PAIR बटन दबाएँ।' : 'To switch outputs from the phone, first press PAIR on the kit.',
                style: Theme.of(context).textTheme.bodySmall,
              ),
            ),
          if (_engineer) _engineerView(),
        ],
      ),
    );
  }

  Widget _tile(Tile t, bool hindi) {
    final v = _live?.valueOf(t.ref);
    final label = Text(t.labelFor(hindi), style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 12), maxLines: 2);
    Widget body;
    switch (t.kind) {
      case TileKind.toggle:
        final on = _live?.outputs[t.ref] ?? false;
        body = Row(children: [
          Switch(value: on, activeColor: green, onChanged: _live == null ? null : (x) => _toggle(t.ref, x)),
          Text(on ? (hindi ? 'चालू' : 'ON') : (hindi ? 'बंद' : 'OFF')),
        ]);
      case TileKind.lamp:
        final ok = (v ?? 0) > 0.5;
        body = Row(children: [
          Icon(Icons.circle, color: v == null ? Colors.grey : (ok ? green : Colors.red), size: 18),
          const SizedBox(width: 6),
          Text(v == null ? '—' : (ok ? (hindi ? 'ठीक' : 'OK') : (hindi ? 'खाली' : 'Empty'))),
        ]);
      case TileKind.gauge:
        body = Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          _reading(v, t.unit, 24),
          LinearProgressIndicator(value: v == null ? null : (v.clamp(0, 100) / 100), color: green),
        ]);
      case TileKind.graph:
        body = Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          _reading(v, t.unit, 24),
          Expanded(child: CustomPaint(painter: _Spark(_history[t.ref] ?? const []), size: Size.infinite)),
        ]);
      case TileKind.value:
        body = _reading(v, t.unit, 26);
    }
    return Card(child: Padding(padding: const EdgeInsets.all(10), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [label, const SizedBox(height: 6), Expanded(child: body)])));
  }

  Widget _reading(double? v, String unit, double size) {
    return Text.rich(TextSpan(children: [
      TextSpan(text: v == null ? '—' : v.toStringAsFixed(1), style: TextStyle(fontSize: size, fontWeight: FontWeight.bold)),
      if (v != null && unit.isNotEmpty) TextSpan(text: ' $unit', style: const TextStyle(fontSize: 14)),
    ]));
  }

  Widget _engineerView() {
    return Card(
      margin: const EdgeInsets.only(top: 12),
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          const Text("Engineer's view", style: TextStyle(fontWeight: FontWeight.bold, color: green)),
          Text('Board ${_hello?['id']} · firmware ${_hello?['fw']} · MTU ${widget.device.mtuNow}'),
          const SizedBox(height: 6),
          Text(_link.raw.reversed.take(12).join('\n'), style: const TextStyle(fontFamily: 'monospace', fontSize: 11)),
        ]),
      ),
    );
  }
}

class _Spark extends CustomPainter {
  final List<double> points;
  _Spark(this.points);

  @override
  void paint(Canvas canvas, Size size) {
    if (points.length < 2) return;
    var lo = points.reduce((a, b) => a < b ? a : b), hi = points.reduce((a, b) => a > b ? a : b);
    // At least 2 units of height, so a 0.1 step in a steady reading stays a small wiggle.
    if (hi - lo < 2) { final mid = (hi + lo) / 2; lo = mid - 1; hi = mid + 1; }
    final span = hi - lo;
    final path = Path();
    for (var i = 0; i < points.length; i++) {
      final x = size.width * i / (points.length - 1);
      final y = size.height * (1 - (points[i] - lo) / span);
      i == 0 ? path.moveTo(x, y) : path.lineTo(x, y);
    }
    canvas.drawPath(path, Paint()..color = green..strokeWidth = 2..style = PaintingStyle.stroke..strokeCap = StrokeCap.round);
  }

  @override
  bool shouldRepaint(_Spark old) => true;
}
