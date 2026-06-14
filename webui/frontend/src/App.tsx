import { useEffect, useMemo, useState } from 'react';

import { fetchLatest, fetchRobots, getApiBase, getWsBase } from './api';
import FieldView from './components/FieldView';
import StatusCard from './components/StatusCard';
import type { RobotSnapshot } from './types';

function fmt(value: unknown, digits = 2): string {
  if (value === null || value === undefined || value === '') return '--';
  if (typeof value === 'number') return Number.isFinite(value) ? value.toFixed(digits) : '--';
  if (typeof value === 'boolean') return value ? 'yes' : 'no';
  return String(value);
}

function badge(value: boolean | undefined, label: string) {
  return <span className={`badge ${value ? 'on' : 'off'}`}>{label}: {value ? 'ON' : 'OFF'}</span>;
}

export default function App() {
  const [robots, setRobots] = useState<RobotSnapshot[]>([]);
  const [selectedRobotId, setSelectedRobotId] = useState<string>('');
  const [snapshot, setSnapshot] = useState<RobotSnapshot | null>(null);
  const [error, setError] = useState<string>('');

  useEffect(() => {
    let cancelled = false;

    async function loadRobots() {
      try {
        const list = await fetchRobots();
        if (cancelled) return;
        setRobots(list);
        if (!selectedRobotId && list.length > 0) {
          setSelectedRobotId(list[0].robot_id);
        }
        setError('');
      } catch (exc) {
        if (!cancelled) setError(String(exc));
      }
    }

    loadRobots();
    const timer = window.setInterval(loadRobots, 2000);
    return () => {
      cancelled = true;
      window.clearInterval(timer);
    };
  }, [selectedRobotId]);

  useEffect(() => {
    if (!selectedRobotId) return;
    let socket: WebSocket | null = null;
    let cancelled = false;

    fetchLatest(selectedRobotId).then(setSnapshot).catch(() => undefined);

    try {
      socket = new WebSocket(`${getWsBase()}/ws/robots/${encodeURIComponent(selectedRobotId)}`);
      socket.onmessage = (event) => {
        if (cancelled) return;
        setSnapshot(JSON.parse(event.data));
      };
      socket.onerror = () => setError('WebSocket disconnected or unreachable');
      socket.onopen = () => setError('');
    } catch (exc) {
      setError(String(exc));
    }

    return () => {
      cancelled = true;
      socket?.close();
    };
  }, [selectedRobotId]);

  const status = snapshot?.status;
  const behavior = status?.behavior ?? {};
  const game = status?.game ?? {};
  const robot = status?.robot ?? {};
  const ball = status?.ball;
  const prediction = status?.prediction;
  const health = status?.health ?? {};
  const team = status?.team;

  const lastSeenText = useMemo(() => {
    if (!snapshot?.last_seen_at) return '--';
    const ageSec = Date.now() / 1000 - snapshot.last_seen_at;
    return `${ageSec.toFixed(1)}s ago`;
  }, [snapshot?.last_seen_at]);

  return (
    <main>
      <header className="hero">
        <div>
          <h1>K1 Robot WebUI</h1>
          <p>Backend: {getApiBase()}</p>
          {error && <p className="error">{error}</p>}
        </div>
        <div className="selector">
          <label htmlFor="robot">Robot</label>
          <select id="robot" value={selectedRobotId} onChange={(event) => setSelectedRobotId(event.target.value)}>
            <option value="">No robot</option>
            {robots.map((item) => (
              <option key={item.robot_id} value={item.robot_id}>{item.robot_id}</option>
            ))}
          </select>
        </div>
      </header>

      <section className="summary">
        <div className={`onlineDot ${snapshot?.online ? 'on' : 'off'}`} />
        <strong>{snapshot?.robot_id ?? 'No robot selected'}</strong>
        <span>Last seen: {lastSeenText}</span>
        <span>Player: {fmt(robot.player_id, 0)}</span>
        <span>Role: {fmt(robot.role_current)}</span>
        <span>Game: {fmt(game.state)}</span>
        <span>Decision: {fmt(behavior.decision)}</span>
      </section>

      <div className="grid">
        <StatusCard title="Behavior">
          <div className="badges">
            {badge(Boolean(behavior.is_chase), 'Chase')}
            {badge(Boolean(behavior.is_adjust), 'Adjust')}
            {badge(Boolean(behavior.is_rl_visual_kick), 'RLVisionKick')}
            {badge(Boolean(behavior.odom_calibrated), 'Odom calibrated')}
          </div>
          <dl>
            <dt>Decision</dt><dd>{fmt(behavior.decision)}</dd>
            <dt>Defend</dt><dd>{fmt(behavior.defend_decision)}</dd>
            <dt>Visual kick version</dt><dd>{fmt(behavior.visual_kick_version)}</dd>
            <dt>Cost rank</dt><dd>{fmt(behavior.tm_my_cost_rank, 0)}</dd>
            <dt>Cost</dt><dd>{fmt(behavior.tm_my_cost)}</dd>
          </dl>
        </StatusCard>

        <StatusCard title="Ball">
          <div className="badges">
            {badge(ball?.detected, 'Detected')}
            {badge(ball?.known, 'Known')}
          </div>
          <dl>
            <dt>Range</dt><dd>{fmt(ball?.current?.range)} m</dd>
            <dt>Yaw</dt><dd>{fmt(ball?.current?.yaw_to_robot)} rad</dd>
            <dt>Confidence</dt><dd>{fmt(ball?.current?.confidence)}</dd>
            <dt>Field x/y</dt><dd>{fmt(ball?.current?.pos_to_field?.x)}, {fmt(ball?.current?.pos_to_field?.y)}</dd>
            <dt>Kick type</dt><dd>{fmt(ball?.kick_type)}</dd>
          </dl>
        </StatusCard>

        <StatusCard title="Prediction">
          <div className="badges">
            {badge(prediction?.enabled, 'Enabled')}
            {badge(prediction?.use_for_chase, 'Use for chase')}
            {badge(prediction?.valid, 'Valid')}
            {badge(prediction?.predicted_only, 'Predicted only')}
          </div>
          <dl>
            <dt>Predicted x/y</dt><dd>{fmt(prediction?.predicted_ball?.pos_to_field?.x)}, {fmt(prediction?.predicted_ball?.pos_to_field?.y)}</dd>
            <dt>Velocity x/y</dt><dd>{fmt(prediction?.velocity?.x)}, {fmt(prediction?.velocity?.y)}</dd>
            <dt>Acceleration x/y</dt><dd>{fmt(prediction?.acceleration?.x)}, {fmt(prediction?.acceleration?.y)}</dd>
            <dt>Trajectory points</dt><dd>{prediction?.trajectory?.length ?? 0}</dd>
          </dl>
        </StatusCard>

        <StatusCard title="Health">
          <dl>
            <dt>Camera</dt><dd>{fmt(health.cam_connected)}</dd>
            <dt>Detection lag</dt><dd>{fmt(health.ms_since_detection, 0)} ms</dd>
            <dt>Line lag</dt><dd>{fmt(health.ms_since_line_detection, 0)} ms</dd>
            <dt>GameController lag</dt><dd>{fmt(health.ms_since_gamecontroller, 0)} ms</dd>
            <dt>Localize lag</dt><dd>{fmt(health.ms_since_localize, 0)} ms</dd>
          </dl>
        </StatusCard>

        <StatusCard title="Field">
          <FieldView snapshot={snapshot} />
        </StatusCard>

        <StatusCard title="Team">
          <dl>
            <dt>Com</dt><dd>{fmt(team?.enable_com)}</dd>
            <dt>IP</dt><dd>{fmt(team?.tm_ip)}</dd>
            <dt>Send id</dt><dd>{fmt(team?.send_id, 0)}</dd>
            <dt>Teammates</dt><dd>{team?.teammates?.length ?? 0}</dd>
          </dl>
        </StatusCard>
      </div>
    </main>
  );
}
