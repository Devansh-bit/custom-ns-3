"use client";
import { cn } from "@/lib/utils";
import { Card, CardContent } from "@/components/ui/card";
import { Users, ArrowUpRight, History, ArrowUpFromLine } from "lucide-react";
import { useEffect, useMemo, useState } from "react";
import { isAxiosError } from "axios";
import * as React from "react";


import {
  fetchAPMetrics,
  fetchAPClients,
  fetchAPChannelUtilization,
  type ConnectedClient,
  type ChannelUtilizationDataPoint,
} from "@/lib/api-dashboard";
import type { APMetrics } from "@/lib/types";
import { DataModeContext } from "@/lib/contexts";

function secsToLabel(s?: number | null): { num: number | string; unit: string } {
  if (s == null || !Number.isFinite(s)) return { num: "—", unit: "" };
  const secs = Math.max(0, Math.floor(s));
  if (secs < 60) return {
    num: secs,
    unit: "seconds"
  };
  const mins = Math.floor(secs / 60);
  if (mins < 60) return {
    num: mins,
    unit: "minutes"
  };
  const hrs = Math.floor(mins / 60);
  return {
    num: hrs,
    unit: "hours"
  };
}



type Props = {
  bssid: string;
  refreshMs?: number; // optional polling interval
};

export default function StatisticCard12({ bssid, refreshMs }: Props) {
  const [metrics, setMetrics] = useState<APMetrics | null>(null);
  const [clientsCountFallback, setClientsCountFallback] = useState<number | null>(null);
  const [utilSeries, setUtilSeries] = useState<ChannelUtilizationDataPoint[] | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const dataModeContext = React.useContext(DataModeContext);
  const refreshKey = dataModeContext?.refreshKey ?? 0;

    // load everything together (metrics, util series; optional clients fallback)
    useEffect(() => {
      if (!bssid) return;

      let cancelled = false;
      let timer: number | undefined;

      const load = async () => {
        setLoading(true);
        setErr(null);
        try {
          // Parallelize API calls
          const [m, util] = await Promise.all([
            fetchAPMetrics(bssid),
            fetchAPChannelUtilization(bssid, 1), // last 1 hour
          ]);

          if (!cancelled) {
            if (m.bytes_sent != null) {
              const bytes_sent = m.bytes_sent / 1024;
              m.bytes_sent = parseFloat(bytes_sent.toFixed(2));
            }
            setMetrics(m);
            setUtilSeries(util || []);
          }

          // Only fetch clients count if metrics.count_clients is null/undefined
          if ((m?.count_clients ?? null) === null) {
            const clients: ConnectedClient[] = await fetchAPClients(bssid);
            if (!cancelled) setClientsCountFallback(clients.length);
          } else {
            if (!cancelled) setClientsCountFallback(null);
          }
        } catch (e) {
          if (!cancelled) setErr(isAxiosError(e) ? e.message : "Unexpected error");
        } finally {
          if (!cancelled) setLoading(false);
        }
      };

      load();
      if (refreshMs && refreshMs > 0) {
        timer = window.setInterval(load, refreshMs);
      }

      return () => {
        cancelled = true;
        if (timer) window.clearInterval(timer);
      };
    }, [bssid, refreshMs, refreshKey]);

    // derive the four display values
    const connectedClients = useMemo(() => {
      if (metrics?.count_clients != null) return metrics.count_clients;
      if (clientsCountFallback != null) return clientsCountFallback;
      return loading ? "…" : err ? "—" : 0;
    }, [metrics, clientsCountFallback, loading, err]);

    // Data transmitted: take latest channel utilization (0-100). Adjust if your API defines differently.
    const dataTransmittedPct = useMemo(() => {
      if (!utilSeries || utilSeries.length === 0) return loading ? "…" : err ? "—" : "0%";
      const latest = utilSeries.reduce((a, b) =>
        (a.sim_time_seconds ?? 0) > (b.sim_time_seconds ?? 0) ? a : b
      );
      const pct = latest.channel_utilization ?? 0;
      return `${Math.round(pct)}%`;
    }, [utilSeries, loading, err]);

    const isSimulationMode = dataModeContext?.mode === 'simulation';

    const lastUpdatedParts = useMemo(() => {
      const secs = Math.max(0, metrics?.last_update_seconds ?? 0);
      return secsToLabel(secs);
    }, [metrics, loading, err]);

    // Calculate Data Transmitted in Mbps
    const dataTransmittedMbps = useMemo(() => {
      if (loading) return "…";
      if (err || metrics == null) return "—";
      if (metrics.bytes_received == null) return "—";
      // Convert bytes to Megabits: (bytes * 8 bits/byte) / 1,000,000 bits per Megabit
      const mbps = (metrics.bytes_received * 8) / 1000000;
      return mbps.toFixed(2);
    }, [metrics, loading, err]);

    const cards = useMemo(
      () => [
        {
          icon: Users,
          iconBg:
            "border-blue-200 dark:border-blue-800 text-blue-600 dark:text-blue-400",
          value: connectedClients,
          label1: "Connected Clients",
          label2: "Active connections",
        },
        {
          icon: ArrowUpFromLine,
          iconBg:
            "border-yellow-200 dark:border-yellow-800 text-yellow-600 dark:text-yellow-400",
          value: metrics?.bytes_sent != null ? metrics.bytes_sent.toFixed(2) : "—",
          label1: "Data Transmitted",
          label2: "KB",
        },
        {
          icon: ArrowUpRight,
          iconBg:
            "border-green-200 dark:border-green-800 text-green-600 dark:text-green-400",
          value: metrics?.downlink_throughput_mbps != null ? metrics.downlink_throughput_mbps.toFixed(2) : "—",
          label1: "Throughput",
          label2: "Mbps",
        },
        {
          icon: History,
          iconBg:
            "border-cyan-200 dark:border-cyan-800 text-cyan-600 dark:text-cyan-400",
          value: lastUpdatedParts.num,
          label1: isSimulationMode ? "Sim Time" : "Replay Time",
          label2: lastUpdatedParts.unit,
        },
      ],
      [connectedClients, dataTransmittedMbps, metrics, lastUpdatedParts.num, lastUpdatedParts.unit, isSimulationMode]
    );

  return (
    <div className="grid mt-7 w-full grid-cols-1 gap-5 sm:grid-cols-2 lg:grid-cols-4">
    {cards.map((card, i) => (
      <Card key={i} className="h-full">
        <CardContent className="flex h-full flex-col justify-between gap-6">
          <div className={cn("flex size-12 items-center justify-center rounded-xl border", card.iconBg)}>
            <card.icon className="size-6" />
          </div>
          <div className="space-y-0.5">
            <div className="text-sm text-muted-foreground">{card.label1}</div>
            <div className="text-2xl font-bold leading-none">{card.value}</div>
            <div className="text-sm text-muted-foreground">{card.label2}</div>
          </div>
        </CardContent>
      </Card>
    ))}
  </div>

  );
}
