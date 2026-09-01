import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../models/device_status.dart';
import '../models/hardware_config.dart';
import '../models/program.dart';
import '../providers/providers.dart';

/// Pictorial "at a glance" view of the Mini's real, fixed hardware —
/// styled after the reference layout Kamta sent (icons + color-coded
/// state + live values along a schematic pipe run), scoped to exactly
/// what THIS board has: one main pump, one doser, up to 2 pressure
/// sensors, one water meter, 4 valves, and an optional pair of
/// source-level float switches (L1/L2). Which of the optional items
/// actually show is driven by HardwareConfig (Settings > Hardware
/// Configuration) — nothing here invents a reading for a sensor that
/// was never wired up.
class SchematicDiagram extends ConsumerWidget {
  final DeviceStatus status;
  final HardwareConfig config;
  final bool connected;
  const SchematicDiagram({super.key, required this.status, required this.config, required this.connected});

  static const _onColor = Color(0xFF2E7D32);
  static const _offColor = Color(0xFF9E9E9E);
  static const _doneColor = Color(0xFF1976D2);
  static const _warnColor = Color(0xFFE53935);
  static const _pipeColor = Color(0xFFB0BEC5);

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final programs = ref.watch(programsProvider).valueOrNull ?? const [];
    Program? activeProgram;
    for (final p in programs) {
      if (p.id == status.activeProgramId) { activeProgram = p; break; }
    }

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05), blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          const Text('IRRIGATION LINE', style: TextStyle(fontSize: 11, fontWeight: FontWeight.w600, letterSpacing: 0.5, color: Colors.grey)),
          const Spacer(),
          if (status.state == SchedulerState.running || status.state == SchedulerState.paused) ...[
            _PauseResumeButton(status: status, connected: connected),
            const SizedBox(width: 8),
          ],
          _StopButton(connected: connected),
        ]),
        const SizedBox(height: 12),
        if (config.hasWaterLevel) ...[
          _waterLevelRow(),
          const SizedBox(height: 8),
          _pipeDown(),
        ],
        _pumpRow(),
        _pipeDown(),
        if (config.hasPressure1 || config.hasPressure2 || config.hasWaterMeter) ...[
          _pipeMountedSensors(),
          const SizedBox(height: 4),
        ],
        _valveManifold(activeProgram),
        const SizedBox(height: 16),
        _legend(),
      ]),
    );
  }

  Widget _pipeDown() => Center(
        child: Container(width: 3, height: 16, color: _pipeColor),
      );

  Widget _waterLevelRow() {
    final ok = status.waterLevelOk;
    final names = status.relayNames;
    return Row(children: [
      _TankGlyph(l1Ok: status.waterL1Ok, l2Ok: status.waterL2Ok, size: 48),
      const SizedBox(width: 10),
      Expanded(
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          const Text('Water Source', style: TextStyle(fontWeight: FontWeight.w600, fontSize: 13)),
          const SizedBox(height: 3),
          Wrap(spacing: 4, runSpacing: 2, children: [
            _LevelTag(label: names.waterUpperDisplay, ok: status.waterL1Ok),
            _LevelTag(label: names.waterLowerDisplay, ok: status.waterL2Ok),
          ]),
        ]),
      ),
      _StatePill(label: ok ? 'OK' : 'LOW — Paused', color: ok ? Colors.blue : _warnColor),
    ]);
  }

  Widget _pumpRow() {
    final names = status.relayNames;
    return Row(children: [
      Expanded(
        child: _ComponentColumn(
          label: names.pumpDisplay,
          on: status.pump,
          onColor: _onColor,
          offColor: _offColor,
          icon: (color) => _PumpGlyph(color: color, size: 56),
        ),
      ),
      if (config.hasDoser) ...[
        const SizedBox(width: 12),
        Expanded(
          child: _ComponentColumn(
            label: names.dosingDisplay,
            on: status.dosing,
            onColor: const Color(0xFF7B1FA2),
            offColor: _offColor,
            icon: (color) => Icon(Icons.propane_tank, color: color, size: 32),
          ),
        ),
      ],
    ]);
  }

  Widget _pipeMountedSensors() {
    final names = status.relayNames;
    final tiles = <Widget>[
      if (config.hasPressure1) _PipeSensorTile(icon: Icons.speed, label: names.pressure1Display, value: '${status.pressure1Bar.toStringAsFixed(2)} bar'),
      if (config.hasPressure2) _PipeSensorTile(icon: Icons.speed, label: names.pressure2Display, value: '${status.pressure2Bar.toStringAsFixed(2)} bar'),
      if (config.hasWaterMeter) _PipeSensorTile(icon: Icons.water_damage_outlined, label: names.flowDisplay, value: '${status.flowRateLpm.toStringAsFixed(1)} L/m'),
    ];
    return Column(children: [
      Row(mainAxisAlignment: MainAxisAlignment.spaceEvenly, children: tiles),
      const SizedBox(height: 2),
      Container(height: 4, color: _pipeColor),
    ]);
  }

  Widget _valveManifold(Program? activeProgram) {
    return Column(children: [
      Container(height: 3, color: _pipeColor),
      const SizedBox(height: 10),
      Row(children: [
        for (int i = 0; i < 4; i++) ...[
          if (i > 0) const SizedBox(width: 8),
          Expanded(
            child: Column(children: [
              Container(width: 3, height: 12, color: _pipeColor),
              _ValveIcon(
                label: status.relayNames.valveDisplay(i),
                phase: _valvePhase(i, activeProgram),
                physicallyOn: status.valves.length > i ? status.valves[i] : false,
              ),
            ]),
          ),
        ],
      ]),
    ]);
  }

  // Blue/Green/Gray = Done/Running/To-do, matching the reference's valve
  // legend — driven by this valve's position relative to the active
  // program's CURRENT sequence, not just its raw on/off state (a valve
  // already run earlier in the same cycle should read "done" even
  // though it's now off, not just "off").
  _ValvePhase _valvePhase(int valveIndex, Program? activeProgram) {
    if (activeProgram == null || status.activeSeqIndex == null) {
      // No active program: nothing to sequence against, just show
      // whatever's physically on right now (manual control).
      final on = status.valves.length > valveIndex ? status.valves[valveIndex] : false;
      return on ? _ValvePhase.running : _ValvePhase.todo;
    }
    final activeIndex = status.activeSeqIndex!;
    for (int s = 0; s < activeProgram.sequences.length; s++) {
      if (!activeProgram.sequences[s].valveOn(valveIndex)) continue;
      if (s == activeIndex) return _ValvePhase.running;
      if (s < activeIndex) return _ValvePhase.done;
    }
    return _ValvePhase.todo;
  }

  Widget _legend() {
    return Wrap(spacing: 12, runSpacing: 6, children: [
      _legendItem(_doneColor, 'Done'),
      _legendItem(_onColor, 'Running'),
      _legendItem(_offColor, 'To do'),
      if (config.hasWaterLevel) _legendItem(_warnColor, 'Low Level'),
    ]);
  }

  Widget _legendItem(Color color, String label) => Row(mainAxisSize: MainAxisSize.min, children: [
        Container(width: 10, height: 10, decoration: BoxDecoration(color: color, shape: BoxShape.circle)),
        const SizedBox(width: 4),
        Text(label, style: const TextStyle(fontSize: 11, color: Colors.grey)),
      ]);
}

