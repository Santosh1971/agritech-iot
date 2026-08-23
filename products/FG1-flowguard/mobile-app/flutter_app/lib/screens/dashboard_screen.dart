import 'dart:async';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../providers/providers.dart';
import '../models/device_status.dart';
import '../models/cycle.dart';
import '../models/history_entry.dart';
import '../services/device_service.dart';
import '../utils/time_format.dart';
import '../widgets/pump_icon.dart';

class DashboardScreen extends ConsumerStatefulWidget {
  const DashboardScreen({super.key});
  @override
  ConsumerState<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends ConsumerState<DashboardScreen> {
  bool _retrying = false;
  bool _didInitialRequest = false;
  Timer? _tickTimer;

  // Last-known Today's Summary, cached on the phone (SharedPreferences) so
  // it still shows something meaningful when the device has been offline
  // this whole app session and historyProvider never got a live response
  // — same "show the last known value rather than blank" principle as the
  // MQTT-retained status/cycles topics, just implemented locally since
  // history itself isn't retained on the broker (deliberately — it's an
  // append-only log, not a snapshot).
  double _cachedTodayTotal = 0;
  double _cachedManualTotal = 0;
  int _cachedTodayCycleCount = 0;
  HistoryEntry? _cachedLastEntry;

  Future<void> _loadCachedSummary() async {
    final prefs = await SharedPreferences.getInstance();
    final cachedDay = prefs.getString('summary_day');
    final today = DateTime.now();
    final todayKey = '${today.year}-${today.month}-${today.day}';
    // Only trust the cache if it's actually from today — yesterday's
    // totals showing up as "today's" would be misleading, not helpful.
    if (cachedDay != todayKey) return;
    final lastEntryJson = prefs.getString('summary_last_entry');
    if (!mounted) return;
    setState(() {
      _cachedTodayTotal = prefs.getDouble('summary_today_total') ?? 0;
      _cachedManualTotal = prefs.getDouble('summary_manual_total') ?? 0;
      _cachedTodayCycleCount = prefs.getInt('summary_today_count') ?? 0;
      _cachedLastEntry = lastEntryJson == null
          ? null
          : HistoryEntry.fromJson(jsonDecode(lastEntryJson) as Map<String, dynamic>);
    });
  }

  Future<void> _saveCachedSummary(double todayTotal, double manualTotal,
      int todayCycleCount, HistoryEntry? lastEntry) async {
    final prefs = await SharedPreferences.getInstance();
    final today = DateTime.now();
    await prefs.setString('summary_day', '${today.year}-${today.month}-${today.day}');
    await prefs.setDouble('summary_today_total', todayTotal);
    await prefs.setDouble('summary_manual_total', manualTotal);
    await prefs.setInt('summary_today_count', todayCycleCount);
    if (lastEntry != null) {
      await prefs.setString('summary_last_entry', jsonEncode(lastEntry.toJson()));
    }
  }

  @override
  void initState() {
    super.initState();
    _loadCachedSummary();
    // "Today's" totals are computed from DateTime.now() on every build(),
    // but build() only naturally re-runs when new data arrives (status
    // push, cycles/history update). If the connection is quiet for any
    // stretch spanning midnight — including the SoftAP flakiness seen in
    // testing — the display just sits stale showing yesterday's totals
    // until the next data push happens to arrive. This ticks a rebuild
    // every 30s regardless, so the day boundary is never more than that
    // stale even during a connectivity gap.
    _tickTimer = Timer.periodic(const Duration(seconds: 30), (_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    _tickTimer?.cancel();
    super.dispose();
  }

  Future<void> _connect() async {
    setState(() => _retrying = true);
    await ref.read(deviceServiceProvider).connect();
    if (mounted) setState(() => _retrying = false);
  }

  void _requestData() {
    final svc = ref.read(deviceServiceProvider);
    svc.getCycles();
    final now = DateTime.now();
    final startOfToday = DateTime(now.year, now.month, now.day);
    svc.getHistoryRange(startOfToday, now);
  }

  @override
  Widget build(BuildContext context) {
    final statusAsync = ref.watch(deviceStatusProvider);
    final connected = ref.watch(deviceConnectedProvider);
    final online = ref.watch(deviceOnlineProvider);
    final cyclesAsync = ref.watch(cyclesProvider);
    final historyAsync = ref.watch(historyProvider);
    final mqtt = ref.read(deviceServiceProvider);

    // Re-request cycles/history the moment the connection is (re)established
    // — covers first launch (screen builds before connect() resolves),
    // reconnects after a fallback/cloud switch, and WiFi-drop recovery.
    // Same race this fixes was affecting the Cycles screen too.
    ref.listen(deviceConnectedProvider, (prev, next) {
      final wasConnected = prev ?? false;
      if (next && !wasConnected) _requestData();
    });
    if (!_didInitialRequest && connected) {
      _didInitialRequest = true;
      WidgetsBinding.instance.addPostFrameCallback((_) => _requestData());
    }

    final cycles = cyclesAsync.maybeWhen(data: (c) => c, orElse: () => <Cycle>[]);
    final history = historyAsync.maybeWhen(data: (h) => h, orElse: () => <HistoryEntry>[]);
    final hasLiveHistory = historyAsync.hasValue;

    return Scaffold(
      appBar: AppBar(
        elevation: 0,
        // Small logo where the old (also non-functional) hamburger icon
        // used to be.
        leading: Padding(
          padding: const EdgeInsets.all(8),
          child: ClipRRect(
            borderRadius: BorderRadius.circular(8),
            child: Image.asset('assets/images/logo_icon.png', fit: BoxFit.contain),
          ),
        ),
        title: Text('Dashboard',
            style: TextStyle(color: Theme.of(context).colorScheme.onSurface, fontWeight: FontWeight.w600)),
        centerTitle: true,
        // The notification bell was purely decorative (empty onPressed,
        // no notification system exists) — removed rather than leave a
        // button that does nothing when tapped.
      ),
      body: statusAsync.maybeWhen(
        data: (s) => _buildDashboard(s, mqtt, cycles, history, connected, online, hasLiveHistory),
        orElse: () => _buildLoading(connected),
      ),
    );
  }

  Widget _buildLoading(bool connected) {
    final label = _retrying
        ? 'Connecting...'
        : connected
            ? 'Waiting for device...'
            : 'Not connected — retry?';
    return Center(
    child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
      ClipRRect(
        borderRadius: BorderRadius.circular(20),
        child: Image.asset('assets/images/logo_icon.png', width: 120, height: 120, fit: BoxFit.contain),
      ),
      const SizedBox(height: 24),
      Text(label, style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 16)),
      const SizedBox(height: 16),
      const CircularProgressIndicator(color: Color(0xFF2196F3)),
      const SizedBox(height: 24),
      ElevatedButton.icon(
        onPressed: _connect,
        icon: const Icon(Icons.refresh),
        label: const Text('Retry'),
        style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF2196F3)),
      ),
    ]));
  }

  Widget _buildDashboard(DeviceStatus s, DeviceService mqtt,
      List<Cycle> cycles, List<HistoryEntry> history,
      bool connected, bool online, bool hasLiveHistory) {
    // Today's real totals, computed from history — not the live
    // current-cycle field, which is legitimately 0 whenever nothing is
    // actively running right now.
    final now = DateTime.now();
    double todayTotal;
    double manualTotal;
    int todayCycleCount;
    HistoryEntry? lastEntry;
    if (hasLiveHistory) {
      final todayEntries = history.where((h) =>
          h.dateTime.year == now.year &&
          h.dateTime.month == now.month &&
          h.dateTime.day == now.day);
      todayTotal = todayEntries.fold<double>(0, (sum, h) => sum + h.litersDelivered);
      manualTotal = todayEntries
          .where((h) => h.status == 'manual')
          .fold<double>(0, (sum, h) => sum + h.litersDelivered);
      todayCycleCount = todayEntries.length;
      lastEntry = history.isEmpty ? null : history.reduce(
          (a, b) => a.timestamp >= b.timestamp ? a : b);
      _saveCachedSummary(todayTotal, manualTotal, todayCycleCount, lastEntry);
    } else {
      // No live history response this session (device offline the whole
      // time, most likely) — fall back to the last successfully computed
      // summary, cached locally on the phone, rather than showing 0/blank
      // just because this particular session never got a fresh answer.
      todayTotal = _cachedTodayTotal;
      manualTotal = _cachedManualTotal;
      todayCycleCount = _cachedTodayCycleCount;
      lastEntry = _cachedLastEntry;
    }

    // Next scheduled cycle — nearest enabled cycle's start time from now,
    // wrapping to tomorrow if everything today has already passed (cycles
    // repeat "Everyday" per the Cycles screen).
    Cycle? nextCycle;
    int nextCycleMinutesAway = -1;
    final enabled = cycles.where((c) => c.enabled).toList();
    if (enabled.isNotEmpty) {
      final nowMins = now.hour * 60 + now.minute;
      int best = 25 * 60; // sentinel > any possible same-day distance
      for (final c in enabled) {
        final startMins = c.startHour * 60 + c.startMinute;
        final away = startMins >= nowMins
            ? startMins - nowMins            // later today
            : (24 * 60 - nowMins) + startMins; // tomorrow
        if (away < best) { best = away; nextCycle = c; }
      }
      nextCycleMinutesAway = best;
    }

    return RefreshIndicator(
      onRefresh: () async { await _connect(); _requestData(); },
      child: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _DeviceBanner(status: s, connected: online),
          const SizedBox(height: 16),

          Row(children: [
            Expanded(child: _PumpStatusCard(status: s)),
            const SizedBox(width: 12),
            Expanded(child: _ProgressCard(todayTotal: todayTotal)),
          ]),
          const SizedBox(height: 12),

          Row(children: [
            Expanded(child: _InfoCard(
              icon: Icons.access_time,
              iconColor: const Color(0xFF2196F3),
              label: 'Next Cycle',
              value: nextCycle == null ? '--:--' : nextCycle.startTimeStr,
              sub: nextCycle == null
                  ? 'None scheduled'
                  : nextCycleMinutesAway < 60
                      ? 'in ${nextCycleMinutesAway}m'
                      : 'in ${(nextCycleMinutesAway / 60).floor()}h ${nextCycleMinutesAway % 60}m',
            )),
            const SizedBox(width: 12),
            Expanded(child: _InfoCard(
              icon: Icons.water_drop,
              iconColor: const Color(0xFF2196F3),
              label: 'Next Cycle Target',
              value: nextCycle == null || nextCycle.mode == OperationMode.timeBased
                  ? '--'
                  : '${nextCycle.targetLiters.toStringAsFixed(1)} L',
              sub: '',
            )),
          ]),
          const SizedBox(height: 12),

          if (s.cycleActive) ...[
            _ActiveCycleCard(status: s, mqtt: mqtt, cycles: cycles),
            const SizedBox(height: 12),
          ],

          _SummaryCard(
            todayTotal: todayTotal,
            todayCycleCount: todayCycleCount,
            manualTotal: manualTotal,
            lastEntry: lastEntry,
          ),
          const SizedBox(height: 12),

          _LastCycleCard(status: s),
        ],
      ),
    );
  }
}

