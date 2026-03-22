interface MetricBarProps {
  name: string;
  value: number; // 0..100
  status: string;
  thresholds: { bad: number; okayish: number }; // e.g. { redEnd: 25, yellowEnd: 60 }
  total_clients: number,
  colors?: {
    red?: string;
    yellow?: string;
    green?: string;
    remaining?: string; // overlay color for the unfilled right side
  };
}

export function MetricBar({
  name,
  value,
  thresholds,
  total_clients,
  colors = {
    red:    "#ec4899",  // pink - matches our palette
    yellow: "#8b5cf6",  // purple - matches our palette
    green:  "#3b82f6",  // bright blue - matches our palette
    remaining: "#3b82f6", // bright blue - matches our palette
  },
}: MetricBarProps) {
  let { bad, okayish } = thresholds;
  bad = bad / total_clients * 100;
  okayish = okayish / total_clients * 100;

  // guard for order
  const r = Math.max(0, Math.min(100, bad));
  const y = r + Math.max(0, Math.min(100, okayish));
  const g = 100 - y;

  return (
    <div>
      <div className="mb-2 flex items-center justify-between">
        <span className="text-muted-foreground">{name}</span>
        <span>{value}% Good</span>
      </div>

      <div className="relative h-2 rounded-full bg-muted overflow-hidden">
        {/* full background: red | yellow | green = 100% */}
        <div className="absolute inset-0 flex">
          <div style={{ width: `${r}%`, background: colors.red }} className="h-full rounded-l-full" />
          <div style={{ width: `${y - r}%`, background: colors.yellow }} className="h-full" />
          <div style={{ width: `${g}%`, background: colors.green }} className="h-full rounded-r-full" />
        </div>
      </div>
    </div>
  );
}
