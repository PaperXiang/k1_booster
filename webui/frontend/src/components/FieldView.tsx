import type { CSSProperties } from 'react';

import type { Point2D, RobotSnapshot } from '../types';

type FieldViewProps = {
  snapshot: RobotSnapshot | null;
};

const FIELD_LENGTH = 14.16;
const FIELD_WIDTH = 9.22;
const PENALTY_DEPTH = 1.65;
const PENALTY_WIDTH = 3.9;
const GOAL_WIDTH = 2.6;
const CENTER_CIRCLE_RADIUS = 1.54;

type UnknownRecord = Record<string, unknown>;

type MappedPoint = {
  x: number;
  y: number;
  raw: Point2D;
};

type FieldMarker = {
  key: string;
  label: string;
  className: string;
  point: MappedPoint;
  title: string;
};

type FieldSegment = {
  key: string;
  className: string;
  start: MappedPoint;
  end: MappedPoint;
  title: string;
};

type Line2D = {
  x0: number;
  y0: number;
  x1: number;
  y1: number;
};

function isRecord(value: unknown): value is UnknownRecord {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function isFiniteNumber(value: unknown): value is number {
  return typeof value === 'number' && Number.isFinite(value);
}

function positiveNumber(value: unknown, fallback: number): number {
  return isFiniteNumber(value) && value > 0 ? value : fallback;
}

function readPoint(value: unknown): Point2D | null {
  if (!isRecord(value) || !isFiniteNumber(value.x) || !isFiniteNumber(value.y)) {
    return null;
  }
  return { x: value.x, y: value.y };
}

function readLine(value: unknown): Line2D | null {
  if (
    !isRecord(value) ||
    !isFiniteNumber(value.x0) ||
    !isFiniteNumber(value.y0) ||
    !isFiniteNumber(value.x1) ||
    !isFiniteNumber(value.y1)
  ) {
    return null;
  }
  return { x0: value.x0, y0: value.y0, x1: value.x1, y1: value.y1 };
}

function readAnyPoint(value: unknown): Point2D | null {
  const direct = readPoint(value);
  if (direct) return direct;
  if (!isRecord(value)) return null;

  for (const key of ['pos_to_field', 'posToField', 'field', 'pose', 'position', 'robot_pose_to_field', 'robotPoseToField', 'current']) {
    const point = readAnyPoint(value[key]);
    if (point) return point;
  }
  return null;
}

function readEntityPose(entity: UnknownRecord): Point2D | null {
  for (const key of ['pose', 'field', 'robot_pose_to_field', 'robotPoseToField', 'pos_to_field', 'posToField', 'field_pose', 'position', 'robot_pose', 'robotPose']) {
    const point = readAnyPoint(entity[key]);
    if (point) return point;
  }
  return readPoint(entity);
}

function readEntityBall(entity: UnknownRecord): Point2D | null {
  for (const key of ['ball', 'ball_pos_to_field', 'ballPosToField', 'tm_ball', 'tmBall', 'tm_ball_pos', 'tmBallPos']) {
    const point = readAnyPoint(entity[key]);
    if (point) return point;
  }
  return null;
}

function readLabel(entity: UnknownRecord, fallback: string): string {
  for (const key of ['name', 'robot_id', 'label', 'player_id', 'id', 'send_id']) {
    const value = entity[key];
    if (typeof value === 'string') {
      const label = value.trim();
      if (label && !['NA', 'N/A', 'NULL', 'UNDEFINED'].includes(label.toUpperCase())) return label;
    }
    if (typeof value === 'number' && Number.isFinite(value)) return String(value);
  }
  return fallback;
}

function readDetail(entity: UnknownRecord, keys: string[]): string | null {
  for (const key of keys) {
    const value = entity[key];
    if (typeof value === 'string' && value.trim()) return value;
    if (typeof value === 'number' && Number.isFinite(value)) return value.toFixed(2);
    if (typeof value === 'boolean') return value ? 'yes' : 'no';
  }
  return null;
}

function entityArray(value: unknown): UnknownRecord[] {
  return Array.isArray(value) ? value.filter(isRecord) : [];
}

function mapPoint(point: Point2D | null | undefined, fieldLength: number, fieldWidth: number): MappedPoint | null {
  if (!point || typeof point.x !== 'number' || typeof point.y !== 'number') {
    return null;
  }
  const x = ((point.x + fieldLength / 2) / fieldLength) * 100;
  const y = ((fieldWidth / 2 - point.y) / fieldWidth) * 100;
  return {
    x: Math.max(0, Math.min(100, x)),
    y: Math.max(0, Math.min(100, y)),
    raw: point,
  };
}

function pointText(point: Point2D): string {
  return `x=${point.x?.toFixed(2)}, y=${point.y?.toFixed(2)}`;
}

function lineText(line: Line2D): string {
  return `(${line.x0.toFixed(2)}, ${line.y0.toFixed(2)}) -> (${line.x1.toFixed(2)}, ${line.y1.toFixed(2)})`;
}

function makeMarkers(
  entities: UnknownRecord[],
  prefix: string,
  className: string,
  fieldLength: number,
  fieldWidth: number,
): FieldMarker[] {
  return entities.flatMap((entity, index) => {
    const rawPoint = readEntityPose(entity);
    const point = mapPoint(rawPoint, fieldLength, fieldWidth);
    if (!point) return [];

    const label = readLabel(entity, `${prefix}${index + 1}`);
    const role = readDetail(entity, ['role', 'role_current']);
    const decision = readDetail(entity, ['decision', 'behavior', 'state']);
    const cost = readDetail(entity, ['cost', 'tm_cost', 'ball_control_cost']);
    const title = [
      `${prefix} ${label}`,
      pointText(point.raw),
      role ? `role=${role}` : null,
      decision ? `decision=${decision}` : null,
      cost ? `cost=${cost}` : null,
    ].filter(Boolean).join(' | ');

    return [{ key: `${className}-${label}-${index}`, label, className, point, title }];
  });
}

function makeBallMarkers(
  entities: UnknownRecord[],
  fieldLength: number,
  fieldWidth: number,
): FieldMarker[] {
  return entities.flatMap((entity, index) => {
    const rawPoint = readEntityBall(entity);
    const point = mapPoint(rawPoint, fieldLength, fieldWidth);
    if (!point) return [];

    const owner = readLabel(entity, `T${index + 1}`);
    return [{
      key: `teammate-ball-${owner}-${index}`,
      label: 'b',
      className: 'teammateBall',
      point,
      title: `ball from ${owner} | ${pointText(point.raw)}`,
    }];
  });
}

function makeFieldLineSegments(
  lines: UnknownRecord[],
  fieldLength: number,
  fieldWidth: number,
): FieldSegment[] {
  return lines.flatMap((lineRecord, index) => {
    const rawLine = readLine(lineRecord.pos_to_field ?? lineRecord.posToField ?? lineRecord.field);
    if (!rawLine) return [];

    const start = mapPoint({ x: rawLine.x0, y: rawLine.y0 }, fieldLength, fieldWidth);
    const end = mapPoint({ x: rawLine.x1, y: rawLine.y1 }, fieldLength, fieldWidth);
    if (!start || !end) return [];

    const type = readDetail(lineRecord, ['type']) ?? 'Line';
    const confidence = readDetail(lineRecord, ['confidence']);
    return [{
      key: `field-line-${type}-${index}`,
      className: `detectedFieldLine lineType${type}`,
      start,
      end,
      title: [type, lineText(rawLine), confidence ? `confidence=${confidence}` : null].filter(Boolean).join(' | '),
    }];
  });
}

function segmentStyle(start: MappedPoint, end: MappedPoint): CSSProperties {
  const dx = end.x - start.x;
  const dy = end.y - start.y;
  return {
    left: `${start.x}%`,
    top: `${start.y}%`,
    width: `${Math.hypot(dx, dy)}%`,
    transform: `rotate(${Math.atan2(dy, dx)}rad)`,
  };
}

function markerStyle(point: MappedPoint): CSSProperties {
  const inset = 2.4;
  const x = Math.max(inset, Math.min(100 - inset, point.x));
  const y = Math.max(inset, Math.min(100 - inset, point.y));
  return { left: `${x}%`, top: `${y}%` };
}

function renderMarker(marker: FieldMarker) {
  return (
    <span
      key={marker.key}
      className={`marker ${marker.className}`}
      style={markerStyle(marker.point)}
      title={marker.title}
    >
      {marker.label}
    </span>
  );
}

function renderSegment(segment: FieldSegment) {
  return (
    <span
      key={segment.key}
      className={`fieldSegment ${segment.className}`}
      style={segmentStyle(segment.start, segment.end)}
      title={segment.title}
    />
  );
}

export default function FieldView({ snapshot }: FieldViewProps) {
  const status = snapshot?.status;
  const statusRecord = isRecord(status) ? status : null;
  const perception = isRecord(statusRecord?.perception) ? statusRecord.perception : null;
  const fieldLength = positiveNumber(status?.field?.length, FIELD_LENGTH);
  const fieldWidth = positiveNumber(status?.field?.width, FIELD_WIDTH);
  const ball = mapPoint(status?.ball?.current?.pos_to_field, fieldLength, fieldWidth);
  const predictedBall = mapPoint(status?.prediction?.predicted_ball?.pos_to_field, fieldLength, fieldWidth);
  const trajectory = status?.prediction?.trajectory ?? [];
  const teammates = entityArray(status?.team?.teammates);
  const goalposts = entityArray(perception?.goalposts);
  const fieldLines = entityArray(perception?.field_lines);
  const opponents = entityArray(perception?.opponents);
  const teammateMarkers = makeMarkers(teammates, 'T', 'teammate', fieldLength, fieldWidth);
  const teammateBallMarkers = makeBallMarkers(teammates, fieldLength, fieldWidth);
  const goalpostMarkers = makeMarkers(goalposts, 'G', 'goalpost', fieldLength, fieldWidth);
  const opponentMarkers = makeMarkers(opponents, 'O', 'opponent', fieldLength, fieldWidth);
  const fieldLineSegments = makeFieldLineSegments(fieldLines, fieldLength, fieldWidth);
  const showTeammates = teammateMarkers.length > 0 || teammateBallMarkers.length > 0;

  // 本机位姿 (带朝向) —— 之前没画, 验证时看不到"自己在哪/朝哪"
  const selfRaw = status?.pose?.field;
  const self = mapPoint(readAnyPoint(selfRaw), fieldLength, fieldWidth);
  const selfTheta = isRecord(selfRaw) && isFiniteNumber(selfRaw.theta) ? selfRaw.theta : null;

  // 球速矢量 (验证球路预测方向) —— 从球沿速度方向画一条短线 (0.6s 提前量)
  const velocity = status?.prediction?.velocity;
  let velocitySeg: FieldSegment | null = null;
  if (
    ball
    && isFiniteNumber(ball.raw.x)
    && isFiniteNumber(ball.raw.y)
    && isRecord(velocity)
    && isFiniteNumber(velocity.x)
    && isFiniteNumber(velocity.y)
  ) {
    const speed = Math.hypot(velocity.x, velocity.y);
    if (speed > 0.15) {
      const end = mapPoint({ x: ball.raw.x + velocity.x * 0.6, y: ball.raw.y + velocity.y * 0.6 }, fieldLength, fieldWidth);
      if (end) velocitySeg = { key: 'ball-velocity', className: 'velocityVector', start: ball, end, title: `ball velocity ${speed.toFixed(2)} m/s` };
    }
  }

  // 队友球共享: 从被采用的来源队友连一条线到球 (验证"共享信息"采用了谁)
  const ballShare = status?.team?.ball_share;
  let shareSeg: FieldSegment | null = null;
  if (ball && ballShare?.active && ballShare?.reliable && ballShare?.source_player_id) {
    const src = teammates.find((t) => Number(t.player_id) === Number(ballShare.source_player_id));
    const srcPose = src ? mapPoint(readEntityPose(src), fieldLength, fieldWidth) : null;
    if (srcPose) shareSeg = { key: 'share-source', className: 'shareLine', start: srcPose, end: ball, title: `shared ball from P${ballShare.source_player_id}` };
  }
  const penaltyDepth = positiveNumber(status?.field?.penalty_area_length, PENALTY_DEPTH);
  const penaltyWidth = positiveNumber(status?.field?.penalty_area_width, PENALTY_WIDTH);
  const goalWidth = positiveNumber(status?.field?.goal_width, GOAL_WIDTH);
  const circleRadius = positiveNumber(status?.field?.circle_radius, CENTER_CIRCLE_RADIUS);
  const fieldStyle = {
    '--penalty-depth': `${Math.min(45, (penaltyDepth / fieldLength) * 100)}%`,
    '--penalty-width': `${Math.min(88, (penaltyWidth / fieldWidth) * 100)}%`,
    '--goal-width': `${Math.min(70, (goalWidth / fieldWidth) * 100)}%`,
    '--center-circle-size': `${Math.min(55, ((circleRadius * 2) / fieldWidth) * 100)}%`,
  } as CSSProperties;

  return (
    <>
    <div className="field" style={fieldStyle}>
      <div className="fieldLine centerLine" />
      <div className="fieldLine centerCircle" />
      <div className="fieldLine penaltyBox leftPenalty" />
      <div className="fieldLine penaltyBox rightPenalty" />
      <div className="fieldLine goalBox leftGoal" />
      <div className="fieldLine goalBox rightGoal" />
      {fieldLineSegments.map(renderSegment)}
      {shareSeg && renderSegment(shareSeg)}
      {velocitySeg && renderSegment(velocitySeg)}
      {trajectory.map((point, index) => {
        const mapped = mapPoint(point, fieldLength, fieldWidth);
        if (!mapped) return null;
        return (
          <span
            key={`${point.x}-${point.y}-${index}`}
            className="marker trajectory"
            style={{ left: `${mapped.x}%`, top: `${mapped.y}%` }}
            title={`trajectory ${index + 1}`}
          />
        );
      })}
      {teammateBallMarkers.map(renderMarker)}
      {goalpostMarkers.map(renderMarker)}
      {opponentMarkers.map(renderMarker)}
      {teammateMarkers.map(renderMarker)}
      {ball && <span className="marker ball" style={markerStyle(ball)} title={`ball | ${pointText(ball.raw)}`}>B</span>}
      {predictedBall && <span className="marker predicted" style={markerStyle(predictedBall)} title={`predicted ball | ${pointText(predictedBall.raw)}`}>P</span>}
      {self && (
        <span
          className="marker robot"
          style={markerStyle(self)}
          title={`self | ${pointText(self.raw)}${selfTheta != null ? ` | theta=${selfTheta.toFixed(2)}` : ''}`}
        >
          R
          {selfTheta != null && <span className="heading" style={{ transform: `rotate(${-selfTheta}rad)` }} />}
        </span>
      )}
    </div>
    <div className="fieldLegend">
      <span><b className="legendDot robotDot" />self</span>
      <span><b className="legendDot ballDot" />ball</span>
      <span><b className="legendDot predictedDot" />predicted</span>
      {velocitySeg && <span><b className="legendLine velocityLegend" />ball vel</span>}
      {shareSeg && <span><b className="legendLine shareLegend" />share src</span>}
      {showTeammates && <span><b className="legendDot teammateDot" />teammate</span>}
      {opponentMarkers.length > 0 && <span><b className="legendDot opponentDot" />opponent</span>}
      <span><b className="legendDot goalpostDot" />goalpost</span>
      <span><b className="legendLine" />line</span>
    </div>
    </>
  );
}
