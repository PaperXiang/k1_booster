import type { Point2D, RobotSnapshot } from '../types';

type FieldViewProps = {
  snapshot: RobotSnapshot | null;
};

const FIELD_LENGTH = 14.16;
const FIELD_WIDTH = 9.22;

function mapPoint(point?: Point2D | null) {
  if (!point || typeof point.x !== 'number' || typeof point.y !== 'number') {
    return null;
  }
  const x = ((point.x + FIELD_LENGTH / 2) / FIELD_LENGTH) * 100;
  const y = ((FIELD_WIDTH / 2 - point.y) / FIELD_WIDTH) * 100;
  return {
    x: Math.max(0, Math.min(100, x)),
    y: Math.max(0, Math.min(100, y)),
  };
}

export default function FieldView({ snapshot }: FieldViewProps) {
  const status = snapshot?.status;
  const robot = mapPoint(status?.pose?.field);
  const ball = mapPoint(status?.ball?.current?.pos_to_field);
  const predictedBall = mapPoint(status?.prediction?.predicted_ball?.pos_to_field);
  const trajectory = status?.prediction?.trajectory ?? [];

  return (
    <div className="field">
      <div className="fieldLine centerLine" />
      <div className="fieldLine centerCircle" />
      {trajectory.map((point, index) => {
        const mapped = mapPoint(point);
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
      {robot && <span className="marker robot" style={{ left: `${robot.x}%`, top: `${robot.y}%` }}>R</span>}
      {ball && <span className="marker ball" style={{ left: `${ball.x}%`, top: `${ball.y}%` }}>B</span>}
      {predictedBall && <span className="marker predicted" style={{ left: `${predictedBall.x}%`, top: `${predictedBall.y}%` }}>P</span>}
    </div>
  );
}
