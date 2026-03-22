import { ReactNode } from 'react';

interface StatCardProps {
  icon: ReactNode;
  title: string;
  value: string | number;
  subtitle: string;
  badge?: string;
  badgeColor?: string;
}

export function StatCard({ icon, title, value, subtitle, badge, badgeColor = 'bg-cyan-50 text-cyan-600' }: StatCardProps) {
  return (
    <div className="bg-white rounded-lg p-6 shadow-sm border border-gray-100 ">
      <div className="flex items-start justify-between mb-4">
        <div className="w-10 h-10 bg-blue-50 rounded-lg flex items-center justify-center text-blue-500">
          {icon}
        </div>
        {badge && (
          <span className={`px-2 py-1 rounded text-xs ${badgeColor}`}>
            {badge}
          </span>
        )}
      </div>
      <div className="text-sm text-gray-500 mb-1">{title}</div>
      <div className="text-3xl mb-1">{value}</div>
      <div className="text-xs text-gray-400">{subtitle}</div>
    </div>
  );
}
