import { LucideIcon } from "lucide-react";
import { MetricBar } from "./MetricBar";

interface Metric {
  name: string;
  value: number;
  status: string;
  thresholds: { redEnd: number; yellowEnd: number };
}

interface MetricCardProps {
  icon: LucideIcon;
  title: string;
  subtitle: string;
  metrics: Metric[];
}

export function MetricCard({ icon: Icon, title, subtitle, metrics }: MetricCardProps) {
  return (
    <div className="rounded-lg border bg-card p-6">
      <div className="mb-6 flex items-start gap-3">
        <div className="rounded bg-muted p-2">
          <Icon className="h-5 w-5 text-muted-foreground" />
        </div>
        <div className="flex-1">
          <h3 className="mb-1">{title}</h3>
          <p className="text-sm text-muted-foreground">{subtitle}</p>
        </div>
      </div>

      <div className="space-y-6">
        <MetricBar
          name="Latency"
          value={Math.floor(metrics.latency.good / metrics.total * 100)}
          total_clients={metrics.total}
          thresholds={metrics.latency}
        />
        <MetricBar
          name="Jitter"
          value={Math.floor(metrics.jitter.good / metrics.total * 100)}
          total_clients={metrics.total}
          thresholds={metrics.jitter}
        />

        <MetricBar
          name="Packet Loss"
          value={Math.floor(metrics.packet_loss.good / metrics.total * 100)}
          total_clients={metrics.total}
          thresholds={metrics.packet_loss}
        />
      </div>
    </div>
  );
}