enum _ValvePhase { done, running, todo }

class _PauseResumeButton extends ConsumerWidget {
  final DeviceStatus status;
  final bool connected;
  const _PauseResumeButton({required this.status, required this.connected});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final running = status.state == SchedulerState.running;
    // A pause held by power/water isn't something the user can clear
    // from here — Resume only makes sense (and only appears) for a
    // pause the user themselves triggered. Power/water pauses already
    // say why on the Active Run card above; nothing to add here.
    final userPaused = status.state == SchedulerState.paused && status.pauseReason == 'manual';
    if (!running && !userPaused) return const SizedBox.shrink();

    return SizedBox(
      height: 30,
      child: OutlinedButton.icon(
        style: OutlinedButton.styleFrom(
          foregroundColor: Colors.orange.shade800,
          side: BorderSide(color: Colors.orange.shade800),
          padding: const EdgeInsets.symmetric(horizontal: 10),
          visualDensity: VisualDensity.compact,
        ),
        onPressed: connected
            ? () {
                final svc = ref.read(deviceServiceProvider);
                running ? svc.pause() : svc.resume();
                ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(running ? 'Pause sent' : 'Resume sent')));
              }
            : null,
        icon: Icon(running ? Icons.pause_circle : Icons.play_circle, size: 16),
        label: Text(running ? 'PAUSE' : 'RESUME', style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w700)),
      ),
    );
  }
}

