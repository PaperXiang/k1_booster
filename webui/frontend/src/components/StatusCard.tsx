import type { ReactNode } from 'react';

type StatusCardProps = {
  title: string;
  children: ReactNode;
  className?: string;
};

export default function StatusCard({ title, children, className }: StatusCardProps) {
  return (
    <section className={`card${className ? ` ${className}` : ''}`}>
      <h2>{title}</h2>
      {children}
    </section>
  );
}
