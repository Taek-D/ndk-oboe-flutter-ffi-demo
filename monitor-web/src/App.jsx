import { useState, useEffect, useCallback } from 'react';
import './App.css';

// 층간소음 측정 기기(공기계)의 내장 로컬 웹서버에 같은 WiFi로 접속해
// 실시간 상태 폴링 / 원격 제어(일시정지·재개·임계) / 이벤트 WAV 스트리밍 재생.
export default function App() {
  const [baseUrl, setBaseUrl] = useState('http://localhost:8080');
  const [status, setStatus] = useState(null);
  const [events, setEvents] = useState([]);
  const [error, setError] = useState(null);
  const [threshold, setThreshold] = useState(-20);

  const fetchStatus = useCallback(async () => {
    try {
      const r = await fetch(`${baseUrl}/api/status`);
      const j = await r.json();
      setStatus(j);
      setError(null);
    } catch {
      setError(`연결 실패: ${baseUrl} (같은 WiFi인지, 기기 앱이 실행 중인지 확인)`);
      setStatus(null);
    }
  }, [baseUrl]);

  const fetchEvents = useCallback(async () => {
    try {
      const r = await fetch(`${baseUrl}/api/events`);
      setEvents(await r.json());
    } catch { /* ignore */ }
  }, [baseUrl]);

  useEffect(() => {
    fetchStatus();
    const id = setInterval(fetchStatus, 1000); // 실시간 폴링(1초)
    return () => clearInterval(id);
  }, [fetchStatus]);

  const post = async (path) => {
    try {
      await fetch(`${baseUrl}${path}`, { method: 'POST' });
      fetchStatus();
    } catch { /* ignore */ }
  };

  const commitThreshold = () =>
    fetch(`${baseUrl}/api/threshold?db=${threshold}`, { method: 'POST' }).then(fetchStatus);

  const norm = status ? Math.max(0, Math.min(1, (status.db + 100) / 100)) : 0;
  const thrNorm = status ? (status.threshold + 100) / 100 : 0;
  const over = norm > thrNorm;

  return (
    <div className="app">
      <h1>🎧 층간소음 모니터링</h1>

      <div className="row">
        <input
          className="url"
          value={baseUrl}
          onChange={(e) => setBaseUrl(e.target.value)}
          placeholder="http://기기IP:8080"
        />
        <button onClick={fetchStatus}>연결</button>
      </div>

      {error && <p className="err">{error}</p>}

      {status && (
        <>
          <div className="card">
            <div className="db" style={{ color: over ? '#e53935' : '#1a237e' }}>
              {status.db.toFixed(1)} <small>dBFS</small>
            </div>
            <div className="bar">
              <div
                className="fill"
                style={{ width: `${norm * 100}%`, background: over ? '#e53935' : '#3949ab' }}
              />
              <div className="thr" style={{ left: `${thrNorm * 100}%` }} title="임계값" />
            </div>
            <div className="meta">
              <span className={status.running ? 'on' : 'off'}>
                {status.running ? '● 측정 중' : '○ 정지'}
              </span>
              <span>임계 {status.threshold.toFixed(0)} dBFS</span>
              <span>이벤트 {status.eventCount}건</span>
            </div>
          </div>

          <div className="row">
            <button onClick={() => post('/api/pause')} disabled={!status.running}>
              ⏸ 일시정지
            </button>
            <button onClick={() => post('/api/resume')} disabled={status.running}>
              ▶ 재개
            </button>
          </div>

          <div className="row col">
            <label>원격 임계값: {threshold} dBFS</label>
            <input
              type="range"
              min="-100"
              max="0"
              value={threshold}
              onChange={(e) => setThreshold(+e.target.value)}
              onMouseUp={commitThreshold}
              onTouchEnd={commitThreshold}
            />
          </div>

          <div className="row between">
            <h2>저장된 소음 이벤트</h2>
            <button onClick={fetchEvents}>목록 불러오기</button>
          </div>
          <ul className="events">
            {events.map((ev) => (
              <li key={ev.name}>
                <div className="evname">
                  {ev.name}
                  <small>
                    {(ev.bytes / 1024 / 1024).toFixed(1)}MB · {new Date(ev.modified).toLocaleString()}
                  </small>
                </div>
                <audio controls preload="none" src={`${baseUrl}/api/audio/${ev.name}`} />
              </li>
            ))}
            {events.length === 0 && <li className="muted">(「목록 불러오기」를 눌러 조회)</li>}
          </ul>
        </>
      )}

      <footer>기기 내장 로컬 웹서버(LAN) · 실시간 폴링 · 원격 제어 · WAV Range 스트리밍</footer>
    </div>
  );
}
