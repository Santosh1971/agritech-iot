// Smoke test -- just confirms the app boots to the home screen without
// a bench config set yet (shows the "set Flash Bridge address" panel).

import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import 'package:fg1_production_tester/main.dart';

void main() {
  testWidgets('App boots and shows the no-config prompt', (WidgetTester tester) async {
    await tester.pumpWidget(const ProviderScope(child: FG1ProductionTesterApp()));
    await tester.pump();

    expect(find.text('FG1 Production Tester'), findsOneWidget);
    expect(find.text('Set the Flash Bridge address first.'), findsOneWidget);
  });
}
