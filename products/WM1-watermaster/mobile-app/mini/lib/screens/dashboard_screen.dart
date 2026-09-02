import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/providers.dart';
import '../models/device_status.dart';
import '../models/program.dart';
import 'local_setup_screen.dart';
import 'schematic_diagram.dart';

class DashboardScreen extends ConsumerStatefulWidget {
  const DashboardScreen({super.key});
  @override
  ConsumerState<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends ConsumerState<DashboardScreen> {
  bool _didInitialConnect = false;

  @override
  Widget build(BuildContext context) {
    final connected = ref.watch(deviceConnectedProvider);
    final statusAsync = ref.watch(deviceStatusProvider);
    final mode = ref.watch(transportModeProvider);
    final hardwareConfig = ref.watch(hardwareConfigProvider);

    if (!_didInitialConnect) {
      _didInitialConnect = true;
      WidgetsBinding.instance.addPostFrameCallback((_) => ref.read(deviceServiceProvider).connect());
    }

    final status = statusAsync.valueOrNull ?? DeviceStatus.empty();

    return Scaffold(
      appBar: AppBar(
        // Same brand mark FG1/WPC use in their own AppBars.
        leading: Padding(
          padding: const EdgeInsets.all(8),
          child: ClipRRect(
            borderRadius: BorderRadius.circular(8),
            child: Image.asset('assets/images/logo_icon.png', fit: BoxFit.contain),
          ),
        ),
        title: const Text('NB Agri-WM', style: TextStyle(fontWeight: FontWeight.w600)),
        centerTitle: true,
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 12),
            child: Center(
              child: Chip(
                label: Text(mode == TransportMode.local ? 'Local' : 'Cloud', style: const TextStyle(fontSize: 12)),
                avatar: Icon(mode == TransportMode.local ? Icons.wifi_tethering : Icons.cloud, size: 16),
                visualDensity: VisualDensity.compact,
              ),
            ),
          ),
        ],
      ),
      body: RefreshIndicator(
        onRefresh: () async => ref.read(deviceServiceProvider).connect(),
        child: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            _ConnectionCard(connected: connected, status: status),
            const SizedBox(height: 16),
            if (!connected) ...[
              _ConnectionLogCard(),
              const SizedBox(height: 16),
            ],
            if (connected && !status.rtcOk) ...[
              _RtcWarningCard(),
              const SizedBox(height: 16),
            ],
            if (status.state == SchedulerState.running || status.state == SchedulerState.paused)
              _ActiveRunCard(status: status),
            if (status.state == SchedulerState.running || status.state == SchedulerState.paused)
              const SizedBox(height: 16),
            SchematicDiagram(status: status, config: hardwareConfig, connected: connected),
            if (connected && status.nextRunEpoch != null) ...[
              const SizedBox(height: 16),
              _UpcomingScheduleCard(status: status),
            ],
          ],
        ),
      ),
    );
  }
}

class _ConnectionCard extends ConsumerWidget {
  final bool connected;
  final DeviceStatus status;
  const _ConnectionCard({required this.connected, required this.status});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final color = connected ? const Color(0xFF4CAF50) : Colors.grey;
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: connected ? color.withOpacity(0.08) : Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: connected ? color : Colors.grey.shade300),
      ),
      child: Row(children: [
        Icon(connected ? Icons.check_circle : Icons.error_outline, color: color, size: 28),
        const SizedBox(width: 12),
        Expanded(
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text(connected ? 'Device Online' : 'Device Offline',
                style: TextStyle(fontWeight: FontWeight.w600, color: connected ? color : Colors.grey)),
            if (connected)
              Text(
                status.deviceId.isEmpty ? '—' : '${status.deviceId} • RTC ${status.rtcDate} ${status.rtcTime}',
                style: const TextStyle(color: Colors.grey, fontSize: 12),
              ),
          ]),
        ),
        if (connected && status.forcedLocal)
          const Padding(
            padding: EdgeInsets.only(left: 8),
            child: Chip(label: Text('Forced Local', style: TextStyle(fontSize: 11)), visualDensity: VisualDensity.compact),
          ),
        // Explicit manual retry — for exactly the case where the app was
        // opened before actually joining the device's WiFi, or the
        // phone switched networks while the app sat open. In Local
        // mode this calls retryNow(), which forces a fresh WiFi bind
        // (the background auto-retry loop deliberately reuses the
        // existing bind to avoid hammering Android's network APIs —
        // see LocalService's _minRebindInterval — so a plain connect()
        // here could silently do nothing right after switching networks).
        if (!connected)
          FilledButton.icon(
            onPressed: () {
              if (ref.read(transportModeProvider) == TransportMode.local) {
                ref.read(localServiceProvider).retryNow();
              } else {
                ref.read(deviceServiceProvider).connect();
              }
            },
            icon: const Icon(Icons.refresh, size: 18),
            label: const Text('Retry'),
          ),
      ]),
    );
  }
}

