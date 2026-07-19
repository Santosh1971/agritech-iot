import 'package:flutter/material.dart';
import '../models/cycle.dart';

class AddCycleScreen extends StatefulWidget {
  final Cycle? cycle;
  final List<Cycle> existingCycles;
  final Function(List<Cycle>) onSaved;

  const AddCycleScreen({
    super.key,
    this.cycle,
    required this.existingCycles,
    required this.onSaved,
  });

  @override
  State<AddCycleScreen> createState() => _AddCycleScreenState();
}

class _AddCycleScreenState extends State<AddCycleScreen> {
  final _nameController   = TextEditingController();
  final _litersController = TextEditingController(text: '20.0');
  OperationMode _mode     = OperationMode.timeWindowLiter;
  TimeOfDay _startTime    = const TimeOfDay(hour: 10, minute: 0);
  TimeOfDay _endTime      = const TimeOfDay(hour: 11, minute: 0);
  bool _enabled           = true;

  @override
  void initState() {
    super.initState();
    if (widget.cycle != null) {
      final c = widget.cycle!;
      _nameController.text   = c.name;
      _litersController.text = c.targetLiters.toStringAsFixed(1);
      _mode      = c.mode;
      _startTime = TimeOfDay(hour: c.startHour, minute: c.startMinute);
      _endTime   = TimeOfDay(hour: c.endHour,   minute: c.endMinute);
      _enabled   = c.enabled;
    }
  }

  @override
  void dispose() {
    _nameController.dispose();
    _litersController.dispose();
    super.dispose();
  }

  Future<void> _pickTime(bool isStart) async {
    final picked = await showTimePicker(
      context: context,
      initialTime: isStart ? _startTime : _endTime,
    );
    if (picked != null) setState(() {
      if (isStart) _startTime = picked; else _endTime = picked;
    });
  }

  void _save() {
    final newCycle = Cycle(
      id: widget.cycle?.id ?? (widget.existingCycles.length + 1),
      name:         _nameController.text.trim(),
      startHour:    _startTime.hour,
      startMinute:  _startTime.minute,
      endHour:      _endTime.hour,
      endMinute:    _endTime.minute,
      mode:         _mode,
      targetLiters: double.tryParse(_litersController.text) ?? 20.0,
      enabled:      _enabled,
    );

    final updated = List<Cycle>.from(widget.existingCycles);
    if (widget.cycle != null) {
      final idx = updated.indexWhere((c) => c.id == widget.cycle!.id);
      if (idx >= 0) updated[idx] = newCycle; else updated.add(newCycle);
    } else {
      updated.add(newCycle);
    }

    widget.onSaved(updated);
    Navigator.pop(context);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        elevation: 0,
        leading: BackButton(color: Theme.of(context).colorScheme.onSurface),
        title: Text(widget.cycle != null ? 'Edit Cycle' : 'Add Cycle',
            style: TextStyle(color: Theme.of(context).colorScheme.onSurface,
                fontWeight: FontWeight.w600)),
        centerTitle: true,
        actions: [
          TextButton(
            onPressed: _save,
            child: const Text('Save',
                style: TextStyle(color: Color(0xFF2196F3),
                    fontWeight: FontWeight.w600, fontSize: 16)),
          ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          // Cycle Name
          _Card(children: [
            _Label('Cycle Name (Optional)'),
            TextField(
              controller: _nameController,
              decoration: _dec('e.g. Morning Watering'),
            ),
          ]),
          const SizedBox(height: 12),

          // Start Time
          _Card(children: [
            _Label('Cycle Start Time'),
            _TimePicker(
                time: _startTime, onTap: () => _pickTime(true)),
          ]),
          const SizedBox(height: 12),

          // Operation Mode
          _Card(children: [
            _Label('Operation Mode'),
            const SizedBox(height: 8),
            Row(children: [
              _ModeCard(
                icon: Icons.water_drop,
                label: 'Liter\nBased',
                selected: _mode == OperationMode.literBased,
                color: const Color(0xFF2196F3),
                onTap: () => setState(
                    () => _mode = OperationMode.literBased),
              ),
              const SizedBox(width: 8),
              _ModeCard(
                icon: Icons.access_time,
                label: 'Time\nBased',
                selected: _mode == OperationMode.timeBased,
                color: const Color(0xFF4CAF50),
                onTap: () => setState(
                    () => _mode = OperationMode.timeBased),
              ),
              const SizedBox(width: 8),
              _ModeCard(
                icon: Icons.timer,
                label: 'Time +\nLiter',
                selected: _mode == OperationMode.timeWindowLiter,
                color: const Color(0xFF9C27B0),
                onTap: () => setState(
                    () => _mode = OperationMode.timeWindowLiter),
              ),
            ]),
            const SizedBox(height: 10),
            Container(
              padding: const EdgeInsets.all(10),
              decoration: BoxDecoration(
                color: const Color(0xFFE3F2FD),
                borderRadius: BorderRadius.circular(8)),
              child: Row(children: [
                const Icon(Icons.info_outline,
                    color: Color(0xFF2196F3), size: 16),
                const SizedBox(width: 8),
                Expanded(child: Text(_modeHint,
                    style: const TextStyle(
                        color: Color(0xFF1565C0), fontSize: 12))),
              ]),
            ),
          ]),
          const SizedBox(height: 12),

          // End Time
          if (_mode != OperationMode.literBased) ...[
            _Card(children: [
              _Label('Cycle End Time'),
              _TimePicker(
                  time: _endTime, onTap: () => _pickTime(false)),
            ]),
            const SizedBox(height: 12),
          ],

          // Target Liters
          if (_mode != OperationMode.timeBased) ...[
            _Card(children: [
              _Label('Target Quantity'),
              TextField(
                controller: _litersController,
                keyboardType: const TextInputType.numberWithOptions(
                    decimal: true),
                decoration: _dec('20.0').copyWith(
                    suffixText: 'Liters'),
              ),
            ]),
            const SizedBox(height: 12),
          ],

          // Status
          _Card(children: [
            Row(children: [
              const Text('Status',
                  style: TextStyle(
                      fontWeight: FontWeight.w500, fontSize: 15)),
              const Spacer(),
              Switch(
                value: _enabled,
                onChanged: (v) => setState(() => _enabled = v),
                activeColor: const Color(0xFF4CAF50),
              ),
            ]),
            Text(_enabled ? 'Enabled' : 'Disabled',
                style: TextStyle(
                    color: _enabled
                        ? const Color(0xFF4CAF50) : Colors.grey,
                    fontSize: 13)),
          ]),
          const SizedBox(height: 24),

          Row(children: [
            Expanded(
              child: OutlinedButton(
                onPressed: () => Navigator.pop(context),
                style: OutlinedButton.styleFrom(
                    padding: const EdgeInsets.symmetric(vertical: 14),
                    shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(12))),
                child: const Text('Cancel',
                    style: TextStyle(fontSize: 16)),
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: ElevatedButton(
                onPressed: _save,
                style: ElevatedButton.styleFrom(
                  backgroundColor: const Color(0xFF2196F3),
                  padding: const EdgeInsets.symmetric(vertical: 14),
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(12)),
                ),
                child: const Text('Save',
                    style: TextStyle(
                        color: Colors.white, fontSize: 16,
                        fontWeight: FontWeight.w600)),
              ),
            ),
          ]),
          const SizedBox(height: 24),
        ],
      ),
    );
  }

  String get _modeHint => switch (_mode) {
        OperationMode.literBased =>
            'Pump starts at start time and stops after delivering target liters.',
        OperationMode.timeBased =>
            'Pump starts at start time and stops at end time.',
        OperationMode.timeWindowLiter =>
            'Pump stops after delivering target liters OR at end time — whichever comes first.',
      };

  InputDecoration _dec(String hint) => InputDecoration(
        hintText: hint,
        border: OutlineInputBorder(
            borderRadius: BorderRadius.circular(8)),
        contentPadding: const EdgeInsets.symmetric(
            horizontal: 12, vertical: 12),
      );
}

