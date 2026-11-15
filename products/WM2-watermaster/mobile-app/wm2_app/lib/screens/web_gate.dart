import 'package:flutter/material.dart';

import '../services/account.dart';
import '../services/node_store.dart';
import 'home_screen.dart';
import 'node_screen.dart';

/// Web app start: sign in, then the farms' nodes from the server.
class WebGate extends StatefulWidget {
  const WebGate({super.key, required this.store});
  final NodeStore store;
  @override
  State<WebGate> createState() => _WebGateState();
}

class _WebGateState extends State<WebGate> {
  final acct = Account.instance;
  bool loading = true;
  String? err;

  @override
  void initState() {
    super.initState();
    acct.addListener(_onAccount);
    refresh();
  }

  @override
  void dispose() {
    acct.removeListener(_onAccount);
    super.dispose();
  }

  void _onAccount() {
    if (mounted) setState(() {});
  }

  Future<void> refresh() async {
    setState(() {
      loading = true;
      err = null;
    });
    final list = await acct.devices();
    if (!mounted) return;
    if (list == null) {
      err = 'Server not reachable. Check the internet and try again.';
    } else {
      loadServerNodes(widget.store, list);
    }
    setState(() => loading = false);
  }

  @override
  Widget build(BuildContext context) {
    if (loading) return const Scaffold(body: Center(child: CircularProgressIndicator()));
    if (err != null) {
      return Scaffold(
        body: Center(
          child: Column(mainAxisSize: MainAxisSize.min, children: [
            Text(err!),
            const SizedBox(height: 12),
            FilledButton(onPressed: refresh, child: const Text('Try again')),
          ]),
        ),
      );
    }
    if (!acct.signedIn) return SignInScreen(onDone: refresh);
    return HomeScreen(store: widget.store, onRefresh: refresh);
  }
}

/// Server list -> the app's nodes. Zones are keyed per farm, so "Zone 1" of
/// two farms stay apart; the node's own "z1/n1" gives zone and position.
void loadServerNodes(NodeStore store, List<Map<String, dynamic>> list) {
  store.nodes = [];
  store.zones.clear();
  for (final d in list) {
    final farm = '${d['farm'] ?? 'No farm'}';
    final parts = '${d['node'] ?? 'z1/n1'}'.split('/');
    final z = parts.isNotEmpty && parts[0].isNotEmpty ? parts[0] : 'z1';
    final n = parts.length > 1 ? parts[1] : 'n1';
    final key = '$farm|$z';
    final zn = RegExp(r'\d+').firstMatch(z)?.group(0);
    store.zones.putIfAbsent(key, () => ZoneInfo(key: key, name: '$farm · ${zn != null ? 'Zone $zn' : z}'));
    store.nodes.add(NodeEntry(
      name: '${d['name'] ?? d['deviceId']}',
      agId: '${d['deviceId']}',
      broker: 'agrisense',
      farm: farm,
      zone: key,
      node: n,
      position: int.tryParse(RegExp(r'\d+').firstMatch(n)?.group(0) ?? '') ?? 1,
    ));
  }
  store.changed();
}

class SignInScreen extends StatefulWidget {
  const SignInScreen({super.key, required this.onDone});
  final VoidCallback onDone;
  @override
  State<SignInScreen> createState() => _SignInScreenState();
}

class _SignInScreenState extends State<SignInScreen> {
  final email = TextEditingController(text: Account.instance.email);
  final code = TextEditingController();
  bool codeSent = false, busy = false;
  String? msg;

  Future<void> _send() async {
    setState(() {
      busy = true;
      msg = null;
    });
    final e = await Account.instance.requestCode(email.text);
    setState(() {
      busy = false;
      codeSent = e == null;
      msg = e ?? 'We sent a 6-digit code to ${email.text.trim()}. It is valid for 5 minutes (check spam too).';
    });
  }

