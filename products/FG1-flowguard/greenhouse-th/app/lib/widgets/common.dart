import 'package:flutter/material.dart';

String fmtT(double? t) => t == null ? '-- °C' : '${t.toStringAsFixed(1)} °C';
String fmtRh(double? rh) => rh == null ? '-- %' : '${rh.toStringAsFixed(0)} %';

/// Round pump indicator: blue water drop when ON, grey when OFF.
class PumpBadge extends StatelessWidget {
  const PumpBadge({super.key, required this.on, this.size = 64});
  final bool on;
  final double size;
  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return AnimatedContainer(
      duration: const Duration(milliseconds: 300),
      width: size,
      height: size,
      decoration: BoxDecoration(
        shape: BoxShape.circle,
        color: on ? const Color(0xFF1E88E5) : cs.surfaceContainerHighest,
        boxShadow: on ? [BoxShadow(color: const Color(0xFF1E88E5).withValues(alpha: 0.4), blurRadius: 14)] : null,
      ),
      child: Icon(on ? Icons.water_drop : Icons.water_drop_outlined,
          color: on ? Colors.white : cs.onSurfaceVariant, size: size * 0.5),
    );
  }
}

/// How the app is talking to the controller right now.
class LinkChip extends StatelessWidget {
  const LinkChip({super.key, required this.via, required this.fresh});
  final String via;
  final bool fresh;
  @override
  Widget build(BuildContext context) {
    final online = via.isNotEmpty && fresh;
    final (icon, label, color) = !online
        ? (Icons.cloud_off, 'Offline', Colors.grey)
        : via == 'Local'
            ? (Icons.wifi, 'Local', Colors.green)
            : via == 'Demo'
                ? (Icons.science_outlined, 'Demo', Colors.orange)
                : (Icons.cloud_done, 'Internet', Colors.blue);
    return Chip(
      avatar: Icon(icon, size: 16, color: color),
      label: Text(label, style: const TextStyle(fontSize: 12)),
      visualDensity: VisualDensity.compact,
      side: BorderSide(color: color.withValues(alpha: 0.5)),
    );
  }
}

/// Signal bars from RSSI (dBm). 0 = unknown.
class SignalBars extends StatelessWidget {
  const SignalBars({super.key, required this.rssi});
  final int rssi;
  @override
  Widget build(BuildContext context) {
    final bars = rssi == 0 ? 0 : rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : 1;
    final color = bars >= 3 ? Colors.green : bars == 2 ? Colors.orange : Colors.red;
    return Row(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.end, children: [
      for (int i = 0; i < 4; i++)
        Container(
          width: 4,
          height: 5.0 + i * 3,
          margin: const EdgeInsets.only(right: 2),
          decoration: BoxDecoration(
              color: i < bars ? color : Colors.grey.withValues(alpha: 0.3), borderRadius: BorderRadius.circular(1)),
        ),
      const SizedBox(width: 4),
      Text(rssi == 0 ? '—' : '$rssi dBm', style: const TextStyle(fontSize: 11)),
    ]);
  }
}

void toast(BuildContext context, String msg) {
  ScaffoldMessenger.of(context)
    ..hideCurrentSnackBar()
    ..showSnackBar(SnackBar(content: Text(msg), duration: const Duration(seconds: 3)));
}

/// Sends a command and shows a message if it failed. Returns the reply.
Future<Map<String, dynamic>> runCmd(BuildContext context, Future<Map<String, dynamic>> f,
    {String? okMsg}) async {
  final r = await f;
  if (!context.mounted) return r;
  if (r['ok'] == true) {
    if (okMsg != null) toast(context, okMsg);
  } else {
    toast(context, 'Failed: ${r['err'] ?? r['error'] ?? 'no reply'}');
  }
  return r;
}

class SectionTitle extends StatelessWidget {
  const SectionTitle(this.text, {super.key, this.trailing});
  final String text;
  final Widget? trailing;
  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.fromLTRB(16, 18, 16, 6),
        child: Row(children: [
          Expanded(
              child: Text(text.toUpperCase(),
                  style: TextStyle(
                      fontSize: 12,
                      letterSpacing: 0.8,
                      fontWeight: FontWeight.w600,
                      color: Theme.of(context).colorScheme.primary))),
          if (trailing != null) trailing!,
        ]),
      );
}