class _Card extends StatelessWidget {
  final List<Widget> children;
  const _Card({required this.children});
  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.all(16),
    decoration: BoxDecoration(
      color: Theme.of(context).cardColor,
      borderRadius: BorderRadius.circular(12),
      boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05),
          blurRadius: 8, offset: const Offset(0, 2))],
    ),
    child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: children),
  );
}

class _Label extends StatelessWidget {
  final String text;
  const _Label(this.text);
  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: 8),
    child: Text(text,
        style: const TextStyle(
            fontWeight: FontWeight.w500, fontSize: 14)),
  );
}

class _TimePicker extends StatelessWidget {
  final TimeOfDay time;
  final VoidCallback onTap;
  const _TimePicker({required this.time, required this.onTap});
  @override
  Widget build(BuildContext context) => InkWell(
    onTap: onTap,
    borderRadius: BorderRadius.circular(8),
    child: Container(
      padding: const EdgeInsets.symmetric(
          horizontal: 12, vertical: 14),
      decoration: BoxDecoration(
        border: Border.all(color: Colors.grey.shade400),
        borderRadius: BorderRadius.circular(8)),
      child: Row(children: [
        Text(time.format(context),
            style: const TextStyle(fontSize: 16)),
        const Spacer(),
        const Icon(Icons.access_time,
            color: Colors.grey, size: 20),
      ]),
    ),
  );
}

class _ModeCard extends StatelessWidget {
  final IconData icon;
  final String label;
  final bool selected;
  final Color color;
  final VoidCallback onTap;
  const _ModeCard({required this.icon, required this.label,
                   required this.selected, required this.color,
                   required this.onTap});
  @override
  Widget build(BuildContext context) => Expanded(
    child: GestureDetector(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(
            vertical: 12, horizontal: 4),
        decoration: BoxDecoration(
          color: selected
              ? color.withOpacity(0.1) : Colors.grey.shade50,
          borderRadius: BorderRadius.circular(8),
          border: Border.all(
              color: selected ? color : Colors.grey.shade300,
              width: selected ? 2 : 1)),
        child: Column(children: [
          Icon(icon,
              color: selected ? color : Colors.grey, size: 26),
          const SizedBox(height: 6),
          Text(label,
              textAlign: TextAlign.center,
              style: TextStyle(
                fontSize: 11,
                fontWeight: selected
                    ? FontWeight.bold : FontWeight.normal,
                color: selected ? color : Colors.grey)),
        ]),
      ),
    ),
  );
}