class _ConnectionLogCard extends ConsumerWidget {
  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final log = ref.watch(localDebugLogProvider);
    final recent = log.length > 6 ? log.sublist(log.length - 6) : log;
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05), blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          const Text('WHY OFFLINE?', style: TextStyle(fontSize: 11, fontWeight: FontWeight.w600, letterSpacing: 0.5, color: Colors.grey)),
          const Spacer(),
          TextButton(
            onPressed: () => Navigator.push(context, MaterialPageRoute(builder: (_) => const LocalSetupScreen())),
            child: const Text('Full log', style: TextStyle(fontSize: 12)),
          ),
        ]),
        const SizedBox(height: 6),
        if (recent.isEmpty)
          const Text('(nothing logged yet — pull to refresh)', style: TextStyle(color: Colors.grey, fontSize: 12))
        else
          ...recent.map((l) => Padding(
                padding: const EdgeInsets.only(bottom: 2),
                child: Text(l, style: const TextStyle(fontFamily: 'monospace', fontSize: 11)),
              )),
      ]),
    );
  }
}

class _RtcWarningCard extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.orange.withOpacity(0.1),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.orange.withOpacity(0.4)),
      ),
      child: Row(children: [
        const Icon(Icons.warning_amber, color: Colors.orange),
        const SizedBox(width: 12),
        const Expanded(
          child: Text('RTC not set — schedules will not trigger until the device\'s clock is synced.',
              style: TextStyle(color: Colors.deepOrange, fontSize: 13)),
        ),
        TextButton(
          onPressed: () => Navigator.push(context, MaterialPageRoute(builder: (_) => const LocalSetupScreen())),
          child: const Text('Fix'),
        ),
      ]),
    );
  }
}

/// Next auto-fire across every enabled+autoStart program — the epoch
/// comes from the device (Scheduler::computeNextRun), not re-derived
/// here, since the app never sees the per-day tracking needed to know
/// whether an interval-days program is actually due today. The epoch
/// uses the same "wall-clock fields reinterpreted as UTC" convention
/// as rtc_sync/rtc_date/rtc_time elsewhere in this app — decoding it
/// with isUtc:true recovers those same fields directly, no timezone
/// math needed.
class _UpcomingScheduleCard extends StatelessWidget {
  final DeviceStatus status;
  const _UpcomingScheduleCard({required this.status});

  @override
  Widget build(BuildContext context) {
    final target = DateTime.fromMillisecondsSinceEpoch(status.nextRunEpoch! * 1000, isUtc: true);
    final now = DateTime.now();
    final nowAsDeviceEpoch = DateTime.utc(now.year, now.month, now.day, now.hour, now.minute, now.second);
    final diff = target.difference(nowAsDeviceEpoch);

    String dayLabel;
    final sameDay = target.year == nowAsDeviceEpoch.year && target.month == nowAsDeviceEpoch.month && target.day == nowAsDeviceEpoch.day;
    final tomorrow = nowAsDeviceEpoch.add(const Duration(days: 1));
    final isTomorrow = target.year == tomorrow.year && target.month == tomorrow.month && target.day == tomorrow.day;
    if (sameDay) {
      dayLabel = 'Today';
    } else if (isTomorrow) {
      dayLabel = 'Tomorrow';
    } else {
      const weekdays = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
      dayLabel = weekdays[target.weekday - 1];
    }
    final h12 = target.hour % 12 == 0 ? 12 : target.hour % 12;
    final period = target.hour >= 12 ? 'PM' : 'AM';
    final timeStr = '$h12:${target.minute.toString().padLeft(2, '0')} $period';

    String countdown;
    if (diff.inMinutes < 1) {
      countdown = 'starting soon';
    } else if (diff.inHours < 1) {
      countdown = 'in ${diff.inMinutes} min';
    } else if (diff.inHours < 24) {
      countdown = 'in ${diff.inHours}h ${diff.inMinutes % 60}m';
    } else {
      countdown = 'in ${diff.inDays}d';
    }

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05), blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Row(children: [
        Container(
          padding: const EdgeInsets.all(10),
          decoration: BoxDecoration(color: const Color(0xFF2196F3).withOpacity(0.12), shape: BoxShape.circle),
          child: const Icon(Icons.event_available, color: Color(0xFF2196F3), size: 22),
        ),
        const SizedBox(width: 12),
        Expanded(
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            const Text('UPCOMING', style: TextStyle(fontSize: 11, fontWeight: FontWeight.w600, letterSpacing: 0.5, color: Colors.grey)),
            const SizedBox(height: 2),
            Text(status.nextRunProgramName ?? 'Program', style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 15)),
            Text('$dayLabel at $timeStr', style: TextStyle(fontSize: 12, color: Colors.grey.shade600)),
          ]),
        ),
        Text(countdown, style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w600, color: Color(0xFF2196F3))),
      ]),
    );
  }
}

