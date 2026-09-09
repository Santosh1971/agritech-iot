import 'dart:convert';
import 'dart:io';
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/test_report.dart';

/// Local, on-device store of every unit tested this run/all-time --
/// see PRODUCTION_TOOL_SPEC.md section 8.5. Deliberately just
/// SharedPreferences + a JSON blob, not a database: ~10 units/run
/// doesn't need one, and this is trivial to inspect/debug.
class ReportStore {
  static const _key = 'fg1_test_reports_v1';

  Future<List<TestReport>> loadAll() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getStringList(_key) ?? [];
    return raw
        .map((s) {
          try {
            return TestReport.fromJson(jsonDecode(s) as Map<String, dynamic>);
          } catch (_) {
            return null;
          }
        })
        .whereType<TestReport>()
        .toList();
  }

  Future<void> add(TestReport report) async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getStringList(_key) ?? [];
    raw.add(jsonEncode(report.toJson()));
    await prefs.setStringList(_key, raw);
  }

  Future<void> clear() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_key);
  }

  /// Writes a CSV matching testing/results_logger.py's schema
  /// (timestamp_utc, device_id, tier, passed, steps_json) so this
  /// app's export and the old Python tool's log can be merged, and
  /// returns the file for sharing.
  Future<File> exportCsv() async {
    final reports = await loadAll();
    final dir = await getTemporaryDirectory();
    final file = File('${dir.path}/fg1_test_results_${DateTime.now().millisecondsSinceEpoch}.csv');

    final header = 'timestamp_utc,device_id,tier,passed,steps_json';
    final lines = [header];
    for (final r in reports) {
      final row = r.toCsvRow();
      String esc(String s) => '"${s.replaceAll('"', '""')}"';
      lines.add([
        esc(row['timestamp_utc']!),
        esc(row['device_id']!),
        esc(row['tier']!),
        row['passed']!,
        esc(row['steps_json']!),
      ].join(','));
    }
    await file.writeAsString(lines.join('\n'));
    return file;
  }
}
