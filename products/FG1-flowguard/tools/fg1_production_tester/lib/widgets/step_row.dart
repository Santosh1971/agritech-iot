import 'package:flutter/material.dart';
import '../models/test_report.dart';

/// One row in the running-tests checklist -- ⏳ while unset, ✅/❌ once
/// the step has a result. See PRODUCTION_TOOL_SPEC.md section 8.2
/// screen 4.
class StepRow extends StatelessWidget {
  final StepResult step;
  const StepRow({super.key, required this.step});

  @override
  Widget build(BuildContext context) {
    final label = kStepLabels[step.name] ?? step.name;
    final Widget icon;
    final Color? color;
    if (step.passed == null) {
      icon = const SizedBox(
          width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2));
      color = null;
    } else if (step.passed == true) {
      icon = const Icon(Icons.check_circle, color: Colors.green);
      color = Colors.green;
    } else {
      icon = const Icon(Icons.cancel, color: Colors.red);
      color = Colors.red;
    }
    return ListTile(
      leading: icon,
      title: Text(label),
      subtitle: step.detail.isNotEmpty ? Text(step.detail) : null,
      titleTextStyle: TextStyle(fontSize: 16, color: color, fontWeight: FontWeight.w600),
    );
  }
}