// ── Device Banner ──────────────────────────────────────────
class _DeviceBanner extends StatelessWidget {
  final DeviceStatus status;
  final bool connected;
  const _DeviceBanner({required this.status, required this.connected});

  @override
  Widget build(BuildContext context) {
    // Whether the DEVICE ITSELF is actually online right now — driven by
    // deviceOnlineProvider (LWT-based in Cloud mode), not just whether the
    // phone has a link to the broker (which stays up even when the device
    // is powered off, since the broker is on a separate always-on server).
    final online = connected;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05),
            blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Row(children: [
        Icon(online ? Icons.wifi : Icons.wifi_off,
             color: online ? const Color(0xFF4CAF50) : Colors.red, size: 28),
        const SizedBox(width: 12),
        Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(online ? 'Device Connected' : 'Device Offline',
               style: TextStyle(
                 fontWeight: FontWeight.w600, fontSize: 15,
                 color: online ? const Color(0xFF4CAF50) : Colors.red)),
          Text(status.deviceId.isEmpty ? 'Searching...' : status.deviceId,
               style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 12)),
        ]),
        const Spacer(),
        Container(
          padding: const EdgeInsets.all(8),
          decoration: BoxDecoration(
            color: Colors.grey.shade100,
            borderRadius: BorderRadius.circular(8)),
          child: const Icon(Icons.router, color: Colors.grey, size: 28)),
      ]),
    );
  }
}

