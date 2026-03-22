"use client";
import { BarChart, Bar, XAxis, YAxis, CartesianGrid } from 'recharts';
import { ChartContainer, ChartTooltip, ChartTooltipContent, ChartConfig } from './ui/chart';
import { useMemo } from 'react';
import { readableMetricShown } from '@/lib/utils';
import { type HistogramData, MetricType } from '@/lib/api-dashboard';
import {
  Card,
  CardContent,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"

type QoEHistogramSingleProps = {
  metricType: MetricType;
  data: HistogramData;
};

export default function QoEHistogramSingle({ metricType, data }: QoEHistogramSingleProps) {
  // Use dummy data for now
  const histogramData = data;

  // Transform bins data for chart
  const chartData = useMemo(() => {
    if (!histogramData || !histogramData.data) return [];
    
    return histogramData.data.map((item) => ({
      bin: item.range,
      count: item.clients,
    }));
  }, [histogramData]);

  const maxCount = useMemo(() => {
    if (chartData.length === 0) return 0;
    return Math.max(...chartData.map(d => d.count));
  }, [chartData]);

  // Use the same color as APHealth for consistency
  const chartColor = "#3b82f6"; // bright blue

  // Chart config for colors
  const chartConfig = useMemo(() => {
    return {
      count: {
        label: readableMetricShown(metricType),
        color: chartColor,
      },
    } as ChartConfig;
  }, [metricType]);

  return (
    <Card className="shadow-sm h-[38vh] min-h-[380px] max-h-[550px]">
      <CardHeader className="pb-2 border-b">
        <CardTitle>{readableMetricShown(metricType)}</CardTitle>
      </CardHeader>

      <CardContent className="flex-1 min-h-0 overflow-hidden w-full pt-4">
        {chartData.length === 0 ? (
          <div className="flex items-center justify-center h-full">
            <p className="text-muted-foreground text-sm">No data available</p>
          </div>
        ) : (
          <ChartContainer config={chartConfig} className="h-full w-full">
            <BarChart
              data={chartData}
              width={undefined}
              height={undefined}
              margin={{ top: 15, right: 20, left: 5, bottom: 10 }}
            >
              <defs>
                <linearGradient id="barGradient" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stopColor={chartColor} stopOpacity={0.8} />
                  <stop offset="100%" stopColor={chartColor} stopOpacity={0.3} />
                </linearGradient>
              </defs>
              <CartesianGrid strokeDasharray="0" stroke="hsl(var(--border))" horizontal={false} vertical={false} />
              
              <XAxis
                type="category"
                dataKey="bin"
                axisLine={{ stroke: 'hsl(var(--border))' }}
                tickLine={false}
                tick={{ fontSize: 11 }}
                label={{
                  value: readableMetricShown(metricType),
                  position: 'insideBottom',
                  offset: -5,
                  fontSize: 11,
                }}
              />

              <YAxis
                type="number"
                axisLine={{ stroke: 'hsl(var(--border))' }}
                tickLine={false}
                tick={{ fontSize: 11 }}
                domain={[0, Math.ceil(maxCount * 1.1)]}
                allowDecimals={false}
                tickFormatter={(value) => Math.round(value).toString()}
                label={{
                  value: "Client Count",
                  angle: -90,
                  position: 'left',
                  fontSize: 11,
                  style: { textAnchor: 'middle' },
                  offset: -9,
                }}
              />

              <ChartTooltip cursor={false} content={<ChartTooltipContent />} />
              <Bar
                dataKey="count"
                fill="url(#barGradient)"
                radius={[4, 4, 0, 0]}
              />
            </BarChart>
          </ChartContainer>
        )}
      </CardContent>
    </Card>
  );
}

