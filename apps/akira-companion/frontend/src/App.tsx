import { useEffect, useState } from 'react';
import './App.css';
import akiraIcon from './assets/images/akira-icon.jpg';
import {
  Status,
  Login,
  RegenerateDUID,
  PushToSwitch,
  OpenPsnLogin,
  OpenNpssoPage,
  DiscoverSwitches,
} from '../wailsjs/go/main/App';

interface SwitchInfo {
  name: string;
  host: string;
  port: number;
}

interface AppStatus {
  duid: string;
  hasToken: boolean;
  tokenValid: boolean;
  onlineId: string;
  accountId: string;
}

interface PushOutcome {
  result: string;
  message: string;
}

type Screen = 'loading' | 'login' | 'send';

function errText(e: unknown, fallback: string): string {
  if (typeof e === 'string') return e;
  if (e && typeof e === 'object' && 'message' in e) return String((e as { message: unknown }).message);
  return fallback;
}

export default function App() {
  const [screen, setScreen] = useState<Screen>('loading');
  const [status, setStatus] = useState<AppStatus | null>(null);

  useEffect(() => {
    Status().then((s) => {
      setStatus(s as AppStatus);
      setScreen((s as AppStatus).tokenValid ? 'send' : 'login');
    });
  }, []);

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <img className="brand-mark" src={akiraIcon} alt="Akira" />
          <span className="brand-name">Akira Companion</span>
        </div>
        {status?.tokenValid && (
          <div className="chip">
            <span className="dot" />
            {status.onlineId || 'Signed in'}
            <button className="linklike" onClick={() => setScreen('login')}>
              switch account
            </button>
          </div>
        )}
      </header>

      {screen === 'loading' && <div className="center muted">Loading…</div>}
      {screen === 'login' && (
        <LoginScreen
          duid={status?.duid || ''}
          onStatus={(s) => setStatus(s)}
          onDone={(s) => {
            setStatus(s);
            setScreen('send');
          }}
        />
      )}
      {screen === 'send' && status && <SendScreen status={status} />}
    </div>
  );
}

function LoginScreen({
  duid,
  onStatus,
  onDone,
}: {
  duid: string;
  onStatus: (s: AppStatus) => void;
  onDone: (s: AppStatus) => void;
}) {
  const [npsso, setNpsso] = useState('');
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');
  const [confirmRegen, setConfirmRegen] = useState(false);
  const [regenBusy, setRegenBusy] = useState(false);
  const [regenDone, setRegenDone] = useState(false);

  const regenerate = async () => {
    if (!confirmRegen) {
      setConfirmRegen(true);
      return;
    }
    setRegenBusy(true);
    setErr('');
    try {
      const s = (await RegenerateDUID()) as AppStatus;
      onStatus(s);
      setRegenDone(true);
    } catch (e) {
      setErr(errText(e, 'Could not generate a new device ID.'));
    } finally {
      setRegenBusy(false);
      setConfirmRegen(false);
    }
  };

  const submit = async () => {
    if (!npsso.trim()) {
      setErr('Paste your npsso token first.');
      return;
    }
    setBusy(true);
    setErr('');
    try {
      const s = (await Login(npsso.trim())) as AppStatus;
      onDone(s);
    } catch (e) {
      setErr(errText(e, 'Sign in failed. The npsso token may be invalid or expired.'));
    } finally {
      setBusy(false);
    }
  };

  return (
    <main className="screen">
      <h1>Sign in to PlayStation</h1>
      <p className="muted">
        Log in on Sony's site, copy your npsso token, and paste it below. Your password never touches this app.
      </p>

      <details className="advanced">
        <summary>Device ID (optional)</summary>
        <div className="hint">
          PlayStation sees this app as a device with the ID below. It is created once and kept — you normally never
          change it. Generate a new one if you want this app to look like a brand new device, for example after
          PlayStation stopped accepting it. Doing so signs you out here and you'll need to sign in again and resend
          credentials to your Switch.
        </div>
        <div className="duid mono">{duid || 'not set'}</div>
        <div className="advanced-actions">
          <button className="ghost" onClick={regenerate} disabled={regenBusy}>
            {regenBusy ? 'Generating…' : confirmRegen ? 'Confirm — generate new device ID' : 'Generate new device ID'}
          </button>
          {confirmRegen && !regenBusy && (
            <button className="linklike" onClick={() => setConfirmRegen(false)}>
              cancel
            </button>
          )}
        </div>
        {regenDone && <div className="hint ok-text">New device ID generated. Sign in below.</div>}
      </details>

      <ol className="steps">
        <li>
          <span className="step-n">1</span>
          <div className="step-body">
            <div className="step-title">Sign in to PlayStation</div>
            <div className="hint">
              Opens playstation.com — sign in with your PlayStation account (top-right), then come back here.
            </div>
            <button className="ghost" onClick={() => OpenPsnLogin()}>
              Open PlayStation.com ↗
            </button>
          </div>
        </li>
        <li>
          <span className="step-n">2</span>
          <div className="step-body">
            <div className="step-title">Open the NPSSO page</div>
            <div className="hint">
              Now that you're signed in, this page shows <span className="mono">{'{"npsso":"..."}'}</span> — copy the
              value between the quotes.
            </div>
            <button className="ghost" onClick={() => OpenNpssoPage()}>
              Open NPSSO page ↗
            </button>
          </div>
        </li>
        <li>
          <span className="step-n">3</span>
          <div className="step-body">
            <div className="step-title">Paste the npsso token</div>
            <textarea
              className="field mono"
              placeholder="Paste the npsso value here"
              value={npsso}
              rows={3}
              onChange={(e) => setNpsso(e.target.value)}
            />
          </div>
        </li>
      </ol>

      {err && <div className="banner error">{err}</div>}

      <div className="actions">
        <button className="primary" onClick={submit} disabled={busy}>
          {busy ? 'Signing in…' : 'Sign in'}
        </button>
      </div>
    </main>
  );
}

