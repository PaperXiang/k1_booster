import type { ReactNode } from 'react';

type StatusCardProps = {
  title: string;
  children: ReactNode;
};

export default function StatusCard({ title, children }: StatusCardProps) {
  return (
    <section className="card">
      <h2>{title}</h2>
      {children}
    </section>
  );
}
