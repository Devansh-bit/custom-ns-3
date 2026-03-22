"use client";
import { BarChart, Bar, XAxis, YAxis, CartesianGrid } from 'recharts';
import { ChartContainer, ChartTooltip, ChartTooltipContent, ChartLegend, ChartLegendContent, ChartConfig } from './ui/chart';
import { useMemo } from 'react';
import { type HistogramData } from '@/lib/api-dashboard';
import {
  Card,
  CardContent,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"

type QoEHistogramDoubleProps = {
  title?: string;
  data: {
    snr: HistogramData;
    rssi: HistogramData;
  }
};

export default function QoEHistogramDouble({ title = "SNR / RSSI", data }: QoEHistogramDoubleProps) {
  // Use dummy data
  const snrData = data?.snr;
  const rssiData = data?.rssi;

  // Transform bins data for grouped bars
  const chartData = useMemo(() => {
    if (!snrData?.data || !rssiData?.data) return [];
    
    // Combine SNR and RSSI bins - align by index for grouped bars
    const minBins = Math.min(snrData.data.length, rssiData.data.length);
    const combinedData = [];
    
    for (let i = 0; i < minBins; i++) {
      const snrBin = snrData.data[i];
      const rssiBin = rssiData.data[i];
      
      // Create a combined entry with both metrics for grouped bars
      combinedData.push({
        bin: snrBin.range,
        snrCount: Number(snrBin.clients) || 0,
        rssiCount: Number(rssiBin.clients) || 0,
      });
    }
    
    return combinedData;
  }, [snrData, rssiData]);

  const maxCount = useMemo(() => {
    if (chartData.length === 0) return 0;
    return Math.max(
      ...chartData.map(d => Math.max(d.snrCount || 0, d.rssiCount || 0))
    );
  }, [chartData]);

  // Use colors from the consistent palette
  const chartColors = {
    snr: "#3b82f6",   // bright blue
    rssi: "#8b5cf6",  // purple
  };

  // Chart config for colors
  const chartConfig = {
    snrCount: {
      label: "SNR",
      color: chartColors.snr,
    },
    rssiCount: {
      label: "RSSI",
      color: chartColors.rssi,
    },
  } as ChartConfig;

  return (
    <Card className="shadow-sm h-[38vh] min-h-[380px] max-h-[550px]">
      <CardHeader className="pb-2 border-b">
        <CardTitle>{title}</CardTitle>
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
                <linearGradient id="snrGradient" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stopColor={chartColors.snr} stopOpacity={0.8} />
                  <stop offset="100%" stopColor={chartColors.snr} stopOpacity={0.3} />
                </linearGradient>
                <linearGradient id="rssiGradient" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stopColor={chartColors.rssi} stopOpacity={0.8} />
                  <stop offset="100%" stopColor={chartColors.rssi} stopOpacity={0.3} />
                </linearGradient>
              </defs>
              <CartesianGrid strokeDasharray="0" stroke="hsl(var(--border))" horizontal={false} vertical={false} />
              
              <XAxis
                type="category"
                dataKey="bin"
                axisLine={{ stroke: 'hsl(var(--border))' }}
                tickLine={false}
                tick={{ fontSize: 11 }}
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
                  offset: -8,
                }}
              />

              <ChartTooltip cursor={false} content={<ChartTooltipContent />} />
              <Bar
                dataKey="snrCount"
                fill="url(#snrGradient)"
                radius={[4, 4, 0, 0]}
                legendType="circle"
              />
              <Bar
                dataKey="rssiCount"
                fill="url(#rssiGradient)"
                radius={[4, 4, 0, 0]}
                legendType="circle"
              />
              <ChartLegend
                content={(props) => {
                  const { payload } = props;
                  if (!payload?.length) return null;

                  return (
                    <div className="flex items-center justify-center gap-4 pt-3">
                      {payload.map((entry) => (
                        <div key={entry.value} className="flex items-center gap-1.5">
                          <div
                            className="h-2 w-2 shrink-0 rounded-[2px]"
                            style={{
                              backgroundColor: entry.dataKey === 'snrCount' ? chartColors.snr : chartColors.rssi,
                            }}
                          />
                          <span className="text-sm">{entry.dataKey === 'snrCount' ? 'SNR' : 'RSSI'}</span>
                        </div>
                      ))}
                    </div>
                  );
                }}
              />
            </BarChart>
          </ChartContainer>
        )}
      </CardContent>
    </Card>
  );
}

