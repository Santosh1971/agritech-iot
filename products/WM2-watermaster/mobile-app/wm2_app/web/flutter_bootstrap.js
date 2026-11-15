{{flutter_js}}
{{flutter_build_config}}

// No service worker: the web app is always loaded fresh from the server, so
// an update reaches every phone on the next open (no offline copy; the app
// needs the server anyway). See tool/deploy_web.sh for the cleanup worker
// that removes the offline copy older builds installed.
_flutter.loader.load();
