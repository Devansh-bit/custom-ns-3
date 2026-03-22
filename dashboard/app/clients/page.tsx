"use client";

import { MetricsChart } from "@/components/MetricsChart";
import ClientApp from "@/components/ClientApp";
import QoEHistogramSingle from "@/components/QoEHistogramSingle";
import QoEHistogramDouble from "@/components/QoEHistogramDouble";
import { ConnectedClientsList } from "@/components/ConnectedClientsList";
import { SimulationPanel } from "@/components/replay/SimulationPanel";
import { useEffect, useState, useContext } from "react";
import { fetchClientDistribution, type HistogramData } from "@/lib/api-dashboard";
import { SelectedClientContext, DataModeContext } from "@/lib/contexts";
import type { MetricType } from "@/lib/api-dashboard";

export default function Home() {
  const [distributionData, setDistributionData] = useState<Record<MetricType, HistogramData> | null>(null);
  const [selectedClient, setSelectedClient] = useState<string | null>(null);
  const dataModeContext = useContext(DataModeContext);
  const refreshKey = dataModeContext?.refreshKey ?? 0;
  const isReplayMode = dataModeContext?.mode === 'replay';

  useEffect(() => {
    const fetchData = () => {
      fetchClientDistribution().then(data => {
        setDistributionData(data);
      }).catch(error => {
        console.error("Failed to fetch client distribution:", error);
      });
    };

    fetchData();
    const interval = setInterval(fetchData, 5000);

    return () => clearInterval(interval);
  }, [refreshKey]);

  return (
    <div className="min-h-screen flex flex-col">
      <div className="flex-1 items-center justify-center p-4">
      {/* Header */}
      <div className="flex items-center justify-between mt-6 mb-4">
        <h2 className="font-inter font-medium text-[24px] leading-9 tracking-[0.07px]">Client Distribution</h2>
      </div>

      {/* QoE Histogram Charts Grid */}
      <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
        <QoEHistogramSingle metricType="latency" data={distributionData?.latency} />
        <QoEHistogramSingle metricType="jitter" data={distributionData?.jitter} />
        <QoEHistogramDouble title={
    <span>
      SNR 
      <span style={{ fontSize: '0.8em', fontWeight: 'bold' }}> (dBm)</span> 
      / RSSI 
      <span style={{ fontSize: '0.8em', fontWeight: 'bold' }}> (dBm)</span>
    </span>
  } data={distributionData ? {rssi: distributionData.rssi, snr: distributionData.snr} : undefined} />
        <QoEHistogramSingle metricType="packet_loss" data={distributionData?.packet_loss_rate} />
      </div>
      
      <ClientApp/>
      
      {/* Connected Clients List and QoE Metrics Chart */}
      <div className="mt-6 grid grid-cols-1 gap-6 lg:grid-cols-[400px_1fr]">
        <SelectedClientContext.Provider value={{ selectedClient, setSelectedClient }}>
          <ConnectedClientsList />
          <MetricsChart/>
        </SelectedClientContext.Provider>
      </div>
      </div>

      {/* Simulation Panel - Only show in replay mode */}
      {isReplayMode && (
        <SimulationPanel />
      )}
    </div>
  );
}
