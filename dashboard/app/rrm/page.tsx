"use client";

import { NetworkPlannerUpdates } from "@/components/NetworkPlannerUpdates";
import { ChartAreaDefault } from "@/components/BO";
import { SimulationPanel } from "@/components/replay/SimulationPanel";
import { fetchRRMUpdates } from "@/lib/api-dashboard";
import { useEffect, useState, useContext } from "react";
import type { NetworkPlannerUpdate } from "@/lib/api-dashboard";
import { DataModeContext } from "@/lib/contexts";

export default function RRMPage() {
  const [updates, setUpdates] = useState<NetworkPlannerUpdate[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [loopType, setLoopType] = useState<'slow' | 'fast'>('slow');
  const dataModeContext = useContext(DataModeContext);
  const refreshKey = dataModeContext?.refreshKey ?? 0;
  const isReplayMode = dataModeContext?.mode === 'replay';

  useEffect(() => {
    let intervalId: NodeJS.Timeout;

    const loadUpdates = async () => {
      try {
        const data = await fetchRRMUpdates();
        // Backend already sorts by time_of_update descending
        setUpdates(data);
      } catch (e: any) {
        setError(e?.message ?? "Failed to fetch network planner updates");
      } finally {
        setLoading(false);
      }
    };

    // Initial load
    loadUpdates();

    // Poll every 2 seconds
    intervalId = setInterval(loadUpdates, 2000);

    return () => {
      clearInterval(intervalId);
    };
  }, [refreshKey]);

  return (
    <div className="min-h-screen flex flex-col">
      <div className="flex-1 p-8">
      <div className="mx-auto max-w">
        {/* Header */}
        <div className="mb-8 flex items-start justify-between">
          <div>
            <h1 className="font-inter font-medium text-[24px] leading-9 tracking-[0.07px]">
              Network Planner
            </h1>
            <p className="text-muted-foreground">
              View and manage radio resource management update history
            </p>
          </div>
        </div>

        {/* BO Graph */}
        <div className="mb-6">
          <ChartAreaDefault refreshMs={5000} />
        </div>

        {/* Content */}
        {loading && (
          <div className="flex items-center justify-center py-12">
            <p className="text-muted-foreground">Loading updates...</p>
          </div>
        )}

        {error && (
          <div className="mb-6 rounded-lg border border-destructive/20 bg-destructive/10 p-4">
            <p className="text-destructive">{error}</p>
          </div>
        )}

        {!loading && !error && (
          <>
            {updates.length === 0 ? (
              <div className="flex items-center justify-center py-12">
                <p className="text-muted-foreground">No network planner updates found</p>
              </div>
            ) : (
              <NetworkPlannerUpdates updates={updates} />
            )}
          </>
        )}
      </div>
      </div>

      {/* Simulation Panel - Only show in replay mode */}
      {isReplayMode && (
        <SimulationPanel />
      )}
    </div>
  );
}
