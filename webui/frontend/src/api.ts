import type { LogSource, RobotSnapshot } from './types';

const apiBase = import.meta.env.VITE_API_BASE_URL || 'http://localhost:8000';

export function getApiBase(): string {
  return apiBase.replace(/\/$/, '');
}

export function getWsBase(): string {
  const base = getApiBase();
  if (base.startsWith('https://')) {
    return `wss://${base.slice('https://'.length)}`;
  }
  if (base.startsWith('http://')) {
    return `ws://${base.slice('http://'.length)}`;
  }
  return base;
}

export function getRobotLogWsUrl(robotId: string, source: LogSource): string {
  return `${getWsBase()}/ws/robots/${encodeURIComponent(robotId)}/logs/${source}`;
}

export async function fetchRobots(): Promise<RobotSnapshot[]> {
  const response = await fetch(`${getApiBase()}/api/robots`);
  if (!response.ok) {
    throw new Error(`Failed to fetch robots: ${response.status}`);
  }
  return response.json();
}

export async function fetchLatest(robotId: string): Promise<RobotSnapshot> {
  const response = await fetch(`${getApiBase()}/api/robots/${encodeURIComponent(robotId)}/latest`);
  if (!response.ok) {
    throw new Error(`Failed to fetch latest status: ${response.status}`);
  }
  return response.json();
}
