import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';

/// Sign-in to the AgriSense server (email + one-time code). The web app runs
/// on the server's own site, so the session is a cookie the browser keeps;
/// the Android app keeps the same session as a bearer token.
class Account extends ChangeNotifier {
  Account._();
  static final Account instance = Account._();

  /// Server root: the page's own site on the web, else the production server.
  static String get base => kIsWeb ? Uri.base.origin : 'https://agrisenseandcontrol.in';

  String? token; // Android only
  String email = '';
  String role = '';
  bool signedIn = false;

  bool get isAdmin => role == 'ADMIN';

  Map<String, String> get headers => {
        'Content-Type': 'application/json',
        if (token != null) 'Authorization': 'Bearer $token',
      };

  Future<void> load() async {
    final p = await SharedPreferences.getInstance();
    token = p.getString('server_token');
    email = p.getString('server_email') ?? '';
  }

  Future<String?> requestCode(String mail) async {
    try {
      final r = await http.post(Uri.parse('$base/api/auth/request-otp'),
          headers: headers, body: jsonEncode({'email': mail.trim()}));
      return r.statusCode == 200 ? null : 'could not send the code (${r.statusCode})';
    } catch (_) {
      return 'no internet or server not reachable';
    }
  }

  Future<String?> verify(String mail, String code) async {
    try {
      final r = await http.post(Uri.parse('$base/api/auth/verify-otp'),
          headers: headers, body: jsonEncode({'email': mail.trim(), 'code': code.trim(), if (!kIsWeb) 'client': 'app'}));
      final j = jsonDecode(r.body) as Map<String, dynamic>;
      if (r.statusCode != 200) return '${j['error'] ?? 'sign-in failed'}';
      email = mail.trim();
      role = '${j['role'] ?? ''}';
      final p = await SharedPreferences.getInstance();
      await p.setString('server_email', email);
      if (!kIsWeb && j['token'] is String) {
        token = j['token'];
        await p.setString('server_token', token!);
      }
      signedIn = true;
      notifyListeners();
      return null;
    } catch (_) {
      return 'no internet or server not reachable';
    }
  }

  /// The nodes this account may use: [] when signed out (sets signedIn false).
  Future<List<Map<String, dynamic>>?> devices({String product = 'WM2'}) async {
    try {
      final r = await http.get(Uri.parse('$base/api/app/devices?product=$product'), headers: headers);
      if (r.statusCode == 401) {
        signedIn = false;
        notifyListeners();
        return [];
      }
      final j = jsonDecode(r.body) as Map<String, dynamic>;
      if (j['ok'] != true) return null;
      signedIn = true;
      email = '${j['user']?['email'] ?? email}';
      role = '${j['user']?['role'] ?? role}';
      return [for (final d in (j['devices'] as List)) Map<String, dynamic>.from(d)];
    } catch (_) {
      return null; // offline: keep what we have
    }
  }

  Future<void> signOut() async {
    try {
      await http.post(Uri.parse('$base/api/app/logout'), headers: headers);
    } catch (_) {}
    token = null;
    signedIn = false;
    role = '';
    final p = await SharedPreferences.getInstance();
    await p.remove('server_token');
    notifyListeners();
  }
}
