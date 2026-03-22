import { BarChart, Bar, XAxis, YAxis, CartesianGrid } from 'recharts';
import { ChartContainer, ChartTooltip, ChartTooltipContent } from './ui/chart';
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectLabel,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { useState, useEffect, useMemo } from 'react';
import { fetchClientDistribution } from '@/lib/api-dashboard';
import type { HistogramData } from '@/lib/api-dashboard';

export default function QOEHistogramChart() {
  const [metricShown, setMetricShown] = useState("latency");
  const [allMetricsData, setAllMetricsData] = useState<Record<string, HistogramData> | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    setLoading(true);
    setError(null);
    fetchClientDistribution()
      .then((data) => {
        setAllMetricsData(data);
      })
      .catch((err: any) => {
        console.error("Failed to fetch client distribution:", err);
        setError(err.message || "Failed to load histogram data");
        setAllMetricsData(null);
      })
      .finally(() => {
        setLoading(false);
      });
  }, []); // Fetch only once on component mount

  // Transform data for the selected metric
    const { chartData, label } = useMemo(() => {
      // The API uses 'packet_loss_rate' but the dropdown uses 'packet_loss'
      const dataKey = metricShown === 'packet_loss' ? 'packet_loss_rate' : metricShown;
      
      if (!allMetricsData || !allMetricsData[dataKey]) {
        return { chartData: [], label: '' };
      }
      
      const metricData = allMetricsData[dataKey];
      
      const transformedData = metricData.data.map((bin) => ({
        bin: bin.range, // Use the range string for the x-axis
        count: bin.clients,
      }));

      console.log(`Metric: ${metricShown}, Label from API: ${metricData.label}`); // Temporary debug log

      return { chartData: transformedData, label: metricData.label };
    }, [allMetricsData, metricShown]);

  const maxCount = useMemo(() => {
    if (chartData.length === 0) return 0;
    return Math.max(...chartData.map(d => d.count));
  }, [chartData]);

  return (
    <div className="bg-white rounded-lg shadow-none border border-gray-200 p-4 w-full h-full flex flex-col overflow-hidden">
      {/* Header */}
      <div className="flex items-center justify-between mb-3 pb-3 border-b border-gray-200 flex-shrink-0">
        <h2 className="text-gray-900 font-bold">QOE Metrics</h2>
        <Select value={metricShown} onValueChange={(v) => setMetricShown(v)}>
          <SelectTrigger className="w-[180px]">
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
      </div>

      {/* Chart */}
      <div className="flex-1 min-h-0 overflow-hidden w-full">
        {loading ? (
          <div className="flex items-center justify-center h-full">
            <p className="text-gray-500">Loading...</p>
          </div>
        ) : error ? (
          <div className="flex items-center justify-center h-full">
            <p className="text-red-500">{error}</p>
          </div>
        ) : chartData.length === 0 ? (
          <div className="flex items-center justify-center h-full">
            <p className="text-gray-500">No data available for {metricShown}</p>
          </div>
        ) : (
          <ChartContainer config={{}} className="h-full w-full">
            <BarChart data={chartData} margin={{ top: 40, right: 10, left: -15, bottom: 20 }}>
              <CartesianGrid strokeDasharray="0" stroke="#e5e5e5" vertical={false} />
              
              <XAxis
                dataKey="bin"
                axisLine={{ stroke: '#e5e5e5' }}
                tickLine={false}
                tick={{ fill: '#737373', fontSize: 11 }}
                height={40}
                label={{ 
                  value: label,
                  position: 'insideBottom',
                  fontSize: 11 
                }}
              />

              <YAxis
                axisLine={{ stroke: '#e5e5e5' }}
                tickLine={false}
                tick={{ fill: '#737373', fontSize: 11 }}
                domain={[0, Math.ceil(maxCount * 1.1)]}
                allowDecimals={false}
                tickFormatter={(value) => Math.round(value).toString()}
                label={{
                  value: "Count",
                  angle: -90,
                  position: 'insideLeft',
                  fill: '#737373',
                  fontSize: 11,
                  offset: 10,
                }}
              />

              <ChartTooltip cursor={false} content={<ChartTooltipContent />} />
              <Bar 
                dataKey="count" 
                fill="#4DB8AC"
                radius={[4, 4, 0, 0]}
              />
            </BarChart>
          </ChartContainer>
        )}
      </div>
    </div>
  );
}


