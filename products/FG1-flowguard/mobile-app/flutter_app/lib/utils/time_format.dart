/// Formats an hour/minute pair as 12-hour time with AM/PM, e.g. (22, 29) -> "10:29 PM".
String formatTime12(int hour, int minute) {
  final period = hour >= 12 ? 'PM' : 'AM';
  final h12 = hour % 12 == 0 ? 12 : hour % 12;
  return '$h12:${minute.toString().padLeft(2, '0')} $period';
}

/// Same, but parses firmware's raw "HH:MM" 24-hour string (e.g. rtc_time).
/// Falls back to returning the input unchanged if it doesn't parse.
String formatTime12FromString(String hhmm) {
  final parts = hhmm.split(':');
  if (parts.length != 2) return hhmm;
  final h = int.tryParse(parts[0]);
  final m = int.tryParse(parts[1]);
  if (h == null || m == null) return hhmm;
  return formatTime12(h, m);
}