// ── Pump Status Card ──────────────────────────────────────
class _PumpStatusCard extends StatelessWidget {
  final DeviceStatus status;
  const _PumpStatusCard({required this.status});

  @override
  Widget build(BuildContext context) {
    final on = status.pumpOn;
    final start = status.cycleStartTime;
    final sinceLabel = start != null
        ? 'Since ${formatTime12(start.hour, start.minute)}'
        : 'Since --:--';
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05),
            blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text('Pump Status',
            style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 12)),
        const SizedBox(height: 8),
        Center(
          child: PumpIcon(size: 56, isOn: on,
              color: on ? const Color(0xFFFF9800) : Colors.grey.shade400),
        ),
        const SizedBox(height: 8),
        Center(
          child: Text(on ? 'ON' : 'OFF',
              style: TextStyle(
                fontSize: 18, fontWeight: FontWeight.bold,
                color: on ? const Color(0xFF4CAF50) : Colors.grey)),
        ),
        if (on) ...[
          const SizedBox(height: 4),
          Center(
            child: Text(sinceLabel,
                style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 11))),
        ],
      ]),
    );
  }
}

// ── Progress Ring Card ────────────────────────────────────
class _ProgressCard extends StatelessWidget {
  final double todayTotal;
  const _ProgressCard({required this.todayTotal});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05),
            blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
        Text('Today\'s Progress',
            style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 12)),
        const SizedBox(height: 12),
        Icon(Icons.water_drop, color: const Color(0xFF2196F3), size: 32),
        const SizedBox(height: 4),
        // FittedBox so this can never overflow regardless of how large
        // the number gets (the old fixed-size circle had no such
        // protection, which was the source of the reported UI overlap).
        FittedBox(
          fit: BoxFit.scaleDown,
          child: Text(todayTotal.toStringAsFixed(1),
              style: TextStyle(
                fontSize: 22, fontWeight: FontWeight.bold,
                color: Theme.of(context).colorScheme.onSurface)),
        ),
        Text('Liters today',
            style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 11)),
      ]),
    );
  }
}

