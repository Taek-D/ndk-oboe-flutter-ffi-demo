// main.dart
//
// NDK Oboe 층간소음 데모 UI(#5).
//   - 실시간 dBFS 게이지(StreamBuilder, ~8Hz)
//   - 임계값 슬라이더 → engine_set_threshold
//   - 측정 시작/정지(권한 확보 후 네이티브 start)
//   - 저장된 이벤트 WAV 목록(getExternalFilesDir) + 재생(just_audio)
import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:just_audio/just_audio.dart';

import 'audio_engine.dart';
import 'local_server.dart';

void main() => runApp(const NdkAudioApp());

class NdkAudioApp extends StatelessWidget {
  const NdkAudioApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'NDK Oboe Audio Demo',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.deepPurple),
        useMaterial3: true,
      ),
      home: const HomePage(),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final AudioEngine _engine = AudioEngine.instance;
  final AudioPlayer _player = AudioPlayer();
  late final LocalServer _server = LocalServer(_engine);

  bool _running = false;
  double _threshold = -20.0;
  double _currentDb = -100.0;
  String _status = '대기 중';
  String? _serverUrl;
  List<File> _wavFiles = [];
  StreamSubscription<double>? _sub;

  @override
  void initState() {
    super.initState();
    _sub = _engine.dbStream.listen((db) {
      if (mounted) setState(() => _currentDb = db);
    });
    _startServer();
  }

  Future<void> _startServer() async {
    try {
      final url = await _server.start(port: 8080);
      if (mounted) setState(() => _serverUrl = url);
    } catch (_) {
      // 서버 시작 실패는 측정 기능에 영향 없음.
    }
  }

  @override
  void dispose() {
    _sub?.cancel();
    _player.dispose();
    _server.stop();
    super.dispose();
  }

  Future<void> _toggle() async {
    if (_running) {
      await _engine.stop();
      if (!mounted) return;
      setState(() {
        _running = false;
        _status = '정지됨';
      });
      await _refreshFiles();
    } else {
      setState(() => _status = '권한 확인 중...');
      final ok = await _engine.start(thresholdDb: _threshold);
      if (!mounted) return;
      setState(() {
        _running = ok;
        _status = ok ? '측정 중 (48kHz)' : '시작 실패 — 마이크 권한 거부 또는 48kHz 미지원';
      });
    }
  }

  Future<void> _refreshFiles() async {
    final dir = _engine.outputDir;
    if (dir == null) return;
    final d = Directory(dir);
    if (await d.exists()) {
      final files = await d
          .list()
          .where((f) => f is File && f.path.toLowerCase().endsWith('.wav'))
          .cast<File>()
          .toList();
      files.sort((a, b) => b.path.compareTo(a.path));
      if (mounted) setState(() => _wavFiles = files);
    }
  }

  Future<void> _play(File f) async {
    try {
      await _player.setFilePath(f.path);
      await _player.play();
    } catch (_) {
      // 재생 실패는 무시(데모).
    }
  }

  @override
  Widget build(BuildContext context) {
    final norm = ((_currentDb + 100.0) / 100.0).clamp(0.0, 1.0);
    final theme = Theme.of(context);

    return Scaffold(
      appBar: AppBar(
        backgroundColor: theme.colorScheme.inversePrimary,
        title: const Text('NDK Oboe 층간소음 데모'),
      ),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            if (_serverUrl != null)
              Card(
                color: theme.colorScheme.secondaryContainer,
                child: ListTile(
                  leading: const Icon(Icons.wifi_tethering),
                  title: const Text('모니터링 웹 접속 주소'),
                  subtitle: Text('$_serverUrl\n같은 WiFi의 PC 브라우저/모니터링 웹에서 접속'),
                  isThreeLine: true,
                ),
              ),
            const SizedBox(height: 8),
            Text('실시간 dBFS', style: theme.textTheme.titleMedium),
            const SizedBox(height: 8),
            Text(
              '${_currentDb.toStringAsFixed(1)} dBFS',
              style: theme.textTheme.displaySmall,
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 8),
            ClipRRect(
              borderRadius: BorderRadius.circular(8),
              child: LinearProgressIndicator(
                value: norm,
                minHeight: 18,
                color: norm > ((_threshold + 100.0) / 100.0)
                    ? Colors.redAccent
                    : theme.colorScheme.primary,
              ),
            ),
            const SizedBox(height: 24),
            Text('이벤트 임계값: ${_threshold.toStringAsFixed(1)} dBFS'),
            Slider(
              value: _threshold,
              min: -100,
              max: 0,
              divisions: 100,
              label: _threshold.toStringAsFixed(0),
              onChanged: (v) {
                setState(() => _threshold = v);
                if (_running) _engine.setThreshold(v);
              },
            ),
            const SizedBox(height: 8),
            FilledButton.icon(
              onPressed: _toggle,
              icon: Icon(_running ? Icons.stop : Icons.mic),
              label: Text(_running ? '정지' : '측정 시작'),
            ),
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(_status, textAlign: TextAlign.center),
            ),
            const Divider(height: 32),
            Row(
              children: [
                Text('저장된 이벤트 WAV', style: theme.textTheme.titleMedium),
                const Spacer(),
                IconButton(
                  onPressed: _refreshFiles,
                  icon: const Icon(Icons.refresh),
                  tooltip: '새로고침',
                ),
              ],
            ),
            Expanded(
              child: _wavFiles.isEmpty
                  ? const Center(
                      child: Text('(아직 없음 — 임계 초과 시 전후 6초가 자동 저장됩니다)'),
                    )
                  : ListView.builder(
                      itemCount: _wavFiles.length,
                      itemBuilder: (context, i) {
                        final f = _wavFiles[i];
                        final name = f.path.split(Platform.pathSeparator).last;
                        return ListTile(
                          leading: const Icon(Icons.audiotrack),
                          title: Text(name),
                          trailing: IconButton(
                            icon: const Icon(Icons.play_arrow),
                            onPressed: () => _play(f),
                          ),
                        );
                      },
                    ),
            ),
          ],
        ),
      ),
    );
  }
}
