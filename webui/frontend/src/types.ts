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

export type BoundingBox = {
  xmin?: number | null;
  xmax?: number | null;
  ymin?: number | null;
  ymax?: number | null;
};

export type FieldLine2D = {
  x0?: number | null;
  y0?: number | null;
  x1?: number | null;
  y1?: number | null;
};

export type GameObject = {
  label?: string;
  color?: string | null;
  confidence?: number | null;
  range?: number | null;
  yaw_to_robot?: number | null;
  pitch_to_robot?: number | null;
  pos_to_robot?: Point3D;
  pos_to_field?: Point3D;
  bounding_box?: BoundingBox;
  id?: string | number | null;
  name?: string | null;
  id_confidence?: number | null;
  info?: string | null;
  timestamp?: number | null;
};

export type FieldLineInfo = Record<string, unknown> & {
  pos_to_field?: FieldLine2D;
  pos_to_robot?: FieldLine2D;
  pos_on_cam?: FieldLine2D;
  dir?: string | null;
  half?: string | null;
  side?: string | null;
  type?: string | null;
  confidence?: number | null;
  timestamp?: number | null;
};

export type FieldEntity = Record<string, unknown> & {
  id?: string | number | null;
  robot_id?: string | null;
  player_id?: number | null;
  label?: string | null;
  role?: string | null;
  decision?: string | null;
  online?: boolean | null;
  last_seen_at?: number | null;
  pose?: Pose2D;
  field?: Pose2D;
  pos_to_field?: Point3D;
  robot_pose_to_field?: Pose2D;
  robotPoseToField?: Pose2D;
  ball?: GameObject | Record<string, unknown>;
  ball_pos_to_field?: Point3D;
  ballPosToField?: Point3D;
  cost?: number | null;
  cost_rank?: number | null;
  is_lead?: boolean | null;
};

export type FieldInfo = {
  type?: string | null;
  length?: number | null;
  width?: number | null;
  penalty_dist?: number | null;
  goal_width?: number | null;
  circle_radius?: number | null;
  penalty_area_length?: number | null;
  penalty_area_width?: number | null;
  goal_area_length?: number | null;
  goal_area_width?: number | null;
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
    field?: FieldInfo;
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
      teammates?: FieldEntity[];
      ball_share?: {
        active?: boolean;
        reliable?: boolean;
        source_player_id?: number | null;
        confidence?: number | null;
        fresh?: boolean;
      };
    };
    perception?: {
      robots?: FieldEntity[];
      opponents?: FieldEntity[];
      obstacles?: FieldEntity[];
      goalposts?: GameObject[];
      markings?: GameObject[];
      field_lines?: FieldLineInfo[];
    };
    opponents?: FieldEntity[];
    obstacles?: FieldEntity[];
    health?: Record<string, unknown>;
  };
};