// ── Info Card ─────────────────────────────────────────────
class _InfoCard extends StatelessWidget {
  final IconData icon;
  final Color iconColor;
  final String label, value, sub;
  const _InfoCard({required this.icon, required this.iconColor,
                   required this.label, required this.value, required this.sub});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05),
            blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Row(children: [
        Icon(icon, color: iconColor, size: 22),
        const SizedBox(width: 8),
        Expanded(child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(label, style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 11)),
          Text(value, style: const TextStyle(
              fontWeight: FontWeight.bold, color: Color(0xFF2196F3), fontSize: 14)),
          if (sub.isNotEmpty)
            Text(sub, style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 11)),
        ])),
      ]),
    );
  }
}

// ── Active Cycle Card ─────────────────────────────────────
class _ActiveCycleCard extends StatelessWidget {
  final DeviceStatus status;
  final DeviceService mqtt;
  final List<Cycle> cycles;
  const _ActiveCycleCard({required this.status, required this.mqtt, required this.cycles});

  @override
  Widget build(BuildContext context) {
    final paused = status.cyclePaused;
    final isManual = status.cycleId == 255;
    Cycle? cfg;
    if (!isManual) {
      for (final c in cycles) {
        if (c.id == status.cycleId) { cfg = c; break; }
      }
    }

    final start = status.cycleStartTime;
    final startStr = start == null ? '--:--'
        : formatTime12(start.hour, start.minute);
    final elapsedSecs = status.elapsedSeconds;
    String fmtMinSec(int totalSeconds) {
      final m = totalSeconds ~/ 60;
      final s = totalSeconds % 60;
      return m > 0 ? '${m}m ${s}s' : '${s}s';
    }

    if (isManual) {
      // A manual start has no schedule config behind it -- no duration
      // or liters target was ever set, since the point is just "run
      // until I say stop." So this shows only what's actually true:
      // when it started, how long it's been going, and how much water
      // has gone out so far -- framed as a running total, not progress
      // toward some target that was never set. No pause (pausing an
      // indefinite run doesn't mean anything) and no "cycle" language
      // (it isn't one) -- just Stop.
      return Container(
        decoration: BoxDecoration(
          color: const Color(0xFF4CAF50),
          borderRadius: BorderRadius.circular(12),
        ),
        child: Column(children: [
          const Padding(
            padding: EdgeInsets.all(12),
            child: Row(children: [
              Icon(Icons.touch_app, color: Colors.white, size: 18),
              SizedBox(width: 8),
              Text('Manual ON',
                  style: TextStyle(color: Colors.white,
                      fontWeight: FontWeight.bold)),
            ]),
          ),
          Container(
            margin: const EdgeInsets.fromLTRB(8, 0, 8, 8),
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
              color: Theme.of(context).cardColor,
              borderRadius: BorderRadius.circular(8)),
            child: Column(children: [
              Row(children: [
                _CycleDetailItem(label: 'Start Time', value: startStr),
                _CycleDetailItem(label: 'Elapsed', value: fmtMinSec(elapsedSecs),
                    valueColor: const Color(0xFF2196F3)),
                _CycleDetailItem(label: 'Liters',
                    value: '${status.litersDelivered.toStringAsFixed(1)} L',
                    valueColor: const Color(0xFF2196F3)),
              ]),
              const SizedBox(height: 12),
              SizedBox(
                width: double.infinity,
                child: ElevatedButton.icon(
                  onPressed: () => mqtt.manualOff(),
                  icon: const Icon(Icons.stop, size: 18, color: Colors.white),
                  label: const Text('STOP', style: TextStyle(color: Colors.white)),
                  style: ElevatedButton.styleFrom(backgroundColor: Colors.red),
                ),
              ),
            ]),
          ),
        ]),
      );
    }

