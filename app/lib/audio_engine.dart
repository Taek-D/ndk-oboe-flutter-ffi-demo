// audio_engine.dart
//
// libndk_audio.so FFI 브릿지(#4). 계획서 §5-4:
//   - export 호출: engine_start/stop/set_threshold/set_output_dir/set_db_callback/get_db
//   - engine → Dart dB 콜백은 NativeCallable.listener(오디오 스레드에서 호출돼도
//     타깃 isolate 이벤트 루프에 enqueue되어 안전 — 계획 N2).
//   - RECORD_AUDIO 런타임 권한을 확보한 뒤에만 start.
import 'dart:async';
import 'dart:ffi';

import 'package:ffi/ffi.dart';
import 'package:path_provider/path_provider.dart';
import 'package:permission_handler/permission_handler.dart';

typedef _StartNative = Int32 Function();
typedef _StartDart = int Function();
typedef _StopNative = Void Function();
typedef _StopDart = void Function();
typedef _SetThreshNative = Void Function(Float);
typedef _SetThreshDart = void Function(double);
typedef _SetDirNative = Void Function(Pointer<Utf8>);
typedef _SetDirDart = void Function(Pointer<Utf8>);
typedef _DbCallbackNative = Void Function(Float, Pointer<Void>);
typedef _SetCbNative =
    Void Function(Pointer<NativeFunction<_DbCallbackNative>>, Pointer<Void>);
typedef _SetCbDart =
    void Function(Pointer<NativeFunction<_DbCallbackNative>>, Pointer<Void>);
typedef _GetDbNative = Float Function();
typedef _GetDbDart = double Function();

class AudioEngine {
  AudioEngine._() {
    _lib = DynamicLibrary.open('libndk_audio.so');
    _start = _lib.lookupFunction<_StartNative, _StartDart>('engine_start');
    _stop = _lib.lookupFunction<_StopNative, _StopDart>('engine_stop');
    _setThreshold =
        _lib.lookupFunction<_SetThreshNative, _SetThreshDart>('engine_set_threshold');
    _setOutputDir =
        _lib.lookupFunction<_SetDirNative, _SetDirDart>('engine_set_output_dir');
    _setDbCallback =
        _lib.lookupFunction<_SetCbNative, _SetCbDart>('engine_set_db_callback');
    _getDb = _lib.lookupFunction<_GetDbNative, _GetDbDart>('engine_get_db');
  }

  static final AudioEngine instance = AudioEngine._();

  late final DynamicLibrary _lib;
  late final _StartDart _start;
  late final _StopDart _stop;
  late final _SetThreshDart _setThreshold;
  late final _SetDirDart _setOutputDir;
  late final _SetCbDart _setDbCallback;
  late final _GetDbDart _getDb;

  final StreamController<double> _dbController = StreamController<double>.broadcast();
  Stream<double> get dbStream => _dbController.stream;

  NativeCallable<_DbCallbackNative>? _dbCallable;
  bool _running = false;
  bool get isRunning => _running;
  String? _outputDir;
  String? get outputDir => _outputDir;
  double _threshold = -20.0;
  double get threshold => _threshold;

  Future<bool> requestMicPermission() async {
    final status = await Permission.microphone.request();
    return status.isGranted;
  }

  /// 권한 확보 → 출력 경로/콜백 설정 → 네이티브 start. 성공 시 true.
  /// 실패: 권한 거부 또는 48kHz 거부(M3).
  Future<bool> start({double thresholdDb = -20.0}) async {
    if (_running) return true;
    final granted = await requestMicPermission();
    if (!granted) return false;

    // app-specific external(= getExternalFilesDir). adb pull 가능, scoped storage 호환.
    final dir = await getExternalStorageDirectory() ??
        await getApplicationDocumentsDirectory();
    _outputDir = dir.path;
    final dirPtr = _outputDir!.toNativeUtf8();
    _setOutputDir(dirPtr);
    calloc.free(dirPtr);

    _threshold = thresholdDb;
    _setThreshold(thresholdDb);

    _dbCallable = NativeCallable<_DbCallbackNative>.listener(_onDbNative);
    _setDbCallback(_dbCallable!.nativeFunction, nullptr);

    final ok = _start() == 1;
    _running = ok;
    if (!ok) {
      _dbCallable?.close();
      _dbCallable = null;
    }
    return ok;
  }

  // 네이티브(오디오 스레드)에서 125ms마다 호출 → isolate 이벤트 루프로 enqueue됨.
  void _onDbNative(double db, Pointer<Void> user) {
    if (!_dbController.isClosed) _dbController.add(db);
  }

  void setThreshold(double db) {
    _threshold = db;
    _setThreshold(db);
  }

  double getDb() => _getDb();

  Future<void> stop() async {
    if (!_running) return;
    _stop();
    _running = false;
    _dbCallable?.close();
    _dbCallable = null;
  }
}
