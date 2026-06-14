export type Point2D = {
  x?: number | null;
  y?: number | null;
};

export type Point3D = Point2D & {
  z?: number | null;
};

export type Pose2D = Point2D & {
  theta?: number | null;
};

export type GameObject = {
  label?: string;
  confidence?: number | null;
  range?: number | null;
  yaw_to_robot?: number | null;
  pitch_to_robot?: number | null;
  pos_to_robot?: Point3D;
  pos_to_field?: Point3D;
};

export type RobotSnapshot = {
  robot_id: string;
  online: boolean;
  last_seen_at?: number | null;
  status: {
    timestamp?: number;
    robot?: Record<string, unknown>;
    game?: Record<string, unknown>;
    behavior?: Record<string, unknown>;
    pose?: {
      field?: Pose2D;
      odom?: Pose2D;
      head?: Record<string, number | null>;
    };
    ball?: {
      detected?: boolean;
      known?: boolean;
      current?: GameObject;
      kick_dir?: number | null;
      kick_type?: string;
    };
    prediction?: {
      enabled?: boolean;
      use_for_chase?: boolean;
      valid?: boolean;
      predicted_only?: boolean;
      filtered_ball?: GameObject;
      predicted_ball?: GameObject;
      velocity?: Point2D;
      acceleration?: Point2D;
      trajectory?: Point2D[];
    };
    team?: {
      enable_com?: boolean;
      tm_ip?: string;
      send_id?: number;
      teammates?: Array<Record<string, unknown>>;
    };
    health?: Record<string, unknown>;
  };
};