class _StopButton extends ConsumerWidget {
  final bool connected;
  const _StopButton({required this.connected});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return SizedBox(
      height: 30,
      child: OutlinedButton.icon(
        style: OutlinedButton.styleFrom(
          foregroundColor: Colors.red,
          side: const BorderSide(color: Colors.red),
          padding: const EdgeInsets.symmetric(horizontal: 10),
          visualDensity: VisualDensity.compact,
        ),
        onPressed: connected
            ? () {
                ref.read(deviceServiceProvider).forceStop();
                ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Stop sent')));
              }
            : null,
        icon: const Icon(Icons.stop_circle, size: 16),
        label: const Text('STOP', style: TextStyle(fontSize: 12, fontWeight: FontWeight.w700)),
      ),
    );
  }
}

class _LevelTag extends StatelessWidget {
  final String label;
  final bool ok;
  const _LevelTag({required this.label, required this.ok});
  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
        decoration: BoxDecoration(
          color: (ok ? Colors.blue : SchematicDiagram._warnColor).withOpacity(0.12),
          borderRadius: BorderRadius.circular(6),
        ),
        child: Text(label, style: TextStyle(fontSize: 10, fontWeight: FontWeight.w700, color: ok ? Colors.blue : SchematicDiagram._warnColor)),
      );
}

class _ComponentColumn extends StatelessWidget {
  final String label;
  final bool on;
  final Color onColor;
  final Color offColor;
  final Widget Function(Color color) icon;
  const _ComponentColumn({required this.label, required this.on, required this.onColor, required this.offColor, required this.icon});

  @override
  Widget build(BuildContext context) {
    final color = on ? onColor : offColor;
    return Column(children: [
      Container(
        width: 64,
        height: 64,
        decoration: BoxDecoration(
          color: color.withOpacity(0.12),
          shape: BoxShape.circle,
          border: Border.all(color: color, width: 2),
        ),
        child: Center(child: icon(color)),
      ),
      const SizedBox(height: 4),
      Text(label, textAlign: TextAlign.center, maxLines: 2, overflow: TextOverflow.ellipsis,
          style: TextStyle(fontSize: 12, fontWeight: FontWeight.w600, color: color)),
    ]);
  }
}

/// Small hand-drawn centrifugal-pump glyph — a volute body with an
/// inlet/outlet stub and a motor block — so this reads as "a pump" at a
/// glance instead of the generic gear/settings icon it used to be.
class _PumpGlyph extends StatelessWidget {
  final Color color;
  final double size;
  const _PumpGlyph({required this.color, required this.size});

  @override
  Widget build(BuildContext context) => SizedBox(
        width: size,
        height: size,
        child: CustomPaint(painter: _PumpPainter(color)),
      );
}

class _PumpPainter extends CustomPainter {
  final Color color;
  _PumpPainter(this.color);

  @override
  void paint(Canvas canvas, Size size) {
    final fill = Paint()..color = color..style = PaintingStyle.fill;
    final w = size.width, h = size.height;

    // Motor block (small square, upper-right).
    final motorRect = Rect.fromLTWH(w * 0.52, h * 0.08, w * 0.34, h * 0.30);
    canvas.drawRRect(RRect.fromRectAndRadius(motorRect, const Radius.circular(2)), fill);

    // Volute body (the round pump housing), lower-left, slightly larger.
    final bodyCenter = Offset(w * 0.42, h * 0.60);
    final bodyRadius = w * 0.30;
    canvas.drawCircle(bodyCenter, bodyRadius, fill);

    // Shaft connecting motor to volute.
    canvas.drawRect(Rect.fromLTWH(w * 0.50, h * 0.30, w * 0.10, h * 0.16), fill);

    // Inlet stub (left) and outlet stub (bottom) — gives it the
    // recognizable "pipe in, pipe out" pump silhouette.
    canvas.drawRect(Rect.fromLTWH(w * 0.02, h * 0.54, w * 0.16, h * 0.12), fill);
    canvas.drawRect(Rect.fromLTWH(w * 0.36, h * 0.86, h * 0.12, h * 0.12), fill);

    // Impeller hint: a small lighter wedge cut into the body.
    final cut = Paint()..color = Colors.white.withOpacity(0.85)..style = PaintingStyle.fill;
    canvas.drawCircle(bodyCenter, bodyRadius * 0.42, cut);
  }

  @override
  bool shouldRepaint(covariant _PumpPainter oldDelegate) => oldDelegate.color != color;
}

/// Small sump/tank glyph with two level markers — replaces a plain
/// "Level OK / LOW" text pill with something that actually shows where
/// the water sits, per the hand-drawn layout: a container with L1 (the
/// lower switch) and L2 (upper) marked inside it, filled roughly to
/// whichever of the two discrete levels is currently wet.
class _TankGlyph extends StatelessWidget {
  final bool l1Ok;
  final bool l2Ok;
  final double size;
  const _TankGlyph({required this.l1Ok, required this.l2Ok, this.size = 48});

