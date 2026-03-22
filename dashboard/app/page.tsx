"use client";

import { APHealth } from "@/components/APHealth";
import { ChartAreaDefault } from "@/components/BO";
import ClientPieChart from "@/components/ClientPieChart";
import QOEMetricsChart from "@/components/QoEMetrics";
import { SimulationPanel } from "@/components/replay/SimulationPanel";
import { useContext, useState, useEffect } from "react";
import { ApCount, ClientCount, DataModeContext } from "@/lib/contexts";
import { fetchRRMUpdates } from "@/lib/api-dashboard";

export default function NetworkOverview() {
  const apCount = useContext(ApCount);
  const clientCount = useContext(ClientCount);
  const dataModeContext = useContext(DataModeContext);
  const isReplayMode = dataModeContext?.mode === 'replay';
  const [lastRRMUpdate, setLastRRMUpdate] = useState<string>("--");

  // Fetch latest RRM update timestamp
  useEffect(() => {
    const fetchLatestRRM = async () => {
      try {
        const updates = await fetchRRMUpdates();
        if (updates && updates.length > 0) {
          const latestTimestamp = updates[0].time_of_update;
          const date = new Date(latestTimestamp);
          const timeStr = date.toLocaleTimeString('en-US', {
            hour: 'numeric',
            minute: '2-digit',
            hour12: true
          });
          setLastRRMUpdate(timeStr);
        }
      } catch (e) {
        console.error('Failed to fetch RRM updates:', e);
      }
    };

    fetchLatestRRM();
    const interval = setInterval(fetchLatestRRM, 10000); // Poll every 10 seconds
    return () => clearInterval(interval);
  }, []);

  return (
    <div className="min-h-screen flex flex-col">
      <div className="flex-1 p-8 relative">
      <div className="mx-auto max-w">
        {/* Header */}
        <div className="mb-8 flex items-start justify-between">
          <div>
            <h1 className="font-inter font-medium text-[24px] leading-9 tracking-[0.07px]">Network Overview</h1>
            <p className="text-gray-500">System performance and health monitoring</p>
          </div>
        </div>

        {/* Stats Row */}
        <div className="mb-8 flex items-start justify-between">
          <div className="flex items-start gap-8">
          <div>
            <div className="mb-1 text-sm text-gray-500">AP Count</div>
              <div className="text-4xl">{apCount}</div>
            </div>
            <div>
              <div className="mb-1 text-sm text-gray-500">Client Count</div>
              <div className="text-4xl">{clientCount}</div>
            </div>
          </div>
          <div>
          <span className="text-gray-500 mr-1">Last RRM Updated</span>
          : {lastRRMUpdate}
          </div>
        </div>

        {/* Charts Grid - Top Row */}
        <div className="mb-6 grid grid-cols-1 gap-6 lg:grid-cols-[70%_30%]">
          <ChartAreaDefault refreshMs={5000} />
          <APHealth refreshMs={5000} />
        </div>

        {/* Charts Grid - Bottom Row */}
        <div className="mb-6 grid grid-cols-1 gap-6 lg:grid-cols-[30%_70%]">
          <ClientPieChart refreshMs={5000} />
          <QOEMetricsChart refreshMs={5000} />
        </div>
      </div>
      </div>

      {/* Simulation Panel - Only show in replay mode */}
      {isReplayMode && (
        <SimulationPanel />
      )}
    </div>
  );
}
