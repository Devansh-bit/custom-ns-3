"use client";
import * as React from "react";
import { Card, CardHeader, CardContent, CardTitle } from "./ui/card";
import { BarChart, Bar, XAxis, YAxis, CartesianGrid, Cell } from "recharts";
import { fetchChannelInterference, type ChannelInterferenceRow, fetchChannelDetails, type ChannelDetails } from "@/lib/api-dashboard";
import { ChartContainer, ChartTooltip, ChartTooltipContent } from "./ui/chart";
import { DataModeContext } from "@/lib/contexts";

type Row = { channel: string; interference: number; originalData?: ChannelInterferenceRow };

// Use the same color as APHealth for consistency
const chartColor = "#3b82f6"; // bright blue

export function ChannelInterferenceChart({ bssid }: { bssid: string }) {
  const [rows, setRows] = React.useState<Row[]>([]);
  const [err, setErr] = React.useState<string | null>(null);
  const [loading, setLoading] = React.useState(false);
  const [selectedChannel, setSelectedChannel] = React.useState<Row | null>(null);
  const [channelDetails, setChannelDetails] = React.useState<ChannelDetails | null>(null);
  const [loadingDetails, setLoadingDetails] = React.useState(false);
  const dataModeContext = React.useContext(DataModeContext);
  const refreshKey = dataModeContext?.refreshKey ?? 0;

  React.useEffect(() => {
    let cancelled = false;

    const load = async () => {
      setLoading(true);
      setErr(null);
      try {
        const data = await fetchChannelInterference(bssid);
        if (cancelled) return;

        // Map API rows → chart rows
        const mapped: Row[] = data
          .sort((a, b) => a.channel - b.channel)
          .map((r) => ({
            channel: String(r.channel).padStart(2, "0"),
            interference: Number(r.non_wifi_channel_utilization),
            originalData: r,
          }));

        setRows(mapped);
      } catch (e: any) {
        if (!cancelled) setErr(e?.message ?? "Failed to fetch channel interference");
      } finally {
        if (!cancelled) setLoading(false);
      }
    };

    load();
    return () => { cancelled = true; };
  }, [refreshKey, bssid]);

  const handleBarClick = async (data: any) => {
    if (data && data.activePayload && data.activePayload[0]) {
      const clickedData = data.activePayload[0].payload as Row;
      setSelectedChannel(clickedData);
      
      // Fetch detailed channel information
      setLoadingDetails(true);
      try {
        const channelNum = parseInt(clickedData.channel);
        const details = await fetchChannelDetails(channelNum);
        setChannelDetails(details);
      } catch (e: any) {
        console.error('Failed to fetch channel details:', e);
      } finally {
        setLoadingDetails(false);
      }
    }
  };

  let bounds = React.useMemo(() => {
    let max = -Infinity;
    let min = Infinity;
    for (let point of rows) {
      let val = point.interference;
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

    return [0, ub];
  }, [rows]);

  const overallStats = React.useMemo(() => {
    if (rows.length === 0) return null;
    const avgInterference = rows.reduce((sum, r) => sum + r.interference, 0) / rows.length;
    const maxInterference = Math.max(...rows.map(r => r.interference));
    const minInterference = Math.min(...rows.map(r => r.interference));
    return {
      avgInterference,
      maxInterference,
      minInterference,
      channelCount: rows.length,
    };
  }, [rows]);

  return (
    <Card className="pt-0 pb-6 px-3 relative shadow-sm">
      <div className="mb-0 max-w-fit pl-3 pt-10">
        <h3>Channel Interference</h3>
        <p className="text-sm text-muted-foreground">Interference analysis across channels</p>
      </div>

      <div className="flex items-end gap-3 w-full pb-3 pr-4">
        <div className="flex-1 min-w-0 pl-2">
          <ChartContainer config={{}} className="h-84 w-full">
            <BarChart data={rows} onClick={handleBarClick}>
              <defs>
                <linearGradient id="barGradient" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stopColor={chartColor} stopOpacity={0.8} />
                  <stop offset="100%" stopColor={chartColor} stopOpacity={0.3} />
                </linearGradient>
                <linearGradient id="barGradientSelected" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stopColor={chartColor} stopOpacity={1} />
                  <stop offset="100%" stopColor={chartColor} stopOpacity={0.5} />
                </linearGradient>
              </defs>
              <CartesianGrid strokeDasharray="3 3" stroke="hsl(var(--border))" vertical={false} />
              <XAxis
                dataKey="channel"
                axisLine={false}
                tickLine={false}
                tick={{ fontSize: 12 }}
                label={{ value: "Channels", position: "insideBottom", offset: -4 }}
              />
              <YAxis
                domain={bounds}
                axisLine={false}
                tickLine={false}
                tick={{ fontSize: 12 }}
                scale="linear"
                label={{
                  value: "Channel Interference",
                  angle: -90,
                  position: "insideLeft",
                  offset: 10,
                  dy: -5,
                  style: { textAnchor: "middle" },
                }}
              />
              <ChartTooltip content={<ChartTooltipContent />} />
              <Bar
                dataKey="interference"
                fill="url(#barGradient)"
                radius={[4, 4, 0, 0]}
                cursor="pointer"
              >
                {rows.map((entry, index) => (
                  <Cell
                    key={`cell-${index}`}
                    fill={selectedChannel?.channel === entry.channel ? "url(#barGradientSelected)" : "url(#barGradient)"}
                    style={{ cursor: "pointer" }}
                  />
                ))}
              </Bar>
            </BarChart>
          </ChartContainer>
        </div>

        <Card className="bg-muted border-0 flex-shrink-0 w-[90%] sm:w-[85%] md:w-[75%] lg:w-[60%] xl:w-[50%] max-w-[250px] h-[340px] sm:h-[400px] md:h-[420px] flex flex-col p-0 shadow-sm hidden">
          <CardHeader className="pb-4 pt-6 pl-4 pr-5 border-b border-blue-200/50 dark:border-blue-800/50">
            <CardTitle className="text-sm font-semibold text-center">
              {selectedChannel ? `Channel ${selectedChannel.channel}` : "Overall"}
            </CardTitle>
          </CardHeader>
          <CardContent className="flex-1 pl-4 pr-8 pb-4 overflow-hidden flex flex-col items-center justify-start pt-2">
          {selectedChannel ? (
            <div className="space-y-3 text-sm w-full">
                {loadingDetails ? (
                  <div className="text-muted-foreground">Loading...</div>
                ) : channelDetails ? (
                  <>
                    <div className="flex items-center justify-between py-1">
                      <span className="text-muted-foreground text-left">Quality:</span>
                      <span className="text-foreground text-right">{channelDetails.quality.toFixed(1)}%</span>
                    </div>
                    <div className="flex items-center justify-between py-1">
                      <span className="text-muted-foreground text-left">WiFi:</span>
                      <span className="text-foreground text-right">{channelDetails.wifi.toFixed(1)}%</span>
                    </div>
                    <div className="flex items-center justify-between py-1">
                      <span className="text-muted-foreground text-left">Zigbee:</span>
                      <span className="text-foreground text-right">{channelDetails.zigbee.toFixed(1)}%</span>
                    </div>
                    <div className="flex items-center justify-between py-1">
                      <span className="text-muted-foreground text-left">Bluetooth:</span>
                      <span className="text-foreground text-right">{channelDetails.bluetooth.toFixed(1)}%</span>
                    </div>
                    <div className="flex items-center justify-between py-1">
                      <span className="text-muted-foreground text-left">Microwave:</span>
                      <span className="text-foreground text-right">{channelDetails.microwave.toFixed(1)}%</span>
                    </div>
                    <div className="flex items-center justify-between py-1">
                      <span className="text-muted-foreground text-left">Total non-WiFi:</span>
                      <span className="text-foreground text-right">{channelDetails.total_non_wifi.toFixed(1)}%</span>
                    </div>
                    <div className="flex items-center justify-between py-1">
                      <span className="text-muted-foreground text-left">Utilization:</span>
                      <span className="text-foreground text-right">{channelDetails.utilization.toFixed(1)}%</span>
                    </div>
                  </>
                ) : (
                  <>
                    <div className="flex items-center justify-between py-1">
                      <span className="text-muted-foreground text-left">Interference:</span>
                      <span className="text-foreground text-right">{selectedChannel.interference.toFixed(1)}%</span>
                    </div>
                  </>
                )}
            </div>
          ) : overallStats ? (
            <div className="space-y-3 text-sm w-full">
                <div className="flex items-center justify-between py-1">
                  <span className="text-muted-foreground text-left">Quality:</span>
                  <span className="text-foreground text-right">-</span>
                </div>
                <div className="flex items-center justify-between py-1">
                  <span className="text-muted-foreground text-left">WiFi:</span>
                  <span className="text-foreground text-right">-</span>
                </div>
                <div className="flex items-center justify-between py-1">
                  <span className="text-muted-foreground text-left">Zigbee:</span>
                  <span className="text-foreground text-right">-</span>
                </div>
                <div className="flex items-center justify-between py-1">
                  <span className="text-muted-foreground text-left">Bluetooth:</span>
                  <span className="text-foreground text-right">-</span>
                </div>
                <div className="flex items-center justify-between py-1">
                  <span className="text-muted-foreground text-left">Microwave:</span>
                  <span className="text-foreground text-right">-</span>
                </div>
                <div className="flex items-center justify-between py-1">
                  <span className="text-muted-foreground text-left">Total non-WiFi:</span>
                  <span className="text-foreground text-right">-</span>
                </div>
                <div className="flex items-center justify-between py-1">
                  <span className="text-muted-foreground text-left">Utilization:</span>
                  <span className="text-foreground text-right">-</span>
                </div>
            </div>
          ) : (
            <div className="space-y-3 text-sm w-full">
                <div className="flex items-center justify-between py-1">
                  <span className="text-muted-foreground text-left">Quality:</span>
                  <span className="text-foreground text-right">-</span>
                </div>
                <div className="flex items-center justify-between py-1">
                  <span className="text-muted-foreground text-left">WiFi:</span>
                  <span className="text-foreground text-right">-</span>
                </div>
                <div className="flex items-center justify-between py-1">
                  <span className="text-muted-foreground text-left">Zigbee:</span>
                  <span className="text-foreground text-right">-</span>
                </div>
                <div className="flex items-center justify-between py-1">
                  <span className="text-muted-foreground text-left">Bluetooth:</span>
                  <span className="text-foreground text-right">-</span>
                </div>
                <div className="flex items-center justify-between py-1">
                  <span className="text-muted-foreground text-left">Microwave:</span>
                  <span className="text-foreground text-right">-</span>
                </div>
                <div className="flex items-center justify-between py-1">
                  <span className="text-muted-foreground text-left">Total non-WiFi:</span>
                  <span className="text-foreground text-right">-</span>
                </div>
                <div className="flex items-center justify-between py-1">
                  <span className="text-muted-foreground text-left">Utilization:</span>
                  <span className="text-foreground text-right">-</span>
                </div>
            </div>
          )}
          </CardContent>
        </Card>
      </div>
    </Card>
  );
}
