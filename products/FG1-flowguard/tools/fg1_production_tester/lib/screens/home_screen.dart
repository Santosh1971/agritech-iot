import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/providers.dart';
import '../runner/production_test_runner.dart';
import '../widgets/step_row.dart';
import 'settings_screen.dart';
import 'history_screen.dart';

/// The whole session flow lives on one screen, panel swapped by
/// RunPhase -- see PRODUCTION_TOOL_SPEC.md section 8.2 screens 1-5.
class HomeScreen extends ConsumerStatefulWidget {
  const HomeScreen({super.key});
  @override
  ConsumerState<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends ConsumerState<HomeScreen> {
  final _dutPortController = TextEditingController();
  final _flashLogScrollController = ScrollController();
  Map<String, dynamic>? _bridgeHealth;
  bool _checkingHealth = false;

  @override
  void dispose() {
    _dutPortController.dispose();
    _flashLogScrollController.dispose();
    super.dispose();
  }

  /// Jumps the flash log to its latest line -- called after each
  /// rebuild triggered by a new log line arriving, so the operator
  /// doesn't have to manually scroll to see progress (matches what a
  /// terminal/VS Code does automatically).
  void _scrollFlashLogToBottom() {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!_flashLogScrollController.hasClients) return;
      _flashLogScrollController.jumpTo(_flashLogScrollController.position.maxScrollExtent);
    });
  }

  Future<void> _checkHealth() async {
    setState(() => _checkingHealth = true);
    final runner = ref.read(testRunnerProvider);
    final health = await runner.flashBridge.health();
    setState(() {
      _bridgeHealth = health;
      _checkingHealth = false;
    });
  }

  @override
  Widget build(BuildContext context) {
    final config = ref.watch(benchConfigProvider);
    final runner = ref.watch(testRunnerProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('FG1 Production Tester'),
        actions: [
          IconButton(
            icon: const Icon(Icons.history),
            onPressed: () => Navigator.of(context).push(MaterialPageRoute(builder: (_) => const HistoryScreen())),
          ),
          IconButton(
            icon: const Icon(Icons.settings),
            onPressed: () => Navigator.of(context).push(MaterialPageRoute(builder: (_) => const SettingsScreen())),
          ),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: config.flashBridgeHost.isEmpty
            ? _NoConfigPanel(onOpenSettings: () =>
                Navigator.of(context).push(MaterialPageRoute(builder: (_) => const SettingsScreen())))
            : _buildForPhase(runner),
      ),
    );
  }

  Widget _buildForPhase(ProductionTestRunner runner) {
    switch (runner.phase) {
      case RunPhase.idle:
        return _buildSetupCheck(runner);
      case RunPhase.flashing:
        return _buildFlashing(runner);
      case RunPhase.bootCheck:
        return const Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              CircularProgressIndicator(),
              SizedBox(height: 16),
              Text('Capturing boot log...'),
            ],
          ),
        );
      case RunPhase.awaitingWifiJoin:
        return _buildJoinWifi(runner);
      case RunPhase.runningTests:
        return _buildRunningTests(runner);
      case RunPhase.done:
        return _buildReport(runner);
    }
  }

  Widget _buildSetupCheck(ProductionTestRunner runner) {
    return SingleChildScrollView(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const Text('1. Setup check', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 18)),
          const SizedBox(height: 12),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(12),
              child: Row(
                children: [
                  Expanded(
                    child: _checkingHealth
                        ? const Text('Checking Flash Bridge...')
                        : Text(_bridgeHealth == null
                            ? 'Not checked yet'
                            : _bridgeHealth!['pio_found'] == true
                                ? 'Flash Bridge OK — PlatformIO found'
                                : 'Flash Bridge reachable but `pio` NOT found'),
                  ),
                  Icon(
                    _bridgeHealth == null
                        ? Icons.help_outline
                        : _bridgeHealth!['pio_found'] == true
                            ? Icons.check_circle
                            : Icons.warning,
                    color: _bridgeHealth == null
                        ? Colors.grey
                        : _bridgeHealth!['pio_found'] == true
                            ? Colors.green
                            : Colors.orange,
                  ),
                ],
              ),
            ),
          ),
          TextButton(onPressed: _checkHealth, child: const Text('Check Flash Bridge connection')),
          const SizedBox(height: 24),
          const Text('2. Plug in the DUT via USB, then flash', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 18)),
          const SizedBox(height: 12),
          TextField(
            controller: _dutPortController,
            decoration: const InputDecoration(
              labelText: 'DUT serial port (optional)',
              hintText: 'leave blank to use platformio.ini default',
              border: OutlineInputBorder(),
            ),
          ),
          const SizedBox(height: 16),
          ElevatedButton.icon(
            icon: const Icon(Icons.bolt),
            label: const Text('Flash'),
            onPressed: () => runner.startFlash(
                dutPort: _dutPortController.text.trim().isEmpty ? null : _dutPortController.text.trim()),
          ),
        ],
      ),
    );
  }

  Widget _buildFlashing(ProductionTestRunner runner) {
    _scrollFlashLogToBottom();
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const Text('Flashing...', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 18)),
        const SizedBox(height: 12),
        Expanded(
          child: Container(
            padding: const EdgeInsets.all(8),
            color: Colors.black,
            child: ListView.builder(
              controller: _flashLogScrollController,
              reverse: false,
              itemCount: runner.flashLog.length,
              itemBuilder: (context, i) => Text(
                runner.flashLog[i],
                style: const TextStyle(color: Colors.greenAccent, fontFamily: 'monospace', fontSize: 11),
              ),
            ),
          ),
        ),
      ],
    );
  }

  Widget _buildJoinWifi(ProductionTestRunner runner) {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Icon(Icons.wifi, size: 64),
          const SizedBox(height: 16),
          const Text('Join the device\'s own WiFi network', style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
          const SizedBox(height: 12),
          SelectableText(
            runner.deviceId ?? '(unknown SSID)',
            style: const TextStyle(fontSize: 24, fontFamily: 'monospace', fontWeight: FontWeight.bold),
          ),
          const SizedBox(height: 8),
          const Text('Open system WiFi settings, connect to that network,\nthen come back and tap Continue.',
              textAlign: TextAlign.center),
          const SizedBox(height: 24),
          ElevatedButton(
            onPressed: () => runner.confirmDeviceWifiJoined(),
            child: const Text('Continue — I\'ve joined it'),
          ),
        ],
      ),
    );
  }

  Widget _buildRunningTests(ProductionTestRunner runner) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(
          children: [
            const Text('Running tests', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 18)),
            const Spacer(),
            Chip(
              label: Text(runner.jigAvailable ? 'Jig connected' : 'No jig'),
              backgroundColor: runner.jigAvailable ? Colors.green.shade100 : Colors.orange.shade100,
            ),
          ],
        ),
        Expanded(
          child: ListView(
            children: runner.steps.values.map((s) => StepRow(step: s)).toList(),
          ),
        ),
      ],
    );
  }

  Widget _buildReport(ProductionTestRunner runner) {
    final report = runner.buildReport();
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Center(
          child: Column(
            children: [
              Icon(
                report.overallPassed ? Icons.check_circle : Icons.cancel,
                color: report.overallPassed ? Colors.green : Colors.red,
                size: 64,
              ),
              Text(
                report.overallPassed ? 'PASS' : 'FAIL',
                style: TextStyle(
                  fontSize: 28,
                  fontWeight: FontWeight.bold,
                  color: report.overallPassed ? Colors.green : Colors.red,
                ),
              ),
              Text(report.deviceId ?? 'UNKNOWN', style: const TextStyle(fontFamily: 'monospace', fontSize: 16)),
            ],
          ),
        ),
        const Divider(),
        Expanded(
          child: ListView(
            children: runner.steps.values.map((s) => StepRow(step: s)).toList(),
          ),
        ),
        if (runner.flashLog.isNotEmpty)
          TextButton.icon(
            icon: const Icon(Icons.article_outlined),
            label: const Text('View flash log'),
            onPressed: () => _showFlashLog(runner),
          ),
        if (runner.steps['flash']?.passed == false)
          OutlinedButton.icon(
            icon: const Icon(Icons.refresh),
            label: const Text('Retry Flash'),
            onPressed: () => runner.startFlash(
                dutPort: _dutPortController.text.trim().isEmpty ? null : _dutPortController.text.trim()),
          ),
        ElevatedButton.icon(
          icon: const Icon(Icons.arrow_forward),
          label: const Text('Save & Next Unit'),
          onPressed: () {
            _dutPortController.clear();
            runner.reset();
          },
        ),
      ],
    );
  }

  void _showFlashLog(ProductionTestRunner runner) {
    showModalBottomSheet(
      context: context,
      isScrollControlled: true,
      builder: (context) => DraggableScrollableSheet(
        expand: false,
        initialChildSize: 0.8,
        builder: (context, scrollController) => Container(
          color: Colors.black,
          padding: const EdgeInsets.all(8),
          child: ListView.builder(
            controller: scrollController,
            itemCount: runner.flashLog.length,
            itemBuilder: (context, i) => Text(
              runner.flashLog[i],
              style: const TextStyle(color: Colors.greenAccent, fontFamily: 'monospace', fontSize: 11),
            ),
          ),
        ),
      ),
    );
  }
}

class _NoConfigPanel extends StatelessWidget {
  final VoidCallback onOpenSettings;
  const _NoConfigPanel({required this.onOpenSettings});

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Icon(Icons.settings, size: 48),
          const SizedBox(height: 12),
          const Text('Set the Flash Bridge address first.', style: TextStyle(fontSize: 16)),
          const SizedBox(height: 16),
          ElevatedButton(onPressed: onOpenSettings, child: const Text('Open Settings')),
        ],
      ),
    );
  }
}