    final durationStr = (cfg != null && cfg.mode != OperationMode.literBased)
        ? cfg.durationStr : '--';
    final targetStr = cfg != null && cfg.mode != OperationMode.timeBased
        ? '${cfg.targetLiters.toStringAsFixed(1)} L' : '--';

    final hasTarget = cfg != null && cfg.mode != OperationMode.timeBased && cfg.targetLiters > 0;
    // cfg! below is safe: hasTarget being true guarantees cfg != null (see
    // its definition above) — the analyzer just can't prove that fact
    // through a separate boolean variable, so the assertion is needed.
    final remaining = hasTarget
        ? (cfg.targetLiters - status.litersDelivered).clamp(0.0, cfg.targetLiters) : null;
    final progress = hasTarget
        ? (status.litersDelivered / cfg.targetLiters).clamp(0.0, 1.0) : null;

    // Duration progress — the counterpart to the liters progress above,
    // for modes that have a duration target (timeBased, timeWindowLiter).
    // elapsedSeconds is the firmware's accumulated ACTIVE (relay-on)
    // time, so this stays accurate across a pause/resume or even a
    // power-outage mid-cycle — it's driven by the same field the
    // firmware itself uses to decide when to stop.
    final hasDuration = cfg != null && cfg.mode != OperationMode.literBased
        && cfg.durationMinutes > 0;
    final durationTargetSecs = hasDuration ? cfg.durationMinutes * 60 : 0;
    final remainingSecs = hasDuration
        ? (durationTargetSecs - elapsedSecs).clamp(0, durationTargetSecs) : null;
    final durationProgress = hasDuration
        ? (elapsedSecs / durationTargetSecs).clamp(0.0, 1.0) : null;

