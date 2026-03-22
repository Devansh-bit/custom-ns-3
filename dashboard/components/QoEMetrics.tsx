"use client";
import { AreaChart, Area, XAxis, YAxis, CartesianGrid, ResponsiveContainer } from 'recharts';
import { ChartConfig, ChartContainer, ChartTooltip, ChartTooltipContent } from './ui/chart';
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectLabel,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import {
  Card,
  CardContent,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import { useState, useEffect, useMemo, useContext } from 'react';
import { fetchSTATimeSeries, fetchSimulationUptime } from '@/lib/api-dashboard';
import { ChartLegend } from './ui/chart';
import { readableMetricShown } from '@/lib/utils';
import { DataModeContext } from '@/lib/contexts';

type Props = {
  refreshMs?: number; // optional polling interval
};

// Use the same color as APHealth for consistency
const chartColor = "#3b82f6"; // bright blue

const chartConfig = {
  desktop: {
    label: "Desktop",
    color: chartColor,
  },
  mobile: {
    label: "Mobile",
    color: chartColor,
  },
} satisfies ChartConfig


export default function QOEMetricsChart({ refreshMs }: Props) {
  const [metricShown, setMetricShown] = useState("latency");
  const [timeResolution, setTimeResolution] = useState("10");
  const [qoe_client, setQoeClient] = useState<any[]>([]);
  const [simulationUptime, setSimulationUptime] = useState<number | null>(null);
  const dataModeContext = useContext(DataModeContext);
  const refreshKey = dataModeContext?.refreshKey ?? 0;
  const isSimulationMode = dataModeContext?.mode === 'simulation';

  // Fetch simulation uptime when in simulation mode
  useEffect(() => {
    if (isSimulationMode) {
      fetchSimulationUptime().then((uptime) => {
        setSimulationUptime(uptime);
      });
    } else {
      setSimulationUptime(null);
    }
  }, [isSimulationMode, refreshKey]);

  // Check if a time resolution option should be disabled
  // Enable only if simulation uptime > time resolution (strictly greater)
  const isTimeOptionDisabled = (minutes: number): boolean => {
    if (!isSimulationMode || !simulationUptime) return false;
    const seconds = minutes * 60;
    return simulationUptime <= seconds;
  };

  // Auto-switch to valid resolution if current one is disabled
  useEffect(() => {
    if (isSimulationMode && simulationUptime) {
      const currentMinutes = parseInt(timeResolution);
      const currentSeconds = currentMinutes * 60;
      if (simulationUptime <= currentSeconds) {
        // Current resolution is disabled, find the highest available one
        // Options: 10min, 1hr (60min), 5hr (300min), 12hr (720min)
        const resolutions = [720, 300, 60, 10]; // From longest to shortest
        for (const minutes of resolutions) {
          const seconds = minutes * 60;
          if (simulationUptime > seconds) {
            setTimeResolution(minutes.toString());
            return;
          }
        }
        // If none are available, set to 10 (will show empty)
        setTimeResolution("10");
      }
    }
  }, [isSimulationMode, simulationUptime, timeResolution]);

  useEffect(() => {
    // In simulation mode, wait for uptime to be loaded before fetching
    if (isSimulationMode && simulationUptime === null) {
      // Don't fetch until uptime is known
      setQoeClient([]);
      return;
    }

    let cancelled = false;
    let timer: number | undefined;

    const load = async () => {
      // Fetch data - in simulation mode, filter to show only up to simulation time
      const v = await fetchSTATimeSeries(metricShown as any, undefined, parseInt(timeResolution, 10));
      if (!cancelled) {
        setQoeClient(v);
      }
    };

    load();
    if (refreshMs && refreshMs > 0) {
      // @ts-ignore
      timer = window.setInterval(load, refreshMs);
    }

    return () => {
      cancelled = true;
      if (timer) window.clearInterval(timer);
    };
  }, [metricShown, timeResolution, refreshKey, isSimulationMode, simulationUptime, refreshMs])

  let bounds = useMemo(() => {
    if (!qoe_client || qoe_client.length === 0) return [0, 0];
    let max = -Infinity;
    let min = Infinity;
    for (let point of qoe_client) {
      // either avg_value or value
      let val = point.avg_value ?? point.value;
      if (val !== undefined) {
        if (val > max) max = val;
        if (val < min) min = val;
      }
    }
    let lb = 1.2 * min - 0.2 * max;
    lb = Math.floor(lb); // round down to nearest 1
    let ub = 1.2 * max - 0.2 * min;
    ub = Math.ceil(ub); // round up to nearest 1

    if (max == -Infinity || min == Infinity) {
      lb = 0;
      ub = 0;
    }

    return [lb, ub];
  }, [qoe_client]);

  return (
    <Card className="shadow-sm">
      <CardHeader className="flex items-center gap-2 space-y-0 border-b justify-between sm:flex-row">
        <CardTitle>QOE Metrics</CardTitle>
        <div className="flex gap-2">
          <Select value={metricShown} onValueChange={(v) => setMetricShown(v)}>
            <SelectTrigger className="w-[140px]">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              <SelectGroup>
                <SelectLabel>Metric</SelectLabel>
                <SelectItem value="latency">Latency</SelectItem>
                <SelectItem value="jitter">Jitter</SelectItem>
                <SelectItem value="packet_loss">Packet Loss</SelectItem>
                <SelectItem value="rssi">RSSI</SelectItem>
                <SelectItem value="snr">SNR</SelectItem>
              </SelectGroup>
            </SelectContent>
          </Select>
          <Select value={timeResolution} onValueChange={(v) => setTimeResolution(v)}>
            <SelectTrigger className="w-[140px]">
              <SelectValue placeholder="Time" />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="10" disabled={isTimeOptionDisabled(10)}>
                Last 10 minutes
              </SelectItem>
              <SelectItem value="60" disabled={isTimeOptionDisabled(60)}>
                Last 1 hour
              </SelectItem>
              <SelectItem value="300" disabled={isTimeOptionDisabled(300)}>
                Last 5 hours
              </SelectItem>
              <SelectItem value="720" disabled={isTimeOptionDisabled(720)}>
                Last 12 hours
              </SelectItem>
            </SelectContent>
          </Select>
        </div>
      </CardHeader>

      <CardContent>
        <ChartContainer config={chartConfig} className="h-full w-full max-h-[300px]">
          <AreaChart
            key={timeResolution}
            accessibilityLayer
            data={qoe_client}
            margin={{
              left: 12,
              right: 12,
            }}
          >
            <defs>
              <linearGradient id="fillMobile" x1="0" y1="0" x2="0" y2="1">
                <stop
                  offset="5%"
                  stopColor={chartColor}
                  stopOpacity={0.8}
                />
                <stop
                  offset="95%"
                  stopColor={chartColor}
                  stopOpacity={0.1}
                />
              </linearGradient>
            </defs>
            <CartesianGrid vertical={false} />
            <XAxis
              dataKey="sim_time_seconds"
              tickLine={false}
              axisLine={false}
              height={40}
              label={{ value: "Time", dx: -40, dy: 10, fontSize: 11 }}
              tickFormatter={(v) => {
                if (!qoe_client || qoe_client.length === 0) return "";
                const time = new Date()
                time.setSeconds(time.getSeconds() - (qoe_client[qoe_client.length - 1]["sim_time_seconds"] - v));
                const lastDataPoint = qoe_client[qoe_client.length - 1];
                if (lastDataPoint?.sim_time_seconds !== undefined) {
                  time.setSeconds(time.getSeconds() - (lastDataPoint.sim_time_seconds - v));
                }
                const hour = time.getHours() % 12 == 0 ? 12 : time.getHours() % 12;
                const am_pm = time.getHours() >= 12 ? "PM" : "AM";
                return `${hour}:${time.getMinutes().toString().padStart(2, "0")} ${am_pm}`
              }}
            />
            <YAxis
              axisLine={{ stroke: 'hsl(var(--border))' }}
              tickLine={false}
              tick={{ fontSize: 11 }}
              dataKey="avg_value"
              domain={bounds}
              label={{
                value:
                  metricShown === "latency" ? "Latency (ms)" :
                    metricShown === "jitter" ? "Jitter (ms)" :
                      metricShown === "packet_loss" ? "Packet Loss (%)" :
                        metricShown === "rssi" ? "RSSI (dBm)" :
                          "SNR (dBm)",
                angle: -90,
                position: 'insideLeft',
                fontSize: 11,
                offset: 10,
              }}
            />
            <ChartTooltip
              cursor={false}
              content={<ChartTooltipContent indicator="line" />}
            />
            <Area
              dataKey="avg_value"
              type="natural"
              stroke={chartColor}
              fill="url(#fillMobile)"
              name={readableMetricShown(metricShown)}
              isAnimationActive={true}
              animationDuration={2000}
              animationEasing="ease-out"
              baseValue="dataMin"
            />
            <ChartLegend />
          </AreaChart>
        </ChartContainer>
      </CardContent>
    </Card>
  );
}
