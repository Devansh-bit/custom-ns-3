"use client"

import * as React from "react"
import { Area, AreaChart, CartesianGrid, XAxis, YAxis } from "recharts"

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
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { fetchSTATimeSeries, fetchSimulationUptime } from "@/lib/api-dashboard"
import { useState, useEffect, useMemo, useContext } from "react";
import { readableMetricShown } from "@/lib/utils"
import { SelectedClientContext, DataModeContext } from "@/lib/contexts";

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

export function MetricsChart() {
  let [filteredData, setFilteredData] = useState<any[]>([]);
  const [metricShown, setMetricShown] = useState("rssi");
  const [timeResolution, setTimeResolution] = useState("10");
  const [simulationUptime, setSimulationUptime] = useState<number | null>(null);
  const { selectedClient } = useContext(SelectedClientContext)!;
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
      setFilteredData([]);
      return;
    }

    let timer: number | undefined;

    const fetchData = () => {
        const apiMetric = metricShown === 'packet_loss' ? 'packet_loss_rate' : metricShown;
        fetchSTATimeSeries(apiMetric as any, selectedClient || undefined, parseInt(timeResolution, 10)).then((v) => {
          // In simulation mode, filter to show only up to simulation time
            setFilteredData(v);
        }).catch(error => {
            console.error("fetchSTATimeSeries error:", error);
        });
    }

    fetchData();

    if (selectedClient) {
        // poll for individual client data
        timer = window.setInterval(fetchData, 5000);
    } else {
        // poll for aggregate data every 10s
        timer = window.setInterval(fetchData, 10000);
    }

    return () => {
        if (timer) window.clearInterval(timer);
    }
  }, [metricShown, timeResolution, selectedClient, refreshKey, isSimulationMode, simulationUptime])

  let bounds = useMemo(() => {
    let max = -Infinity;
    let min = Infinity;
    for (let point of filteredData) {
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
  }, [filteredData]);

  const dataKey = selectedClient ? "value" : "avg_value";
  const name = selectedClient ? readableMetricShown(metricShown) : "Average " + readableMetricShown(metricShown);

  return (
    <Card className="pt-0 shadow-sm">
      <CardHeader className="flex flex-col lg:flex-row items-center gap-2 space-y-0 border-b py-3">
        <div className="grid flex-1 gap-1">
          <CardTitle>QoE Metrics</CardTitle>
        </div>

        <div className="flex">
          <Select value={metricShown} onValueChange={(v) => setMetricShown(v)}>
            <SelectTrigger className="hidden w-40 rounded-lg sm:flex">
              <SelectValue placeholder="Metric" />
            </SelectTrigger>
            <SelectContent className="rounded-xl">
              <SelectItem value="latency">Latency</SelectItem>
              <SelectItem value="jitter">Jitter</SelectItem>
              <SelectItem value="packet_loss">Packet Loss</SelectItem>
              <SelectItem value="rssi">RSSI</SelectItem>
              <SelectItem value="snr">SNR</SelectItem>
            </SelectContent>
          </Select>

          <Select value={timeResolution} onValueChange={(v) => setTimeResolution(v)}>
            <SelectTrigger className="hidden w-40 rounded-lg sm:flex">
              <SelectValue placeholder="Time" />
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
        </div>
      </CardHeader>
      <CardContent className="px-2 pt-4 sm:px-6 sm:pt-6">
        <ChartContainer
          config={chartConfig}
          className="aspect-auto h-[250px] w-full"
        >
          <AreaChart
            key={`${timeResolution}-${metricShown}`}
            data={filteredData}
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
              tickMargin={8}
              minTickGap={32}
              height={50}
              label={{ value: "Time", dx: -40, dy: 15 }}
              tickFormatter={(v) => {
                const time = new Date()
                if (filteredData.length > 0) {
                  time.setSeconds(time.getSeconds() - (filteredData[filteredData.length - 1]["sim_time_seconds"] - v));
                }
                const hour = time.getHours() % 12 == 0 ? 12 : time.getHours() % 12;
                const am_pm = time.getHours() >= 12 ? "PM" : "AM";
                return `${hour}:${time.getMinutes().toString().padStart(2, '0')} ${am_pm}`
              }}
            />
            <YAxis
              domain={bounds}
              dataKey={dataKey}
              tickLine={false}
              axisLine={false}
            />
            <ChartTooltip
              cursor={false}
              content={<ChartTooltipContent indicator="dot" />}
            />
            <Area
              dataKey={dataKey}
              type="natural"
              name={name}
              fill="url(#fillMobile)"
              baseValue="dataMin"
              stroke={chartColor}
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