    return Container(
      decoration: BoxDecoration(
        color: const Color(0xFF4CAF50),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(children: [
        Padding(
          padding: const EdgeInsets.all(12),
          child: Row(children: [
            const Icon(Icons.loop, color: Colors.white, size: 18),
            const SizedBox(width: 8),
            Text('Active Cycle — ${paused ? "Paused" : "Running"}',
                style: const TextStyle(color: Colors.white,
                    fontWeight: FontWeight.bold)),
          ]),
        ),
        Container(
          margin: const EdgeInsets.fromLTRB(8, 0, 8, 8),
          padding: const EdgeInsets.all(12),
          decoration: BoxDecoration(
            color: Theme.of(context).cardColor,
            borderRadius: BorderRadius.circular(8)),
          child: Column(children: [
            Row(children: [
              _CycleDetailItem(label: 'Start Time', value: startStr),
              _CycleDetailItem(label: 'Duration', value: durationStr),
              _CycleDetailItem(label: 'Target', value: targetStr),
            ]),
            const Divider(height: 16),
            if (hasDuration) ...[
              Row(children: [
                _CycleDetailItem(
                    label: 'Elapsed',
                    value: fmtMinSec(elapsedSecs),
                    valueColor: const Color(0xFF2196F3)),
                _CycleDetailItem(label: 'Remaining',
                    value: remainingSecs == null ? '--' : fmtMinSec(remainingSecs),
                    valueColor: const Color(0xFFFF9800)),
                _CycleDetailItem(label: 'Progress',
                    value: durationProgress == null ? '--%'
                        : '${(durationProgress * 100).toStringAsFixed(0)}%',
                    valueColor: const Color(0xFF4CAF50)),
              ]),
              const SizedBox(height: 8),
              LinearProgressIndicator(
                value: durationProgress ?? 0.0,
                backgroundColor: Colors.grey.shade200,
                color: const Color(0xFF4CAF50),
                minHeight: 6,
              ),
              const SizedBox(height: 12),
            ],
            if (hasTarget) ...[
            Row(children: [
              _CycleDetailItem(
                  label: 'Delivered',
                  // This value now correctly persists across pause/resume
                  // (firmware fix — was previously reset by an accounting
                  // bug in the periodic state-save).
                  value: '${status.litersDelivered.toStringAsFixed(1)} L',
                  valueColor: const Color(0xFF2196F3)),
              _CycleDetailItem(label: 'Remaining',
                  value: remaining == null ? '-- L' : '${remaining.toStringAsFixed(1)} L',
                  valueColor: const Color(0xFFFF9800)),
              _CycleDetailItem(label: 'Progress',
                  value: progress == null ? '--%' : '${(progress * 100).toStringAsFixed(0)}%',
                  valueColor: const Color(0xFF4CAF50)),
            ]),
            const SizedBox(height: 8),
            LinearProgressIndicator(
              value: progress ?? 0.0,
              backgroundColor: Colors.grey.shade200,
              color: const Color(0xFF4CAF50),
              minHeight: 6,
            ),
            ],
            const SizedBox(height: 12),
            Row(children: [
              Expanded(
                child: OutlinedButton.icon(
                  onPressed: paused ? () => mqtt.resumeCycle()
                                    : () => mqtt.pauseCycle(),
                  icon: Icon(paused ? Icons.play_arrow : Icons.pause, size: 18),
                  label: Text(paused ? 'Resume' : 'Pause'),
                  style: OutlinedButton.styleFrom(
                    foregroundColor: const Color(0xFF4CAF50),
                    side: const BorderSide(color: Color(0xFF4CAF50))),
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: ElevatedButton.icon(
                  onPressed: () => mqtt.stopCycle(),
                  icon: const Icon(Icons.stop, size: 18, color: Colors.white),
                  label: const Text('STOP CYCLE',
                      style: TextStyle(color: Colors.white)),
                  style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.red),
                ),
              ),
            ]),
          ]),
        ),
      ]),
    );
  }
}

