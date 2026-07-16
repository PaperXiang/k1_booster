import { useEffect, useLayoutEffect, useRef, useState } from 'react';

import { getRobotLogWsUrl } from '../api';
import { LOG_SOURCES } from '../types';
import type { LogSource, LogStreamEvent } from '../types';

type LogPanelProps = {
  robotId: string;
};

type ConnectionState = 'idle' | 'connecting' | 'connected' | 'reconnecting';

const MAX_VISIBLE_LINES = 2000;
const LOG_SOURCE_LABELS: Record<LogSource, string> = {
  brain: 'Brain',
  game_controller: 'Game Controller',
  vision: 'Vision',
};

function isLogStreamEvent(value: unknown): value is LogStreamEvent {
  if (typeof value !== 'object' || value === null) return false;
  const candidate = value as Partial<LogStreamEvent>;
  return (candidate.type === 'history' || candidate.type === 'append')
    && typeof candidate.robot_id === 'string'
    && LOG_SOURCES.includes(candidate.source as LogSource)
    && Array.isArray(candidate.lines)
    && candidate.lines.every((line) => typeof line === 'string')
    && typeof candidate.reset === 'boolean';
}

export default function LogPanel({ robotId }: LogPanelProps) {
  const [selectedSource, setSelectedSource] = useState<LogSource>('brain');
  const [lines, setLines] = useState<string[]>([]);
  const [connectionState, setConnectionState] = useState<ConnectionState>('idle');
  const [connectionError, setConnectionError] = useState('');
  const [followLatest, setFollowLatest] = useState(true);
  const viewportRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    setLines([]);
    setConnectionError('');
    setFollowLatest(true);

    if (!robotId) {
      setConnectionState('idle');
      return undefined;
    }

    let cancelled = false;
    let socket: WebSocket | null = null;
    let reconnectTimer: number | undefined;
    let reconnectAttempt = 0;

    const scheduleReconnect = () => {
      if (cancelled || reconnectTimer !== undefined) return;
      reconnectAttempt += 1;
      const delayMilliseconds = Math.min(5000, 500 * (2 ** reconnectAttempt));
      setConnectionState('reconnecting');
      reconnectTimer = window.setTimeout(() => {
        reconnectTimer = undefined;
        connect();
      }, delayMilliseconds);
    };

    const connect = () => {
      if (cancelled) return;
      setConnectionState(reconnectAttempt === 0 ? 'connecting' : 'reconnecting');

      try {
        socket = new WebSocket(getRobotLogWsUrl(robotId, selectedSource));
      } catch (error) {
        setConnectionError(String(error));
        scheduleReconnect();
        return;
      }

      socket.onopen = () => {
        if (cancelled) return;
        reconnectAttempt = 0;
        setConnectionState('connected');
        setConnectionError('');
      };

      socket.onmessage = (event) => {
        if (cancelled) return;

        try {
          const message: unknown = JSON.parse(event.data);
          if (!isLogStreamEvent(message)) return;
          if (message.robot_id !== robotId || message.source !== selectedSource) return;

          setLines((currentLines) => {
            const nextLines = message.reset || message.type === 'history'
              ? message.lines
              : [...currentLines, ...message.lines];
            return nextLines.slice(-MAX_VISIBLE_LINES);
          });
        } catch (error) {
          setConnectionError(`Invalid log message: ${String(error)}`);
        }
      };

      socket.onerror = () => {
        if (cancelled) return;
        setConnectionError('Log stream disconnected');
        socket?.close();
      };

      socket.onclose = () => {
        if (cancelled) return;
        scheduleReconnect();
      };
    };

    connect();

    return () => {
      cancelled = true;
      if (reconnectTimer !== undefined) window.clearTimeout(reconnectTimer);
      socket?.close();
    };
  }, [robotId, selectedSource]);

  useLayoutEffect(() => {
    if (!followLatest || !viewportRef.current) return;
    viewportRef.current.scrollTop = viewportRef.current.scrollHeight;
  }, [followLatest, lines]);

  const handleViewportScroll = () => {
    const viewport = viewportRef.current;
    if (!viewport) return;
    const distanceFromBottom = viewport.scrollHeight - viewport.scrollTop - viewport.clientHeight;
    setFollowLatest(distanceFromBottom < 32);
  };

  const resumeFollowing = () => {
    setFollowLatest(true);
    window.requestAnimationFrame(() => {
      if (!viewportRef.current) return;
      viewportRef.current.scrollTop = viewportRef.current.scrollHeight;
    });
  };

  const emptyMessage = robotId
    ? 'Waiting for log output...'
    : 'Select a robot to view its logs.';

  return (
    <section className="card logCard">
      <div className="logHeader">
        <div>
          <h2>Logs</h2>
          <div className="logConnection">
            <span className={`logConnectionDot ${connectionState}`} />
            <span>{connectionState}</span>
            {connectionError && <span className="logError">{connectionError}</span>}
          </div>
        </div>
        <button
          className={`logFollowButton ${followLatest ? 'active' : ''}`}
          type="button"
          onClick={resumeFollowing}
        >
          {followLatest ? 'Following latest' : 'Resume live scroll'}
        </button>
      </div>

      <div className="logSourceTabs" role="tablist" aria-label="Log source">
        {LOG_SOURCES.map((source) => (
          <button
            className={`logSourceTab ${selectedSource === source ? 'active' : ''}`}
            key={source}
            type="button"
            role="tab"
            aria-selected={selectedSource === source}
            onClick={() => setSelectedSource(source)}
          >
            {LOG_SOURCE_LABELS[source]}
          </button>
        ))}
      </div>

      <div className="logViewport" ref={viewportRef} onScroll={handleViewportScroll}>
        {lines.length > 0 ? (
          <pre>{lines.join('\n')}</pre>
        ) : (
          <div className="logEmpty">{emptyMessage}</div>
        )}
      </div>
    </section>
  );
}
