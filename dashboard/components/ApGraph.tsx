"use client";

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
  ChartLegendContent,
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

import {
  type ChannelUtilizationDataPoint,
} from "@/lib/api-dashboard";
import { DataModeContext } from "@/lib/contexts";

// Use the same color as APHealth for consistency
const chartColor = "#3b82f6"; // bright blue

const chartConfig = {
  mobile: { label: "Channel Utilization (%)", color: chartColor },
} satisfies ChartConfig;

type Props = {
  bssid: string;
  channelUtilData: ChannelUtilizationDataPoint[];
  timeResolution: string;
  onTimeResolutionChange: (value: string) => void;
  simulationUptime: number | null;
};

export function ChartAreaInteractive({ bssid, channelUtilData, timeResolution, onTimeResolutionChange, simulationUptime }: Props) {
  const dataModeContext = React.useContext(DataModeContext);
  const isSimulationMode = dataModeContext?.mode === 'simulation';

  // Check if a time resolution option should be disabled
  // Enable only if simulation uptime > time resolution (strictly greater)
  const isTimeOptionDisabled = (minutes: number): boolean => {
    if (!isSimulationMode || !simulationUptime) return false;
    const seconds = minutes * 60;
    return simulationUptime <= seconds;
  };

  let bounds = React.useMemo(() => {
    let max = -Infinity;
    let min = Infinity;
    for (let point of channelUtilData) {
      let val = point.channel_utilization;
      if (val !== null && val !== undefined) {
        if (val > max) max = val;
        if (val < min) min = val;
      }
    }
    let ub = 1.2 * max - 0.2 * min;
    ub = Math.ceil(ub); // round up to nearest 1

    if (max == -Infinity || min == Infinity) {
      ub = 0;
    }

    return [0, ub];
  }, [channelUtilData]);


  return (
    <Card className="pt-0 shadow-sm">
      <CardHeader className="flex items-center gap-2 space-y-0 border-b py-3 sm:flex-row">
        <div className="grid flex-1 gap-1">
          <CardTitle>Channel Utilization</CardTitle>
        </div>
        <Select value={timeResolution} onValueChange={onTimeResolutionChange}>
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

      <CardContent className="px-2 pt-4 sm:px-6 sm:pt-6">
        <ChartContainer config={chartConfig} className="aspect-auto h-[280px] w-full">
          <AreaChart
            key={timeResolution}
            data={channelUtilData}
          >
            <defs>
              <linearGradient id="fillMobile" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor={chartColor} stopOpacity={0.8} />
                <stop offset="95%" stopColor={chartColor} stopOpacity={0.1} />
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
              label={{ value: "Time", dx: -30, dy: 15, fontSize: 11 }}
              tickFormatter={(v) => {
                const time = new Date()
                if (channelUtilData.length > 0) {
                  time.setSeconds(time.getSeconds() - (channelUtilData[channelUtilData.length - 1].sim_time_seconds - v));
                }
                const hour = time.getHours() % 12 == 0 ? 12 : time.getHours() % 12;
                const am_pm = time.getHours() >= 12 ? "PM" : "AM";
                return `${hour}:${time.getMinutes().toString().padStart(2, '0')} ${am_pm}`
              }}
            />

            <YAxis
              domain={bounds}
              dataKey="channel_utilization"
              tickLine={false}
              axisLine={false}
              tickMargin={6}
              tickFormatter={(v: number) => `${v}%`}
            />

            <ChartTooltip cursor={false} content={<ChartTooltipContent indicator="dot" />} />

            <Area
              dataKey="channel_utilization"
              type="monotone"
              fill="url(#fillMobile)"
              stroke={chartColor}
              name="Channel Utilization"
              baseValue="dataMin"
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
