// local_server.dart
//
// 기기 내장 로컬 HTTP 서버(공고: "기기 내장 로컬 웹서버"). 동일 로컬 네트워크(WiFi)의
// 다른 기기(PC 모니터링 웹)가 접속해 실시간 상태 폴링 / 원격 제어 / WAV 음원 스트리밍.
// dart:io HttpServer만 사용(추가 패키지 0). CORS 허용으로 별도 호스팅 웹에서 접근 가능.
//
// 엔드포인트:
//   GET  /api/status         → {running, db, threshold, eventCount, deviceId}
//   POST /api/pause          → 측정 일시정지(engine_stop)
//   POST /api/resume         → 측정 재개(engine_start)
//   POST /api/threshold?db=  → 임계값 원격 변경
//   GET  /api/events         → [{name, bytes, modified}]
//   GET  /api/audio/<name>   → WAV 스트리밍(Range 지원, audio/wav)
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'audio_engine.dart';

class LocalServer {
  LocalServer(this._engine);

  final AudioEngine _engine;
  HttpServer? _server;
  int _port = 8080;
  String _lanIp = '0.0.0.0';

  int get port => _port;
  String get lanIp => _lanIp;
  bool get isRunning => _server != null;
  String get baseUrl => 'http://$_lanIp:$_port';

  Future<String> start({int port = 8080}) async {
    if (_server != null) return baseUrl;
    _port = port;
    _server = await HttpServer.bind(InternetAddress.anyIPv4, port, shared: true);
    _lanIp = await _resolveLanIp();
    _server!.listen(_handle, onError: (_) {});
    return baseUrl;
  }

  Future<void> stop() async {
    await _server?.close(force: true);
    _server = null;
  }

  Future<String> _resolveLanIp() async {
    try {
      final ifaces = await NetworkInterface.list(
        type: InternetAddressType.IPv4,
        includeLoopback: false,
      );
      for (final iface in ifaces) {
        for (final addr in iface.addresses) {
          if (!addr.isLoopback) return addr.address;
        }
      }
    } catch (_) {}
    return '127.0.0.1';
  }

  Future<void> _handle(HttpRequest req) async {
    final res = req.response;
    // CORS(모니터링 웹이 다른 origin에서 호출).
    res.headers.add('Access-Control-Allow-Origin', '*');
    res.headers.add('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.headers.add('Access-Control-Allow-Headers', 'Content-Type');
    if (req.method == 'OPTIONS') {
      res.statusCode = HttpStatus.noContent;
      await res.close();
      return;
    }

    final path = req.uri.path;
    try {
      if (path == '/api/status') {
        await _json(res, {
          'running': _engine.isRunning,
          'db': _engine.isRunning ? _engine.getDb() : -100.0,
          'threshold': _engine.threshold,
          'eventCount': await _eventCount(),
          'deviceId': 'demo-device',
          'serverTime': DateTime.now().toIso8601String(),
        });
      } else if (path == '/api/pause' && req.method == 'POST') {
        await _engine.stop();
        await _json(res, {'running': false});
      } else if (path == '/api/resume' && req.method == 'POST') {
        final ok = await _engine.start(thresholdDb: _engine.threshold);
        await _json(res, {'running': ok});
      } else if (path == '/api/threshold' && req.method == 'POST') {
        final db = double.tryParse(req.uri.queryParameters['db'] ?? '');
        if (db != null) _engine.setThreshold(db);
        await _json(res, {'threshold': db ?? _engine.threshold});
      } else if (path == '/api/events') {
        await _json(res, await _eventList());
      } else if (path.startsWith('/api/audio/')) {
        await _streamAudio(req, res, Uri.decodeComponent(path.substring('/api/audio/'.length)));
      } else {
        res.statusCode = HttpStatus.notFound;
        await _json(res, {'error': 'not found', 'path': path});
      }
    } catch (e) {
      res.statusCode = HttpStatus.internalServerError;
      try {
        await _json(res, {'error': e.toString()});
      } catch (_) {}
    }
  }

  Future<void> _json(HttpResponse res, Object body) async {
    res.headers.contentType = ContentType.json;
    res.write(jsonEncode(body));
    await res.close();
  }

  Directory? get _outDir {
    final p = _engine.outputDir;
    return p == null ? null : Directory(p);
  }

  Future<int> _eventCount() async {
    final d = _outDir;
    if (d == null || !await d.exists()) return 0;
    return d
        .list()
        .where((f) => f is File && f.path.toLowerCase().endsWith('.wav'))
        .length;
  }

  Future<List<Map<String, Object>>> _eventList() async {
    final d = _outDir;
    if (d == null || !await d.exists()) return [];
    final out = <Map<String, Object>>[];
    await for (final f in d.list()) {
      if (f is File && f.path.toLowerCase().endsWith('.wav')) {
        final stat = await f.stat();
        out.add({
          'name': f.uri.pathSegments.last,
          'bytes': stat.size,
          'modified': stat.modified.toIso8601String(),
        });
      }
    }
    out.sort((a, b) => (b['name'] as String).compareTo(a['name'] as String));
    return out;
  }

  // WAV 스트리밍(HTTP Range 지원 → 브라우저 <audio> seek 가능).
  Future<void> _streamAudio(HttpRequest req, HttpResponse res, String name) async {
    final d = _outDir;
    if (d == null) {
      res.statusCode = HttpStatus.notFound;
      await res.close();
      return;
    }
    // 경로 탈출 방지.
    if (name.contains('/') || name.contains('\\') || name.contains('..')) {
      res.statusCode = HttpStatus.forbidden;
      await res.close();
      return;
    }
    final file = File('${d.path}${Platform.pathSeparator}$name');
    if (!await file.exists()) {
      res.statusCode = HttpStatus.notFound;
      await res.close();
      return;
    }
    final length = await file.length();
    res.headers.contentType = ContentType('audio', 'wav');
    res.headers.add('Accept-Ranges', 'bytes');

    final rangeHeader = req.headers.value('range');
    if (rangeHeader != null && rangeHeader.startsWith('bytes=')) {
      final parts = rangeHeader.substring(6).split('-');
      final start = int.tryParse(parts[0]) ?? 0;
      final end = (parts.length > 1 && parts[1].isNotEmpty)
          ? (int.tryParse(parts[1]) ?? length - 1)
          : length - 1;
      final safeEnd = end >= length ? length - 1 : end;
      res.statusCode = HttpStatus.partialContent;
      res.headers.add('Content-Range', 'bytes $start-$safeEnd/$length');
      res.headers.contentLength = safeEnd - start + 1;
      await res.addStream(file.openRead(start, safeEnd + 1));
    } else {
      res.headers.contentLength = length;
      await res.addStream(file.openRead());
    }
    await res.close();
  }
}