class _CycleDetailItem extends StatelessWidget {
  final String label, value;
  final Color? valueColor;
  const _CycleDetailItem({required this.label, required this.value,
                           this.valueColor});
  @override
  Widget build(BuildContext context) => Expanded(
    child: Column(children: [
      Text(label, style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 11)),
      const SizedBox(height: 2),
      Text(value, style: TextStyle(
          fontWeight: FontWeight.bold, fontSize: 13,
          color: valueColor ?? Theme.of(context).colorScheme.onSurface)),
    ]),
  );
}

// ── Summary Card ──────────────────────────────────────────
class _SummaryCard extends StatelessWidget {
  final double todayTotal;
  final int todayCycleCount;
  final double manualTotal;
  final HistoryEntry? lastEntry;
  const _SummaryCard({required this.todayTotal, required this.todayCycleCount,
                      required this.manualTotal, required this.lastEntry});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05),
            blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        const Text("Today's Summary",
            style: TextStyle(fontWeight: FontWeight.w600, fontSize: 15)),
        const SizedBox(height: 12),
        Row(children: [
          _SummaryItem(label: 'Total Water',
              value: '${todayTotal.toStringAsFixed(1)} L',
              color: const Color(0xFF2196F3)),
          _SummaryItem(label: 'Total Cycles',
              value: '$todayCycleCount',
              color: const Color(0xFF4CAF50)),
          _SummaryItem(label: 'Manual Use',
              value: '${manualTotal.toStringAsFixed(1)} L',
              color: const Color(0xFFFF9800)),
        ]),
        const SizedBox(height: 12),
        Row(
          children: [
            Icon(
                lastEntry == null ? Icons.info_outline : Icons.check_circle,
                color: lastEntry == null ? Colors.grey : const Color(0xFF4CAF50),
                size: 16),
            const SizedBox(width: 6),
            Text(
                lastEntry == null
                    ? 'No completed cycles yet'
                    : 'Last: ${lastEntry!.cycleName.isEmpty ? "Manual" : lastEntry!.cycleName} '
                      '(${lastEntry!.litersDelivered.toStringAsFixed(1)} L)',
                style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 12)),
            const Spacer(),
            if (lastEntry != null)
              Text(lastEntry!.status,
                  style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 12)),
          ],
        ),
      ]),
    );
  }
}

class _SummaryItem extends StatelessWidget {
  final String label, value;
  final Color color;
  const _SummaryItem({required this.label, required this.value,
                      required this.color});
  @override
  Widget build(BuildContext context) => Expanded(
    child: Column(children: [
      Text(value, style: TextStyle(
          fontWeight: FontWeight.bold, fontSize: 16, color: color)),
      Text(label, style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 11)),
    ]),
  );
}

// ── Device Info Card ───────────────────────────────────────
class _LastCycleCard extends StatelessWidget {
  final DeviceStatus status;
  const _LastCycleCard({required this.status});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05),
            blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Row(children: [
        Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          const Text('Device Info',
              style: TextStyle(fontWeight: FontWeight.w600, fontSize: 15)),
          const SizedBox(height: 8),
          Text('RTC: ${status.rtcDate} ${formatTime12FromString(status.rtcTime)}',
              style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 12)),
          Text('WiFi: ${status.wifiRssi} dBm',
              style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 12)),
          Text('Firmware: ${status.firmware}',
              style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 12)),
        ]),
      ]),
    );
  }
}
