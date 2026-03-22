"use client"

import { TrendingDown, TrendingUp } from "lucide-react"
import { Area, AreaChart, CartesianGrid, XAxis, YAxis } from "recharts"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"

import {
  Card,
  CardContent,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import {
  ChartConfig,
  ChartContainer,
  ChartLegend,
  ChartTooltip,
  ChartTooltipContent,
} from "@/components/ui/chart"
import { useEffect, useState, useMemo, useContext } from "react"
import { fetchNetworkScore, fetchSimulationUptime } from "@/lib/api-dashboard"
import { DataModeContext } from "@/lib/contexts"

// Use the same color as APHealth for consistency
const chartColor = "#3b82f6"; // bright blue

const chartConfig = {
  desktop: {
    label: "Desktop",
    color: chartColor,
    icon: TrendingDown,
  },
  mobile: {
    label: "Mobile",
    color: chartColor,
    icon: TrendingUp,
  },
} satisfies ChartConfig

type Props = {
  refreshMs?: number; // optional polling interval
};

export function ChartAreaDefault({ refreshMs }: Props) {
  const [chartData, setChartData] = useState<Array<{ network_score?: number; value?: number; sim_time_seconds?: number }>>([]);
  const [timeResolution, setTimeResolution] = useState("10");
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
      setChartData([]);
      return;
    }

    let cancelled = false;
    let timer: number | undefined;

    const load = async () => {
      // Fetch data - in simulation mode, filter to show only up to simulation time
      const v = await fetchNetworkScore(parseInt(timeResolution));
      if (!cancelled) {
        setChartData(v);
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
  }, [timeResolution, refreshKey, isSimulationMode, simulationUptime, refreshMs])

  let bounds = useMemo(() => {
    let max = -Infinity;
    let min = Infinity;
    for (let point of chartData) {
      // either avg_value or value
      let val = point.network_score ?? point.value;
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
  }, [chartData]);


  return (
    <Card className="shadow-sm">
      <CardHeader className="flex items-center gap-2 space-y-0 border-b justify-between sm:flex-row">
        <CardTitle>Network Quality</CardTitle>
        <Select value={timeResolution} onValueChange={(v) => setTimeResolution(v)}>
          <SelectTrigger className="hidden w-40 rounded-lg sm:flex">
            <SelectValue placeholder="Metric" />
          </SelectTrigger>
          <SelectContent className="rounded-xl">
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
      </CardHeader>
      <CardContent>
        <ChartContainer config={chartConfig} className="mt-9 h-72 w-full">
          <AreaChart
            key={timeResolution}
            accessibilityLayer
            data={chartData}
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
                if (!chartData || chartData.length === 0) return "";
                const time = new Date()
                const lastDataPoint = chartData[chartData.length - 1];
                if (lastDataPoint?.sim_time_seconds !== undefined) {
                  time.setSeconds(time.getSeconds() - (lastDataPoint.sim_time_seconds - v));
                }
                const hour = time.getHours() % 12 == 0 ? 12 : time.getHours() % 12;
                const am_pm = time.getHours() >= 12 ? "PM" : "AM";
                return `${hour}:${time.getMinutes().toString().padStart(2, "0")} ${am_pm}`
              }}
            />
            <YAxis
              dataKey="network_score"
              domain={bounds}
              tickLine={false}
              axisLine={false}
            />
            <ChartTooltip
              cursor={false}
              content={<ChartTooltipContent indicator="line" />}
            />
            <Area
              dataKey="network_score"
              type="natural"
              stroke={chartColor}
              fill="url(#fillMobile)"
              name="Network Score"
              isAnimationActive={true}
              animationDuration={2000}
              animationEasing="ease-out"
            />
            <ChartLegend />
          </AreaChart>
        </ChartContainer>
      </CardContent>
    </Card>
  )
}
