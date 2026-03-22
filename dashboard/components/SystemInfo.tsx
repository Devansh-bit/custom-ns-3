"use client";
import {ChartAreaInteractive} from "@/components/ApGraph";
import { Card } from "@/components/ui/card";
import { isAxiosError } from "axios";
import { fetchAPMetrics, fetchAPClients, type ConnectedClient, type ChannelUtilizationDataPoint} from "@/lib/api-dashboard";
import { APMetrics } from '@/lib/types';
import * as React from "react";
import apMappings from "../app/ap-mappings.json";
import { DataModeContext } from "@/lib/contexts";

type SystemInfoProps = {
  bssid: string;
  refreshMs?: number;
  latestChannelUtilization?: number | null;
};

type ApChartProps = {
  bssid: string;
  channelUtilData: ChannelUtilizationDataPoint[];
  timeResolution: string;
  onTimeResolutionChange: (value: string) => void;
  simulationUptime: number | null;
};

export  function ApChart({ bssid, channelUtilData, timeResolution, onTimeResolutionChange, simulationUptime }: ApChartProps){
  return(

    <div className="mt-1" >
        <ChartAreaInteractive
          bssid={bssid}
          channelUtilData={channelUtilData}
          timeResolution={timeResolution}
          onTimeResolutionChange={onTimeResolutionChange}
          simulationUptime={simulationUptime}
        />
    </div>
  );

}



export function SystemInfo({ bssid, refreshMs, latestChannelUtilization }: SystemInfoProps) {
  const [metrics, setMetrics] = React.useState<APMetrics | null>(null);
  const [clients, setClients] = React.useState<ConnectedClient[]>([]);
  const [err, setErr] = React.useState<string | null>(null);
  const [loading, setLoading] = React.useState(false);
  const dataModeContext = React.useContext(DataModeContext);
  const refreshKey = dataModeContext?.refreshKey ?? 0;

  React.useEffect(() => {
    if (!bssid) return;
    let cancelled = false;
    let timer: number | undefined;

    const load = async () => {
      setLoading(true);
      setErr(null);
      try {
        const [m, clientList] = await Promise.all([
          fetchAPMetrics(bssid),
          fetchAPClients(bssid)
        ]);
        if (!cancelled) {
          setMetrics(m);
          setClients(Array.isArray(clientList) ? clientList : []);
        }
      } catch (e) {
        if (!cancelled) setErr(isAxiosError(e) ? e.message : "Unexpected error");
      } finally {
        if (!cancelled) setLoading(false);
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
  }, [bssid, refreshMs, refreshKey]);

  // Calculate noise as abs(RSSI) - abs(SNR) for each client, then average
  const calculateNoise = (): string => {
    if (clients.length === 0) return "—";
    
    const validClients = clients.filter(
      c => c.ap_view_snr != null && c.ap_view_rssi != null && c.ap_view_snr !== 0 && c.ap_view_rssi !== 0
    );
    
    if (validClients.length === 0) return "—";

    // Calculate noise for each client: RSSI - SNR (direct calculation without abs)
    const clientNoiseValues = validClients.map(c => {
      const rssi = c.ap_view_rssi || 0;
      const snr = c.ap_view_snr || 0;
      return rssi - snr;
    });

    // Average all client noise values
    const avgNoise = clientNoiseValues.reduce((sum, noise) => sum + noise, 0) / clientNoiseValues.length;
    return `${avgNoise.toFixed(1)} dBm`;
  };

  const uplinkThroughput = metrics?.uplink_throughput_mbps
    ? `${metrics.uplink_throughput_mbps.toFixed(2)} Mbps`
    : "—";

  const channelUtilizationValue = latestChannelUtilization != null
    ? `${latestChannelUtilization.toFixed(1)}%`
    : "—";

  const items = [
    { label: "BSSID", value: bssid || "—" },
    { label: "Number of Clients", value: metrics?.count_clients != null ? metrics.count_clients.toString() : "—" },
    { label: "Channel", value: metrics?.channel != null ? metrics.channel.toString() : "—" },
    { label: "Channel Width", value: metrics?.channel_width != null ? `${metrics.channel_width} MHz` : "—" },
    { label: "Transmit Power", value: metrics?.phy_tx_power_level != null ? `${metrics.phy_tx_power_level} dBm` : "—" },
    { label: "Throughput", value: uplinkThroughput },
    { label: "Noise", value: calculateNoise() },
    { label: "Channel Utilization", value: channelUtilizationValue },
    { label: "Interference", value: metrics?.interference != null ? `${Number(metrics.interference).toFixed(2)}%` : "—" },
  ];

  return (
    <Card className="p-4 h-full -mb-3 w-full">
      <h3 className="mb-2 mt-1">System Information</h3>
      <div className="space-y-3">
        <div className="grid grid-cols-2 gap-x-6 gap-y-3">
          {items.map((it) => (
            <div key={it.label}>
              <div className="text-xs text-muted-foreground mb-1">{it.label}</div>
              <div className="text-foreground">{it.value}</div>
            </div>
          ))}
        </div>
      </div>
    </Card>
  );
}
//IMI/BSSID/Transmission power/Channel No