  @override
  Widget build(BuildContext context) => SizedBox(
        width: size * 0.8,
        height: size,
        child: CustomPaint(painter: _TankPainter(l1Ok: l1Ok, l2Ok: l2Ok)),
      );
}

class _TankPainter extends CustomPainter {
  final bool l1Ok;
  final bool l2Ok;
  _TankPainter({required this.l1Ok, required this.l2Ok});

  @override
  void paint(Canvas canvas, Size size) {
    final w = size.width, h = size.height;
    final outline = Paint()
      ..color = Colors.blueGrey.shade400
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2;
    final body = RRect.fromRectAndRadius(Rect.fromLTWH(2, 2, w - 4, h - 4), const Radius.circular(4));
    canvas.drawRRect(body, outline);

    // Both switches dry = below L1 (the lower one) = a real dry-run
    // condition, drawn nearly empty and in the warning color; anything
    // else is a plausible "there's water in here" state drawn in blue.
    final dry = !l1Ok && !l2Ok;
    final fillFrac = dry ? 0.10 : (l2Ok ? 0.85 : 0.45);
    final fillColor = (dry ? SchematicDiagram._warnColor : Colors.blue).withOpacity(0.55);
    final fillHeight = (h - 4) * fillFrac;
    final fillRect = RRect.fromRectAndRadius(
      Rect.fromLTWH(2, h - 2 - fillHeight, w - 4, fillHeight),
      const Radius.circular(2),
    );
    canvas.drawRRect(fillRect, Paint()..color = fillColor);

    // L1/L2 threshold tick marks inside the tank, lower (L1) below upper (L2).
    final marker = Paint()
      ..color = Colors.blueGrey.shade600
      ..strokeWidth = 1;
    final l1Y = h - 2 - (h - 4) * 0.30;
    final l2Y = h - 2 - (h - 4) * 0.65;
    canvas.drawLine(Offset(2, l2Y), Offset(w - 2, l2Y), marker);
    canvas.drawLine(Offset(2, l1Y), Offset(w - 2, l1Y), marker);
  }

  @override
  bool shouldRepaint(covariant _TankPainter oldDelegate) => oldDelegate.l1Ok != l1Ok || oldDelegate.l2Ok != l2Ok;
}

class _PipeSensorTile extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;
  const _PipeSensorTile({required this.icon, required this.label, required this.value});

  @override
  Widget build(BuildContext context) => Column(mainAxisSize: MainAxisSize.min, children: [
        Text(value, style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 12)),
        Text(label, style: TextStyle(fontSize: 9, color: Colors.grey.shade600)),
        const SizedBox(height: 2),
        Container(
          width: 30, height: 30,
          decoration: BoxDecoration(
            color: Colors.blueGrey.shade50,
            shape: BoxShape.circle,
            border: Border.all(color: Colors.blueGrey.shade200, width: 1.5),
          ),
          child: Icon(icon, size: 16, color: Colors.blueGrey.shade700),
        ),
        // Short stub connecting the badge down onto the pipe line below.
        Container(width: 2, height: 6, color: SchematicDiagram._pipeColor),
      ]);
}

class _ValveIcon extends StatelessWidget {
  final String label;
  final _ValvePhase phase;
  final bool physicallyOn;
  const _ValveIcon({required this.label, required this.phase, required this.physicallyOn});

  @override
  Widget build(BuildContext context) {
    final color = switch (phase) {
      _ValvePhase.done => SchematicDiagram._doneColor,
      _ValvePhase.running => SchematicDiagram._onColor,
      _ValvePhase.todo => SchematicDiagram._offColor,
    };
    return Column(children: [
      Container(
        width: 44,
        height: 44,
        decoration: BoxDecoration(
          color: color.withOpacity(0.12),
          shape: BoxShape.circle,
          border: Border.all(color: color, width: 2),
        ),
        child: Icon(Icons.opacity, color: color, size: 20),
      ),
      const SizedBox(height: 4),
      Text(label, textAlign: TextAlign.center, maxLines: 2, overflow: TextOverflow.ellipsis,
          style: TextStyle(fontSize: 10, fontWeight: FontWeight.w600, color: color)),
    ]);
  }
}

class _StatePill extends StatelessWidget {
  final String label;
  final Color color;
  const _StatePill({required this.label, required this.color});
  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
        decoration: BoxDecoration(color: color.withOpacity(0.12), borderRadius: BorderRadius.circular(10)),
        child: Text(label, style: TextStyle(color: color, fontSize: 11, fontWeight: FontWeight.w600)),
      );
}