function SendScreen({ status }: { status: AppStatus }) {
  const [host, setHost] = useState('');
  const [port, setPort] = useState('8080');
  const [code, setCode] = useState('');
  const [busy, setBusy] = useState(false);
  const [outcome, setOutcome] = useState<PushOutcome | null>(null);
  const [switches, setSwitches] = useState<SwitchInfo[]>([]);
  const [scanActive, setScanActive] = useState(true);
  const [scanNonce, setScanNonce] = useState(0);

  useEffect(() => {
    let cancelled = false;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const started = Date.now();
    const windowMs = 15 * 60 * 1000;
    const intervalMs = 5000;
    setScanActive(true);

    const tick = () => {
      DiscoverSwitches()
        .then((list) => {
          if (cancelled) return;
          const found = (list as SwitchInfo[]) || [];
          setSwitches((prev) => {
            const merged = new Map(prev.map((s) => [s.host, s]));
            for (const sw of found) merged.set(sw.host, sw);
            return Array.from(merged.values());
          });
        })
        .catch(() => {})
        .finally(() => {
          if (cancelled) return;
          if (Date.now() - started < windowMs) {
            timer = setTimeout(tick, intervalMs);
          } else {
            setScanActive(false);
          }
        });
    };
    tick();

    return () => {
      cancelled = true;
      if (timer) clearTimeout(timer);
    };
  }, [scanNonce]);

  const send = async () => {
    setOutcome(null);
    if (!host.trim()) {
      setOutcome({ result: 'error', message: 'Enter the Switch IP shown on the Pair screen.' });
      return;
    }
    if (code.trim().length !== 4) {
      setOutcome({ result: 'error', message: 'Enter the 4-digit code shown on the Switch.' });
      return;
    }
    setBusy(true);
    try {
      const o = (await PushToSwitch(host.trim(), parseInt(port, 10) || 8080, code.trim())) as PushOutcome;
      setOutcome(o);
    } catch (e) {
      setOutcome({ result: 'error', message: errText(e, 'Push failed.') });
    } finally {
      setBusy(false);
    }
  };

  return (
    <main className="screen">
      <h1>Send to your Switch</h1>
      <p className="muted">Open Akira on your Switch, choose Pair, then enter the IP and 4-digit code it shows.</p>

      <div className="found">
        <div className="found-head">
          <span>Found on your network</span>
          <button className="linklike" onClick={() => setScanNonce((n) => n + 1)} disabled={scanActive}>
            {scanActive ? 'scanning…' : 'rescan'}
          </button>
        </div>
        {switches.length === 0 && scanActive && (
          <div className="hint">Scanning… open Akira on your Switch and choose Pair.</div>
        )}
        {switches.length === 0 && !scanActive && (
          <div className="hint">No Switch found — enter its IP from the Pair screen below.</div>
        )}
        {switches.map((sw) => (
          <button
            key={sw.host}
            className={`device ${host === sw.host ? 'selected' : ''}`}
            onClick={() => {
              setHost(sw.host);
              setPort(String(sw.port));
            }}
          >
            <span className="device-name">{sw.name || sw.host}</span>
            <span className="device-addr">
              {sw.host} · {sw.port}
            </span>
          </button>
        ))}
      </div>

      <div className="grid">
        <label className="field-label">
          Switch IP address
          <input
            className="field"
            placeholder="192.168.20.170"
            value={host}
            onChange={(e) => setHost(e.target.value)}
          />
        </label>
        <label className="field-label port">
          Port
          <input className="field" value={port} onChange={(e) => setPort(e.target.value.replace(/[^0-9]/g, ''))} />
        </label>
      </div>

      <label className="field-label">
        Pairing code
        <input
          className="field code"
          placeholder="0000"
          inputMode="numeric"
          maxLength={4}
          value={code}
          onChange={(e) => setCode(e.target.value.replace(/[^0-9]/g, ''))}
        />
      </label>

      {outcome && (
        <div className={`banner ${outcome.result === 'imported' ? 'ok' : 'error'}`}>
          {outcome.result === 'imported'
            ? 'Credentials sent and imported by the Switch. You can close this screen.'
            : outcome.result === 'bad_code'
            ? 'The Switch rejected the code. Check the 4-digit code and try again.'
            : outcome.message || 'Could not reach the Switch. Check the IP and port, and that the Pair screen is open.'}
        </div>
      )}

      <div className="actions">
        <button className="primary" onClick={send} disabled={busy}>
          {busy ? 'Sending…' : 'Send credentials'}
        </button>
      </div>
    </main>
  );
}
