import 'package:flutter/material.dart';

/// Custom pump icon (motor + pump housing + discharge pipe with spray +
/// water source) — replaces the generic Material "settings_input_component"
/// icon used as a placeholder before. Filled with [color] when [isOn] is
/// true (matches the "fill orange when running" request); outline-only
/// in [color] when off.
class PumpIcon extends StatelessWidget {
  final double size;
  final Color color;
  final bool isOn;

  const PumpIcon({
    super.key,
    this.size = 56,
    required this.color,
    required this.isOn,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: size,
      height: size,
      child: CustomPaint(
        painter: _PumpPainter(color: color, isOn: isOn),
      ),
    );
  }
}

class _PumpPainter extends CustomPainter {
  final Color color;
  final bool isOn;
  _PumpPainter({required this.color, required this.isOn});

  @override
  void paint(Canvas canvas, Size size) {
    final w = size.width;
    final h = size.height;
    final strokeW = w * 0.045;

    final stroke = Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = strokeW
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round;

    final fill = Paint()
      ..color = color
      ..style = PaintingStyle.fill;

    // Motor body (left cylinder) — filled when running, outline when not.
    final motorRect = Rect.fromLTWH(w * 0.05, h * 0.32, w * 0.34, h * 0.26);
    final motorRRect = RRect.fromRectAndRadius(motorRect, Radius.circular(h * 0.05));
    if (isOn) {
      canvas.drawRRect(motorRRect, fill);
    } else {
      canvas.drawRRect(motorRRect, stroke);
    }
    // Ridge lines across the motor body (always drawn, in white when
    // filled so they read as segment lines rather than disappearing into
    // the fill; in color when outline-only).
    final ridgePaint = Paint()
      ..color = isOn ? Colors.white.withOpacity(0.85) : color
      ..style = PaintingStyle.stroke
      ..strokeWidth = strokeW * 0.7
      ..strokeCap = StrokeCap.round;
    for (final fx in [0.14, 0.22, 0.30]) {
      canvas.drawLine(
        Offset(w * fx, h * 0.34), Offset(w * fx, h * 0.56), ridgePaint);
    }

    // Base/stand line under the motor+pump assembly.
    canvas.drawLine(Offset(w * 0.05, h * 0.58), Offset(w * 0.60, h * 0.58), stroke);
    canvas.drawLine(Offset(w * 0.10, h * 0.58), Offset(w * 0.10, h * 0.64), stroke);
    canvas.drawLine(Offset(w * 0.34, h * 0.58), Offset(w * 0.34, h * 0.64), stroke);

    // Small connector between motor and pump housing.
    canvas.drawLine(Offset(w * 0.39, h * 0.45), Offset(w * 0.45, h * 0.45), stroke);

    // Pump housing (small coupling block) — also filled when running.
    final pumpRect = Rect.fromLTWH(w * 0.45, h * 0.35, w * 0.13, h * 0.20);
    final pumpRRect = RRect.fromRectAndRadius(pumpRect, Radius.circular(w * 0.02));
    canvas.drawRRect(pumpRRect, isOn ? fill : stroke);

    // Discharge pipe: up from the pump housing, curves right, short spout.
    final pipe = Path()
      ..moveTo(w * 0.51, h * 0.35)
      ..lineTo(w * 0.51, h * 0.20)
      ..quadraticBezierTo(w * 0.51, h * 0.11, w * 0.61, h * 0.11)
      ..lineTo(w * 0.70, h * 0.11)
      ..quadraticBezierTo(w * 0.76, h * 0.11, w * 0.76, h * 0.19);
    canvas.drawPath(pipe, stroke);

    // Spray dots arcing up and out from the spout.
    for (final d in [
      Offset(w * 0.81, h * 0.16),
      Offset(w * 0.86, h * 0.12),
      Offset(w * 0.91, h * 0.10),
      Offset(w * 0.85, h * 0.22),
      Offset(w * 0.90, h * 0.18),
    ]) {
      canvas.drawCircle(d, w * 0.016, fill);
    }

    // Outlet pipe: from the base, curves down into the water source.
    final outlet = Path()
      ..moveTo(w * 0.50, h * 0.58)
      ..lineTo(w * 0.63, h * 0.58)
      ..quadraticBezierTo(w * 0.82, h * 0.58, w * 0.82, h * 0.71)
      ..lineTo(w * 0.82, h * 0.78);
    canvas.drawPath(outlet, stroke);

    // Water waves at the bottom.
    final wavePaint = Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = strokeW * 0.85
      ..strokeCap = StrokeCap.round;
    for (final fy in [0.85, 0.93]) {
      final wave = Path()
        ..moveTo(w * 0.55, h * fy)
        ..quadraticBezierTo(w * 0.61, h * (fy - 0.035), w * 0.67, h * fy)
        ..quadraticBezierTo(w * 0.73, h * (fy + 0.035), w * 0.79, h * fy)
        ..quadraticBezierTo(w * 0.85, h * (fy - 0.035), w * 0.91, h * fy);
      canvas.drawPath(wave, wavePaint);
    }
  }

  @override
  bool shouldRepaint(covariant _PumpPainter oldDelegate) =>
      oldDelegate.color != color || oldDelegate.isOn != isOn;
}