class _ActiveRunCard extends ConsumerWidget {
  final DeviceStatus status;
  const _ActiveRunCard({required this.status});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // Bug fix: this used to always show elapsed/target SECONDS, even for
    // a volume-mode sequence — confirmed live (a volume run showed "5
    // min" progress that had nothing to do with what was actually
    // happening, since it kept running well past that mark). runMode
    // says which pair of fields actually determines completion here.
    final isVolume = status.runMode == 'volume';
    final elapsedLiters = status.elapsedLiters ?? 0;
    final targetLiters = (status.runTargetLiters ?? 1).clamp(1, 1 << 30);
    final elapsed = status.elapsedSec ?? 0;
    final target = status.runTargetSec ?? 1;
    final progress = isVolume
        ? (elapsedLiters / targetLiters).clamp(0.0, 1.0)
        : (elapsed / target).clamp(0.0, 1.0);
    final paused = status.state == SchedulerState.paused;
    final activeIndex = status.activeSeqIndex ?? 0;

    final programs = ref.watch(programsProvider).valueOrNull ?? const [];
    Program? activeProgram;
    for (final p in programs) {
      if (p.id == status.activeProgramId) { activeProgram = p; break; }
    }

    final pausedReason = switch (status.pauseReason) {
      'water' => 'water low',
      'power' => 'no power',
      'manual' => 'by user',
      _ => 'unknown',
    };

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05), blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          Icon(paused ? Icons.pause_circle : Icons.play_circle, color: paused ? Colors.orange : Colors.green),
          const SizedBox(width: 8),
          Expanded(
            child: Text('${status.activeProgramName ?? "Program"} — ${status.activeSequenceName ?? ""}',
                style: const TextStyle(fontWeight: FontWeight.w600)),
          ),
          if (paused)
            Text('PAUSED — $pausedReason', style: const TextStyle(color: Colors.orange, fontSize: 12, fontWeight: FontWeight.w600)),
        ]),
        const SizedBox(height: 10),
        ClipRRect(
          borderRadius: BorderRadius.circular(6),
          child: LinearProgressIndicator(value: progress, minHeight: 8,
              color: paused ? Colors.orange : Colors.green, backgroundColor: Colors.grey.shade200),
        ),
        const SizedBox(height: 6),
        Text(
          isVolume
              ? '${elapsedLiters.toStringAsFixed(1)} / $targetLiters L'
              : '${_fmt(elapsed)} / ${_fmt(target)}',
          style: const TextStyle(fontSize: 12, color: Colors.grey),
        ),
        if (activeProgram != null && activeProgram.sequences.length > 1) ...[
          const Divider(height: 24),
          const Text('SEQUENCE TIMELINE', style: TextStyle(fontSize: 10, fontWeight: FontWeight.w600, color: Colors.grey, letterSpacing: 0.5)),
          const SizedBox(height: 8),
          for (int i = 0; i < activeProgram.sequences.length; i++)
            _SequenceTimelineRow(
              sequence: activeProgram.sequences[i],
              relayNames: status.relayNames,
              phase: i < activeIndex ? _SeqPhase.done : (i == activeIndex ? _SeqPhase.current : _SeqPhase.upcoming),
            ),
        ],
      ]),
    );
  }

  String _fmt(int sec) {
    final m = sec ~/ 60, s = sec % 60;
    return '${m}m ${s.toString().padLeft(2, '0')}s';
  }
}

enum _SeqPhase { done, current, upcoming }

class _SequenceTimelineRow extends StatelessWidget {
  final Sequence sequence;
  final RelayNames relayNames;
  final _SeqPhase phase;
  const _SequenceTimelineRow({required this.sequence, required this.relayNames, required this.phase});

  @override
  Widget build(BuildContext context) {
    final (icon, color) = switch (phase) {
      _SeqPhase.done => (Icons.check_circle, const Color(0xFF4CAF50)),
      _SeqPhase.current => (Icons.play_circle_fill, const Color(0xFF2196F3)),
      _SeqPhase.upcoming => (Icons.radio_button_unchecked, Colors.grey),
    };
    final valveLabels = [
      for (int i = 0; i < 4; i++)
        if (sequence.valveOn(i)) relayNames.valveDisplay(i),
    ].join(', ');
    final minutes = sequence.runTargetSec ~/ 60;
    final secs = sequence.runTargetSec % 60;
    final duration = sequence.runMode == RunMode.time
        ? '${minutes}m${secs > 0 ? ' ${secs}s' : ''}'
        : '${sequence.runTargetLiters} L';

    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Icon(icon, size: 18, color: color),
        const SizedBox(width: 8),
        Expanded(
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text(sequence.name,
                style: TextStyle(fontWeight: phase == _SeqPhase.current ? FontWeight.w600 : FontWeight.w400,
                    fontSize: 13, color: phase == _SeqPhase.upcoming ? Colors.grey : null)),
            Text('${valveLabels.isEmpty ? "no valves" : valveLabels} • $duration${sequence.doseEnabled ? " • dosing" : ""}',
                style: TextStyle(fontSize: 11, color: Colors.grey.shade600)),
          ]),
        ),
      ]),
    );
  }
}