  Future<void> _verify() async {
    setState(() {
      busy = true;
      msg = null;
    });
    final e = await Account.instance.verify(email.text, code.text);
    if (!mounted) return;
    setState(() {
      busy = false;
      msg = e;
    });
    if (e == null) widget.onDone();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(24),
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 380),
            child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
              Center(
                child: ClipRRect(
                  borderRadius: BorderRadius.circular(16),
                  child: Image.asset('assets/images/logo-full.png', width: 200),
                ),
              ),
              const SizedBox(height: 24),
              Text('Sign in to ASAC-WM2', style: Theme.of(context).textTheme.titleLarge),
              const SizedBox(height: 8),
              const Text('Use the email your farm admin added. We email you a code; no password needed.'),
              const SizedBox(height: 16),
              TextField(
                controller: email,
                enabled: !codeSent,
                keyboardType: TextInputType.emailAddress,
                autofillHints: const [AutofillHints.email],
                decoration: const InputDecoration(labelText: 'Email', border: OutlineInputBorder()),
                onSubmitted: (_) => _send(),
              ),
              if (codeSent) ...[
                const SizedBox(height: 12),
                TextField(
                  controller: code,
                  keyboardType: TextInputType.number,
                  autofillHints: const [AutofillHints.oneTimeCode],
                  decoration: const InputDecoration(labelText: '6-digit code', border: OutlineInputBorder()),
                  onSubmitted: (_) => _verify(),
                ),
              ],
              const SizedBox(height: 16),
              FilledButton(
                onPressed: busy ? null : (codeSent ? _verify : _send),
                child: Text(codeSent ? 'Sign in' : 'Email me a code'),
              ),
              if (codeSent)
                TextButton(
                  onPressed: busy ? null : () => setState(() => codeSent = false),
                  child: const Text('Use another email'),
                ),
              if (msg != null) Padding(padding: const EdgeInsets.only(top: 12), child: Text(msg!)),
            ]),
          ),
        ),
      ),
    );
  }
}

/// Web app's Nodes tab: the nodes from the server, who is signed in, sign out.
class WebNodesScreen extends StatelessWidget {
  const WebNodesScreen({super.key, required this.store, this.onRefresh});
  final NodeStore store;
  final Future<void> Function()? onRefresh;

  Future<void> _delete(BuildContext context, NodeEntry n) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Delete ${n.name} (${n.agId})?'),
        content: const Text('The node, its history and its command log are removed from the server, and it leaves its '
            'farm. This cannot be undone.\n\nA node that is still powered and online reports again and comes back '
            'by itself, in no farm.'),
        actions: [
          FilledButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Keep it')),
          TextButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Delete')),
        ],
      ),
    );
    if (ok != true || !context.mounted) return;
    final err = await Account.instance.deleteDevice(n.agId);
    if (!context.mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(err ?? '${n.agId} deleted')));
    if (err == null) await onRefresh?.call();
  }

  @override
  Widget build(BuildContext context) {
    final acct = Account.instance;
    return ListenableBuilder(
      listenable: store,
      builder: (context, _) => Scaffold(
        appBar: AppBar(title: const Text('Nodes'), actions: [
          IconButton(icon: const Icon(Icons.refresh), tooltip: 'Reload list', onPressed: onRefresh),
        ]),
        body: ListView(children: [
          ListTile(
            leading: const Icon(Icons.account_circle_outlined),
            title: Text(acct.email),
            subtitle: Text(acct.isAdmin ? 'Admin: all farms' : 'Your farms'),
            trailing: TextButton(onPressed: () => acct.signOut(), child: const Text('Sign out')),
          ),
          const Divider(),
          if (store.nodes.isEmpty)
            const Padding(
              padding: EdgeInsets.all(24),
              child: Text('No nodes for this email yet. Ask your farm admin to add you to the farm.'),
            ),
          for (final n in store.nodes)
            ListTile(
              leading: const Icon(Icons.water_drop_outlined),
              title: Text(n.name),
              subtitle: Text('${store.zone(n.zone).name} · ${n.agId}'),
              trailing: acct.isAdmin
                  ? PopupMenuButton<String>(
                      tooltip: 'Admin',
                      onSelected: (_) => _delete(context, n),
                      itemBuilder: (_) => const [PopupMenuItem(value: 'delete', child: Text('Delete from server...'))],
                    )
                  : null,
              onTap: () => Navigator.push(context, MaterialPageRoute(builder: (_) => NodeScreen(entry: n, store: store))),
            ),
          const Padding(
            padding: EdgeInsets.all(16),
            child: Text(
                'First setup of a node (its own WiFi, farm WiFi name and password) is done once with the Android app. '
                'After that everything works here through the cloud.',
                style: TextStyle(fontSize: 12)),
          ),
        ]),
      ),
    );
  }
}
