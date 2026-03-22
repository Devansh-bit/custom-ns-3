"use client";
import * as React from "react";
import { Card } from "./ui/card";
import {
  Table, TableBody, TableCell, TableHead, TableHeader, TableRow,
} from "./ui/table";

import { fetchAPClients, type ConnectedClient } from "@/lib/api-dashboard";
import { DataModeContext } from "@/lib/contexts";

type Props = {
  bssid: string;
  refreshMs?: number; // optional polling
};

export function ConnectedClientsTable({ bssid, refreshMs }: Props) {
  const [rows, setRows] = React.useState<ConnectedClient[]>([]);
  const [loading, setLoading] = React.useState(false);
  const [err, setErr] = React.useState<string | null>(null);
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
        const data = await fetchAPClients(bssid);
        if (!cancelled) {
          setRows(Array.isArray(data) ? data : []);
        }
      } catch (e: any) {
        if (!cancelled) setErr(e?.message ?? "Failed to fetch clients");
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

  // helpers
  const fmt = (v: number | null | undefined, suffix = "", digits = 1) =>
    v === null || v === undefined || Number.isNaN(Number(v))
      ? "—"
      : `${Number(v).toFixed(digits)}${suffix}`;

  // Some of your rows have ap_view_rssi/ap_view_snr as 0 when unknown; show "—" instead.
  const clean = (v: number | null | undefined) =>
    v === 0 ? null : v;

  return (
    <Card className="p-6">
      <div className="mb-6">
        <h3>Connected Clients</h3>
        <p className="text-sm text-muted-foreground">{rows.length} active devices</p>
      </div>

      <Table>
        <TableHeader>
          <TableRow>
            <TableHead className="font-normal text-muted-foreground w-[25%] pl-4">Device</TableHead>
            <TableHead className="font-normal text-muted-foreground w-[10%]">AP MAC</TableHead>
            <TableHead className="font-normal text-center text-muted-foreground w-[13%] pr-6">SNR</TableHead>
            <TableHead className="font-normal text-center text-muted-foreground w-[13%] pr-6">RSSI</TableHead>
            <TableHead className="font-normal text-center text-muted-foreground w-[13%] pr-6">Latency</TableHead>
            <TableHead className="font-normal text-center text-muted-foreground w-[13%] pr-6">Jitter</TableHead>
            <TableHead className="font-normal text-center text-muted-foreground w-[13%] pr-6">Packet loss</TableHead>
          </TableRow>
        </TableHeader>

        <TableBody>
          {rows.map((c) => (
            <TableRow key={c.mac_address}>
              <TableCell className="pl-4">{c.mac_address}</TableCell>
              <TableCell>{bssid}</TableCell>

              <TableCell className="text-center tabular-nums pr-6">
                {fmt(clean(c.ap_view_snr), " dBm", 1)}
              </TableCell>

              <TableCell className="text-center tabular-nums pr-6">
                {fmt(clean(c.ap_view_rssi), " dBm", 1)}
              </TableCell>

              <TableCell className="text-center tabular-nums pr-6">
                {fmt(c.latency_ms, " ms", 1)}
              </TableCell>

              <TableCell className="text-center tabular-nums pr-6">
                {fmt(c.jitter_ms, " ms", 1)}
              </TableCell>

              <TableCell className="text-center tabular-nums pr-6">
                {fmt(c.packet_loss_rate, " %", 1)}
              </TableCell>
            </TableRow>
          ))}

        </TableBody>
      </Table>
    </Card>
  );
}
