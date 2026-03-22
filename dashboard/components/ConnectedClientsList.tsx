"use client";
import * as React from "react";
import {
  Card,
  CardContent,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";
import { fetchSTAMetrics, type STAMetrics } from "@/lib/api-dashboard";
import { SelectedClientContext, DataModeContext } from "@/lib/contexts";

export function ConnectedClientsList() {
  const [clientMetrics, setClientMetrics] = React.useState<
    STAMetrics["client_metrics"]
  >([]);
  const [loading, setLoading] = React.useState(false);
  const [err, setErr] = React.useState<string | null>(null);
  const { selectedClient, setSelectedClient } = React.useContext(
    SelectedClientContext
  )!;
  const dataModeContext = React.useContext(DataModeContext);
  const refreshKey = dataModeContext?.refreshKey ?? 0;

  React.useEffect(() => {
    let cancelled = false;
    let timer: number | undefined;

    const load = async () => {
      setLoading(true);
      setErr(null);
      try {
        const data = await fetchSTAMetrics();
        if (!cancelled) {
          const metrics = data.client_metrics || [];
          setClientMetrics(metrics);
          
          // Select first client by default if none is selected and clients are available
          if (!selectedClient && metrics.length > 0) {
            setSelectedClient(metrics[0].mac_address);
          }
        }
      } catch (e: any) {
        if (!cancelled) setErr(e?.message ?? "Failed to fetch clients");
      } finally {
        if (!cancelled) setLoading(false);
      }
    };

    load();

    if (!selectedClient) {
      // Poll every 5 seconds only if no client is selected
      timer = window.setInterval(load, 5000);
    }

    return () => {
      cancelled = true;
      if (timer) window.clearInterval(timer);
    };
  }, [selectedClient, setSelectedClient, refreshKey]);

  return (
    <Card className="h-full flex flex-col pt-0">
      <CardHeader className="flex flex-col gap-0 space-y-0 border-b pt-4.5 !pb-2.5 flex-shrink-0">
        <div className="grid flex-1 gap-1">
          <CardTitle className="text-lg">Connected Clients</CardTitle>
        </div>
        <p className="text-sm text-muted-foreground">
          {loading ? "Loading..." : `${clientMetrics.length} active devices`}
        </p>
      </CardHeader>
      <CardContent className="p-0 flex-1 min-h-0 overflow-hidden">
        {err ? (
          <div className="p-6 text-center text-red-500 text-sm">{err}</div>
        ) : (
          <div className="h-[240px] overflow-y-auto">
            {clientMetrics.length === 0 ? (
              <div className="p-6 text-center text-muted-foreground text-sm">
                No clients connected
              </div>
            ) : (
              <div className="divide-y">
                {clientMetrics.map((client, index) => (
                  <div
                    key={`${client.mac_address}-${index}`}
                    className={`px-4 pt-3 pb-2 hover:bg-muted/50 transition-colors cursor-pointer ${
                      selectedClient === client.mac_address ? "bg-muted" : ""
                    }`}
                    onClick={() => setSelectedClient(client.mac_address)}
                  >
                    <div className="flex items-start justify-between">
                      <div className="flex-1 min-w-0">
                        <p className="text-sm font-medium truncate">
                          {client.mac_address}
                        </p>
                        <p className="text-xs text-muted-foreground mt-0.5">
                          AP: {client.ap_bssid}
                        </p>
                      </div>
                    </div>
                  </div>
                ))}
              </div>
            )}
          </div>
        )}
      </CardContent>
    </Card>
  );
}

