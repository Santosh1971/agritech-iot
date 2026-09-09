import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:share_plus/share_plus.dart';
import '../models/test_report.dart';
import '../providers/providers.dart';

/// Every unit tested this session/all-time -- see
/// PRODUCTION_TOOL_SPEC.md section 8.2 screen 6.
class HistoryScreen extends ConsumerStatefulWidget {
  const HistoryScreen({super.key});
  @override
  ConsumerState<HistoryScreen> createState() => _HistoryScreenState();
}

class _HistoryScreenState extends ConsumerState<HistoryScreen> {
  List<TestReport> _reports = [];
  bool _loading = true;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() => _loading = true);
    final reports = await ref.read(reportStoreProvider).loadAll();
    reports.sort((a, b) => b.timestampUtc.compareTo(a.timestampUtc));
    setState(() {
      _reports = reports;
      _loading = false;
    });
  }

  Future<void> _export() async {
    final file = await ref.read(reportStoreProvider).exportCsv();
    await Share.shareXFiles([XFile(file.path)], subject: 'FG1 production test results');
  }

  @override
  Widget build(BuildContext context) {
    final passCount = _reports.where((r) => r.overallPassed).length;
    return Scaffold(
      appBar: AppBar(
        title: const Text('Run History'),
        actions: [
          IconButton(icon: const Icon(Icons.refresh), onPressed: _load),
          IconButton(icon: const Icon(Icons.ios_share), onPressed: _reports.isEmpty ? null : _export),
        ],
      ),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : Column(
              children: [
                Padding(
                  padding: const EdgeInsets.all(12),
                  child: Text('${_reports.length} units tested — $passCount passed, '
                      '${_reports.length - passCount} failed'),
                ),
                Expanded(
                  child: _reports.isEmpty
                      ? const Center(child: Text('No units tested yet.'))
                      : ListView.builder(
                          itemCount: _reports.length,
                          itemBuilder: (context, i) {
                            final r = _reports[i];
                            return ListTile(
                              leading: Icon(
                                r.overallPassed ? Icons.check_circle : Icons.cancel,
                                color: r.overallPassed ? Colors.green : Colors.red,
                              ),
                              title: Text(r.deviceId ?? 'UNKNOWN'),
                              subtitle: Text(r.timestampUtc.toLocal().toString()),
                              onTap: () => _showDetail(r),
                            );
                          },
                        ),
                ),
              ],
            ),
    );
  }

  void _showDetail(TestReport r) {
    showModalBottomSheet(
      context: context,
      isScrollControlled: true,
      builder: (context) => DraggableScrollableSheet(
        expand: false,
        builder: (context, scrollController) => ListView(
          controller: scrollController,
          padding: const EdgeInsets.all(16),
          children: [
            Text(r.deviceId ?? 'UNKNOWN', style: Theme.of(context).textTheme.titleLarge),
            Text('${r.operator} · ${r.station} · ${r.timestampUtc.toLocal()}'),
            const Divider(),
            for (final entry in r.steps.entries)
              ListTile(
                dense: true,
                leading: Icon(
                  entry.value.passed == true ? Icons.check : Icons.close,
                  color: entry.value.passed == true ? Colors.green : Colors.red,
                ),
                title: Text(kStepLabels[entry.key] ?? entry.key),
                subtitle: entry.value.detail.isNotEmpty ? Text(entry.value.detail) : null,
              ),
          ],
        ),
      ),
    );
  }
}
